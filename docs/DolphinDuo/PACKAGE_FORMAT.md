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

Values are read big-endian, as the console stores them. A read that fails (unmapped address, null
pointer) produces an *unavailable* value, shown as `?`.

### Pointer chains

`offsets` work like Dolphin Memory Engine: with `"address": A, "offsets": [o1, o2, …, on]`, Duo
reads a pointer at `A`, then reads a pointer at `ptr + o1`, … and the value lives at `ptr + on`.
Every intermediate pointer must point into RAM or the value is unavailable.

```json
{ "name": "leader_hp", "type": "u16", "address": "0x80123460", "offsets": ["0x10", "0x4"] }
```

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
  — `"gc"` or `"wii"` — `$frame`, `$time`, `$page`, `$index`)
* Element access: `units[2].hp`
* Operators, low to high: `?:`, `||`, `&&`, `|`, `^`, `&`, `== !=`, `< <= > >=`, `<< >>`, `+ -`,
  `* / %`, unary `! - ~`
* Functions: `min(…)`, `max(…)`, `abs`, `floor`, `ceil`, `round`, `clamp(x, lo, hi)`, `bit(x, n)`,
  `bits(x, shift, width)`, `count(array)`, `table(name, key)`, `default(x, fallback)`, `str(x)`

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
