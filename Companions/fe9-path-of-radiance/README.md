# Fire Emblem: Path of Radiance companion

A Dolphin Duo companion for **Fire Emblem: Path of Radiance** (NTSC-U, `GFEE01`, revision 0), drawn
with the game's own windows: the menu plates and gold frames, the maroon name plate, the ornate stat
window, the message window's patterned navy, the battle forecast window, the gold dragon medallion
that marks the chosen menu entry, character portraits, army crests, item icons, the world map and
the title logo. All of it, and all the text it shows (names, classes, items, terrain, chapter
titles), is read from **your disc** while you play; nothing from the game ships with the companion.
Without a disc image (e.g. a game booted another way) it falls back to drawn frames and the game's
internal IDs (`IKE`, `RANGER`).

| Page | Shows |
|---|---|
| **Battle** | Chapter title and victory condition, turn and phase, a live minimap (terrain, units by army, the cursor), the unit under the cursor or the one you tapped, its items, the terrain under the cursor and army counts. |
| **Army** | Your units and the enemy's, with class, level and HP. Tap a row for its status. |
| **Status** | The selected unit in full: stats with gauges against the class maximums, items with uses and weapon numbers. |

When there is no battle map to follow, every page gives way to a scene view:

* **Title, menus, movies and story scenes:** the parchment map of Tellius in a gold frame, under the
  game's title logo (both from the disc).
* **Battle animations:** the two combatants either side of the game's own forecast window, with
  portraits, class, level, HP, weapon and the numbers the game computed for the fight (HP is shown as
  it was when the fight began, so the outcome isn't spoiled).

The minimap is drawn like the game's parchment war maps: inked terrain with tree, mountain and wave
symbols, unit tokens in army colours and the gold map cursor.

It has two layouts: `thor` (1240x1080, the AYN Thor's bottom screen) and `classic` (4:3, e.g.
1024x768). Dolphin Duo picks the closer one for your screen.

It only reads memory: no button presses, no memory writes.

## Install

Download
[fe9-path-of-radiance.zip](https://github.com/CMApex/dolphin-duo/releases/download/duo-latest-master/fe9-path-of-radiance.zip)
(always the current version) and install it from
**Settings → Interface → Dolphin Duo → Manage Companions**. Each
[release](https://github.com/CMApex/dolphin-duo/releases) also carries the zip that goes with it.

To install from a checkout instead, copy this folder to
`<Dolphin user folder>/Duo/Companions/fe9-path-of-radiance/` (or zip `manifest.json` and `fonts/`).

## Rebuilding

```sh
python3 generate.py
dolphin-tool duo validate .
```

`generate.py` builds `manifest.json`; edit the generator, not the manifest.

To see a page without a device, dump MEM1 from Dolphin's memory view (or with the headless harness
in `Tools/DolphinDuo/headless`) and render it:

```sh
dolphin-tool duo preview . --mem1 mem1.raw --disc GFEE01.rvz --screen 1240x1080 --page battle --json > snap.json
node ../../Tools/DolphinDuo/preview/render.mjs snap.json battle.png
```

## Memory map

The top of `generate.py` documents every address. In short:

* **Units** are 0x280-byte records from `0x802B14F8`. Each points to its character record and its
  class record. Displayed stats are *class base + character modifier + gains*: the unit record only
  stores what the unit gained. This was checked against the in-game status screen for Ike (Ranger)
  and Boyd (Fighter).
* **Army** comes from the list the unit belongs to (`+0x17C` → `+0x08`: 0 player, 1 enemy, 2 other).
* **Turn, phase, gold and the victory condition** live at `0x80330719`–`0x8033072F`.
* **The cursor** is two floats in tile units at `0x8032B238`. During events and the enemy phase the
  game parks it at 0, 0 (in the map border); the companion hides it then.
* **Combatants:** the two units in a fight are copied to `0x80330EC0` (attacker) and `0x803311A0`
  (defender), followed by the forecast (Atk, Def, AS, Hit, Avoid, Crit, Dodge). The copies hold the HP
  *after* the fight; the unit records keep the HP from before until the animation ends.
* **Scenes:** without a map (width 0) and units the game is in its title, menus, a movie or a story
  scene. A battle scene fills a block at `0x802EFF18` (two u16 1s, then pointers); it is empty on the
  map, at Game Over and back at the title, where the map size and units linger.
* **Terrain** is read from the chapter's `map.bin`, which the game keeps in RAM with its symbol
  table: `panelindex` (one u16 per tile) indexes `uniquepanel`, whose entries point to the terrain
  name. The companion finds both symbols by name, so it works on every chapter's map.

## Not yet

* Units that have already acted are not greyed out (the flag is not in the unit record).
* HP during a battle animation doesn't count down with the game's gauges (the live values are on the
  heap); the combat view shows the HP from the start of the fight.
* Healing and other one-sided animations still show the forecast numbers.
* Battle forecast, skills, weapon ranks, supports and the biorhythm are not mapped yet.
* Large portraits: `Face/<name>.cms` #0 is drawn with separate eye and mouth layers whose positions
  are not mapped yet; the companion uses the complete small portrait (#13).

## Credits

Fonts: [Marcellus](https://fonts.google.com/specimen/Marcellus) and
[Cinzel](https://fonts.google.com/specimen/Cinzel), SIL Open Font License (see `fonts/`).
