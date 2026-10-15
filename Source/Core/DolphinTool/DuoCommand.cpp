// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinTool/DuoCommand.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <OptionParser.h>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <picojson.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Core/Duo/Evaluator.h"
#include "Core/Duo/Manifest.h"

namespace DolphinTool
{
namespace
{
// Emulated memory backed by raw dumps from Dolphin's memory view ("Dump MEM1"/"Dump MEM2"),
// mapped at the effective addresses games use.
class DumpMemory final : public Duo::MemorySource
{
public:
  void AddRegion(u32 base, std::vector<u8> bytes) { m_regions.push_back({base, std::move(bytes)}); }

  std::optional<u64> Read(u32 address, u32 size) override
  {
    const u8* bytes = Find(address, size);
    if (!bytes)
      return std::nullopt;
    u64 value = 0;
    for (u32 i = 0; i < size; ++i)
      value = (value << 8) | bytes[i];
    return value;
  }

  bool Write(u32 address, u32 size, u64 value) override
  {
    u8* bytes = Find(address, size);
    if (!bytes)
      return false;
    for (u32 i = 0; i < size; ++i)
      bytes[i] = static_cast<u8>(value >> (8 * (size - 1 - i)));
    return true;
  }

  bool IsRAM(u32 address) override { return Find(address, 1) != nullptr; }

private:
  struct Region
  {
    u32 base;
    std::vector<u8> bytes;
  };

  u8* Find(u32 address, u32 size)
  {
    for (Region& region : m_regions)
    {
      // Cached (0x8...) and uncached (0xC...) mirrors share the same dump.
      for (const u32 mirror : {region.base, region.base | 0x40000000u})
      {
        if (address >= mirror && static_cast<u64>(address) + size <= mirror + region.bytes.size())
          return region.bytes.data() + (address - mirror);
      }
    }
    return nullptr;
  }

  std::vector<Region> m_regions;
};

std::string ManifestPathFor(std::string path)
{
  if (File::IsDirectory(path))
  {
    if (!path.ends_with('/') && !path.ends_with('\\'))
      path += '/';
    path += "manifest.json";
  }
  return path;
}

std::optional<Duo::Manifest> LoadManifest(const std::string& path)
{
  std::string json;
  if (!File::ReadFileToString(path, json))
  {
    fmt::print(std::cerr, "Error: could not read {}\n", path);
    return std::nullopt;
  }
  Duo::ManifestParseResult result = Duo::ParseManifest(json);
  if (!result.manifest)
  {
    fmt::print(std::cerr, "{} has {} problem(s):\n", path, result.errors.size());
    for (const std::string& error : result.errors)
      fmt::print(std::cerr, "  - {}\n", error);
    return std::nullopt;
  }
  return std::move(result.manifest);
}

std::string Bar(const picojson::value& fraction, size_t width)
{
  if (!fraction.is<double>())
    return "[" + std::string(width, '?') + "]";
  const size_t filled = static_cast<size_t>(std::lround(fraction.get<double>() * width));
  std::string out = "[";
  for (size_t i = 0; i < width; ++i)
    out += i < filled ? "#" : "-";
  return out + fmt::format("] {:3.0f}%", fraction.get<double>() * 100.0);
}

std::string Str(const picojson::value& value)
{
  return value.is<std::string>() ? value.get<std::string>() : std::string();
}

// A plain-text approximation of what the companion screen draws.
void PrintPage(const picojson::value& snapshot)
{
  std::string tabs;
  for (const picojson::value& page : snapshot.get("pages").get<picojson::array>())
  {
    const bool active = Str(page.get("id")) == Str(snapshot.get("page"));
    tabs += active ? fmt::format("[{}] ", Str(page.get("title"))) :
                     fmt::format(" {}  ", Str(page.get("title")));
  }
  fmt::print("{}\n{}\n", tabs, std::string(60, '='));

  for (const picojson::value& widget : snapshot.get("widgets").get<picojson::array>())
  {
    const std::string type = Str(widget.get("type"));
    if (type == "text")
    {
      const std::string style = Str(widget.get("style"));
      fmt::print("{}{}\n",
                 style == "heading" ? "## " :
                 style == "caption" ? "   " :
                                      "",
                 Str(widget.get("text")));
    }
    else if (type == "bar")
    {
      fmt::print("{:<30}{:>30}\n{}\n", Str(widget.get("label")), Str(widget.get("text")),
                 Bar(widget.get("fraction"), 40));
    }
    else if (type == "stats")
    {
      for (const picojson::value& item : widget.get("items").get<picojson::array>())
        fmt::print("  {:<24} {}\n", Str(item.get("label")), Str(item.get("value")));
    }
    else if (type == "list")
    {
      const picojson::array& rows = widget.get("rows").get<picojson::array>();
      if (rows.empty())
        fmt::print("  ({})\n",
                   widget.get("empty").is<std::string>() ? Str(widget.get("empty")) : "empty");
      for (const picojson::value& row : rows)
      {
        std::string line =
            fmt::format("  {:>2}. {}", row.get("index").get<double>(), Str(row.get("text")));
        if (!Str(row.get("detail")).empty())
          line += "  -  " + Str(row.get("detail"));
        if (!Str(row.get("badge")).empty())
          line += "  <" + Str(row.get("badge")) + ">";
        if (row.contains("fraction"))
          line += "  " + Bar(row.get("fraction"), 10);
        fmt::print("{}\n", line);
      }
      if (!Str(widget.get("action")).empty())
        fmt::print("     (rows run action \"{}\")\n", Str(widget.get("action")));
    }
    else if (type == "buttons")
    {
      std::string line;
      for (const picojson::value& button : widget.get("buttons").get<picojson::array>())
      {
        line += fmt::format("[{}{}] ", Str(button.get("label")),
                            button.get("enabled").get<bool>() ? "" : " (disabled)");
      }
      fmt::print("{}\n", line);
    }
    else if (type == "map")
    {
      const picojson::array& points = widget.get("points").get<picojson::array>();
      fmt::print("Map: {} point(s)\n", points.size());
      for (const picojson::value& point : points)
      {
        fmt::print("  ({:.3f}, {:.3f}) {} {}\n", point.get("x").get<double>(),
                   point.get("y").get<double>(), Str(point.get("shape")), Str(point.get("label")));
      }
    }
    else if (type == "divider")
    {
      fmt::print("{}\n", std::string(60, '-'));
    }
  }
}

int Validate(const std::vector<std::string>& args)
{
  if (args.empty())
  {
    fmt::print(std::cerr, "usage: dolphin-tool duo validate <companion folder or manifest.json>\n");
    return EXIT_FAILURE;
  }
  const std::string path = ManifestPathFor(args[0]);
  const std::optional<Duo::Manifest> manifest = LoadManifest(path);
  if (!manifest)
    return EXIT_FAILURE;

  fmt::print("{} {} ({}) is valid.\n", manifest->name, manifest->version, manifest->id);
  for (const Duo::TargetSpec& target : manifest->targets)
  {
    std::vector<std::string> revisions;
    for (u16 revision : target.revisions)
      revisions.push_back(fmt::format("{}", revision));
    fmt::print("  target {} revision {}\n", target.game_id,
               revisions.empty() ? std::string("any") :
                                   fmt::format("{}", fmt::join(revisions, "/")));
  }
  fmt::print("  {} values, {} arrays, {} pages, {} actions\n", manifest->values.size(),
             manifest->arrays.size(), manifest->pages.size(), manifest->actions.size());
  if (manifest->allow_write || manifest->allow_input)
  {
    fmt::print("  capabilities:{}{}\n", manifest->allow_write ? " write_memory" : "",
               manifest->allow_input ? " input" : "");
  }
  return EXIT_SUCCESS;
}

int Preview(const std::vector<std::string>& args)
{
  optparse::OptionParser parser;
  parser.usage("usage: duo preview <companion folder or manifest.json> [options]");
  parser.add_option("--mem1")
      .action("store")
      .help("MEM1 dump (mem1.raw from Dolphin's memory view), mapped at 0x80000000.")
      .metavar("FILE");
  parser.add_option("--mem2")
      .action("store")
      .help("Optional. MEM2 dump (Wii), mapped at 0x90000000.")
      .metavar("FILE");
  parser.add_option("-p", "--page").action("store").help("Optional. Page id to render.");
  parser.add_option("--game-id")
      .action("store")
      .help("Optional. Game ID to report as $game_id (defaults to the first target).");
  parser.add_option("--revision").action("store").help("Optional. Revision for $revision.");
  parser.add_option("-j", "--json")
      .action("store_true")
      .help("Optional. Print the raw snapshot JSON the companion screen receives.");
  parser.add_option("--layout")
      .action("store")
      .help("Optional. Canvas layout id to render (default: the first layout).");
  parser.add_option("--screen")
      .action("store")
      .help("Optional. Screen size WxH; picks the closest canvas layout like the device does.")
      .metavar("WxH");
  parser.add_option("--state")
      .action("store")
      .help("Optional. UI state overrides, e.g. \"sel=3,tab=1\" (numbers only).")
      .metavar("LIST");

  const optparse::Values& options = parser.parse_args(args);
  const std::vector<std::string>& positional = parser.args();
  if (positional.empty())
  {
    parser.print_help();
    return EXIT_FAILURE;
  }

  const std::optional<Duo::Manifest> manifest = LoadManifest(ManifestPathFor(positional[0]));
  if (!manifest)
    return EXIT_FAILURE;

  DumpMemory memory;
  const auto load_dump = [&](const char* option, u32 base) {
    const std::string& path = options[option];
    if (path.empty())
      return true;
    std::string data;
    if (!File::ReadFileToString(path, data))
    {
      fmt::print(std::cerr, "Error: could not read {}\n", path);
      return false;
    }
    memory.AddRegion(base, std::vector<u8>(data.begin(), data.end()));
    return true;
  };
  if (!load_dump("mem1", 0x80000000) || !load_dump("mem2", 0x90000000))
    return EXIT_FAILURE;
  if (options["mem1"].empty())
    fmt::print(std::cerr, "Note: no --mem1 dump given; every memory value will read as '?'.\n");

  Duo::SystemInfo system;
  system.game_id =
      options.is_set_by_user("game-id") ? options["game-id"] : manifest->targets.front().game_id;
  if (options.is_set_by_user("revision"))
  {
    u16 revision = 0;
    if (!TryParse(options["revision"], &revision))
    {
      fmt::print(std::cerr, "Error: bad --revision\n");
      return EXIT_FAILURE;
    }
    system.revision = revision;
  }
  system.title = manifest->name;
  system.is_wii = !options["mem2"].empty();

  // Decode Shift-JIS game text the same way the runtime does.
  Duo::Evaluator evaluator(*manifest, [](Duo::TextEncoding encoding, std::span<const u8> bytes) {
    const std::string_view raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return encoding == Duo::TextEncoding::ShiftJIS ? SHIFTJISToUTF8(raw) : std::string(raw);
  });
  if (options.is_set_by_user("state"))
  {
    for (const std::string& entry : SplitString(options["state"], ','))
    {
      const size_t equals = entry.find('=');
      double number = 0;
      if (equals == std::string::npos || !TryParse(entry.substr(equals + 1), &number))
      {
        fmt::print(std::cerr, "Error: bad --state entry \"{}\" (expected name=number)\n", entry);
        return EXIT_FAILURE;
      }
      evaluator.SetState(entry.substr(0, equals), Duo::Value(number));
    }
  }
  evaluator.Sample(memory, system);

  std::string layout = options["layout"];
  if (options.is_set_by_user("screen"))
  {
    u32 width = 0, height = 0;
    const std::vector<std::string> parts = SplitString(options["screen"], 'x');
    if (parts.size() != 2 || !TryParse(parts[0], &width) || !TryParse(parts[1], &height))
    {
      fmt::print(std::cerr, "Error: bad --screen (expected WxH, e.g. 1240x1080)\n");
      return EXIT_FAILURE;
    }
    if (const Duo::LayoutSpec* chosen = manifest->ChooseLayout(width, height))
      layout = chosen->id;
  }

  std::string page = options["page"];
  picojson::value snapshot = evaluator.Render(page, layout);
  if (options.is_set_by_user("json"))
  {
    // Like the device snapshot, name the companion folder so previews can find fonts and images.
    std::filesystem::path folder = std::filesystem::absolute(positional[0]);
    if (folder.filename() == "manifest.json")
      folder = folder.parent_path();
    const std::string directory = folder.lexically_normal().string() + "/";
    picojson::object companion;
    companion["id"] = picojson::value(manifest->id);
    companion["name"] = picojson::value(manifest->name);
    companion["version"] = picojson::value(manifest->version);
    companion["directory"] = picojson::value(directory);
    snapshot.get<picojson::object>()["companion"] = picojson::value(std::move(companion));
    fmt::print("{}\n", snapshot.serialize(true));
    return EXIT_SUCCESS;
  }
  PrintPage(snapshot);
  if (snapshot.contains("canvas"))
  {
    const picojson::value& canvas = snapshot.get("canvas");
    fmt::print("\nCanvas page, layout \"{}\" ({}x{}): {} elements. Render it with "
               "Tools/DolphinDuo/preview (pass --json).\n",
               Str(canvas.get("layout")), canvas.get("w").get<double>(),
               canvas.get("h").get<double>(),
               snapshot.get("elements").get<picojson::array>().size());
    for (const picojson::value& element : snapshot.get("elements").get<picojson::array>())
    {
      if (element.contains("text") && element.get("text").is<std::string>() &&
          !element.get("text").get<std::string>().empty())
      {
        fmt::print("  text @({:.0f},{:.0f}): {}\n", element.get("x").get<double>(),
                   element.get("y").get<double>(), element.get("text").get<std::string>());
      }
    }
  }
  return EXIT_SUCCESS;
}
}  // namespace

int DuoCommand(const std::vector<std::string>& args)
{
  const auto usage = [] {
    fmt::print(std::cerr,
               "usage: dolphin-tool duo validate <companion folder or manifest.json>\n"
               "       dolphin-tool duo preview <companion> --mem1 mem1.raw [--mem2 mem2.raw]\n"
               "                                [--page ID] [--layout ID | --screen WxH]\n"
               "                                [--state name=value,...] [--json]\n");
    return EXIT_FAILURE;
  };
  if (args.empty())
    return usage();

  const std::vector<std::string> rest(args.begin() + 1, args.end());
  if (args[0] == "validate")
    return Validate(rest);
  if (args[0] == "preview")
    return Preview(rest);
  return usage();
}
}  // namespace DolphinTool
