#!/usr/bin/env python3
"""Generates manifest.json for the Fire Emblem: Path of Radiance (GFEE01) Dolphin Duo companion.

Edit this file, not manifest.json. Nothing from the game is included: names and other text come from
the disc's message table (Mess/common.m) and window art, portraits and icons from its textures, both
read while the game runs. Fonts are OFL (see fonts/).

Memory map (NTSC-U GFEE01, rev 0), found with the headless harness (Tools/DolphinDuo/headless):

  Units      0x802B14F8 + n*0x280 (a pool; empty slots have no character record)
    +0x174 -> character record  (+0x00 PID, +0x04 MPID name, +0x1C build mod, +0x1D..0x24 HP..Res mods)
    +0x178 -> class record      (+0x00 JID, +0x04 MJID name, +0x3D build, +0x3E move,
                                 +0x44..0x4B HP..Res bases, +0x4C..0x53 caps)
    +0x17C -> army list         (+0x08 army: 0 player, 1 enemy, 2 other)
    +0x186 level  +0x187 EXP  +0x18A x  +0x18B y  +0x194 HP  +0x196 move bonus
    +0x197..0x19E HP..Res gains   +0x1B8 + k*8 items (u32 item record, u8 uses)
    Displayed stat = class base + character modifier + gain (checked on the status screen).
  Item record   +0x00 IID  +0x04 MIID name  +0x12 weapon type (0 sword, 1 lance, 2 axe, 3 bow,
                4-7 fire/thunder/wind/light, 8 knife, 9 laguz, 10 item, 11 ring)  +0x14 bit 0x80: staff
                +0x15 item number (window/icon.tpl #0 icon = 96 + this)  +0x18 max uses  +0x19 might
                +0x1A hit  +0x1B weight
                +0x1C crit  +0x1D/+0x1E range (might checked: Ike Atk 11 = Str 5 + Trainer 6)
  Chapter state 0x80330719 phase, 0x8033071A turn (u16), 0x80330720 gold, 0x80330728 victory key
  Cursor        0x8032B238 / 0x8032B23C (f32 tile x / y); parked at 0, 0 during events
  Combatants    0x80330EC0 + n*0x2E0, n = 0 (attacker), 1 (defender): copies of the two unit records,
                then the forecast as s16s: +0x284 Atk, +0x286 Def, +0x288 AS, +0x28A Hit, +0x28C Avoid,
                +0x290 Crit, +0x292 Dodge (weapon triangle included). Kept after the fight ends.
  Scenes        Title, menus, movies and story scenes have no map (width 0) and no units. During a
                battle scene 0x802EFF18 holds two u16 1s and then pointers (0 otherwise); map.bin is
                unloaded then (data pointer 0). Map size and units linger through Game Over and
                back to the title, so they alone can't tell a battle scene.
                Lead, not yet reliable: live gauge HP at [[[0x802EFF24] + 0x32C] + 0x4B0/0x4B4] + 0x6B
                (left/right) held through the Greil fight but not in a Boyd fight dump.
  Map           0x802EFE60 width, +2 height, +0x0C -> map.bin data, +0x3C map name ("bmap01")
                map.bin keeps its symbol table; panelindex (u16 per cell) indexes uniquepanel
                (12 bytes: four corner heights, then a pointer to the terrain name).
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

UNIT_BASE = 0x802B14F8
UNIT_STRIDE = 0x280
UNIT_SLOTS = 96
ITEM_SLOTS = 7
STATS = ['hp', 'str', 'mag', 'skl', 'spd', 'lck', 'def', 'res']
STAT_LABELS = {'str': 'Str', 'mag': 'Mag', 'skl': 'Skill', 'spd': 'Sp', 'lck': 'Lck', 'def': 'Def', 'res': 'Res'}

# --------------------------------------------------------------------------------------------
# Palette, taken from the game's own windows (status screen, map menu, title plates).

COLORS = {
    'backdrop': '#0E2A27', 'backdrop2': '#051311',
    'navy': '#13246E', 'navy2': '#08113F', 'navy_glow': '#335B8CFF',
    'parch': '#EDE1B4', 'parch2': '#CDB574', 'parch_edge': '#5A4214', 'parch_trim': '#A8853A',
    'maroon': '#6A2218', 'maroon2': '#3A0E09',
    'green': '#205C40', 'green2': '#0B3322',
    'gold': '#D9B65A', 'gold_hi': '#F4DC8A', 'gold_dim': '#8E7232', 'edge': '#1A1206',
    'label': '#F2D35B', 'value': '#FFFFFF', 'header': '#8FC8FF',
    'ink': '#3B2A12', 'ink_soft': '#6B5530', 'cream': '#F6EAC8',
    'hp': '#7BE38A', 'hp2': '#2C9A44', 'hp_text': '#2F8A3E',
    'exp': '#9FD0FF', 'exp2': '#4B86D8',
    'player': '#3D7BFF', 'enemy': '#E23B2E', 'other': '#3FBF5F',
    'track': '#55000000', 'track_light': '#1FFFFFFF',
    'shadow': '#99000000',
}

# Minimap colours by terrain (the Japanese terrain names the map data uses), in the inks of the
# game's parchment war maps: pale ground, olive woods, blue water, terracotta buildings, umber walls.
PAPER = '#D8C592'
TERRAIN_COLOR = {
    '平地': PAPER, '床': '#CDB98C', '石畳': '#BFAE88', '屋根': '#9A5B3A', '木': '#8E9A50',
    '橋': '#B98A4E', '階段': '#B5A68A', '堀': '#4E7FAE', '城門': '#7A4A2A', '川': '#4E86B8',
    '崖': '#8C7458', '壁': '#4A3826', '柵': '#8A6A42', '小物': '#B0A07A', '建物': '#A8603A',
    '茂み': '#AAB065', '宝箱': '#E0B040', '廃墟': '#A8957A', '砦': '#B0643A', '城': '#9A5A36',
    '玉座': '#E0B040', '海': '#3F6FA0', '海岸': '#C9C08A', '池': '#4E86B8', '水路': '#4E86B8',
    '砂漠': '#E4CF92', '雪原': '#EEF0EE', '沼': '#8A9468', '山道': '#A89070', '岩': '#7E7468',
    '扉': '#7A4A2A', '壊れる扉': '#8A5530', '柱': '#5A4A36', '噴水': '#6E9FC8', '花壇': '#A6B060',
    '台座': '#B8AC90', '遺跡': '#A8957A', '船': '#9A6A3E', '穴': '#2A2018', '移動不可': '#3A3024',
    '訪問': '#C07A3A', '到達': '#E0B040', '制圧': '#E0B040', '離脱': '#6AA08A', '防衛': '#C08A4A',
    '土嚢': '#A89068', '光の茂み': '#B8C878', '光の守り': '#B0C4DC', '回復': '#9AC07A',
}

# Inked map symbols drawn over some terrain.
TERRAIN_GLYPH = {
    '木': 'tree', '茂み': 'tree', '光の茂み': 'tree',
    '崖': 'peak', '岩': 'peak', '山道': 'peak',
    '川': 'wave', '海': 'wave', '池': 'wave', '堀': 'wave', '水路': 'wave',
}
GLYPH_INK = {'tree': '#B8424E1E', 'peak': '#A84A3826', 'wave': '#B0E6F0FF'}


# Simple original weapon glyphs on a 24x24 grid.
ICONS = {
    'sword': 'M19 3 L21 5 L9 17 L11 19 L9 21 L7 19 L5 21 L3 19 L5 17 L3 15 L5 13 L7 15 Z',
    'lance': 'M20 2 L22 4 L17 7 L6 18 L7 21 L3 21 L3 17 L6 18 L17 7 Z',
    'axe': 'M8 3 C14 2 18 6 17 11 L12 9 L4 21 L3 20 L11 8 L9 3 Z',
    'bow': 'M6 2 C16 5 19 12 6 22 L7 20 C15 13 14 7 5 4 Z M6 3 L6 21',
    'tome': 'M5 3 L17 3 L19 5 L19 21 L7 21 L5 19 Z M8 6 L16 6 M8 9 L16 9',
    'staff': 'M18 2 C22 4 21 8 18 9 L7 21 L5 19 L16 8 C14 5 15 3 18 2 Z',
    'item': 'M9 3 L15 3 L15 6 L18 9 L18 20 L6 20 L6 9 L9 6 Z',
}


def tables():
    return {
        # Every name the companion shows comes from the disc's own message table.
        'msg': 'disc:Mess/common.m',
        'terrain_color': TERRAIN_COLOR,
        'terrain_glyph': TERRAIN_GLYPH,
        'glyph_ink': GLYPH_INK,
        'weapon_kind': {'0': 'sword', '1': 'lance', '2': 'axe', '3': 'bow', '4': 'tome', '5': 'tome',
                        '6': 'tome', '7': 'tome', '8': 'sword'},
        'icon': dict(ICONS),
        'army_color': {'0': '@player', '1': '@enemy', '2': '@other'},
        'army_name': {'0': 'Player', '1': 'Enemy', '2': 'Other'},
    }


# --------------------------------------------------------------------------------------------
# Memory

def values():
    v = []
    add = v.append
    # Chapter state.
    add({'name': 'phase', 'type': 'u8', 'address': '0x80330719'})
    add({'name': 'turn', 'type': 'u16', 'address': '0x8033071A'})
    add({'name': 'gold', 'type': 'u32', 'address': '0x80330720'})
    add({'name': 'win_key', 'type': 'string', 'address': '0x80330728', 'length': 24, 'encoding': 'sjis'})
    add({'name': 'cursor_xf', 'type': 'f32', 'address': '0x8032B238'})
    add({'name': 'cursor_yf', 'type': 'f32', 'address': '0x8032B23C'})
    add({'name': 'cursor_x', 'expr': 'round(cursor_xf)'})
    add({'name': 'cursor_y', 'expr': 'round(cursor_yf)'})
    # Map.
    add({'name': 'map_w', 'type': 'u16', 'address': '0x802EFE60'})
    add({'name': 'map_h', 'type': 'u16', 'address': '0x802EFE62'})
    add({'name': 'map_data', 'type': 'u32', 'address': '0x802EFE6C'})
    add({'name': 'map_name', 'type': 'string', 'address': '0x802EFE9C', 'length': 16})
    add({'name': 'map_ok', 'expr': 'map_w > 0 && map_w <= 64 && map_h > 0 && map_h <= 64 && map_data > 0x80000000'})
    add({'name': 'md_size', 'type': 'u32', 'address_expr': 'map_ok ? map_data - 0x1C : 0'})
    add({'name': 'md_nrel', 'type': 'u32', 'address_expr': 'map_ok ? map_data - 0x18 : 0'})
    add({'name': 'md_nsym', 'type': 'u32', 'address_expr': 'map_ok ? map_data - 0x14 : 0'})
    add({'name': 'md_syms', 'expr': 'map_data + md_size + md_nrel * 4'})
    add({'name': 'md_strs', 'expr': 'md_syms + md_nsym * 8'})
    add({'name': 'sym_panel', 'expr': "find(syms, 'name', 'panelindex')"})
    add({'name': 'sym_unique', 'expr': "find(syms, 'name', 'uniquepanel')"})
    add({'name': 'panel_addr', 'expr': 'map_data + syms[sym_panel].off'})
    add({'name': 'unique_addr', 'expr': 'map_data + syms[sym_unique].off'})
    # bmapNN are the chapters; Anna's tutorials run on T1, T2...
    add({'name': 'chapter', 'expr': "substr(map_name, 0, 4) == 'bmap' ? default(table(msg, 'MCT' + substr(map_name, 4)), map_name) : substr(map_name, 0, 1) == 'T' ? 'Demonstration' : map_name"})
    add({'name': 'victory', 'expr': "default(table(msg, win_key), '')"})
    add({'name': 'cursor_cell', 'expr': 'cursor_y * map_w + cursor_x'})
    # During events and the enemy phase the game parks the cursor at 0, 0, in the map's border.
    add({'name': 'cursor_live', 'expr': 'map_ok && (cursor_x > 0 || cursor_y > 0)'})
    add({'name': 'terrain_jp', 'expr': "cells[cursor_cell].tname"})
    add({'name': 'terrain', 'expr': "default(table(msg, 'MT_' + terrain_jp), '')"})
    # Units: the one under the cursor, or the one picked by tapping.
    add({'name': 'hover', 'expr': "cursor_live ? default(find(units, 'x', cursor_x, 'y', cursor_y), -1) : -1"})
    add({'name': 'focus', 'expr': 'sel >= 0 && sel < count(units) ? sel : hover'})
    add({'name': 'n_player', 'expr': "countif(units, 'army', 0)"})
    add({'name': 'n_enemy', 'expr': "countif(units, 'army', 1)"})
    add({'name': 'n_other', 'expr': "countif(units, 'army', 2)"})
    # What the game is showing: 0 nothing to follow (title, menus, movies, story scenes),
    # 1 a battle map, 2 a battle animation.
    add({'name': 'n_units', 'expr': 'count(units)'})
    # Set only while a battle scene runs (not on the map, at Game Over or back at the title, where
    # the map size, units and combatants linger).
    add({'name': 'battle_scene', 'type': 'u32', 'address': '0x802EFF1C'})
    add({'name': 'scene', 'expr': 'map_w > 0 && map_w <= 64 && n_units > 0 ? (battle_scene > 0x80000000 && count(fighters) > 0 ? 2 : (map_ok ? 1 : 0)) : 0'})
    # The forecast window has the player's side on the right (blue) and the foe's on the left (red).
    add({'name': 'fr', 'expr': "count(fighters) > 1 && fighters[1].army == 0 && fighters[0].army != 0 ? 1 : 0"})
    add({'name': 'fl', 'expr': 'count(fighters) > 1 ? 1 - fr : -1'})
    # The copies already hold the HP after the fight; the units keep it from before until the
    # animation ends, so show those and don't spoil the outcome.
    for side in ('fl', 'fr'):
        add({'name': f'hp_{side}', 'expr': f"default(units[default(find(units, 'pid', fighters[{side}].pid), -1)].hp, fighters[{side}].hp)"})
    return v


def unit_fields():
    f = []
    add = f.append
    add({'name': 'pid', 'type': 'string', 'offset': '0x174', 'offsets': ['0x0', '0x0'], 'length': 32})
    add({'name': 'mpid', 'type': 'string', 'offset': '0x174', 'offsets': ['0x4', '0x0'], 'length': 32})
    add({'name': 'mjid', 'type': 'string', 'offset': '0x178', 'offsets': ['0x4', '0x0'], 'length': 32})
    add({'name': 'fid', 'type': 'string', 'offset': '0x174', 'offsets': ['0xC', '0x0'], 'length': 32})
    add({'name': 'army', 'type': 'u8', 'offset': '0x17C', 'offsets': ['0x8']})
    add({'name': 'lv', 'type': 'u8', 'offset': '0x186'})
    add({'name': 'exp', 'type': 'u8', 'offset': '0x187'})
    add({'name': 'x', 'type': 'u8', 'offset': '0x18A'})
    add({'name': 'y', 'type': 'u8', 'offset': '0x18B'})
    add({'name': 'hp', 'type': 'u8', 'offset': '0x194'})
    add({'name': 'move_bonus', 'type': 's8', 'offset': '0x196'})
    for i, stat in enumerate(STATS):
        add({'name': f'g_{stat}', 'type': 's8', 'offset': hex(0x197 + i)})
        add({'name': f'c_{stat}', 'type': 's8', 'offset': '0x178', 'offsets': [hex(0x44 + i)]})
        add({'name': f'p_{stat}', 'type': 's8', 'offset': '0x174', 'offsets': [hex(0x1D + i)]})
        add({'name': f'cap_{stat}', 'type': 'u8', 'offset': '0x178', 'offsets': [hex(0x4C + i)]})
    add({'name': 'c_move', 'type': 'u8', 'offset': '0x178', 'offsets': ['0x3E']})
    add({'name': 'c_build', 'type': 'u8', 'offset': '0x178', 'offsets': ['0x3D']})
    add({'name': 'p_build', 'type': 's8', 'offset': '0x174', 'offsets': ['0x1C']})
    for k in range(ITEM_SLOTS):
        base = 0x1B8 + k * 8
        add({'name': f'miid{k}', 'type': 'string', 'offset': hex(base), 'offsets': ['0x4', '0x0'], 'length': 32})
        add({'name': f'uses{k}', 'type': 'u8', 'offset': hex(base + 4)})
        for field, off in (('wtype', 0x12), ('wflags', 0x14), ('itemno', 0x15), ('maxuses', 0x18), ('mt', 0x19),
                           ('hit', 0x1A), ('wt', 0x1B), ('crit', 0x1C), ('rmin', 0x1D), ('rmax', 0x1E)):
            add({'name': f'{field}{k}', 'type': 'u8', 'offset': hex(base), 'offsets': [hex(off)]})
    # Computed.
    # Without a disc the message table is empty; the ID's own suffix (IKE, RANGER) still reads.
    add({'name': 'name', 'expr': "default(table(msg, mpid), substr(mpid, 5))"})
    add({'name': 'cls', 'expr': "default(table(msg, mjid), substr(mjid, 5))"})
    add({'name': 'maxhp', 'expr': 'c_hp + p_hp + g_hp'})
    for stat in STATS[1:]:
        add({'name': stat, 'expr': f'c_{stat} + p_{stat} + g_{stat}'})
        add({'name': f'max_{stat}', 'expr': f'cap_{stat} > 0 ? cap_{stat} : 30'})
    add({'name': 'max_maxhp', 'expr': 'cap_hp > 0 ? cap_hp : 60'})
    add({'name': 'move', 'expr': 'c_move + move_bonus'})
    add({'name': 'build', 'expr': 'c_build + p_build'})
    add({'name': 'items', 'expr': ' + '.join(f'(miid{k} != null ? 1 : 0)' for k in range(ITEM_SLOTS))})
    for k in range(ITEM_SLOTS):
        add({'name': f'item{k}', 'expr': f"miid{k} == null ? '' : default(table(msg, miid{k}), substr(miid{k}, 5))"})
        add({'name': f'kind{k}', 'expr': f"(wflags{k} & 128) != 0 ? 'staff' : default(table(weapon_kind, wtype{k}), 'item')"})
        add({'name': f'icon{k}', 'expr': f'96 + itemno{k}'})
    add({'name': 'color', 'expr': "default(table(army_color, army), '@other')"})
    return f


def arrays():
    return [
        {
            'name': 'units',
            'address': hex(UNIT_BASE), 'stride': hex(UNIT_STRIDE), 'count': UNIT_SLOTS,
            '//where': 'Empty pool slots have no character record; the bounds reject stale data.',
            'where': 'pid != null && army != null && army <= 3 && lv >= 1 && lv <= 40 && hp <= 99',
            'fields': unit_fields(),
        },
        {
            'name': 'fighters',
            '//': 'The two units in the current (or last) fight, with the forecast numbers.',
            'address': '0x80330EC0', 'stride': '0x2E0', 'count': 2,
            'where': 'pid != null && army != null && army <= 3 && lv >= 1 && lv <= 40 && hp <= 99',
            'fields': unit_fields() + [
                {'name': f'b_{name}', 'type': 's16', 'offset': hex(off)}
                for name, off in (('atk', 0x284), ('def', 0x286), ('as', 0x288), ('hit', 0x28A),
                                  ('avo', 0x28C), ('crit', 0x290), ('ddg', 0x292))
            ],
        },
        {
            'name': 'syms',
            '//': "map.bin's symbol table: (u32 offset from data, u32 name offset)",
            'address_expr': 'map_ok ? md_syms : 0', 'stride': '0x8', 'count_expr': 'md_nsym', 'max_count': 32,
            'fields': [
                {'name': 'off', 'type': 'u32', 'offset': '0x0'},
                {'name': 'nameoff', 'type': 'u32', 'offset': '0x4'},
                {'name': 'name', 'type': 'string', 'address_expr': 'md_strs + nameoff', 'length': 20},
            ],
        },
        {
            'name': 'cells',
            'address_expr': 'map_ok && sym_panel != null ? panel_addr : 0', 'stride': '0x2',
            'count_expr': 'map_w * map_h', 'max_count': 1024,
            'fields': [
                {'name': 'idx', 'type': 'u16', 'offset': '0x0'},
                {'name': 'tname', 'type': 'string', 'address_expr': 'unique_addr + idx * 12 + 8',
                 'offsets': ['0x0'], 'length': 16, 'encoding': 'sjis'},
            ],
        },
    ]


# --------------------------------------------------------------------------------------------
# Building blocks. Every helper returns a list of elements in canvas pixels.

def text(x, y, w, h, t, size, color='@value', align='left', font='ui', bold=True, **extra):
    e = {'type': 'text', 'x': x, 'y': y, 'w': w, 'h': h, 'text': t, 'size': size, 'color': color,
         'align': align, 'font': font, 'fit': 1}
    if bold:
        e['weight'] = 'bold'
    e.update(extra)
    return e


def navy(x, y, w, h, **extra):
    e = {'type': 'frame', 'style': 'navy', 'x': x, 'y': y, 'w': w, 'h': h}
    e.update(extra)
    return e


def parchment(x, y, w, h, **extra):
    e = {'type': 'frame', 'style': 'parchment', 'x': x, 'y': y, 'w': w, 'h': h}
    e.update(extra)
    return e


def plate(x, y, w, h, kind='green', **extra):
    e = {'type': 'frame', 'style': kind, 'x': x, 'y': y, 'w': w, 'h': h}
    e.update(extra)
    return e


def tab_header(x, y, w, label, size=26):
    """The light-blue caption tab at the top-left of a navy panel ("Personal Data")."""
    return [
        {'type': 'rect', 'x': x, 'y': y, 'w': w, 'h': size + 12, 'radius': 6, 'fill': '#000000', 'opacity': 0.55},
        text(x + 12, y + 2, w - 20, size + 8, label, size, color='@header'),
    ]


# --------------------------------------------------------------------------------------------
# The game's own windows, decoded from the player's disc at run time ("disc:" images). Each helper
# falls back to a drawn frame when no disc is available ($disc is false).

PLATE = {'green': 2, 'navy': 3, 'maroon': 4}
GOLD_FRAME_CROP = [306, 0, 128, 118]  # The rounded gold frame inside window/talk2.tpl.


def disc_img(src, x, y, w, h, **extra):
    e = {'type': 'image', 'src': src, 'x': x, 'y': y, 'w': w, 'h': h}
    e.update(extra)
    return e


def both(vis, extra):
    return f'({vis}) && ({extra})' if vis else extra


def game_plate(x, y, w, h, color='green', visible_if=None, action=None, opacity=None):
    """A menu plate from window/filemenu.cms (green, navy or maroon) with its gold frame."""
    scale = min(h / 56, w / 300)
    disc_vis = both(visible_if, '$disc')
    fallback = plate(x, y, w, h, color, visible_if=both(visible_if, '!$disc'))
    if color == 'navy':
        fallback = navy(x, y, w, h, visible_if=both(visible_if, '!$disc'))
    out = [fallback]
    out.append(disc_img(f'disc:window/filemenu.cms#{PLATE[color]}', x, y, w, h, slice=[44, 0, 44, 0],
                        slice_scale=scale, visible_if=disc_vis))
    out.append(disc_img('disc:window/filemenu.cms#5', x, y, w, h, slice=[44, 0, 44, 0], slice_scale=scale,
                        visible_if=disc_vis))
    for e in out:
        if action:
            e['action'] = action
        if opacity is not None:
            e['opacity'] = opacity
    return out


def game_tab(x, y, w, h, active, visible_if=None, action=None):
    """Page tabs: the small menu plate from filemenu.cms #0/#1; dimmed when inactive."""
    scale = h / 48
    out = []
    for state, opacity in ((active, 1.0), (f'!({active})', 0.38)):
        vis = both(visible_if, state)
        out.append(plate(x, y, w, h, 'green' if opacity == 1.0 else 'parchment', visible_if=both(vis, '!$disc'),
                         action=action))
        out.append(disc_img('disc:window/filemenu.cms#0', x, y, w, h, slice=[40, 0, 40, 0], slice_scale=scale,
                            visible_if=both(vis, '$disc'), opacity=opacity, action=action))
        out.append(disc_img('disc:window/filemenu.cms#1', x, y, w, h, slice=[40, 0, 40, 0], slice_scale=scale,
                            visible_if=both(vis, '$disc'), opacity=opacity, action=action))
    # The game marks the chosen menu entry with its gold dragon medallion.
    out.append(disc_img('disc:window/filemenu.cms#7', x - h * 0.12, y - h * 0.08, h * 0.95, h * 1.14,
                        visible_if=both(both(visible_if, active), '$disc')))
    return out


def name_plate(x, y, w, h, visible_if=None):
    """The maroon name plate with gold scrollwork from window/level.tpl #2, mirrored for its right end."""
    scale = h / 56
    half = w / 2
    vis = both(visible_if, '$disc')
    return [
        plate(x, y, w, h, 'maroon', visible_if=both(visible_if, '!$disc')),
        disc_img('disc:window/level.tpl#2', x, y, half + 2, h, crop=[0, 0, 146, 56], slice=[60, 0, 0, 0],
                 slice_scale=scale, visible_if=vis),
        disc_img('disc:window/level.tpl#2', x + half - 2, y, half + 2, h, crop=[0, 0, 146, 56], slice=[60, 0, 0, 0],
                 slice_scale=scale, flip_x=1, visible_if=vis),
    ]


def gold_frame(x, y, w, h, corner=28, visible_if=None):
    """The rounded gold frame from window/talk2.tpl, stretched around a panel (disc only)."""
    return [disc_img('disc:window/talk2.tpl#0', x, y, w, h, crop=GOLD_FRAME_CROP, slice=[30, 30, 30, 30],
                     slice_scale=corner / 30, visible_if=both(visible_if, '$disc'))]


def textured_navy(x, y, w, h, visible_if=None):
    """The patterned navy of the game's message window (talk2.tpl) inside its gold frame."""
    return [
        navy(x, y, w, h, visible_if=both(visible_if, '!$disc')),
        disc_img('disc:window/talk2.tpl#0', x + 6, y + 6, w - 12, h - 12, crop=[30, 12, 264, 124],
                 visible_if=both(visible_if, '$disc')),
    ] + gold_frame(x, y, w, h, corner=30, visible_if=visible_if)


def item_icon(x, y, size, u, k, visible_if):
    """The game's own item icon (window/icon.tpl #0, 24x24 cells, 32 per row), or a drawn glyph."""
    cell = f'{u}.icon{k}'
    return [
        {'type': 'path', 'x': x, 'y': y, 'w': size, 'h': size, 'viewbox': [0, 0, 24, 24],
         'd': f'{{table(icon, {u}.kind{k})}}', 'fill': '@gold', 'stroke': '@edge', 'stroke_width': 1,
         'visible_if': both(visible_if, '!$disc')},
        disc_img('disc:window/icon.tpl#0', x, y, size, size,
                 crop=[f'({cell} % 32) * 24', f'floor({cell} / 32) * 24', 24, 24],
                 visible_if=both(visible_if, '$disc')),
    ]


def portrait(x, y, size, u, visible_if=None):
    """The character's small portrait: the last image of Face/<FID>.cms (64x64). Most faces have 14
    images, but faces without eye frames (Oscar, whose eyes are always closed, laguz, some
    soldiers) have 8, and generic units' crests have 2, so count from the end."""
    return [disc_img(f"disc:Face/{{substr({u}.fid, 4)}}.cms#-1", x, y, size, size,
                     visible_if=both(visible_if, f"$disc && {u}.fid != null"))]


def background(W, H):
    cx, cy = W / 2, H / 2
    r = min(W, H) * 0.42
    ring = (f'M{cx - r} {cy} A{r} {r} 0 1 0 {cx + r} {cy} A{r} {r} 0 1 0 {cx - r} {cy} Z '
            f'M{cx - r * 0.8} {cy} A{r * 0.8} {r * 0.8} 0 1 0 {cx + r * 0.8} {cy} '
            f'A{r * 0.8} {r * 0.8} 0 1 0 {cx - r * 0.8} {cy} Z')
    return [
        {'type': 'rect', 'x': 0, 'y': 0, 'w': W, 'h': H, 'fill': '@backdrop', 'fill2': '@backdrop2'},
        # A faint medallion behind everything, like the crest behind the chapter cards.
        {'type': 'path', 'x': 0, 'y': 0, 'w': W, 'h': H, 'viewbox': [0, 0, W, H], 'd': ring,
         'stroke': '@gold', 'stroke_width': 2, 'opacity': 0.07},
    ]


def stat_cell(x, y, w, h, stat, u='units[focus]'):
    label_w = w * 0.42
    return [
        text(x, y, label_w, h * 0.62, STAT_LABELS[stat], h * 0.5, color='@label'),
        text(x + label_w, y, w - label_w, h * 0.62, f'{{{u}.{stat}}}', h * 0.56, align='right'),
        {'type': 'bar', 'x': x, 'y': y + h * 0.68, 'w': w, 'h': max(5, h * 0.14),
         'value': f'{u}.{stat}', 'max': f'{u}.max_{stat}', 'track': '@track_light',
         'fill': '@gold_hi', 'fill2': '@gold', 'radius': 3},
    ]


PD_SLICE = 30                      # level.tpl #4 is 288x208; corners/ornaments within 30 px.
PD_ROWS = [34, 66, 98, 130, 162]   # Slot tops (12 px tall) in the texture.
PD_COLS = [(11, 132), (156, 276)]  # Slot x ranges.
PD_LEFT = ['str', 'mag', 'skl', 'spd', 'lck']
PD_RIGHT = ['def', 'res', 'move', 'build', 'lv']
PD_LABELS = dict(STAT_LABELS, move='Mv', build='Wt', lv='Lv')


def pd_map(v, size, src_size, scale):
    """Maps a texture coordinate to the canvas through the nine-slice of the Personal Data panel."""
    edge = PD_SLICE * scale
    if v <= PD_SLICE:
        return v * scale
    if v >= src_size - PD_SLICE:
        return size - (src_size - v) * scale
    return edge + (v - PD_SLICE) * (size - 2 * edge) / (src_size - 2 * PD_SLICE)


def personal_data(x, y, w, h, u):
    """Stats in the recessed slots of the game's own stat window (level.tpl #4), or a drawn
    fallback without a disc."""
    out = []
    scale = min(w / 288, h / 208) * 0.9
    out.append(disc_img('disc:window/level.tpl#4', x, y, w, h, slice=[PD_SLICE] * 4, slice_scale=scale,
                        visible_if='$disc'))
    for col, stats in zip(PD_COLS, (PD_LEFT, PD_RIGHT)):
        x0 = x + pd_map(col[0], w, 288, scale)
        x1 = x + pd_map(col[1], w, 288, scale)
        for top, stat in zip(PD_ROWS, stats):
            y0 = y + pd_map(top, h, 208, scale)
            y1 = y + pd_map(top + 12, h, 208, scale)
            sh = y1 - y0
            pad = sh * 0.35
            row = []
            if stat in STAT_LABELS:
                row.append({'type': 'bar', 'x': x0, 'y': y0, 'w': x1 - x0, 'h': sh, 'radius': sh / 2,
                            'value': f'{u}.{stat}', 'max': f'{u}.max_{stat}', 'fill': '#59E8C870',
                            'fill2': '#26D9B65A'})
            row.append(text(x0 + pad, y0 - sh * 0.9, (x1 - x0) * 0.5, sh * 2.8, PD_LABELS[stat], sh * 1.45,
                            color='@label', shadow='#C0000000', shadow_dx=1.5, shadow_dy=1.5))
            row.append(text(x0 + (x1 - x0) * 0.5, y0 - sh * 0.9, (x1 - x0) * 0.5 - pad, sh * 2.8,
                            f'{{{u}.{stat}}}', sh * 1.6, align='right', shadow='#C0000000', shadow_dx=1.5,
                            shadow_dy=1.5))
            out.append({'type': 'group', 'visible_if': '$disc', 'elements': row})

    # Without a disc: the drawn navy window.
    fallback = [navy(x, y, w, h)]
    fallback += tab_header(x + 14, y + 12, 210, 'Personal Data', 24)
    cw = (w - 40 - 28) / 2
    ch = min(46, (h - 70) / 4)
    top = y + 58
    for i, stat in enumerate(['str', 'mag', 'skl', 'spd']):
        fallback += stat_cell(x + 20, top + i * ch, cw, ch, stat, u)
    for i, stat in enumerate(['lck', 'def', 'res']):
        fallback += stat_cell(x + 48 + cw, top + i * ch, cw, ch, stat, u)
    fallback.append(text(x + 48 + cw, top + 3 * ch, cw * 0.42, ch * 0.62, 'Mv', ch * 0.5, color='@label'))
    fallback.append(text(x + 48 + cw * 1.42, top + 3 * ch, cw * 0.58, ch * 0.62, f'{{{u}.move}}', ch * 0.56,
                         align='right'))
    out.append({'type': 'group', 'visible_if': '!$disc', 'elements': fallback})
    return out


def unit_card(x, y, w, compact=False):
    """The selected unit: name plate, parchment info with portrait, Personal Data. Returns
    (elements, bottom)."""
    u = 'units[focus]'
    out = []
    name_h = 56 if compact else 64
    out += name_plate(x, y, w, name_h)
    out.append({'type': 'path', 'x': x + w - 52, 'y': y + name_h / 2 - 12, 'w': 24, 'h': 24, 'viewbox': [0, 0, 24, 24],
                'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': f'{{{u}.color}}', 'stroke': '@gold_hi', 'stroke_width': 1.5})
    out.append(text(x + 70, y + 6, w - 140, name_h - 12, f'{{{u}.name}}', name_h * 0.56, color='@cream',
                    align='center', shadow='#B0000000', shadow_dx=2, shadow_dy=2))
    y += name_h + 8

    info_h = 136 if compact else 168
    out.append(parchment(x, y, w, info_h))
    pic = info_h - 20
    out.append({'type': 'rect', 'x': x + w - pic - 13, 'y': y + 9, 'w': pic + 2, 'h': pic + 2, 'radius': 10,
                'fill': '#2A3C78', 'fill2': '#0C1438', 'visible_if': '$disc'})
    out += portrait(x + w - pic - 12, y + 10, pic, u)
    out += gold_frame(x + w - pic - 18, y + 4, pic + 12, pic + 12, corner=16)
    pad = 22
    col = w - pad * 2 - (pic + 20)
    big = info_h * 0.21
    out.append(text(x + pad, y + info_h * 0.07, col, info_h * 0.26, f'{{{u}.cls}}', big, color='@ink'))
    ly = y + info_h * 0.36
    out.append(text(x + pad, ly, 50, info_h * 0.24, 'Lv', big * 0.75, color='@ink_soft'))
    out.append(text(x + pad + 42, ly, 60, info_h * 0.24, f'{{{u}.lv}}', big * 0.95, color='@ink'))
    out.append(text(x + pad + 100, ly, 50, info_h * 0.24, 'EX', big * 0.75, color='@ink_soft'))
    out.append(text(x + pad + 140, ly, 64, info_h * 0.24, f"{{{u}.army == 0 ? str({u}.exp) : '--'}}", big * 0.95,
                    color='@ink'))
    out.append({'type': 'bar', 'x': x + pad + 210, 'y': ly + info_h * 0.09, 'w': col - 210, 'h': 10,
                'value': f'{u}.exp', 'max': 100, 'track': '#405A4214', 'fill': '@exp', 'fill2': '@exp2', 'radius': 5,
                'visible_if': f'{u}.army == 0'})
    hy = y + info_h * 0.66
    out.append(text(x + pad, hy, 50, info_h * 0.26, 'HP', big * 0.8, color='@ink_soft'))
    out.append(text(x + pad + 40, hy - 2, 90, info_h * 0.28, f'{{{u}.hp}}', big * 1.15, color='@ink', align='right'))
    out.append(text(x + pad + 134, hy, 90, info_h * 0.26, f'/ {{{u}.maxhp}}', big * 0.9, color='@hp_text'))
    out.append({'type': 'bar', 'x': x + pad + 226, 'y': hy + info_h * 0.08, 'w': col - 226, 'h': 14, 'value': f'{u}.hp',
                'max': f'{u}.maxhp', 'track': '#405A4214', 'fill': '@hp', 'fill2': '@hp2', 'radius': 7,
                'stroke': '#805A4214', 'stroke_width': 1.5})
    y += info_h + 8

    stats_h = 230 if compact else 300
    out += personal_data(x, y, w, stats_h, u)
    y += stats_h + 8
    return out, y


def item_rows(x, y, w, rows, row_h, u='units[focus]'):
    out = []
    for k in range(rows):
        ry = y + k * row_h
        vis = f"{u}.item{k} != ''"
        out.append({'type': 'rect', 'x': x, 'y': ry + row_h - 2, 'w': w, 'h': 1.5, 'fill': '#14FFFFFF', 'visible_if': vis})
        out += item_icon(x, ry + (row_h - 32) / 2, 32, u, k, vis)
        out.append(text(x + 44, ry, w - 160, row_h, f'{{{u}.item{k}}}', row_h * 0.56, visible_if=vis))
        out.append(text(x + w - 110, ry, 110, row_h, f'{{{u}.uses{k}}}', row_h * 0.56, align='right',
                        color='@label', visible_if=vis))
    return out


def item_details(x, y, w, rows, row_h, u='units[focus]'):
    """Status page items: name, uses gauge and weapon numbers."""
    out = []
    for k in range(rows):
        ry = y + k * row_h
        vis = f"{u}.item{k} != ''"
        weapon = f"{vis} && {u}.kind{k} != 'item'"
        out.append({'type': 'rect', 'x': x - 8, 'y': ry + 2, 'w': w + 16, 'h': row_h - 8, 'radius': 6,
                    'fill': '#12FFFFFF', 'visible_if': vis})
        out += item_icon(x - 4, ry + 8, 40, u, k, vis)
        out.append(text(x + 46, ry + 6, w * 0.55, row_h * 0.42, f'{{{u}.item{k}}}', row_h * 0.34, visible_if=vis))
        out.append(text(x + w - 150, ry + 6, 150, row_h * 0.42, f'{{{u}.uses{k}}} / {{{u}.maxuses{k}}}',
                        row_h * 0.3, color='@label', align='right', visible_if=vis))
        out.append({'type': 'bar', 'x': x + 46, 'y': ry + row_h * 0.5, 'w': w - 46, 'h': 7, 'radius': 3.5,
                    'value': f'{u}.uses{k}', 'max': f'{u}.maxuses{k}', 'track': '@track_light',
                    'fill': '@gold_hi', 'fill2': '@gold', 'visible_if': vis})
        out.append(text(x + 46, ry + row_h * 0.58, w - 46, row_h * 0.32,
                        f"Mt {{{u}.mt{k}}}   Hit {{{u}.hit{k}}}   Crit {{{u}.crit{k}}}   Wt {{{u}.wt{k}}}   "
                        f"Rng {{{u}.rmin{k} == {u}.rmax{k} ? str({u}.rmin{k}) : str({u}.rmin{k}) + '-' + str({u}.rmax{k})}}",
                        row_h * 0.24, color='@header', bold=False, visible_if=weapon))
    return out


def minimap(x, y, w, h):
    """The battle map as a parchment war map: inked terrain, unit tokens and the cursor."""
    gx, gy, gw, gh = x + 18, y + 18, w - 36, h - 36
    return [
        navy(x, y, w, h, corner='diamond'),
        {'type': 'rect', 'x': x + 14, 'y': y + 14, 'w': w - 28, 'h': h - 28, 'fill': '#3A2A14', 'fill2': '#1E1408'},
        {'type': 'grid', 'x': gx, 'y': gy, 'w': gw, 'h': gh,
         'cols': 'map_w', 'rows': 'map_h', 'source': 'cells',
         'cell': f"{{default(table(terrain_color, tname), '#B8A57A')}}",
         'bevel': 0.55, 'grid_color': '#405A4020', 'grid_width': 1,
         # Pinch to zoom on the device; while zoomed the map follows the game's cursor.
         'zoom': 4, 'follow_x': 'cursor_live ? cursor_x : -1', 'follow_y': 'cursor_live ? cursor_y : -1',
         'visible_if': 'map_ok',
         'layers': [
             {'source': 'cells', 'where': "default(table(terrain_glyph, tname), '') != ''",
              'x': '$slot % map_w', 'y': 'floor($slot / map_w)',
              'shape': '{table(terrain_glyph, tname)}', 'fill': '{table(glyph_ink, table(terrain_glyph, tname))}',
              'stroke': "{table(terrain_glyph, tname) == 'wave' ? table(glyph_ink, 'wave') : ''}", 'size': 0.62},
             {'source': 'units', 'x': 'x', 'y': 'y', 'where': '$index == focus', 'shape': 'ring',
              'stroke': '#1A1206', 'size': 1.16},
             {'source': 'units', 'x': 'x', 'y': 'y', 'where': '$index == focus', 'shape': 'ring',
              'stroke': '@gold_hi', 'size': 1.04},
             {'source': 'units', 'x': 'x', 'y': 'y', 'shape': 'token', 'fill': '{color}',
              'size': 0.74, 'action': 'select_unit'},
             {'x': 'cursor_x', 'y': 'cursor_y', 'where': 'cursor_live', 'shape': 'cursor',
              'stroke': '#1A1206', 'size': 1.12},
             {'x': 'cursor_x', 'y': 'cursor_y', 'where': 'cursor_live', 'shape': 'cursor',
              'stroke': '@gold_hi', 'size': 1.0},
         ]},
        text(x, y + h / 2 - 30, w, 60, 'Waiting for a battle map…', 30, color='@gold', align='center',
             visible_if='!map_ok'),
    ] + gold_frame(x + 2, y + 2, w - 4, h - 4, corner=34)


def army_banner(x, y, w, h, label, value, color):
    plate_color = {'@player': 'navy', '@enemy': 'maroon'}.get(color, 'green')
    return game_plate(x, y, w, h, plate_color) + [
        {'type': 'path', 'x': x + h * 0.5, 'y': y + h / 2 - 13, 'w': 26, 'h': 26, 'viewbox': [0, 0, 24, 24],
         'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': color, 'stroke': '@gold_hi', 'stroke_width': 1.5},
        text(x + h * 0.5 + 40, y, w - 160, h, label, h * 0.44, color='@cream', shadow='#B0000000'),
        text(x + w - 110, y, 70, h, value, h * 0.58, align='right', shadow='#B0000000'),
    ]


# --------------------------------------------------------------------------------------------
# Layout chrome (header + tabs), shared by every page.

PAGES = [('battle', 'Battle'), ('army', 'Army'), ('status', 'Status')]


def chrome(W, H, compact):
    out = background(W, H)
    hh = 92 if compact else 112
    m = 12 if compact else 16
    tw = 180 if compact else 210
    pw = 230 if compact else 270
    cw = W - m * 4 - tw - pw
    out += game_plate(m, m, cw, hh, 'green')
    out.append(text(m + 56, m + 8, cw - 112, hh * 0.52, '{chapter}', hh * 0.36, color='@cream',
                    shadow='#B0000000'))
    out.append(text(m + 56, m + hh * 0.56, cw - 112, hh * 0.34, "{victory != '' ? 'Victory: ' + victory : ''}",
                    hh * 0.24, color='@gold_hi', shadow='#B0000000'))
    tx = m * 2 + cw
    out += game_plate(tx, m, tw, hh, 'navy')
    out.append(text(tx, m + 10, tw, hh * 0.3, 'Turn', hh * 0.22, color='@label', align='center'))
    out.append(text(tx, m + hh * 0.36, tw, hh * 0.54, '{turn}', hh * 0.48, align='center', shadow='#B0000000'))
    px = tx + tw + m
    for phase, color in ((0, 'navy'), (1, 'maroon'), (2, 'green')):
        cond = f'phase == {phase}' if phase else 'phase != 1 && phase != 2'
        out += game_plate(px, m, pw, hh, color, visible_if=cond)
    out.append(text(px, m + 8, pw, hh * 0.5, "{phase == 1 ? 'Enemy' : phase == 2 ? 'Other' : 'Player'}",
                    hh * 0.42, font='title', color='@cream', align='center', fill2='@gold_hi',
                    shadow='@shadow'))
    out.append(text(px, m + hh * 0.54, pw, hh * 0.36, 'PHASE', hh * 0.26, font='title', color='@gold_hi',
                    align='center', letter_spacing=0.2, shadow='@shadow'))

    th = 64 if compact else 72
    ty = H - th - m
    tab_w = (W - m * (len(PAGES) + 1)) / len(PAGES)
    for i, (page, label) in enumerate(PAGES):
        x = m + i * (tab_w + m)
        out += game_tab(x, ty, tab_w, th, f"$page == '{page}'", action=f'go_{page}')
        out.append(text(x, ty, tab_w, th, label, th * 0.48, align='center', shadow='#B0000000',
                        color=f"{{$disc || $page == '{page}' ? '#F6EAC8' : '#3B2A12'}}"))
    return out, hh + m * 2, ty - m


# --------------------------------------------------------------------------------------------
# Pages per layout

def battle_page(W, H, top, bottom, compact):
    m = 12 if compact else 16
    out = []
    map_w = (W - m * 3) * (0.49 if compact else 0.5)
    banner_h = 50 if compact else 62
    map_h = min(map_w, bottom - top - (banner_h * 2 + m * 3 if not compact else banner_h + m * 2) - m)
    out += minimap(m, top, map_w, map_h)
    by = top + map_h + m
    if compact:
        half = (map_w - m) / 2
        out += army_banner(m, by, half, banner_h, 'Allies', '{n_player}', '@player')
        out += army_banner(m * 2 + half, by, half, banner_h, 'Enemies', '{n_enemy}', '@enemy')
    else:
        out += army_banner(m, by, map_w, banner_h, 'Your Units', '{n_player}', '@player')
        out += army_banner(m, by + banner_h + m, map_w, banner_h, 'Enemy Units', '{n_enemy}', '@enemy')
    # Terrain under the cursor.
    ty = (by + banner_h + m) if compact else (by + (banner_h + m) * 2)
    th = bottom - ty
    if th > 40:
        out.append(parchment(m, ty, map_w, th, corner='bracket', corner_size=10))
        out.append(text(m + 20, ty, map_w * 0.6, th, "{cursor_live && terrain != '' ? terrain : '—'}", min(th * 0.5, 32), color='@ink'))
        out.append(text(m + map_w * 0.55, ty, map_w * 0.45 - 20, th, '{cursor_x}, {cursor_y}',
                        min(th * 0.42, 26), color='@ink_soft', align='right', visible_if='cursor_live'))

    cx = m * 2 + map_w
    cw = W - cx - m
    card, cy = unit_card(cx, top, cw, compact)
    out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements': card})
    rest = bottom - cy
    if rest > 80:
        out += textured_navy(cx, cy, cw, rest, visible_if='focus >= 0')
        out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements':
                    tab_header(cx + 14, cy + 12, 120, 'Items', 24) +
                    item_rows(cx + 24, cy + 52, cw - 48, min(ITEM_SLOTS, int((rest - 64) // 44)), 44)})
    # Nothing under the cursor.
    out.append(navy(cx, top, cw, bottom - top, visible_if='focus < 0'))
    out.append(text(cx + 30, top + (bottom - top) / 2 - 70, cw - 60, 50, 'No unit selected', 36,
                    color='@label', align='center', visible_if='focus < 0'))
    out.append(text(cx + 30, top + (bottom - top) / 2 - 10, cw - 60, 40,
                    'Move the cursor onto a unit,', 26, align='center', bold=False, visible_if='focus < 0'))
    out.append(text(cx + 30, top + (bottom - top) / 2 + 26, cw - 60, 40,
                    'or tap one on the map.', 26, align='center', bold=False, visible_if='focus < 0'))
    out.append(plate(cx + cw - 190, top + 8, 176, 40, 'parchment', corner='bracket', corner_size=8,
                     action='follow_cursor', visible_if='sel >= 0'))
    out.append(text(cx + cw - 190, top + 8, 176, 40, 'Follow cursor', 22, color='@ink', align='center',
                    visible_if='sel >= 0'))
    return out


def roster_column(x, y, w, h, army, title, color, compact):
    row_h = 52 if compact else 60
    rows = int((h - 70) // row_h)
    out = textured_navy(x, y, w, h)
    out += tab_header(x + 14, y + 12, 190, title, 24)
    out.append(text(x + w - 120, y + 10, 100, 36, f"{{countif(units, 'army', {army})}}", 30, align='right', color='@label'))
    row = [
        {'type': 'rect', 'x': 0, 'y': 0, 'w': w - 36, 'h': row_h - 6, 'radius': 6,
         'fill': "{$index == focus ? '#26FFFFFF' : '#0DFFFFFF'}", 'action': 'inspect_unit'},
        {'type': 'path', 'x': 10, 'y': (row_h - 6) / 2 - 10, 'w': 20, 'h': 20, 'viewbox': [0, 0, 24, 24],
         'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': color, 'visible_if': '!$disc || fid == null'},
        disc_img("disc:Face/{substr(fid, 4)}.cms#-1", 2, -1, row_h - 4, row_h - 4,
                 visible_if='$disc && fid != null'),
        text(row_h + 4, 2, (w - 36) * 0.36 - row_h + 36, row_h * 0.5, '{name}', row_h * 0.4),
        text(row_h + 4, row_h * 0.48, (w - 36) * 0.36 - row_h + 36, row_h * 0.4, '{cls}', row_h * 0.3,
             color='@label', bold=False),
        text(40 + (w - 36) * 0.37, 0, 70, row_h - 6, 'Lv {lv}', row_h * 0.34, color='@cream'),
        {'type': 'bar', 'x': 40 + (w - 36) * 0.37 + 80, 'y': (row_h - 6) / 2 - 5, 'w': (w - 36) * 0.63 - 210,
         'h': 10, 'value': 'hp', 'max': 'maxhp', 'track': '@track', 'fill': '@hp', 'fill2': '@hp2', 'radius': 5},
        text(w - 36 - 100, 0, 92, row_h - 6, '{hp}/{maxhp}', row_h * 0.34, align='right'),
    ]
    out.append({'type': 'repeat', 'source': 'units', 'where': f'army == {army}', 'x': x + 18, 'y': y + 58,
                'dy': row_h, 'limit': rows, 'elements': row})
    return out


def army_page(W, H, top, bottom, compact):
    m = 12 if compact else 16
    col_w = (W - m * 3) / 2
    return (roster_column(m, top, col_w, bottom - top, 0, 'Your Units', '@player', compact) +
            roster_column(m * 2 + col_w, top, col_w, bottom - top, 1, 'Enemy Units', '@enemy', compact))


def status_page(W, H, top, bottom, compact):
    m = 12 if compact else 16
    out = []
    cw = (W - m * 3) * 0.5
    card, cy = unit_card(m, top, cw, compact)
    out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements': card})
    ph = bottom - cy
    if ph > 60:
        u = 'units[focus]'
        panel = [parchment(m, cy, cw, ph, corner='bracket', corner_size=10)]
        line = min(44, ph / 4.2)
        panel.append(text(m + 22, cy + 12, cw * 0.5, line, 'Class Maximums', line * 0.62, color='@ink'))
        panel.append(text(m + cw * 0.5, cy + 12, cw * 0.5 - 22, line,
                          "{cursor_live && terrain != '' ? 'On ' + terrain : ''}", line * 0.56, color='@ink_soft', align='right'))
        panel.append({'type': 'rect', 'x': m + 22, 'y': cy + 16 + line, 'w': cw - 44, 'h': 1.5, 'fill': '@parch_trim'})
        caps = [('HP', 'max_maxhp')] + [(STAT_LABELS[st], f'max_{st}') for st in STATS[1:]]
        cell_w = (cw - 44) / 4
        cell_h = min(line * 1.5, (ph - line - 40) / 2)
        for i, (label, field) in enumerate(caps):
            cx_ = m + 22 + (i % 4) * cell_w
            cy_ = cy + 28 + line + (i // 4) * cell_h
            panel.append(text(cx_, cy_, cell_w * 0.5, cell_h * 0.6, label, cell_h * 0.36, color='@ink_soft'))
            panel.append(text(cx_ + cell_w * 0.45, cy_, cell_w * 0.45, cell_h * 0.6, f'{{{u}.{field}}}',
                              cell_h * 0.42, color='@ink', align='right'))
        out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements': panel})
    rx = m * 2 + cw
    rw = W - rx - m
    row_h = min(110, (bottom - top - 70) / ITEM_SLOTS)
    out += textured_navy(rx, top, rw, bottom - top, visible_if='focus >= 0')
    out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements':
                tab_header(rx + 14, top + 12, 120, 'Items', 24) +
                item_details(rx + 30, top + 58, rw - 60, ITEM_SLOTS, row_h)})
    out.append(navy(m, top, W - m * 2, bottom - top, visible_if='focus < 0'))
    out.append(text(m, top + (bottom - top) / 2 - 30, W - m * 2, 60, 'Select a unit on the Battle or Army page.',
                    30, color='@label', align='center', visible_if='focus < 0'))
    return out


# --------------------------------------------------------------------------------------------
# Overlays for scenes without a battle map to follow.

def idle_overlay(W, H, compact):
    """Title, menus, movies and story scenes: the parchment map of Tellius and the title logo,
    both from the disc."""
    out = background(W, H)
    map_w = W * (0.53 if compact else 0.62)
    map_h = map_w * 448 / 608
    mx, my = (W - map_w) / 2, H * (0.035 if compact else 0.06)
    out.append({'type': 'rect', 'x': mx - 4, 'y': my + 10, 'w': map_w + 8, 'h': map_h + 8, 'radius': 10,
                'fill': '#99000000'})
    out.append(disc_img('disc:gmap/gmap_zentai.cms#0', mx, my, map_w, map_h, visible_if='$disc'))
    out += gold_frame(mx - 10, my - 10, map_w + 20, map_h + 20, corner=40)
    # The logo is drawn on black; the game adds it onto the screen, so blend it the same way.
    logo_w = min(W * (0.5 if compact else 0.52), (H - (my + map_h) - 10) * 512 / 320)
    logo_h = logo_w * 320 / 512
    ly = my + map_h + (H - my - map_h - logo_h) / 2
    out.append(disc_img('disc:title.cmp>s/qig#1', (W - logo_w) / 2, ly, logo_w, logo_h, blend='screen',
                        visible_if='$disc'))
    out.append(text(0, H * 0.42, W, 80, 'Path of Radiance', 64, font='title', color='@gold_hi', align='center',
                    shadow='@shadow', visible_if='!$disc'))
    return [{'type': 'group', 'visible_if': 'scene == 0', 'elements': out}]


# Row centres of the forecast window's labels, and its name/weapon slots (source pixels).
FORECAST = [('hp', 89), ('atk', 119), ('def', 149), ('hit', 179), ('crit', 209), ('as', 238)]
FORECAST_SLOTS = [('fr', 'name', 23), ('fr', 'item0', 54), ('fl', 'item0', 282), ('fl', 'name', 304)]


def forecast_value(stat, me, foe):
    a, b = f'fighters[{me}]', f'fighters[{foe}]'
    return {
        'hp': f'{{hp_{me}}}',
        'atk': f'{{{a}.b_atk}}',
        'def': f'{{{a}.b_def}}',
        'hit': f'{{max(0, min(100, {a}.b_hit - {b}.b_avo))}}',
        'crit': f'{{max(0, min(100, {a}.b_crit - {b}.b_ddg))}}',
        'as': f'{{{a}.b_as}}',
    }[stat]


def fighter_column(x, y, w, h, side, compact):
    """One combatant: name, portrait, class and level, HP and weapon."""
    u = f'fighters[{side}]'
    out = []
    plate_h = 56 if compact else 70
    for army, color in ((0, 'navy'), (1, 'maroon')):
        out += game_plate(x, y, w, plate_h, color, visible_if=f'{u}.army == {army}')
    out += game_plate(x, y, w, plate_h, 'green', visible_if=f'{u}.army != 0 && {u}.army != 1')
    out.append(text(x, y, w, plate_h, f'{{{u}.name}}', plate_h * 0.5, color='@cream', align='center',
                    shadow='#B0000000'))
    py = y + plate_h + (10 if compact else 14)
    side_len = min(w - 40, h * (0.36 if compact else 0.4))
    px = x + (w - side_len) / 2
    out += textured_navy(px - 10, py, side_len + 20, side_len + 20)
    out += portrait(px, py + 10, side_len, u)
    cy = py + side_len + 20 + (10 if compact else 14)
    card_h = 50 if compact else 62
    out.append(parchment(x, cy, w, card_h, corner='bracket', corner_size=10))
    out.append(text(x + 18, cy, w * 0.62, card_h, f'{{{u}.cls}}', card_h * 0.48, color='@ink'))
    out.append(text(x + w * 0.6, cy, w * 0.4 - 18, card_h, f'Lv {{{u}.lv}}', card_h * 0.44, color='@ink',
                    align='right'))
    hy = cy + card_h + (10 if compact else 14)
    hp_h = 74 if compact else 96
    out += textured_navy(x, hy, w, hp_h)
    out.append(text(x + 22, hy + 8, 80, hp_h * 0.55, 'HP', hp_h * 0.3, color='@label'))
    out.append(text(x + 80, hy + 4, w - 102, hp_h * 0.6, f'{{hp_{side}}} / {{{u}.maxhp}}', hp_h * 0.42,
                    align='right', shadow='#B0000000'))
    out.append({'type': 'bar', 'x': x + 22, 'y': hy + hp_h * 0.68, 'w': w - 44, 'h': hp_h * 0.16,
                'radius': 3, 'value': f'hp_{side}', 'max': f'{u}.maxhp', 'track': '@track',
                'fill': '@hp', 'fill2': '@hp2', 'count': f'{u}.maxhp', 'off': '#55000000'})
    wy = hy + hp_h + (10 if compact else 14)
    wh = min(60, y + h - wy)
    if wh > 30:
        out.append({'type': 'rect', 'x': x + 6, 'y': wy, 'w': w - 12, 'h': wh, 'radius': 8, 'fill': '#40000000'})
        out += item_icon(x + 16, wy + (wh - 36) / 2, 36, u, 0, f"{u}.item0 != ''")
        out.append(text(x + 62, wy, w - 80, wh, f'{{{u}.item0}}', wh * 0.46, visible_if=f"{u}.item0 != ''"))
    return [{'type': 'group', 'visible_if': f'count(fighters) > {side}', 'elements': out}]


def combat_overlay(W, top, bottom, compact):
    """A battle animation: the two combatants either side of the game's forecast window
    (window/btl_mae.tpl), with the numbers the game computed for the fight."""
    m = 12 if compact else 16
    h = bottom - top
    win_h = h - 8
    win_w = win_h * 168 / 328
    col_w = (W - m * 4 - win_w) / 2
    wx = m * 2 + col_w
    scale = win_w / 168
    out = [{'type': 'rect', 'x': 0, 'y': top - m / 2, 'w': W, 'h': h + m, 'fill': '@backdrop2'}]
    out += fighter_column(m, top, col_w, h, 'fl', compact)
    out += fighter_column(W - m - col_w, top, col_w, h, 'fr', compact)
    out.append(navy(wx, top + 4, win_w, win_h, visible_if='!$disc'))
    out.append(disc_img('disc:window/btl_mae.tpl#0', wx, top + 4, win_w, win_h, visible_if='$disc'))
    labels = {'hp': 'HP', 'atk': 'Atk', 'def': 'Def', 'hit': 'Hit', 'crit': 'Crit', 'as': 'AS'}
    size = 13 * scale
    for stat, sy in FORECAST:
        ry = top + 4 + (sy - 13) * scale
        rh = 26 * scale
        out.append(text(wx + 58 * scale, ry, 52 * scale, rh, labels[stat], size * 0.8, color='@ink',
                        align='center', visible_if='!$disc'))
        out.append(text(wx + 2 * scale, ry, 54 * scale, rh, forecast_value(stat, 'fl', 'fr'), size, align='center',
                        shadow='#C0000000', visible_if='count(fighters) > 1'))
        out.append(text(wx + 112 * scale, ry, 54 * scale, rh, forecast_value(stat, 'fr', 'fl'), size,
                        align='center', shadow='#C0000000', visible_if='count(fighters) > 1'))
    # Top slots: the right-hand (player's) side; bottom slots: the left-hand side.
    for side, field, sy in FORECAST_SLOTS:
        out.append(text(wx + 22 * scale, top + 4 + (sy - 9) * scale, 124 * scale, 18 * scale,
                        f'{{fighters[{side}].{field}}}', 10 * scale, align='center', shadow='#C0000000',
                        visible_if=f'count(fighters) > 1'))
    return [{'type': 'group', 'visible_if': 'scene == 2', 'elements': out}]


def layout(lid, W, H, compact):
    elements, top, bottom = chrome(W, H, compact)
    pages = {
        'battle': battle_page(W, H, top, bottom, compact),
        'army': army_page(W, H, top, bottom, compact),
        'status': status_page(W, H, top, bottom, compact),
    }
    # Every page gives way to the same views when there is no battle map to follow.
    for page in pages.values():
        page += combat_overlay(W, top, bottom, compact) + idle_overlay(W, H, compact)
    return {'id': lid, 'canvas': [W, H], 'background': '@backdrop2', 'elements': elements}, pages


def manifest():
    thor, thor_pages = layout('thor', 1240, 1080, compact=False)
    classic, classic_pages = layout('classic', 1024, 768, compact=True)
    pages = []
    for page, title in PAGES:
        pages.append({'id': page, 'title': title, 'widgets': [],
                      'elements': {'thor': thor_pages[page], 'classic': classic_pages[page]}})
    actions = [{'id': f'go_{page}', 'steps': [{'page': page}]} for page, _ in PAGES]
    actions += [
        {'id': 'select_unit', 'steps': [{'set': 'sel', 'value': '$index'}]},
        {'id': 'inspect_unit', 'steps': [{'set': 'sel', 'value': '$index'}, {'page': 'status'}]},
        {'id': 'follow_cursor', 'steps': [{'set': 'sel', 'value': -1}]},
    ]
    return {
        '_about': 'Generated by generate.py - edit the generator, not this file.',
        'format': 1,
        'id': 'fe9-path-of-radiance',
        'name': 'Path of Radiance',
        'version': '0.1.0',
        'author': 'CMApex & Claude',
        'description': 'Tactical map, unit status and army roster for Fire Emblem: Path of Radiance (NTSC-U).',
        'targets': [{'game_id': 'GFEE01', 'revisions': [0]}],
        'poll_hz': 12,
        'theme': {
            'colors': COLORS,
            'fonts': {'ui': 'fonts/Marcellus-Regular.ttf', 'title': 'fonts/Cinzel.ttf'},
            'styles': {
                'navy': {'fill': '@navy', 'fill2': '@navy2', 'edge': '@edge', 'trim': '@gold',
                         'glow': '@navy_glow', 'corner': 'diamond', 'corner_size': 12, 'inset': 6,
                         'stroke_width': 3, 'trim_width': 2},
                'parchment': {'fill': '@parch', 'fill2': '@parch2', 'edge': '@parch_edge',
                              'trim': '@parch_trim', 'corner': 'notch', 'corner_size': 12, 'inset': 6,
                              'stroke_width': 3, 'trim_width': 1.5},
                'maroon': {'fill': '@maroon', 'fill2': '@maroon2', 'edge': '@edge', 'trim': '@gold',
                           'corner': 'notch', 'corner_size': 16, 'inset': 5, 'stroke_width': 3,
                           'trim_width': 2},
                'green': {'fill': '@green', 'fill2': '@green2', 'edge': '@edge', 'trim': '@gold',
                          'glow': '#3398D8A8', 'corner': 'notch', 'corner_size': 16, 'inset': 6,
                          'stroke_width': 3, 'trim_width': 2},
            },
        },
        'layouts': [thor, classic],
        'state': {'sel': -1},
        'tables': tables(),
        'values': values(),
        'arrays': arrays(),
        'pages': pages,
        'actions': actions,
    }


if __name__ == '__main__':
    out = os.path.join(HERE, 'manifest.json')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(manifest(), f, ensure_ascii=False, indent=1)
    print('wrote', out, os.path.getsize(out), 'bytes')
