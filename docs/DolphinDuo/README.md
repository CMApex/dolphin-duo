# Dolphin Duo

Dolphin Duo is a fork of Dolphin for dual-screen Android handhelds such as the **AYN Thor**. The
game runs on the main screen as usual; the second screen shows a **live companion** for the game
you are playing: maps, party and unit stats, inventories, boss health, quick actions. Each
companion is a small, declarative package written for one game.

It was inspired by [Eden Duo](https://github.com/igawa6/eden-duo), which does the same for Switch
games, and by [EmuLnk](https://github.com/EmuLnk/emulnk), which does it across emulators through a
separate app. Dolphin Duo runs inside the emulator: values are read on the emulation thread between
frames, there is no second app and no network hop, and button macros are frame-exact.

## Using it

1. Install the APK and play on a device with a second display. Dolphin Duo looks for an Android
   *presentation* display other than the one the game is on (the Thor's bottom screen).
2. With nothing installed you get the **Inspector**: game ID, revision and a live view of any
   addresses listed in `Duo/Watches/<GAMEID>.txt`.
3. Install companions from **Settings → Interface → Dolphin Duo → Manage Companions** (a `.zip` or a
   bare `manifest.json`). The installer shows which games a companion targets and whether it can
   change memory or press buttons.
4. If several companions target the running game, a **Switch** chip appears on the companion
   screen.

Settings (Interface → Dolphin Duo):

* **Companion Screen**: turn the second screen off entirely. On single-screen devices nothing runs
  either way.
* **Allow Companions to Change Game Memory**: lets companion actions write memory. Always off in
  RetroAchievements hardcore mode.

## Writing a companion

1. Find addresses: Dolphin's memory view and cheat search on desktop, or Dolphin Memory Engine.
2. Watch them live on the device by listing them in `Duo/Watches/<GAMEID>.txt`.
3. Start from [`examples/template`](examples/template/manifest.json) and read
   [PACKAGE_FORMAT.md](PACKAGE_FORMAT.md).
4. Check it on desktop:

   ```sh
   dolphin-tool duo validate my-companion/
   dolphin-tool duo preview my-companion/ --mem1 mem1.raw --page party
   ```

   Want to see the template without a game?
   `python3 docs/DolphinDuo/examples/template/make_demo_dump.py demo-mem1.raw` writes a fake MEM1
   dump laid out for it.
5. Zip the folder and install it.

Companions contain no game assets: images are optional and must be the author's own.

## How it fits together

```
 Android UI thread                         Emulation (CPU) thread
 ─────────────────                         ──────────────────────
 EmulationActivity                         VideoInterface::EndField
   └─ DuoScreenController                    └─ Core::OnFrameEnd
        └─ DuoPresentation (2nd display)          └─ Duo::Runtime::OnFrameEnd
             └─ DuoCompanionView (Canvas)              ├─ load companion for game ID + revision
                  │  taps / page changes               ├─ ActionRunner (one step per frame)
                  ▼                                    ├─ Evaluator::Sample (MMU HostTryRead)
           DuoNative (JNI) ── requests ──────────▶     └─ Evaluator::Render → JSON snapshot
                  ▲                                              │
                  └────────── latest snapshot ◀──────────────────┘
```

* `Source/Core/Core/Duo/`: the runtime. `Expression`, `Template`, `Manifest`, `Evaluator` and
  `ActionRunner` are pure and unit tested (`Source/UnitTests/Core/DuoTest.cpp`) against a fake
  big-endian memory; `Runtime` connects them to Dolphin.
* `Source/Android/jni/Duo/DuoNative.cpp`: JNI bridge and the input sink, which presses buttons
  through the same `InputOverrider` the touchscreen overlay uses.
* `Source/Android/app/.../features/duo/`: second-screen presentation, Canvas renderer, installer
  and companion manager.
* `Source/Core/DolphinTool/DuoCommand.cpp`: `dolphin-tool duo validate|preview`.

The runtime does nothing until the Android frontend attaches a second screen, so desktop builds and
single-screen phones pay no cost.

## Status

See [ROADMAP.md](ROADMAP.md).
