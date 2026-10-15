#!/usr/bin/env python3
"""Generates manifest.json for the Fire Emblem: Path of Radiance (GFEE01) Dolphin Duo companion.

Edit this file, not manifest.json. Names come from text.json (tools/extract_text.py reads them from
your own disc); fonts are OFL (see fonts/). No game art is included.

Memory map (NTSC-U GFEE01, rev 0), found with the headless harness (Tools/DolphinDuo/headless):

  Units      0x802B14F8 + n*0x280 (a pool; empty slots have no character record)
    +0x174 -> character record  (+0x00 PID, +0x04 MPID name, +0x1C build mod, +0x1D..0x24 HP..Res mods)
    +0x178 -> class record      (+0x00 JID, +0x04 MJID name, +0x3D build, +0x3E move,
                                 +0x44..0x4B HP..Res bases, +0x4C..0x53 caps)
    +0x17C -> army list         (+0x08 army: 0 player, 1 enemy, 2 other)
    +0x186 level  +0x187 EXP  +0x18A x  +0x18B y  +0x194 HP  +0x196 move bonus
    +0x197..0x19E HP..Res gains   +0x1B8 + k*8 items (u32 item record, u8 uses)
    Displayed stat = class base + character modifier + gain (checked on the status screen).
  Item record   +0x00 IID  +0x04 MIID name  +0x18 max uses  +0x19 might  +0x1B weight
  Chapter state 0x80330719 phase, 0x8033071A turn (u16), 0x80330720 gold, 0x80330728 victory key
  Cursor        0x8032B238 / 0x8032B23C (f32 tile x / y)
  Map           0x802EFE60 width, +2 height, +0x0C -> map.bin data, +0x3C map name ("bmap01")
                map.bin keeps its symbol table; panelindex (u16 per cell) indexes uniquepanel
                (12 bytes: four corner heights, then a pointer to the terrain name).
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TEXT = json.load(open(os.path.join(HERE, 'text.json'), encoding='utf-8'))

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

# Minimap colours by terrain (the Japanese terrain names the map data uses).
TERRAIN_COLOR = {
    '平地': '#56663A', '床': '#6E675B', '石畳': '#7A7366', '屋根': '#5A3A2A', '木': '#2C4A2A',
    '橋': '#8A6A3A', '階段': '#6F6A60', '堀': '#2F4D6A', '城門': '#6A4A2A', '川': '#2F5D8A',
    '崖': '#45454E', '壁': '#2B2B33', '柵': '#6A5A3A', '小物': '#5E5E52', '建物': '#8A5E36',
    '茂み': '#3A5A30', '宝箱': '#C9A24A', '廃墟': '#6F6253', '砦': '#8A5A2A', '城': '#7A5A3A',
    '玉座': '#B8962E', '海': '#24507A', '海岸': '#8E9A6A', '池': '#2F5D8A', '水路': '#2F5D8A',
    '砂漠': '#B8A46A', '雪原': '#D8DDE6', '沼': '#4A5A3A', '山道': '#7A6A4A', '岩': '#55555C',
    '扉': '#6A4A2A', '壊れる扉': '#6A4A2A', '柱': '#3A3A42', '噴水': '#4A7AA0', '花壇': '#6E7A3A',
    '台座': '#8A8070', '遺跡': '#6F6253', '船': '#7A5A3A', '穴': '#141414', '移動不可': '#101716',
    '訪問': '#A8763A', '到達': '#B8962E', '制圧': '#B8962E', '離脱': '#4A8A6A', '防衛': '#8A6A3A',
    '土嚢': '#7A6A4A', '光の茂み': '#5A8A50', '光の守り': '#8AA0C0', '回復': '#5A8A50',
}

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


def item_kind(iid):
    key = iid.upper()
    if any(w in key for w in ('STAFF', 'HEAL', 'MEND', 'RECOVER', 'PHYSIC', 'FORTIFY', 'RESTORE',
                              'RESCUE', 'WARP', 'HAMMERNE', 'SLEEP', 'SILENCE', 'WARD', 'BARRIER')):
        return 'staff'
    if any(w in key for w in ('SWORD', 'BLADE', 'RAPIER', 'BRAVE_S', 'KATANA', 'REGAL', 'RAGNELL',
                              'WISHBLADE', 'VENIN_S', 'KILLING_E', 'KILLINGEDGE', 'WO_DAO', 'WODAO')):
        return 'sword'
    if any(w in key for w in ('LANCE', 'SPEAR', 'PIKE', 'JAVELIN', 'GUARD', 'WISHLANCE')):
        return 'lance'
    if any(w in key for w in ('AXE', 'HAMMER', 'POLEAX', 'URVAN', 'TOMAHAWK', 'BOLTAXE')):
        return 'axe'
    if any(w in key for w in ('BOW', 'BALLISTA', 'BOLTCASTER')):
        return 'bow'
    if any(w in key for w in ('FIRE', 'THUNDER', 'THORON', 'WIND', 'TORNADO', 'BLIZZARD', 'LIGHT',
                              'SHINE', 'NOSFE', 'PURGE', 'REXCALIBUR', 'REXBOLT', 'REXFLAME', 'REXAURA',
                              'ELFIRE', 'ARCFIRE', 'BOLGANONE', 'METEOR', 'ELWIND', 'ELTHUNDER',
                              'ARCTHUNDER', 'ARCWIND', 'CREIDDYLAD', 'CYMBELINE', 'ELLIGHT', 'VALAURA')):
        return 'tome'
    return 'item'


def tables():
    names = {k: v for k, v in TEXT.items() if k.startswith(('MPID_', 'MJID_', 'MIID_'))}
    chapters = {f'bmap{int(k[3:]):02d}': v for k, v in TEXT.items()
                if k.startswith('MCT') and k[3:].isdigit()}
    victory = {k: v.capitalize() for k, v in TEXT.items() if k.startswith('MW_')}
    terrain_en = {k[3:]: v for k, v in TEXT.items() if k.startswith('MT_') and not k[3:].isascii()}
    kinds = {}
    for key in TEXT:
        if key.startswith('MIID_'):
            iid = 'IID_' + key[5:]
            kinds[iid] = item_kind(iid)
    # Practice weapons are named after other items; classify them from the IID too.
    for iid in ('IID_PRACTICESWORD', 'IID_PRACTICEAXE', 'IID_PRACTICELANCE', 'IID_PRACTICEBOW'):
        kinds[iid] = item_kind(iid)
    icons = {k: v for k, v in ICONS.items()}
    return {
        'names': names, 'chapters': chapters, 'victory': victory, 'terrain_en': terrain_en,
        'terrain_color': TERRAIN_COLOR, 'item_kind': kinds, 'icon': icons,
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
    add({'name': 'chapter', 'expr': "default(table(chapters, map_name), map_name)"})
    add({'name': 'victory', 'expr': "default(table(victory, win_key), '')"})
    add({'name': 'cursor_cell', 'expr': 'cursor_y * map_w + cursor_x'})
    add({'name': 'terrain_jp', 'expr': "cells[cursor_cell].tname"})
    add({'name': 'terrain', 'expr': "default(table(terrain_en, terrain_jp), '')"})
    # Units: the one under the cursor, or the one picked by tapping.
    add({'name': 'hover', 'expr': "default(find(units, 'x', cursor_x, 'y', cursor_y), -1)"})
    add({'name': 'focus', 'expr': 'sel >= 0 && sel < count(units) ? sel : hover'})
    add({'name': 'n_player', 'expr': "countif(units, 'army', 0)"})
    add({'name': 'n_enemy', 'expr': "countif(units, 'army', 1)"})
    add({'name': 'n_other', 'expr': "countif(units, 'army', 2)"})
    return v


def unit_fields():
    f = []
    add = f.append
    add({'name': 'pid', 'type': 'string', 'offset': '0x174', 'offsets': ['0x0', '0x0'], 'length': 32})
    add({'name': 'mpid', 'type': 'string', 'offset': '0x174', 'offsets': ['0x4', '0x0'], 'length': 32})
    add({'name': 'mjid', 'type': 'string', 'offset': '0x178', 'offsets': ['0x4', '0x0'], 'length': 32})
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
        add({'name': f'iid{k}', 'type': 'string', 'offset': hex(base), 'offsets': ['0x0', '0x0'], 'length': 32})
        add({'name': f'miid{k}', 'type': 'string', 'offset': hex(base), 'offsets': ['0x4', '0x0'], 'length': 32})
        add({'name': f'uses{k}', 'type': 'u8', 'offset': hex(base + 4)})
        add({'name': f'maxuses{k}', 'type': 'u8', 'offset': hex(base), 'offsets': ['0x18']})
    # Computed.
    add({'name': 'name', 'expr': "default(table(names, mpid), '???')"})
    add({'name': 'cls', 'expr': "default(table(names, mjid), '')"})
    add({'name': 'maxhp', 'expr': 'c_hp + p_hp + g_hp'})
    for stat in STATS[1:]:
        add({'name': stat, 'expr': f'c_{stat} + p_{stat} + g_{stat}'})
        add({'name': f'max_{stat}', 'expr': f'cap_{stat} > 0 ? cap_{stat} : 30'})
    add({'name': 'max_maxhp', 'expr': 'cap_hp > 0 ? cap_hp : 60'})
    add({'name': 'move', 'expr': 'c_move + move_bonus'})
    add({'name': 'build', 'expr': 'c_build + p_build'})
    add({'name': 'items', 'expr': ' + '.join(f'(iid{k} != null ? 1 : 0)' for k in range(ITEM_SLOTS))})
    for k in range(ITEM_SLOTS):
        add({'name': f'item{k}', 'expr': f"default(table(names, miid{k}), '')"})
        add({'name': f'kind{k}', 'expr': f"default(table(item_kind, iid{k}), 'item')"})
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


def unit_card(x, y, w, compact=False):
    """The selected unit: name plate, parchment info, Personal Data, items. Returns (elements, height)."""
    u = 'units[focus]'
    out = []
    name_h = 56 if compact else 64
    out.append(plate(x, y, w, name_h, 'maroon'))
    out.append({'type': 'path', 'x': x + 18, 'y': y + name_h / 2 - 12, 'w': 24, 'h': 24, 'viewbox': [0, 0, 24, 24],
                'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': f'{{{u}.color}}', 'stroke': '@gold_hi', 'stroke_width': 1.5})
    out.append(text(x + 52, y + 6, w - 104, name_h - 12, f'{{{u}.name}}', name_h * 0.58, color='@cream', align='center'))
    y += name_h + 8

    info_h = 136 if compact else 168
    out.append(parchment(x, y, w, info_h))
    pad = 22
    col = (w - pad * 2)
    out.append(text(x + pad, y + 14, col * 0.62, 40, f'{{{u}.cls}}', 34, color='@ink'))
    out.append(text(x + pad + col * 0.62, y + 14, col * 0.38, 40,
                    f'Mv {{{u}.move}}   Wt {{{u}.build}}', 28, color='@ink', align='right'))
    out.append(text(x + pad, y + 56, 60, 36, 'Lv', 26, color='@ink_soft'))
    out.append(text(x + pad + 48, y + 54, 70, 40, f'{{{u}.lv}}', 34, color='@ink'))
    out.append(text(x + pad + 130, y + 56, 50, 36, 'EX', 26, color='@ink_soft'))
    out.append(text(x + pad + 172, y + 54, 80, 40, f"{{{u}.army == 0 ? str({u}.exp) : '--'}}", 34, color='@ink'))
    out.append({'type': 'bar', 'x': x + pad + 262, 'y': y + 70, 'w': col - 262, 'h': 12, 'value': f'{u}.exp', 'max': 100,
                'track': '#405A4214', 'fill': '@exp', 'fill2': '@exp2', 'radius': 6,
                'visible_if': f'{u}.army == 0'})
    hp_y = y + (96 if not compact else 92)
    out.append(text(x + pad, hp_y, 60, 40, 'HP', 28, color='@ink_soft'))
    out.append(text(x + pad + 54, hp_y - 2, 170, 44, f'{{{u}.hp}}', 40, color='@ink', align='right'))
    out.append(text(x + pad + 228, hp_y + 2, 120, 40, f'/ {{{u}.maxhp}}', 32, color='@hp_text'))
    out.append({'type': 'bar', 'x': x + pad + 350, 'y': hp_y + 14, 'w': col - 350, 'h': 16, 'value': f'{u}.hp',
                'max': f'{u}.maxhp', 'track': '#405A4214', 'fill': '@hp', 'fill2': '@hp2', 'radius': 8,
                'stroke': '#805A4214', 'stroke_width': 1.5})
    y += info_h + 8

    stats_h = 196 if compact else 240
    out.append(navy(x, y, w, stats_h))
    out += tab_header(x + 14, y + 12, 210, 'Personal Data', 24)
    cw = (w - 40 - 28) / 2
    ch = 38 if compact else 46
    left = ['str', 'mag', 'skl', 'spd']
    right = ['lck', 'def', 'res']
    top = y + (52 if compact else 60)
    for i, stat in enumerate(left):
        out += stat_cell(x + 20, top + i * ch, cw, ch, stat)
    for i, stat in enumerate(right):
        out += stat_cell(x + 48 + cw, top + i * ch, cw, ch, stat)
    out.append(text(x + 48 + cw, top + 3 * ch, cw * 0.42, ch * 0.62, 'Mv', ch * 0.5, color='@label'))
    out.append(text(x + 48 + cw * 1.42, top + 3 * ch, cw * 0.58, ch * 0.62, f'{{{u}.move}}', ch * 0.56, align='right'))
    y += stats_h + 8
    return out, y


def item_rows(x, y, w, rows, row_h, u='units[focus]'):
    out = []
    for k in range(rows):
        ry = y + k * row_h
        vis = f"{u}.item{k} != ''"
        out.append({'type': 'rect', 'x': x, 'y': ry + row_h - 2, 'w': w, 'h': 1.5, 'fill': '#14FFFFFF', 'visible_if': vis})
        out.append({'type': 'path', 'x': x + 2, 'y': ry + (row_h - 30) / 2, 'w': 30, 'h': 30, 'viewbox': [0, 0, 24, 24],
                    'd': f'{{table(icon, {u}.kind{k})}}', 'fill': '@gold', 'stroke': '@edge', 'stroke_width': 1,
                    'visible_if': vis})
        out.append(text(x + 44, ry, w - 160, row_h, f'{{{u}.item{k}}}', row_h * 0.56, visible_if=vis))
        out.append(text(x + w - 110, ry, 110, row_h, f'{{{u}.uses{k}}}', row_h * 0.56, align='right',
                        color='@label', visible_if=vis))
    return out


def minimap(x, y, w, h):
    return [
        navy(x, y, w, h, corner='diamond'),
        {'type': 'rect', 'x': x + 14, 'y': y + 14, 'w': w - 28, 'h': h - 28, 'fill': '#0A1430', 'fill2': '#050A1C'},
        {'type': 'grid', 'x': x + 18, 'y': y + 18, 'w': w - 36, 'h': h - 36,
         'cols': 'map_w', 'rows': 'map_h', 'source': 'cells',
         'cell': "{default(table(terrain_color, tname), '#30302C')}",
         'grid_color': '#55000000', 'grid_width': 1,
         'visible_if': 'map_ok',
         'layers': [
             {'source': 'units', 'x': 'x', 'y': 'y', 'shape': 'diamond', 'fill': '{color}', 'stroke': '#CCFFFFFF',
              'size': 0.78, 'action': 'select_unit'},
             {'source': 'units', 'x': 'x', 'y': 'y', 'where': '$index == focus', 'shape': 'ring',
              'stroke': '@gold_hi', 'size': 1.1},
             {'x': 'cursor_x', 'y': 'cursor_y', 'shape': 'cursor', 'stroke': '#FFFFFF', 'size': 1.05},
         ]},
        text(x, y + h / 2 - 30, w, 60, 'Waiting for a battle map…', 30, color='@gold', align='center',
             visible_if='!map_ok'),
    ]


def army_banner(x, y, w, h, label, value, color):
    return [
        plate(x, y, w, h, 'navy', fill=color, fill2='#0A0A14', corner='bracket', corner_size=12),
        {'type': 'path', 'x': x + 14, 'y': y + h / 2 - 14, 'w': 28, 'h': 28, 'viewbox': [0, 0, 24, 24],
         'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': color, 'stroke': '@gold_hi', 'stroke_width': 1.5},
        text(x + 54, y, w - 150, h, label, h * 0.46, color='@cream'),
        text(x + w - 96, y, 80, h, value, h * 0.62, align='right'),
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
    out.append(plate(m, m, cw, hh, 'green'))
    out.append(text(m + 26, m + 10, cw - 52, hh * 0.5, '{chapter}', hh * 0.36, color='@cream'))
    out.append(text(m + 26, m + hh * 0.56, cw - 52, hh * 0.36, "{victory != '' ? 'Victory: ' + victory : ''}",
                    hh * 0.25, color='@gold_hi'))
    tx = m * 2 + cw
    out.append(navy(tx, m, tw, hh))
    out.append(text(tx, m + 8, tw, hh * 0.3, 'Turn', hh * 0.22, color='@label', align='center'))
    out.append(text(tx, m + hh * 0.34, tw, hh * 0.56, '{turn}', hh * 0.5, align='center'))
    px = tx + tw + m
    out.append(plate(px, m, pw, hh, 'navy', fill="{phase == 1 ? '#8A1A12' : phase == 2 ? '#1D6A3A' : '#1D3F9A'}",
                     fill2="{phase == 1 ? '#3A0806' : phase == 2 ? '#0A2A16' : '#0A1A4A'}"))
    out.append(text(px, m + 8, pw, hh * 0.5, "{phase == 1 ? 'Enemy' : phase == 2 ? 'Other' : 'Player'}",
                    hh * 0.42, font='title', color='@cream', align='center', fill2='@gold_hi',
                    shadow='@shadow'))
    out.append(text(px, m + hh * 0.54, pw, hh * 0.36, 'PHASE', hh * 0.26, font='title', color='@gold_hi',
                    align='center', letter_spacing=0.2))

    th = 64 if compact else 72
    ty = H - th - m
    tab_w = (W - m * (len(PAGES) + 1)) / len(PAGES)
    for i, (page, label) in enumerate(PAGES):
        x = m + i * (tab_w + m)
        out.append(plate(x, ty, tab_w, th, 'green', visible_if=f"$page == '{page}'", action=f'go_{page}'))
        out.append(plate(x, ty, tab_w, th, 'parchment', visible_if=f"$page != '{page}'", action=f'go_{page}'))
        out.append(text(x, ty, tab_w, th, label, th * 0.48, align='center',
                        color=f"{{$page == '{page}' ? '#F6EAC8' : '#3B2A12'}}"))
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
        out.append(text(m + 20, ty, map_w * 0.6, th, "{terrain != '' ? terrain : '—'}", min(th * 0.5, 32), color='@ink'))
        out.append(text(m + map_w * 0.55, ty, map_w * 0.45 - 20, th, '{cursor_x}, {cursor_y}',
                        min(th * 0.42, 26), color='@ink_soft', align='right'))

    cx = m * 2 + map_w
    cw = W - cx - m
    card, cy = unit_card(cx, top, cw, compact)
    out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements': card})
    rest = bottom - cy
    if rest > 80:
        out.append(navy(cx, cy, cw, rest, visible_if='focus >= 0'))
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
    out = [navy(x, y, w, h)]
    out += tab_header(x + 14, y + 12, 190, title, 24)
    out.append(text(x + w - 120, y + 10, 100, 36, f"{{countif(units, 'army', {army})}}", 30, align='right', color='@label'))
    row = [
        {'type': 'rect', 'x': 0, 'y': 0, 'w': w - 36, 'h': row_h - 6, 'radius': 6,
         'fill': "{$index == focus ? '#26FFFFFF' : '#0DFFFFFF'}", 'action': 'select_unit'},
        {'type': 'path', 'x': 10, 'y': (row_h - 6) / 2 - 10, 'w': 20, 'h': 20, 'viewbox': [0, 0, 24, 24],
         'd': 'M12 1 L23 12 L12 23 L1 12 Z', 'fill': color},
        text(40, 2, (w - 36) * 0.36, row_h * 0.5, '{name}', row_h * 0.4),
        text(40, row_h * 0.48, (w - 36) * 0.36, row_h * 0.4, '{cls}', row_h * 0.3, color='@label', bold=False),
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
    if bottom - cy > 60:
        out.append(parchment(m, cy, cw, bottom - cy, visible_if='focus >= 0', corner='bracket', corner_size=10))
        out.append(text(m + 20, cy, cw - 40, bottom - cy, "{terrain != '' ? 'Standing on ' + terrain : ''}",
                        min(30, (bottom - cy) * 0.45), color='@ink', visible_if='focus >= 0'))
    rx = m * 2 + cw
    rw = W - rx - m
    out.append(navy(rx, top, rw, bottom - top, visible_if='focus >= 0'))
    out.append({'type': 'group', 'visible_if': 'focus >= 0', 'elements':
                tab_header(rx + 14, top + 12, 120, 'Items', 24) +
                item_rows(rx + 24, top + 56, rw - 48, ITEM_SLOTS, 50 if not compact else 44)})
    out.append(navy(m, top, W - m * 2, bottom - top, visible_if='focus < 0'))
    out.append(text(m, top + (bottom - top) / 2 - 30, W - m * 2, 60, 'Select a unit on the Battle or Army page.',
                    30, color='@label', align='center', visible_if='focus < 0'))
    return out


def layout(lid, W, H, compact):
    elements, top, bottom = chrome(W, H, compact)
    pages = {
        'battle': battle_page(W, H, top, bottom, compact),
        'army': army_page(W, H, top, bottom, compact),
        'status': status_page(W, H, top, bottom, compact),
    }
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
        {'id': 'follow_cursor', 'steps': [{'set': 'sel', 'value': -1}]},
    ]
    return {
        '_about': 'Generated by generate.py - edit the generator, not this file.',
        'format': 1,
        'id': 'fe9-path-of-radiance',
        'name': 'Path of Radiance',
        'version': '0.1.0',
        'author': 'Nat McPherson & Claude',
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
