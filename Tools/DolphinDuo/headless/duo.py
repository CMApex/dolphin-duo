"""Drive headless Dolphin (dolphin-emu-nogui with DuoHarness) from Python, for mapping a game's
memory without anyone at the controls.

    from duo import Dolphin
    d = Dolphin('GFEE01.rvz')
    d.press('START'); d.wait(60); d.shot('title'); d.mem1('/tmp/m.raw')

Environment: DUO_DOLPHIN (the dolphin-emu-nogui binary), DUO_USER_DIR (a scratch user folder),
DUO_HARNESS_DIR (where the command FIFO and log live). Rendering uses Mesa's llvmpipe through
surfaceless EGL, so no display or GPU is needed.
"""
import os
import subprocess
import time

BUILD = os.environ.get('DUO_DOLPHIN', 'build/Binaries/dolphin-emu-nogui')
USER = os.environ.get('DUO_USER_DIR', os.path.expanduser('~/.duo-harness/user'))
HARNESS = os.environ.get('DUO_HARNESS_DIR', os.path.expanduser('~/.duo-harness/run'))


class Dolphin:
    def __init__(self, game, backend='OGL', speed=0.0, extra=None, log=None):
        os.makedirs(HARNESS, exist_ok=True)
        for f in ('log',):
            p = os.path.join(HARNESS, f)
            if os.path.exists(p):
                os.remove(p)
        env = dict(os.environ, DUO_HARNESS=HARNESS, EGL_PLATFORM='surfaceless',
                   LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe')
        args = [BUILD, '-p', 'headless', '-u', USER, '-v', backend, '-e', game,
                '-C', f'Dolphin.Core.EmulationSpeed={speed}',
                '-C', 'Dolphin.Interface.ConfirmStop=False',
                '-C', 'Dolphin.Core.SIDevice0=6',
                '-C', 'Dolphin.DSP.Backend=No Audio Output']
        args += extra or []
        self.logfile = open(log or os.path.join(HARNESS, 'dolphin.log'), 'w')
        self.proc = subprocess.Popen(args, env=env, stdout=self.logfile, stderr=subprocess.STDOUT)
        self._seen = 0
        self._wait_for(lambda line: line == 'ready', 120)
        self.fifo = open(os.path.join(HARNESS, 'cmd'), 'w')

    def _lines(self):
        p = os.path.join(HARNESS, 'log')
        if not os.path.exists(p):
            return []
        with open(p) as f:
            return f.read().splitlines()

    def _wait_for(self, predicate, timeout):
        end = time.time() + timeout
        while time.time() < end:
            lines = self._lines()
            for i in range(self._seen, len(lines)):
                if predicate(lines[i]):
                    self._seen = i + 1
                    return lines[i]
            if self.proc.poll() is not None:
                raise RuntimeError(f'dolphin exited ({self.proc.returncode}); see log')
            time.sleep(0.05)
        raise TimeoutError('harness did not answer')

    def cmd(self, line, timeout=300):
        self.fifo.write(line + '\n')
        self.fifo.flush()
        got = self._wait_for(lambda l: l.endswith(' ' + line) and (l.startswith('done') or l.startswith('error'))
                             or l.startswith('error'), timeout)
        if got.startswith('error'):
            raise RuntimeError(got)
        return int(got.split()[1])  # pad polls so far

    def press(self, buttons, polls=4):
        return self.cmd(f'press {buttons} {polls}')

    def stick(self, x, y, polls=4):
        return self.cmd(f'stick {x} {y} {polls}')

    def wait(self, polls):
        return self.cmd(f'wait {polls}', timeout=max(300, polls))

    def mem1(self, path):
        self.cmd(f'mem1 {path}')
        return path

    def shot(self, name, timeout=60):
        game_dir = os.path.join(USER, 'ScreenShots')
        before = time.time()
        self.cmd(f'shot {name}')
        end = time.time() + timeout
        while time.time() < end:
            for root, _, files in os.walk(game_dir):
                if f'{name}.png' in files:
                    p = os.path.join(root, f'{name}.png')
                    if os.path.getmtime(p) >= before - 1 and os.path.getsize(p) > 0:
                        time.sleep(0.3)
                        return p
            time.sleep(0.1)
        raise TimeoutError('screenshot not written')

    def save(self, path):
        return self.cmd(f'save {path}')

    def load(self, path):
        return self.cmd(f'load {path}')

    def speed(self, factor):
        return self.cmd(f'speed {factor}')

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(10)
        except Exception:
            self.proc.kill()
