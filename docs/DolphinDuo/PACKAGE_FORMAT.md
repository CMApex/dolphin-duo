# Dolphin Duo companion format (format 1)

A companion is a folder (or a `.zip` of one) containing a `manifest.json` and, optionally, images
it refers to. The manifest declares **what to read** from the running game, **how to show it** on
the second screen and **what the player can do** from there. It is data only: no code runs.

```
my-companion/
├── manifest.json
└── maps/
    └── chapter1.png      (optional, referenced by a map widget)
```

Install it from **Settings → Interface → Dolphin Duo → Manage Companions**, or copy the folder to
`<Dolphin user folder>/Duo/Companions/<id>/`.

While writing one, use the desktop tool for instant feedback:

```sh
dolphin-tool duo validate my-companion/
dolphin-tool duo preview my-companion/ --mem1 mem1.raw [--mem2 mem2.raw] [--page map] [--json]
```

`mem1.raw`/`mem2.raw` come from Dolphin's memory view (**Dump MEM1** / **Dump MEM2**). The
preview prints a text version of each page using the values in the dump.

---

## Top level

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | Always `1` for now. Newer formats are refused with a clear message. |
| `id` | yes | Folder name and identity. Letters, digits, `-`, `_`, `.`; at most 64 characters. |
| `name`, `version` | yes | Shown on the companion screen and in the installer. |
| `author`, `description` | no | Shown in the installer and companion list. |
| `targets` | yes | `[{"game_id": "GZLE01", "revisions": [0]}]`. Omit `revisions` to accept any. |
| `controller` | no | Controller port (0–3) button presses go to. Default `0`. |
| `poll_hz` | no | How often values are read and the screen updates, 1–60. Default `20`. |
| `capabilities` | no | `"write_memory"` and/or `"input"`. Required by write and press steps. |
| `tables` | no | Text lookup tables, see below. |
| `values` | no | Top-level values, evaluated in order. |
| `arrays` | no | Repeated structures (party, inventory, units…). |
| `pages` | yes | At least one page of widgets. |
| `actions` | no | Things buttons, list rows and map points can trigger. |
| `layouts` | no | Design canvases per screen shape, for [canvas pages](#canvas-pages). |
| `theme` | no | Named colours, fonts and element styles for canvas pages. |
| `state` | no | UI state such as the selected unit, changed by `set` steps. |

Keys starting with `//` or `_` are comments and ignored anywhere. **Any other unknown key is an
error**, which catches typos like `"adress"`.

### Why exact revisions matter

A companion reads raw memory, and addresses move between disc revisions and regions. Dolphin Duo
checks the running game ID and revision before loading a companion; on a mismatch it does not load
and the screen explains which revision the companion wants. Dolphin shows a game's ID and revision
in its game properties.

---

## Values

```json
{ "name": "money", "type": "u16", "address": "0x80123450" }
```

| Key | Meaning |
|---|---|
| `name` | Identifier used in expressions: letters, digits, `_`. |
| `type` | `u8` `s8` `u16` `s16` `u32` `s32` `f32` `f64` `bool` `string` |
| `address` | Effective address, e.g. `"0x80123456"` (MEM1) or `"0x90123456"` (Wii MEM2). |
| `offsets` | Pointer chain, see below. |
| `mask`, `shift` | For bitfields: `(raw & mask) >> shift`. Integer and bool types only. |
| `length` | Strings: bytes to read (1–256). Text stops at the first NUL. |
| `encoding` | Strings: `ascii` (default, also correct for UTF-8), `sjis`, `utf16`. |
| `expr` | A computed value instead of a memory read (no `type`/`address`). |
| `address_expr` | An address computed from earlier values, instead of `address`. `offsets` still apply. |

Values are read big-endian, as the console stores them. A read that fails (unmapped address, null
pointer) produces an *unavailable* value, shown as `?`.

### Pointer chains

`offsets` work like Dolphin Memory Engine: with `"address": A, "offsets": [o1, o2, …, on]`, Duo
reads a pointer at `A`, then reads a pointer at `ptr + o1`, … and the value lives at `ptr + on`.
Every intermediate pointer must point into RAM or the value is unavailable.

```json
{ "name": "leader_hp", "type": "u16", "address": "0x80123460", "offsets": ["0x10", "0x4"] }
```

### Computed addresses

Some data is only reachable through a table the game builds at run time: a map file whose symbol
table says where the terrain grid starts, an index into a list of records. `address_expr` computes
the address from values declared earlier (in arrays: from earlier fields of the same element, or
from values), and `offsets` then work as usual:

```json
{ "name": "map_data", "type": "u32", "address": "0x802EFE6C" },
{ "name": "map_symbols", "type": "u32", "address_expr": "map_data - 0x1C" }
```

Arrays accept `address_expr` in place of `address` too. A computed address that is null or 0 makes
the value unavailable (an array becomes empty).

### Computed values

```json
{ "name": "hp_percent", "expr": "hp * 100 / max_hp" }
```

A computed value can use any value declared **before** it, any array (`count(units)`,
`units[0].hp`), tables and system values.

---

## Arrays

```json
{
  "name": "party",
  "address": "0x80400000",
  "stride": "0x48",
  "count": 8,
  "where": "hp > 0",
  "fields": [
    { "name": "name", "type": "string", "offset": "0x00", "length": 12 },
    { "name": "hp", "type": "u8", "offset": "0x12" },
    { "name": "max_hp", "type": "u8", "offset": "0x13" },
    { "name": "pct", "expr": "hp * 100 / max_hp" }
  ]
}
```

| Key | Meaning |
|---|---|
| `address`, `offsets` | Where element 0 starts (pointer chains allowed). |
| `stride` | Bytes between elements. |
| `pointer_table` | `true` if each slot holds a *pointer* to the element (null slots are skipped). |
| `count` or `count_expr` | Fixed number of slots, or an expression such as `"party_size"`. |
| `max_count` | Safety cap, default 256, at most 1024. |
| `where` | Elements for which this is false are skipped. |
| `fields` | Like values, but `offset` (relative to the element) instead of `address`. Fields may have their own `offsets` chain and can be computed from earlier fields. |

Inside an element, `$index` is its position after filtering, `$slot` its position in memory and
`$address` its address.

Arrays are read lazily: the first expression that touches an array reads it, so arrays can depend
on values and values can depend on arrays.

---

## Expressions

Used by `expr`, `count_expr`, `where`, `visible_if`, `enabled_if`, bar values, map coordinates and
inside `{…}` in text.

* Literals: `12`, `0x1F`, `1.5`, `'text'`, `"text"`, `true`, `false`, `null`
* Names: values, element fields, system values (`$game_id`, `$revision`, `$title`, `$platform`
  — `"gc"` or `"wii"` — `$frame`, `$time`, `$page`, `$index`, and `$disc`: true when
  [`disc:` images](#disc-images) can be loaded)
* Element access: `units[2].hp`
* Operators, low to high: `?:`, `||`, `&&`, `|`, `^`, `&`, `== !=`, `< <= > >=`, `<< >>`, `+ -`,
  `* / %`, unary `! - ~`
* Functions: `min(…)`, `max(…)`, `abs`, `floor`, `ceil`, `round`, `clamp(x, lo, hi)`, `bit(x, n)`,
  `bits(x, shift, width)`, `count(array)`, `table(name, key)`, `default(x, fallback)`, `str(x)`,
  `find(array, 'field', value[, 'field2', value2])` (index of the first matching element, or
  null), `countif(array, 'field', value[, 'field2', value2])` and `substr(text, start[, length])`
* State and layout: names from `state` (see below), and `$layout`, the canvas layout being drawn

Arithmetic with an unavailable value is unavailable, so a failed read shows as `?` instead of a
misleading `0`. `default(x, y)` substitutes a fallback. `==`/`!=` work with `null`, so
`visible_if: "boss_hp != null"` hides a widget until the value exists.

## Text templates

Any `text`, `label`, `detail`, `badge`, `title` or `color` is a template: literal text with `{expr}`
or `{expr:format}` holes.

| Format | Example | Result |
|---|---|---|
| default | `{hp}` | `42`, decimals to 2 places |
| width | `{hp:4}` / `{name:-8}` | right / left aligned |
| zero pad | `{gold:07}` | `0001234` |
| decimals | `{speed:.1}` | `3.3` |
| hex | `{flags:x}` / `{flags:08X}` | `1f` / `0000001F` |
| percent | `{ratio:%}` | `25%` |

Write `{{` and `}}` for literal braces.

## Tables

```json
"tables": { "class": { "0": "Lord", "1": "Archer", "0x10": "Sage" } }
```

`table(class, unit_class)` returns the text for a key (hex keys are allowed).

### Text from the disc

Names of characters, classes and items belong to the game, so a companion should not ship them.
Instead, a table can be the game's own message file, read from the disc when the companion loads:

```json
"tables": { "msg": "disc:Mess/common.m" },
"values": [ { "name": "hero", "expr": "default(table(msg, hero_key), substr(hero_key, 5))" } ]
```

The game's text then shows in the language of the player's disc. Supported today: Intelligent
Systems message tables (`.m`, as in Fire Emblem: Path of Radiance and Radiant Dawn), LZ10-compressed
or not; text is Shift-JIS on Japanese releases (game IDs ending in `J`) and Windows-1252 otherwise.
Without a disc image the table is empty, so give `table()` a `default`, as above.

---

## Pages and widgets

```json
{ "id": "party", "title": "Party ({count(party)})", "visible_if": "in_menu", "widgets": [ … ] }
```

Pages appear as tabs; hidden pages disappear from the tab bar. Every widget accepts `visible_if`.

| Widget | Keys |
|---|---|
| `heading` | `text` |
| `text` | `text`, `style` (`body` `caption` `title` `mono`), `color` |
| `bar` | `label`, `value`, `max`, `text`, `color` |
| `stats` | `columns` (1–6), `items: [{label, value}]` |
| `list` | `source` (array), `text`, `detail`, `badge`, `color`, `bar` + `bar_max`, `action`, `empty` |
| `buttons` | `buttons: [{label, action, enabled_if}]` |
| `map` | `bounds: [min_x, min_y, max_x, max_y]`, `flip_y`, `aspect`, `image`, `points` |
| `divider` | — |

List templates are evaluated per element, so `{name}` in a list means the element's `name`.

### Maps

`bounds` are in game coordinates (numbers or expressions, so they can follow the current area).
Each entry in `points` is either one point (`x`, `y`) or one per element of `source`:

```json
{ "type": "map", "bounds": [-5000, -5000, 5000, 5000], "flip_y": true, "image": "maps/field.png",
  "points": [
    { "source": "enemies", "x": "x", "y": "z", "color": "#ff5252", "where": "alive" },
    { "x": "player_x", "y": "player_z", "shape": "player" }
  ] }
```

Shapes: `dot`, `player`, `square`, `diamond`. Points can trigger an `action`. Images come from the
companion's own folder only.

---

## Actions

```json
{ "id": "heal", "steps": [ { "write": "party[].hp", "value": "max_hp" } ] }
```

| Step | Meaning |
|---|---|
| `{"press": "START", "frames": 4}` | Hold a control for N frames (1–600, default 4), then release. Analog controls take `"amount"` (-1…1). Needs `input`. |
| `{"wait": 10}` | Wait N frames. |
| `{"write": "gold", "value": "gold + 100"}` | Write a value. Needs `write_memory`. |
| `{"write": "party[].hp", "value": "max_hp", "index": "$index"}` | Write an array field; `index` defaults to the tapped row. |
| `{"page": "map"}` | Switch pages. |
| `{"set": "sel", "value": "$index"}` | Set a `state` entry. Touches no game memory, so it needs no capability. |

When an action comes from a list row or map point, its expressions see that element's fields.
Writes clamp to the field's range (writing 300 to a `u8` stores 255) and keep the other bits of a
masked field. Players can turn memory writes off in settings, and they are always off in
RetroAchievements hardcore mode.

Steps run on the emulation thread, frame by frame, so press timings are exact. Many games read
input once per *game* frame, which is every second video frame at 30 fps, so hold buttons for at
least 4 frames.

**Controls.** GameCube: `A B X Y Z START UP DOWN LEFT RIGHT L R L_ANALOG R_ANALOG STICK_X STICK_Y
C_STICK_X C_STICK_Y`. Wii Remote: `WII_A WII_B WII_1 WII_2 WII_PLUS WII_MINUS WII_HOME WII_UP
WII_DOWN WII_LEFT WII_RIGHT WII_IR_X WII_IR_Y`. Nunchuk: `NUNCHUK_C NUNCHUK_Z NUNCHUK_STICK_X
NUNCHUK_STICK_Y`. Classic Controller: `CLASSIC_A`… (`_B _X _Y _ZL _ZR _PLUS _MINUS _HOME _UP _DOWN
_LEFT _RIGHT _L _R _L_ANALOG _R_ANALOG _LEFT_STICK_X _LEFT_STICK_Y _RIGHT_STICK_X _RIGHT_STICK_Y`).


---

## Canvas pages

Widgets stack top to bottom in Dolphin Duo's own style. A **canvas page** instead places every
element at an exact position on a fixed design canvas, in the game's own visual language: its
colours, fonts, window frames and map cursor. This is how Eden Duo's companions look like part of
their game, and what makes room for different layouts per screen shape.

```json
"layouts": [
  { "id": "thor",    "canvas": [1240, 1080], "background": "@backdrop", "elements": [ … ] },
  { "id": "classic", "canvas": [1024, 768] }
],
"pages": [
  { "id": "battle", "title": "Battle", "elements": { "thor": [ … ], "classic": [ … ] } }
]
```

* The runtime picks the layout whose canvas shape is closest to the companion screen (the AYN
  Thor's bottom screen is 1240x1080; 4:3 handhelds get `classic` above). The canvas is scaled to
  fit and centred, and `background` fills any margin.
* A layout's own `elements` are drawn under every page: backdrop, title bar, page tabs.
* A page's `elements` is either one list for every layout or an object keyed by layout id, with
  `"*"` for any layout not listed. A page can keep `widgets` for screens without a canvas.
* Tabs are just elements whose `action` runs a `page` step; `$page` tells which page is showing.

### Elements

Every element has `type`, optional `visible_if`, and optional `action` (tapping it runs the action;
inside a `repeat`, the action sees that element, like a list row). Numbers (`x`, `y`, `w`, `h`,
`size`, `value`, …) are expressions; text properties (`text`, colours, `font`, `align`, `d`, …) are
templates, so both can follow the game.

| Type | Properties |
|---|---|
| `rect` | `x y w h`, `fill`, `fill2` (vertical gradient), `stroke`, `stroke_width`, `radius` |
| `frame` | A window: `fill`/`fill2` body, `edge` outline, `trim` inner line `inset` px inside, `trim_width`, `glow` (top sheen), `corner`: `notch` (clipped corners with lozenges), `diamond`, `bracket` or `none`, `corner_size`, `radius` |
| `text` | `x y w h`, `text`, `font`, `size`, `weight` (`bold`), `color`, `fill2` (gradient), `align` (`left` `center` `right`), `valign` (`top` `middle` `bottom`), `fit` (1: shrink to the width, then ellipsize), `min_size`, `shadow` + `shadow_dx`/`shadow_dy`, `outline` + `outline_width`, `letter_spacing` (em), `case` (`upper`) |
| `bar` | `x y w h`, `value`, `max`, `fill`/`fill2` (horizontal gradient), `track`/`track2`, `stroke`, `radius`, `count` (segment ticks) with `off` and `gap` |
| `pips` | `x y w h`, `count`, `filled`, `fill`, `off`, `shape` (`diamond` `circle` `square`), `gap` |
| `image` | `x y w h`, `src` (a file in the companion folder, or a [`disc:` image](#disc-images)), `crop` `[x, y, w, h]` (part of the image, e.g. one icon of an atlas), `slice` `[left, top, right, bottom]` (nine-slice insets in source pixels) with `slice_scale`, `flip_x`, `tint`, `fallback` (another `src`, drawn when `src` can't be loaded, e.g. an image some characters lack). `blend`: `screen` or `add` (for art drawn on black that the game adds onto the screen, such as a logo) or `multiply`. `crop` and `slice` entries are expressions. |
| `path` | `x y w h`, `d` (SVG path data), `viewbox` `[x, y, w, h]`, `fill`, `stroke`, `stroke_width`: icons and ornaments without image files |
| `grid` | A tile map: `x y w h`, `cols`, `rows`, optional `view_cols`/`view_rows` with `center_x`/`center_y` to show a window that follows something, `source` + `cell` (colour per cell, from an array in row-major `$slot` order), `grid_color`, `grid_width`, `bevel` (0-1: a light and a dark edge on every tile, for relief), `layers` of marks |
| `group` | `x`, `y`, `elements`: children placed relative to it |
| `repeat` | `source` array, `x`, `y`, `dx`, `dy`, `columns`, `limit`, `start`, `where`, `elements`: children once per (matching) element, which see its fields |

`opacity` (0–1) works on everything. Grid `layers` take `source`, `x`, `y` (in cells), `where`,
`fill`, `stroke`, `shape` (`dot` `square` `diamond` `ring` `cursor`; `token`, a unit counter with a shadow, rim and glint; map symbols `tree` `peak` `wave`), `size` (fraction of a cell),
`label` and `action`.

### Disc images

A companion can show the game's own art without shipping any of it: an `image` whose `src` starts
with `disc:` is read from the player's disc while the game runs, decoded with Dolphin's GameCube
texture decoders and cached as a PNG under `<Dolphin user folder>/Duo/Cache/<game id>_r<revision>/`.

| `src` | Meaning |
|---|---|
| `disc:window/level.tpl#3` | Image 3 of a TPL texture file. |
| `disc:Face/IKE.cms#13` | `.cms`/`.cmp` (LZ10-compressed) files are decompressed first. |
| `disc:zmap/bmap01/map.cmp>texpack.tpl#0` | A member of a `pack` archive. |
| `disc:Face/OSCAR.cms#-1` | A negative number counts from the end: `#-1` is the last image. |

`src` is a template, so it can follow the game: `"disc:Face/{substr(fid, 4)}.cms#-1"` shows the
portrait of whichever character is selected. (Path of Radiance keeps the small portrait last, but
the number of images before it varies between characters, hence `#-1`.) Window frames usually need `slice`: corners keep their
size (times `slice_scale`), edges stretch along one axis and the centre along both, so one texture
fits any panel. Use `crop` to pick one icon out of an atlas, or one piece of a texture that packs
several.

`$disc` is true when disc images can be loaded (the game was started from a disc image), so a
companion can draw its own frames otherwise:

```json
{ "type": "frame", "style": "window", "x": 16, "y": 16, "w": 600, "h": 120, "visible_if": "!$disc" },
{ "type": "image", "src": "disc:window/filemenu.cms#2", "x": 16, "y": 16, "w": 600, "h": 120,
  "slice": [44, 0, 44, 0], "slice_scale": 2, "visible_if": "$disc" }
```

To find textures, extract the disc with `dolphin-tool extract` and look through its `.tpl`/`.cms`
files; `dolphin-tool duo preview --disc <image>` decodes the ones a page uses for the desktop
renderer.

### Theme

```json
"theme": {
  "colors": { "gold": "#D9B65A", "navy": "#13246E", "veil": "#80000000" },
  "fonts":  { "ui": "fonts/Marcellus-Regular.ttf", "title": "fonts/Cinzel.ttf" },
  "styles": { "window": { "fill": "@navy", "edge": "#1A1206", "trim": "@gold", "corner": "notch" } }
}
```

* Colours are `#RGB`, `#RRGGBB` or `#AARRGGBB`: **alpha comes first**, as on Android. Anywhere a
  colour goes, `@name` uses a theme colour.
* `fonts` are files in the companion folder; text picks one with `"font": "ui"`. `serif`, `sans` and
  `mono` are always available. Ship only fonts whose licence allows it (the OFL fonts on Google
  Fonts are a good source) and include the licence.
* `styles` are presets an element names with `"style": "window"`; its own keys win.

### State

```json
"state": { "sel": -1 },
"actions": [ { "id": "pick", "steps": [ { "set": "sel", "value": "$index" } ] } ]
```

`state` entries are plain names in expressions. They start at their initial value whenever the
companion loads and change only through `set` steps, so they suit UI choices: the unit whose card is
showing, a sort order, a page of a long list.

### Previewing canvas pages on desktop

```sh
dolphin-tool duo preview my-companion --mem1 mem1.raw --screen 1240x1080 --page battle --json > snap.json
node Tools/DolphinDuo/preview/render.mjs snap.json battle.png
```

`--screen` picks the layout as the device would (or name one with `--layout`), and `--state
sel=3` sets state. The renderer (headless Chromium through Playwright) draws exactly what the
Android companion screen draws.

---

## Watches: the zero-effort companion

With no companion installed, the second screen shows the built-in **Inspector**: game ID, revision,
platform and frame counter. Add a file `Duo/Watches/<GAMEID>.txt` to watch addresses live while you
hunt for them:

Addresses here are made-up examples.

```
# type[:length]  address  [pointer offsets...]  label
u32       0x80123450                Money
u8        0x80123460  0x10  0x4     Leader HP
string:16 0x80400000                Player name
```
