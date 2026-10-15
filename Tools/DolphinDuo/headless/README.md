# Headless authoring harness

Plays a game in `dolphin-emu-nogui` from a script so you can map its memory without sitting at
the controls: press buttons for exact numbers of frames, dump MEM1, take screenshots and chain runs
through save states. This is how the Path of Radiance companion was mapped.

## Build

Build the headless frontend (no Qt needed):

```sh
cmake -B build -G Ninja -DENABLE_QT=OFF -DENABLE_NOGUI=ON -DCMAKE_BUILD_TYPE=Release
ninja -C build dolphin-nogui dolphin-tool
ln -s "$PWD/Data/Sys" build/Binaries/Sys
```

Rendering uses OpenGL through surfaceless EGL, so Mesa's software renderer (llvmpipe) is enough:
no display or GPU.

## Use

With `DUO_HARNESS=<folder>` set, Dolphin creates the FIFO `<folder>/cmd` and appends one line per
finished command to `<folder>/log` (see `Source/Core/DolphinNoGUI/DuoHarness.cpp`). The Python
helpers wrap that:

```sh
export DUO_DOLPHIN=build/Binaries/dolphin-emu-nogui DUO_GAME=GFEE01.rvz
python3 play.py - title.sav w:600 START w:200 shot:title      # boot, save a state at the title
python3 play.py title.sav menu.sav 'A*3' w:120 shot:menu       # continue from it
python3 play.py menu.sav - w:10 mem:/tmp/before.raw RIGHT mem:/tmp/after.raw
```

Then diff the dumps for the value you are after (`python3` with `open(...).read()` is plenty), and
check your manifest against them with `dolphin-tool duo preview --mem1`.

Notes:

* Emulation runs unthrottled (about 75 fps for Path of Radiance on two cores with llvmpipe).
* Loading a state while the game is in its menus can make it re-check the memory card and return to
  the main menu; states taken in battle are fine.
* Screenshots land in `<user folder>/ScreenShots/<game id>/`; `sheet.py` makes contact sheets.
