// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <bit>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <picojson.h>

#include "Core/Duo/ActionRunner.h"
#include "Core/Duo/Evaluator.h"
#include "Core/Duo/Expression.h"
#include "Core/Duo/Manifest.h"
#include "Core/Duo/Template.h"

namespace
{
// Big-endian emulated memory, like the GameCube's, covering 0x80000000-0x80100000.
class FakeMemory final : public Duo::MemorySource
{
public:
  static constexpr u32 BASE = 0x80000000;
  static constexpr u32 SIZE = 0x100000;

  FakeMemory() : m_bytes(SIZE, 0) {}

  std::optional<u64> Read(u32 address, u32 size) override
  {
    if (!InRange(address, size))
      return std::nullopt;
    u64 value = 0;
    for (u32 i = 0; i < size; ++i)
      value = (value << 8) | m_bytes[address - BASE + i];
    return value;
  }

  bool Write(u32 address, u32 size, u64 value) override
  {
    if (!InRange(address, size))
      return false;
    for (u32 i = 0; i < size; ++i)
      m_bytes[address - BASE + i] = static_cast<u8>(value >> (8 * (size - 1 - i)));
    return true;
  }

  bool IsRAM(u32 address) override { return InRange(address, 1); }

  void Put8(u32 address, u8 v) { Write(address, 1, v); }
  void Put16(u32 address, u16 v) { Write(address, 2, v); }
  void Put32(u32 address, u32 v) { Write(address, 4, v); }
  void PutString(u32 address, std::string_view text)
  {
    for (size_t i = 0; i < text.size(); ++i)
      Put8(address + static_cast<u32>(i), static_cast<u8>(text[i]));
  }

private:
  static bool InRange(u32 address, u32 size)
  {
    return address >= BASE && static_cast<u64>(address) + size <= static_cast<u64>(BASE) + SIZE;
  }
  std::vector<u8> m_bytes;
};

class FakeInput final : public Duo::InputSink
{
public:
  void Press(int controller, Duo::Control control, double amount) override
  {
    log.push_back(fmt::format("press {} {}", controller, Duo::ControlName(control)));
  }
  void Release(int controller, Duo::Control control) override
  {
    log.push_back(fmt::format("release {} {}", controller, Duo::ControlName(control)));
  }
  std::vector<std::string> log;
};

class MapScope final : public Duo::Scope
{
public:
  Duo::Value Lookup(std::string_view name) const override
  {
    const auto it = values.find(std::string(name));
    return it == values.end() ? Duo::Value::Null() : it->second;
  }
  std::map<std::string, Duo::Value> values;
};

Duo::Value Eval(std::string_view source, const Duo::Scope& scope)
{
  std::string error;
  const std::optional<Duo::Expression> expression = Duo::Expression::Parse(source, &error);
  EXPECT_TRUE(expression.has_value()) << source << ": " << error;
  return expression ? expression->Evaluate(scope) : Duo::Value::Null();
}

std::string Render(std::string_view source, const Duo::Scope& scope)
{
  std::string error;
  const std::optional<Duo::Template> text = Duo::Template::Parse(source, &error);
  EXPECT_TRUE(text.has_value()) << source << ": " << error;
  return text ? text->Render(scope) : std::string();
}

Duo::Manifest ParseOrFail(const std::string& json)
{
  Duo::ManifestParseResult result = Duo::ParseManifest(json);
  for (const std::string& error : result.errors)
    ADD_FAILURE() << error;
  EXPECT_TRUE(result.manifest.has_value());
  return result.manifest ? std::move(*result.manifest) : Duo::Manifest{};
}

// A small "tactics game" used by several tests.
const char* const TACTICS_MANIFEST = R"json({
  "format": 1,
  "id": "test-tactics",
  "name": "Test Tactics",
  "version": "1.0.0",
  "targets": [{ "game_id": "GTTE01", "revisions": [0, 1] }],
  "capabilities": ["write_memory", "input"],
  "tables": { "class": { "0": "Lord", "1": "Archer", "0x10": "Sage" } },
  "values": [
    { "name": "gold", "type": "u32", "address": "0x80001000" },
    { "name": "chapter", "type": "u8", "address": "0x80001004" },
    { "name": "in_battle", "type": "bool", "address": "0x80001005" },
    { "name": "turn", "type": "u16", "address": "0x80001006", "mask": "0x0FF0", "shift": 4 },
    { "name": "cursor_x", "type": "f32", "address": "0x80001008" },
    { "name": "leader_hp", "type": "u8", "address": "0x80001100", "offsets": ["0x10", "0x4"] },
    { "name": "unit_count", "type": "u8", "address": "0x8000100C" },
    { "name": "total_hp", "expr": "units[0].hp + units[1].hp" },
    { "name": "alive", "expr": "count(units)" }
  ],
  "arrays": [
    {
      "name": "units",
      "address": "0x80002000",
      "stride": "0x20",
      "count_expr": "unit_count",
      "where": "hp > 0",
      "fields": [
        { "name": "name", "type": "string", "offset": 0, "length": 8 },
        { "name": "hp", "type": "u8", "offset": "0x10" },
        { "name": "max_hp", "type": "u8", "offset": "0x11" },
        { "name": "class", "type": "u8", "offset": "0x12" },
        { "name": "x", "type": "s16", "offset": "0x14" },
        { "name": "y", "type": "s16", "offset": "0x16" },
        { "name": "class_name", "expr": "default(table(class, class), '???')" }
      ]
    }
  ],
  "pages": [
    {
      "id": "army",
      "title": "Army ({alive})",
      "widgets": [
        { "type": "heading", "text": "Chapter {chapter}" },
        { "type": "text", "text": "Gold {gold:07}", "style": "mono" },
        { "type": "bar", "label": "Leader", "value": "units[0].hp", "max": "units[0].max_hp",
          "text": "{units[0].hp}/{units[0].max_hp}" },
        { "type": "list", "source": "units", "text": "{name}", "detail": "{class_name} Lv?",
          "bar": "hp", "bar_max": "max_hp", "action": "heal" },
        { "type": "buttons", "buttons": [
          { "label": "Menu", "action": "open_menu" },
          { "label": "Battle only", "action": "open_menu", "enabled_if": "in_battle" }
        ]},
        { "type": "text", "text": "hidden", "visible_if": "false" }
      ]
    },
    {
      "id": "map",
      "title": "Map",
      "visible_if": "in_battle",
      "widgets": [
        { "type": "map", "bounds": [0, 0, 16, 16], "flip_y": true,
          "points": [{ "source": "units", "x": "x", "y": "y", "label": "{name}" }] }
      ]
    }
  ],
  "actions": [
    { "id": "heal", "steps": [ { "write": "units[].hp", "value": "max_hp" } ] },
    { "id": "rich", "steps": [ { "write": "gold", "value": "gold + 1000" }, { "page": "army" } ] },
    { "id": "open_menu", "steps": [ { "press": "START", "frames": 2 }, { "wait": 3 },
                                    { "press": "A", "frames": 1 } ] }
  ]
})json";

void FillTactics(FakeMemory& memory)
{
  memory.Put32(0x80001000, 4321);
  memory.Put8(0x80001004, 7);
  memory.Put8(0x80001005, 1);
  memory.Put16(0x80001006, 0x0120);  // turn = 0x12 = 18
  memory.Put32(0x80001008, std::bit_cast<u32>(2.5f));
  memory.Put8(0x8000100C, 4);

  // leader_hp: *(*(0x80001100) + 0x10) + 0x4
  memory.Put32(0x80001100, 0x80003000);
  memory.Put32(0x80003010, 0x80003100);
  memory.Put8(0x80003104, 33);

  const auto unit = [&](u32 slot, std::string_view name, u8 hp, u8 max_hp, u8 cls, s16 x, s16 y) {
    const u32 base = 0x80002000 + slot * 0x20;
    memory.PutString(base, name);
    memory.Put8(base + 0x10, hp);
    memory.Put8(base + 0x11, max_hp);
    memory.Put8(base + 0x12, cls);
    memory.Put16(base + 0x14, static_cast<u16>(x));
    memory.Put16(base + 0x16, static_cast<u16>(y));
  };
  unit(0, "Ike", 30, 40, 0, 4, 4);
  unit(1, "Rolf", 0, 20, 1, 5, 5);  // Defeated: filtered out by "where".
  unit(2, "Soren", 18, 25, 16, 8, 12);
  unit(3, "Mist", 15, 22, 9, -1, 0);  // Unknown class; off-map x.
}
}  // namespace

TEST(DuoExpression, ArithmeticAndPrecedence)
{
  MapScope scope;
  EXPECT_EQ(Eval("1 + 2 * 3", scope), Duo::Value(7));
  EXPECT_EQ(Eval("(1 + 2) * 3", scope), Duo::Value(9));
  EXPECT_EQ(Eval("10 - 4 - 3", scope), Duo::Value(3));
  EXPECT_EQ(Eval("2 * -3", scope), Duo::Value(-6));
  EXPECT_EQ(Eval("0x10 | 0x01", scope), Duo::Value(17));
  EXPECT_EQ(Eval("1 << 4 >> 2", scope), Duo::Value(4));
  EXPECT_EQ(Eval("7 % 4", scope), Duo::Value(3));
  EXPECT_EQ(Eval("1 < 2 && 2 < 3", scope), Duo::Value(true));
  EXPECT_EQ(Eval("1 > 2 ? 'a' : 'b'", scope), Duo::Value("b"));
  EXPECT_EQ(Eval("1 ? 2 ? 3 : 4 : 5", scope), Duo::Value(3));
  EXPECT_EQ(Eval("'Lv ' + 5", scope), Duo::Value("Lv 5"));
}

TEST(DuoExpression, NullPropagatesAndDefaultRecovers)
{
  MapScope scope;
  scope.values["hp"] = Duo::Value(10);
  EXPECT_TRUE(Eval("missing + 1", scope).IsNull());
  EXPECT_TRUE(Eval("hp / 0", scope).IsNull());
  EXPECT_EQ(Eval("default(missing, 5)", scope), Duo::Value(5));
  EXPECT_EQ(Eval("missing == null", scope), Duo::Value(true));
  EXPECT_EQ(Eval("!missing", scope), Duo::Value(true));
}

TEST(DuoExpression, Functions)
{
  MapScope scope;
  EXPECT_EQ(Eval("min(4, 2, 9)", scope), Duo::Value(2));
  EXPECT_EQ(Eval("max(4, 2, 9)", scope), Duo::Value(9));
  EXPECT_EQ(Eval("clamp(15, 0, 10)", scope), Duo::Value(10));
  EXPECT_EQ(Eval("bit(0x8, 3)", scope), Duo::Value(true));
  EXPECT_EQ(Eval("bits(0xABCD, 4, 8)", scope), Duo::Value(0xBC));
  EXPECT_EQ(Eval("round(2.5)", scope), Duo::Value(3));
  EXPECT_EQ(Eval("str(42) + '!'", scope), Duo::Value("42!"));
}

TEST(DuoExpression, ParseErrors)
{
  std::string error;
  EXPECT_FALSE(Duo::Expression::Parse("1 +", &error));
  EXPECT_NE(error.find("end of expression"), std::string::npos);
  EXPECT_FALSE(Duo::Expression::Parse("nope(1)", &error));
  EXPECT_NE(error.find("unknown function"), std::string::npos);
  EXPECT_FALSE(Duo::Expression::Parse("clamp(1, 2)", &error));
  EXPECT_FALSE(Duo::Expression::Parse("(1 + 2", &error));
  EXPECT_FALSE(Duo::Expression::Parse("0x", &error));
  EXPECT_FALSE(Duo::Expression::Parse("'open", &error));
  EXPECT_FALSE(Duo::Expression::Parse("a # b", &error));
}

TEST(DuoTemplate, Formatting)
{
  MapScope scope;
  scope.values["hp"] = Duo::Value(7);
  scope.values["gold"] = Duo::Value(1234);
  scope.values["ratio"] = Duo::Value(0.25);
  scope.values["speed"] = Duo::Value(1.0 / 3.0);
  scope.values["name"] = Duo::Value("Ike");

  EXPECT_EQ(Render("HP {hp}", scope), "HP 7");
  EXPECT_EQ(Render("{gold:07}", scope), "0001234");
  EXPECT_EQ(Render("[{hp:3}]", scope), "[  7]");
  EXPECT_EQ(Render("[{name:-5}]", scope), "[Ike  ]");
  EXPECT_EQ(Render("{gold:x} {gold:08X}", scope), "4d2 000004D2");
  EXPECT_EQ(Render("{ratio:%}", scope), "25%");
  EXPECT_EQ(Render("{speed}", scope), "0.33");
  EXPECT_EQ(Render("{speed:.3}", scope), "0.333");
  EXPECT_EQ(Render("{missing}", scope), "?");
  EXPECT_EQ(Render("{{literal}}", scope), "{literal}");
  EXPECT_EQ(Render("{hp > 5 ? 'ok' : 'low'}", scope), "ok");
  EXPECT_EQ(Render("{hp > 5 ? hp : 0:03}", scope), "007");
}

TEST(DuoTemplate, Errors)
{
  std::string error;
  EXPECT_FALSE(Duo::Template::Parse("{hp", &error));
  EXPECT_FALSE(Duo::Template::Parse("hp}", &error));
  EXPECT_FALSE(Duo::Template::Parse("{hp:q}", &error));
  EXPECT_FALSE(Duo::Template::Parse("{1 +}", &error));
}

TEST(DuoManifest, ParsesTacticsManifest)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  EXPECT_EQ(manifest.id, "test-tactics");
  EXPECT_TRUE(manifest.Targets("GTTE01", 1));
  EXPECT_FALSE(manifest.Targets("GTTE01", 2));
  EXPECT_FALSE(manifest.Targets("GTTP01", 0));
  EXPECT_TRUE(manifest.allow_write);
  EXPECT_TRUE(manifest.allow_input);
  ASSERT_NE(manifest.FindAction("heal"), nullptr);
  EXPECT_TRUE(manifest.FindAction("heal")->needs_write);
  EXPECT_TRUE(manifest.FindAction("open_menu")->needs_input);
  EXPECT_EQ(manifest.tables.at("class").at("16"), "Sage");
}

TEST(DuoManifest, ReportsHelpfulErrors)
{
  const Duo::ManifestParseResult result = Duo::ParseManifest(R"json({
    "format": 1, "id": "bad", "name": "Bad", "version": "1",
    "targets": [{ "game_id": "GTTE01" }],
    "values": [
      { "name": "hp", "type": "u12", "address": "0x80000000" },
      { "name": "mp", "type": "u8", "adress": "0x80000000" }
    ],
    "pages": [{ "id": "main", "title": "Main", "widgets": [
      { "type": "list", "source": "nothing", "text": "{x}" },
      { "type": "buttons", "buttons": [{ "label": "Go", "action": "missing" }] }
    ]}],
    "actions": [{ "id": "cheat", "steps": [{ "write": "mp", "value": 1 }] }]
  })json");
  EXPECT_FALSE(result.manifest.has_value());

  const auto has_error = [&](std::string_view needle) {
    for (const std::string& error : result.errors)
    {
      if (error.find(needle) != std::string::npos)
        return true;
    }
    return false;
  };
  EXPECT_TRUE(has_error("unknown type \"u12\""));
  EXPECT_TRUE(has_error("unknown key \"adress\""));
  EXPECT_TRUE(has_error("unknown array \"nothing\""));
  EXPECT_TRUE(has_error("unknown action \"missing\""));
  EXPECT_TRUE(has_error("write_memory"));
}

TEST(DuoManifest, RejectsFutureFormat)
{
  const Duo::ManifestParseResult result = Duo::ParseManifest(R"({"format": 2})");
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors.front().find("format 2"), std::string::npos);
}

TEST(DuoEvaluator, SamplesValuesPointersAndArrays)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);

  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, Duo::SystemInfo{"GTTE01", 0, "Test", false, 1, 0.0});

  EXPECT_EQ(evaluator.GetValue("gold"), Duo::Value(4321));
  EXPECT_EQ(evaluator.GetValue("in_battle"), Duo::Value(true));
  EXPECT_EQ(evaluator.GetValue("turn"), Duo::Value(0x12));
  EXPECT_EQ(evaluator.GetValue("cursor_x"), Duo::Value(2.5));
  EXPECT_EQ(evaluator.GetValue("leader_hp"), Duo::Value(33));

  // "where hp > 0" removed Rolf, so units[1] is Soren.
  const std::vector<Duo::ArrayElement>* units = evaluator.GetArray("units");
  ASSERT_NE(units, nullptr);
  ASSERT_EQ(units->size(), 3u);
  EXPECT_EQ((*units)[0].fields[0], Duo::Value("Ike"));
  EXPECT_EQ((*units)[1].fields[0], Duo::Value("Soren"));
  EXPECT_EQ((*units)[1].slot, 2u);
  EXPECT_EQ((*units)[1].fields[6], Duo::Value("Sage"));
  EXPECT_EQ((*units)[2].fields[6], Duo::Value("???"));
  EXPECT_EQ((*units)[2].fields[4], Duo::Value(-1));

  // Computed values reached into the array before it was otherwise sampled.
  EXPECT_EQ(evaluator.GetValue("total_hp"), Duo::Value(48));
  EXPECT_EQ(evaluator.GetValue("alive"), Duo::Value(3));
}

TEST(DuoEvaluator, BrokenPointerChainIsUnavailable)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);
  memory.Put32(0x80003010, 0);  // Null pointer in the middle of the chain.

  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});
  EXPECT_TRUE(evaluator.GetValue("leader_hp").IsNull());
}

TEST(DuoEvaluator, RendersPages)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  std::string page = "army";
  const picojson::value snapshot = evaluator.Render(page);
  const picojson::object& root = snapshot.get<picojson::object>();

  const picojson::array& pages = root.at("pages").get<picojson::array>();
  ASSERT_EQ(pages.size(), 2u);
  EXPECT_EQ(pages[0].get("title").get<std::string>(), "Army (3)");

  const picojson::array& widgets = root.at("widgets").get<picojson::array>();
  ASSERT_EQ(widgets.size(), 5u);  // The visible_if:false text is gone.
  EXPECT_EQ(widgets[0].get("text").get<std::string>(), "Chapter 7");
  EXPECT_EQ(widgets[1].get("text").get<std::string>(), "Gold 0004321");
  EXPECT_DOUBLE_EQ(widgets[2].get("fraction").get<double>(), 0.75);
  EXPECT_EQ(widgets[2].get("text").get<std::string>(), "30/40");

  const picojson::array& rows = widgets[3].get("rows").get<picojson::array>();
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[1].get("text").get<std::string>(), "Soren");
  EXPECT_EQ(rows[1].get("detail").get<std::string>(), "Sage Lv?");
  EXPECT_DOUBLE_EQ(rows[1].get("fraction").get<double>(), 18.0 / 25.0);

  const picojson::array& buttons = widgets[4].get("buttons").get<picojson::array>();
  EXPECT_TRUE(buttons[1].get("enabled").get<bool>());

  // The map page is visible only in battle; its off-map unit is dropped and y is flipped.
  page = "map";
  const picojson::value map_snapshot = evaluator.Render(page);
  EXPECT_EQ(page, "map");
  const picojson::array& points =
      map_snapshot.get("widgets").get<picojson::array>()[0].get("points").get<picojson::array>();
  ASSERT_EQ(points.size(), 2u);
  EXPECT_DOUBLE_EQ(points[0].get("x").get<double>(), 0.25);
  EXPECT_DOUBLE_EQ(points[0].get("y").get<double>(), 0.75);

  // Leaving battle hides the map page and falls back to the first page.
  memory.Put8(0x80001005, 0);
  evaluator.Sample(memory, {});
  page = "map";
  evaluator.Render(page);
  EXPECT_EQ(page, "army");
}

TEST(DuoEvaluator, WritesClampAndRespectMasks)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  // Heal Soren (row 1 of the filtered list, slot 2 in memory) to his max HP.
  const Duo::ActionStep& heal = manifest.FindAction("heal")->steps[0];
  EXPECT_FALSE(evaluator.Write(memory, heal, "units", 1).has_value());
  EXPECT_EQ(memory.Read(0x80002000 + 2 * 0x20 + 0x10, 1), 25u);

  Duo::ActionStep write;
  write.kind = Duo::ActionStep::Kind::Write;
  write.target = "turn";
  write.value = *Duo::Expression::Parse("0x1FF");  // Clamped to the 8-bit field.
  memory.Put16(0x80001006, 0xF00F);
  EXPECT_FALSE(evaluator.Write(memory, write, "", -1).has_value());
  EXPECT_EQ(memory.Read(0x80001006, 2), 0xFFFFu);

  write.target = "chapter";
  write.value = *Duo::Expression::Parse("-5");
  EXPECT_FALSE(evaluator.Write(memory, write, "", -1).has_value());
  EXPECT_EQ(memory.Read(0x80001004, 1), 0u);
}

TEST(DuoActionRunner, PressesForExactFramesAndRunsSteps)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  Duo::ActionRunner runner;
  FakeInput input;
  std::string page = "map";
  std::vector<std::string> messages;
  ASSERT_TRUE(runner.Enqueue({"open_menu", "", -1}));
  ASSERT_TRUE(runner.Enqueue({"rich", "", -1}));

  // START for 2 frames, wait 3, A for 1, then the gold write and page switch.
  std::vector<std::string> timeline;
  for (int frame = 0; frame < 10; ++frame)
  {
    input.log.clear();
    runner.Step(manifest, evaluator, memory, &input, true, page, messages);
    std::string events;
    for (const std::string& entry : input.log)
      events += entry + ";";
    timeline.push_back(events);
  }
  EXPECT_EQ(timeline[0], "press 0 START;");
  EXPECT_EQ(timeline[1], "");
  EXPECT_EQ(timeline[2], "release 0 START;");
  EXPECT_EQ(timeline[5], "press 0 A;");
  EXPECT_EQ(timeline[6], "release 0 A;");
  EXPECT_TRUE(messages.empty());
  EXPECT_EQ(memory.Read(0x80001000, 4), 5321u);
  EXPECT_EQ(page, "army");
  EXPECT_FALSE(runner.IsBusy());
  EXPECT_TRUE(runner.TakeDirty());
}

TEST(DuoActionRunner, RefusesWritesWhenDisabled)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  FillTactics(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  Duo::ActionRunner runner;
  std::string page = "army";
  std::vector<std::string> messages;
  runner.Enqueue({"rich", "", -1});
  runner.Step(manifest, evaluator, memory, nullptr, false, page, messages);
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(memory.Read(0x80001000, 4), 4321u);
}

TEST(DuoActionRunner, CancelReleasesHeldButton)
{
  const Duo::Manifest manifest = ParseOrFail(TACTICS_MANIFEST);
  FakeMemory memory;
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  Duo::ActionRunner runner;
  FakeInput input;
  std::string page;
  std::vector<std::string> messages;
  runner.Enqueue({"open_menu", "", -1});
  runner.Step(manifest, evaluator, memory, &input, true, page, messages);
  runner.Cancel(&input, 0);
  ASSERT_EQ(input.log.size(), 2u);
  EXPECT_EQ(input.log[1], "release 0 START");
  EXPECT_FALSE(runner.IsBusy());
}

// ---------------------------------------------------------------------------------------------
// Canvas pages, UI state, computed addresses and the find()/countif() helpers.

namespace
{
const char* const CANVAS_MANIFEST = R"json({
  "format": 1, "id": "canvas-test", "name": "Canvas", "version": "1",
  "targets": [{"game_id": "GCNE01"}],
  "theme": {
    "colors": {"gold": "#FFD700", "panel": "#102030"},
    "fonts": {"ui": "fonts/ui.ttf"},
    "styles": {"card": {"fill": "@panel", "edge": "@gold", "corner": "notch"}}
  },
  "layouts": [
    {"id": "wide", "canvas": [1240, 1080], "background": "@panel",
     "elements": [{"type": "text", "x": 0, "y": 0, "w": 100, "h": 20, "text": "Turn {turn}"}]},
    {"id": "classic", "canvas": [1024, 768]}
  ],
  "state": {"sel": -1},
  "values": [
    {"name": "turn", "type": "u8", "address": "0x80001000"},
    {"name": "table_ptr", "type": "u32", "address": "0x80001004"},
    {"name": "via_expr", "type": "u16", "address_expr": "table_ptr + 0x10"},
    {"name": "cx", "type": "u8", "address": "0x80001008"},
    {"name": "cy", "type": "u8", "address": "0x80001009"},
    {"name": "hover", "expr": "default(find(units, 'x', cx, 'y', cy), -1)"},
    {"name": "focus", "expr": "sel >= 0 ? sel : hover"},
    {"name": "enemies", "expr": "countif(units, 'army', 1)"}
  ],
  "arrays": [
    {"name": "units", "address": "0x80002000", "stride": "0x10", "count": 4, "where": "hp > 0",
     "fields": [
       {"name": "hp", "type": "u8", "offset": "0x0"},
       {"name": "army", "type": "u8", "offset": "0x1"},
       {"name": "x", "type": "u8", "offset": "0x2"},
       {"name": "y", "type": "u8", "offset": "0x3"},
       {"name": "kind", "type": "u8", "offset": "0x4"},
       {"name": "kind_name", "type": "string", "address_expr": "0x80003000 + kind * 8", "length": 8}
     ]},
    {"name": "cells", "address_expr": "table_ptr", "stride": "0x1", "count": 6,
     "fields": [{"name": "t", "type": "u8", "offset": "0x0"}]}
  ],
  "tables": {"terrain": {"0": "#00FF00", "1": "@gold"}},
  "pages": [
    {"id": "battle", "title": "Battle", "elements": {
      "wide": [
        {"type": "frame", "style": "card", "x": 10, "y": 10, "w": 200, "h": 100, "edge": "#FF0000"},
        {"type": "group", "x": 300, "y": 40, "elements": [
          {"type": "rect", "x": 5, "y": 5, "w": 10, "h": 10, "fill": "@gold"}]},
        {"type": "repeat", "source": "units", "where": "army == 0", "x": 20, "y": 200, "dy": 30,
         "elements": [{"type": "text", "x": 0, "y": 0, "w": 100, "h": 30, "text": "{kind_name}",
                       "action": "pick"}]},
        {"type": "grid", "x": 0, "y": 400, "w": 300, "h": 200, "cols": 3, "rows": 2,
         "source": "cells", "cell": "{table(terrain, t)}",
         "layers": [{"source": "units", "x": "x", "y": "y", "fill": "{army == 1 ? '#FF0000' : '#0000FF'}"},
                    {"x": "cx", "y": "cy", "shape": "cursor"}]},
        {"type": "text", "x": 0, "y": 700, "w": 300, "h": 30,
         "text": "Focus {focus} of {count(units)}, {enemies} enemy, layout {$layout}"}
      ],
      "*": [{"type": "text", "x": 0, "y": 0, "w": 50, "h": 20, "text": "small"}]
    }}
  ],
  "actions": [{"id": "pick", "steps": [{"set": "sel", "value": "$index"}]}]
})json";

void FillCanvas(FakeMemory& memory)
{
  memory.Put8(0x80001000, 3);
  memory.Put32(0x80001004, 0x80004000);
  memory.Put16(0x80004010, 0x0102);  // via_expr
  memory.Put8(0x80001008, 2);        // cursor on (2, 1)
  memory.Put8(0x80001009, 1);
  // Units: hp, army, x, y, kind
  const u8 units[4][5] = {{10, 0, 0, 0, 1}, {0, 0, 1, 1, 0}, {7, 1, 2, 1, 2}, {5, 0, 1, 0, 0}};
  for (u32 i = 0; i < 4; ++i)
    for (u32 b = 0; b < 5; ++b)
      memory.Put8(0x80002000 + i * 0x10 + b, units[i][b]);
  memory.PutString(0x80003000, "Sword");
  memory.PutString(0x80003008, "Lance");
  memory.PutString(0x80003010, "Axe");
  const u8 cells[6] = {0, 1, 0, 1, 1, 0};
  for (u32 i = 0; i < 6; ++i)
    memory.Put8(0x80004000 + i, cells[i]);
}

const picojson::object* FindElement(const picojson::array& elements, std::string_view type,
                                    size_t nth = 0)
{
  for (const picojson::value& element : elements)
  {
    if (element.get("t").get<std::string>() == type && nth-- == 0)
      return &element.get<picojson::object>();
  }
  return nullptr;
}
}  // namespace

TEST(DuoCanvas, ChoosesLayoutByScreenShape)
{
  const Duo::Manifest manifest = ParseOrFail(CANVAS_MANIFEST);
  EXPECT_EQ(manifest.ChooseLayout(1240, 1080)->id, "wide");  // AYN Thor bottom screen
  EXPECT_EQ(manifest.ChooseLayout(640, 480)->id, "classic");
  EXPECT_EQ(manifest.ChooseLayout(0, 0)->id, "wide");  // Unknown size: the first layout.
}

TEST(DuoCanvas, RendersPositionedThemedElements)
{
  const Duo::Manifest manifest = ParseOrFail(CANVAS_MANIFEST);
  FakeMemory memory;
  FillCanvas(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});

  EXPECT_EQ(evaluator.GetValue("via_expr"), Duo::Value(0x0102));
  EXPECT_EQ(evaluator.GetValue("hover"), Duo::Value(1));  // units[1] after "where" is at (2, 1)
  EXPECT_EQ(evaluator.GetValue("enemies"), Duo::Value(1));

  std::string page;
  const picojson::value snapshot = evaluator.Render(page, "wide");
  const picojson::object& canvas = snapshot.get("canvas").get<picojson::object>();
  EXPECT_EQ(canvas.at("layout").get<std::string>(), "wide");
  EXPECT_EQ(canvas.at("background").get<std::string>(), "#102030");
  EXPECT_EQ(canvas.at("fonts").get("ui").get<std::string>(), "fonts/ui.ttf");

  const picojson::array& elements = snapshot.get("elements").get<picojson::array>();
  // Layout chrome comes first.
  EXPECT_EQ(elements[0].get("text").get<std::string>(), "Turn 3");

  // The style supplies fill and corner; the element's own edge wins.
  const picojson::object* frame = FindElement(elements, "frame");
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->at("fill").get<std::string>(), "#102030");
  EXPECT_EQ(frame->at("edge").get<std::string>(), "#FF0000");
  EXPECT_EQ(frame->at("corner").get<std::string>(), "notch");

  // Groups offset their children.
  const picojson::object* rect = FindElement(elements, "rect");
  ASSERT_NE(rect, nullptr);
  EXPECT_DOUBLE_EQ(rect->at("x").get<double>(), 305);
  EXPECT_DOUBLE_EQ(rect->at("y").get<double>(), 45);
  EXPECT_EQ(rect->at("fill").get<std::string>(), "#FFD700");

  // Repeat skips the enemy and packs the rest; names come from computed addresses.
  const picojson::object* first = FindElement(elements, "text", 1);
  const picojson::object* second = FindElement(elements, "text", 2);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(first->at("text").get<std::string>(), "Lance");
  EXPECT_DOUBLE_EQ(first->at("y").get<double>(), 200);
  EXPECT_EQ(second->at("text").get<std::string>(), "Sword");
  EXPECT_DOUBLE_EQ(second->at("y").get<double>(), 230);
  EXPECT_EQ(second->at("action").get<std::string>(), "pick");
  EXPECT_EQ(second->at("source").get<std::string>(), "units");
  EXPECT_DOUBLE_EQ(second->at("index").get<double>(), 2);

  // The grid: palette indices per cell, unit marks and the cursor.
  const picojson::object* grid = FindElement(elements, "grid");
  ASSERT_NE(grid, nullptr);
  const picojson::array& palette = grid->at("palette").get<picojson::array>();
  ASSERT_EQ(palette.size(), 2u);
  EXPECT_EQ(palette[0].get<std::string>(), "#00FF00");
  EXPECT_EQ(palette[1].get<std::string>(), "#FFD700");
  const picojson::array& cells = grid->at("cells").get<picojson::array>();
  ASSERT_EQ(cells.size(), 6u);
  EXPECT_DOUBLE_EQ(cells[1].get<double>(), 1);
  const picojson::array& marks = grid->at("marks").get<picojson::array>();
  ASSERT_EQ(marks.size(), 4u);  // Three living units and the cursor.
  EXPECT_EQ(marks[1].get("fill").get<std::string>(), "#FF0000");
  EXPECT_EQ(marks[3].get("shape").get<std::string>(), "cursor");

  const picojson::object* summary = FindElement(elements, "text", 3);
  EXPECT_EQ(summary->at("text").get<std::string>(), "Focus 1 of 3, 1 enemy, layout wide");

  // A layout without its own elements uses the "*" list.
  const picojson::value classic = evaluator.Render(page, "classic");
  const picojson::array& classic_elements = classic.get("elements").get<picojson::array>();
  ASSERT_EQ(classic_elements.size(), 1u);
  EXPECT_EQ(classic_elements[0].get("text").get<std::string>(), "small");
}

TEST(DuoCanvas, SetStepChangesUiState)
{
  const Duo::Manifest manifest = ParseOrFail(CANVAS_MANIFEST);
  FakeMemory memory;
  FillCanvas(memory);
  Duo::Evaluator evaluator(manifest);
  evaluator.Sample(memory, {});
  EXPECT_EQ(evaluator.GetState("sel"), Duo::Value(-1));

  Duo::ActionRunner runner;
  ASSERT_TRUE(runner.Enqueue({"pick", "units", 2}));
  std::string page = "battle";
  std::vector<std::string> messages;
  runner.Step(manifest, evaluator, memory, nullptr, false, page, messages);
  EXPECT_TRUE(messages.empty());
  EXPECT_EQ(evaluator.GetState("sel"), Duo::Value(2));
  EXPECT_TRUE(runner.TakeDirty());

  evaluator.Sample(memory, {});
  EXPECT_EQ(evaluator.GetValue("focus"), Duo::Value(2));
}

TEST(DuoCanvas, ReportsCanvasErrors)
{
  const Duo::ManifestParseResult result = Duo::ParseManifest(R"json({
    "format": 1, "id": "bad", "name": "Bad", "version": "1", "targets": [{"game_id": "GCNE01"}],
    "layouts": [{"id": "wide", "canvas": [8, 1080]}],
    "state": {"sel": 0},
    "pages": [{"id": "p", "title": "P", "elements": {"nope": [
      {"type": "frame", "x": 0, "y": 0, "w": 1},
      {"type": "sparkle"},
      {"type": "text", "x": 0, "y": 0, "w": 1, "h": 1, "text": "x", "colour": "#fff"},
      {"type": "rect", "style": "missing", "x": 0, "y": 0, "w": 1, "h": 1}
    ]}}],
    "actions": [{"id": "a", "steps": [{"set": "unknown", "value": 1}]}]
  })json");
  EXPECT_FALSE(result.manifest.has_value());
  const auto has = [&](std::string_view needle) {
    return std::ranges::any_of(result.errors,
                               [&](const std::string& e) { return e.find(needle) != e.npos; });
  };
  EXPECT_TRUE(has("canvas"));
  EXPECT_TRUE(has("needs \"h\""));
  EXPECT_TRUE(has("unknown element type"));
  EXPECT_TRUE(has("unknown key \"colour\""));
  EXPECT_TRUE(has("unknown style \"missing\""));
  EXPECT_TRUE(has("names a layout that is not in \"layouts\""));
  EXPECT_TRUE(has("sets unknown state \"unknown\""));
}
