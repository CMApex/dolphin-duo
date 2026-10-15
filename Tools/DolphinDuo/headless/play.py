"""play.py <in.sav|-> <out.sav|-> <step> [step...]   (game image in DUO_GAME)

Steps: A, B, START, UP..., 'A:12' (hold 12 frames), 'A*10' (press ten times), 'w:120' (wait
frames), 'shot:name', 'mem:path' (dump MEM1), 'stick:x,y,frames'. Chain runs through save states:
    play.py - title.sav w:600 START w:200 shot:title
    play.py title.sav menu.sav A w:120 shot:menu
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import duo
src, dst, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
d = duo.Dolphin(os.environ['DUO_GAME'])
try:
    if src != '-':
        d.load(src); d.wait(5)
    shots = []
    for s in steps:
        if s.startswith('w:'):
            d.wait(int(s[2:]))
        elif s.startswith('shot:'):
            shots.append(d.shot(s[5:]))
        elif s.startswith('mem:'):
            d.mem1(s[4:])
        elif s.startswith('stick:'):
            x, y, f = s[6:].split(',')
            d.stick(x, y, int(f))
        elif '*' in s:
            b, _, n = s.partition('*')
            for _ in range(int(n)):
                d.press(b, 6); d.wait(20)
        else:
            b, _, f = s.partition(':')
            d.press(b, int(f or 6))
            d.wait(12)
    if dst != '-':
        d.save(dst); d.wait(30)
    print('\n'.join(shots))
finally:
    d.close()
