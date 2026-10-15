// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A Dolphin Duo companion manifest: what to read from a game, how to present it on the second
// screen and which actions the player can trigger from it. See docs/DolphinDuo/PACKAGE_FORMAT.md
// for the file format this is parsed from.

#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Duo/Expression.h"
#include "Core/Duo/Template.h"

namespace Duo
{
constexpr int MANIFEST_FORMAT_VERSION = 1;
constexpr u32 MAX_ARRAY_ELEMENTS = 1024;
constexpr u32 MAX_STRING_LENGTH = 256;
constexpr u32 MAX_POINTER_DEPTH = 8;

enum class MemoryType
{
  U8,
  S8,
  U16,
  S16,
  U32,
  S32,
  F32,
  F64,
  Bool,
  String,
};

enum class TextEncoding
{
  Ascii,     // Plain bytes; also correct for UTF-8 text.
  ShiftJIS,  // Japanese releases.
  Utf16BE,   // Common in Wii titles.
};

// Where a value lives and how to decode it, or how to compute it from other values.
struct FieldSpec
{
  std::string name;

  // Memory-backed fields. For top-level values `address` is absolute; for array fields it is an
  // offset from the start of the element.
  bool computed = false;
  MemoryType type = MemoryType::U32;
  u32 address = 0;
  // Dolphin Memory Engine-style pointer chain: with offsets [o1 .. on], read a pointer at
  // `address`, follow it through o1 .. o(n-1), and the field lives at pointer + on.
  std::vector<u32> offsets;
  std::optional<u64> mask;
  u32 shift = 0;
  u32 length = 0;  // Strings: maximum number of bytes to read.
  TextEncoding encoding = TextEncoding::Ascii;

  // Computed fields.
  Expression expression;
};

struct ArraySpec
{
  std::string name;
  u32 address = 0;
  std::vector<u32> offsets;
  u32 stride = 0;
  // When true, each slot of the array holds a pointer to the element rather than the element.
  bool pointer_table = false;
  u32 count = 0;  // Fixed count, used when count_expression is absent.
  std::optional<Expression> count_expression;
  u32 max_count = 256;
  std::optional<Expression> where;  // Elements for which this is falsy are skipped.
  std::vector<FieldSpec> fields;
};

struct TargetSpec
{
  std::string game_id;
  std::set<u16> revisions;  // Empty means any revision.
};

enum class WidgetType
{
  Heading,
  Text,
  Bar,
  Stats,
  List,
  Buttons,
  Map,
  Divider,
};

struct ButtonSpec
{
  Template label;
  std::string action;
  std::optional<Expression> enabled_if;
};

struct StatSpec
{
  Template label;
  Template value;
};

struct MapLayerSpec
{
  std::string source;  // Array name, or empty for a single point.
  Expression x;
  Expression y;
  std::optional<Template> label;
  std::optional<Template> color;
  std::string shape = "dot";  // dot, player, square, diamond
  std::optional<Expression> where;
  std::string action;
};

struct WidgetSpec
{
  WidgetType type = WidgetType::Text;
  std::optional<Expression> visible_if;

  // Heading, Text, Bar (text over the bar), List (row text).
  std::optional<Template> text;
  std::string style;  // Text: "body", "caption", "title", "mono"
  std::optional<Template> color;

  // Bar.
  std::optional<Template> label;
  std::optional<Expression> value;
  std::optional<Expression> max;

  // Stats.
  u32 columns = 2;
  std::vector<StatSpec> stats;

  // List.
  std::string source;
  std::optional<Template> detail;
  std::optional<Expression> bar;
  std::optional<Expression> bar_max;
  std::optional<Template> badge;
  std::string action;
  std::optional<Template> empty_text;

  // Buttons.
  std::vector<ButtonSpec> buttons;

  // Map.
  std::array<Expression, 4> bounds;  // min_x, min_y, max_x, max_y in game coordinates
  bool flip_y = false;
  std::string image;  // Optional background image inside the package.
  std::vector<MapLayerSpec> layers;
  float aspect = 1.0f;  // Height / width of the map view.
};

struct PageSpec
{
  std::string id;
  Template title;
  std::optional<Expression> visible_if;
  std::vector<WidgetSpec> widgets;
};

enum class Control : int
{
  // Values match ciface::Touch::ControlID so the Android input sink can pass them through.
  GC_A = 0,
  GC_B = 1,
  GC_X = 2,
  GC_Y = 3,
  GC_Z = 4,
  GC_START = 5,
  GC_DPAD_UP = 6,
  GC_DPAD_DOWN = 7,
  GC_DPAD_LEFT = 8,
  GC_DPAD_RIGHT = 9,
  GC_L = 10,
  GC_R = 11,
  GC_L_ANALOG = 12,
  GC_R_ANALOG = 13,
  GC_STICK_X = 14,
  GC_STICK_Y = 15,
  GC_C_STICK_X = 16,
  GC_C_STICK_Y = 17,
  WII_A = 18,
  WII_B = 19,
  WII_1 = 20,
  WII_2 = 21,
  WII_PLUS = 22,
  WII_MINUS = 23,
  WII_HOME = 24,
  WII_DPAD_UP = 25,
  WII_DPAD_DOWN = 26,
  WII_DPAD_LEFT = 27,
  WII_DPAD_RIGHT = 28,
  WII_IR_X = 29,
  WII_IR_Y = 30,
  NUNCHUK_C = 31,
  NUNCHUK_Z = 32,
  NUNCHUK_STICK_X = 33,
  NUNCHUK_STICK_Y = 34,
  CLASSIC_A = 35,
  CLASSIC_B = 36,
  CLASSIC_X = 37,
  CLASSIC_Y = 38,
  CLASSIC_ZL = 39,
  CLASSIC_ZR = 40,
  CLASSIC_PLUS = 41,
  CLASSIC_MINUS = 42,
  CLASSIC_HOME = 43,
  CLASSIC_DPAD_UP = 44,
  CLASSIC_DPAD_DOWN = 45,
  CLASSIC_DPAD_LEFT = 46,
  CLASSIC_DPAD_RIGHT = 47,
  CLASSIC_L = 48,
  CLASSIC_R = 49,
  CLASSIC_L_ANALOG = 50,
  CLASSIC_R_ANALOG = 51,
  CLASSIC_LEFT_STICK_X = 52,
  CLASSIC_LEFT_STICK_Y = 53,
  CLASSIC_RIGHT_STICK_X = 54,
  CLASSIC_RIGHT_STICK_Y = 55,
};

std::optional<Control> ParseControl(std::string_view name);
std::string_view ControlName(Control control);
bool IsGameCubeControl(Control control);

struct ActionStep
{
  enum class Kind
  {
    Press,  // Hold `control` at `amount` for `frames` frames, then release.
    Wait,   // Do nothing for `frames` frames.
    Write,  // Write `value` to a memory-backed value or array field.
    Page,   // Switch the companion to page `page`.
  };

  Kind kind = Kind::Wait;
  Control control = Control::GC_A;
  double amount = 1.0;
  u32 frames = 1;

  std::string target;               // Write: value name, or array name with `field`.
  std::string field;                // Write: array field.
  std::optional<Expression> index;  // Write to array field: element index (default $index).
  Expression value;                 // Write.

  std::string page;  // Page.
};

struct ActionSpec
{
  std::string id;
  std::optional<Template> label;
  std::vector<ActionStep> steps;
  bool needs_input = false;
  bool needs_write = false;
};

struct Manifest
{
  int format = MANIFEST_FORMAT_VERSION;
  std::string id;
  std::string name;
  std::string version;
  std::string author;
  std::string description;

  std::vector<TargetSpec> targets;
  int controller = 0;
  u32 poll_hz = 20;
  bool allow_write = false;
  bool allow_input = false;

  std::vector<FieldSpec> values;
  std::vector<ArraySpec> arrays;
  std::map<std::string, std::map<std::string, std::string>, std::less<>> tables;
  std::vector<PageSpec> pages;
  std::vector<ActionSpec> actions;

  // Whether this manifest declares support for the given running game.
  bool Targets(std::string_view game_id, u16 revision) const;

  const ActionSpec* FindAction(std::string_view id) const;
  const ArraySpec* FindArray(std::string_view name) const;
  const FieldSpec* FindValue(std::string_view name) const;
};

struct ManifestParseResult
{
  std::optional<Manifest> manifest;
  std::vector<std::string> errors;
};

ManifestParseResult ParseManifest(std::string_view json);
}  // namespace Duo
