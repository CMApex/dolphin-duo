# Dolphin Duo roadmap

## Done (second pass)

- Canvas pages: per-screen-shape `layouts`, a `theme` (colours, package fonts, styles), positioned
  elements (frame, text, bar, pips, image, SVG path, tile-map grid, repeat, group), UI `state` with
  `set` steps, `address_expr`, `find()`/`countif()`.
- Android canvas renderer, plus a desktop twin (`Tools/DolphinDuo/preview`) for previews from dumps.
- Headless authoring harness in `dolphin-emu-nogui` (`Tools/DolphinDuo/headless`): scripted input,
  MEM1 dumps, screenshots and save states, for mapping a game without anyone at the controls.

## Done (first pass)

- Runtime on the emulation thread: game ID + revision lock, values, pointer chains, bitfields,
  strings (ASCII/UTF-8, Shift-JIS, UTF-16BE), arrays with filters and pointer tables, computed
  values, tables, templates.
- Widgets: heading, text, bar, stats, list, buttons, map, divider. Pages with visibility rules.
- Actions: frame-exact button macros, clamped memory writes, page switches; writes can be disabled
  and are blocked in RetroAchievements hardcore mode.
- Built-in Inspector with per-game watch files.
- Android: second-display presentation, Canvas renderer with scrolling and haptics, installer with
  validation and safe unzip, companion manager, settings.
- `dolphin-tool duo validate|preview` against MEM1/MEM2 dumps.
- Unit tests for the pure runtime.

## Needs a device

These could not be checked in the environment the first pass was written in:

- [ ] Build the APK (`./gradlew assembleDebug`) and fix anything the Kotlin compiler finds; the
      Kotlin code was reviewed but not compiled.
- [ ] Confirm the Thor's bottom screen is reported as a presentation display and the companion
      appears there; check the AYANEO Pocket DS, where the bottom screen is the primary display.
- [ ] Button macros with the touch overlay hidden and with a physical controller mapped.
- [ ] Frame-time impact at 20 Hz with a large list on a Snapdragon 865 (Thor Lite).

## Next

- [x] **First real companion**: Fire Emblem: Path of Radiance (`Companions/fe9-path-of-radiance`),
      with canvas layouts for the Thor and 4:3 screens.
- [ ] Path of Radiance: battle forecast, skills, weapon ranks, units that have acted.
- [ ] Radiant Dawn (RFEE01 rev 1) on the same framework.
- [ ] **Swap screens**: run the game on the bottom panel and the companion on top.
- [ ] **Disc assets**: let companions read files from the running disc (`disc:` paths) for map
      backgrounds and icons, like Eden Duo builds art from the game's own files. Needs Yaz0, RARC/U8,
      BTI/TPL decoding.
- [ ] **GBA link on the second screen** for Four Swords Adventures, Crystal Chronicles and
      Pac-Man Vs., if Dolphin's mGBA integration can be enabled on Android.
- [ ] **Touch as Wii pointer**: map the companion screen to IR for pointer-heavy games.
- [ ] Desktop frontend (a second window in DolphinQt) for authoring with a real game.
- [ ] Hot reload of a companion folder while the game runs (authoring loop on device).
- [ ] Native module ABI, only if a companion ever needs logic manifests cannot express.

## Upstream

This is a fork of [dolphin-emu/dolphin](https://github.com/dolphin-emu/dolphin). Pull upstream with a
normal merge; Duo touches upstream files in a few small places:

| File | Change |
|---|---|
| `Source/Core/Core/Core.cpp` | Calls the runtime from `OnFrameEnd` and on CPU thread exit. |
| `Source/Core/Core/Config/MainSettings.*` | `Duo.Enabled`, `Duo.AllowMemoryWrites`. |
| `Source/Core/Core/CMakeLists.txt`, `Source/UnitTests/Core/CMakeLists.txt` | New sources and test. |
| `Source/Core/DolphinTool/` | `duo` command. |
| `Source/Android/jni/CMakeLists.txt` | JNI source. |
| `Source/Android/app/.../EmulationActivity.kt` | Starts and stops the companion screen. |
| `Source/Android/app/.../settings/` | Two switches and the companion manager entry. |
| `AndroidManifest.xml`, `res/values/strings.xml` | Activity and English strings. |
