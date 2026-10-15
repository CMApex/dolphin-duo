// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Runtime.h"
#include "Core/Duo/DiscAssets.h"

#include <tuple>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <picojson.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Core/AchievementManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/VideoInterface.h"
#include "Core/PowerPC/MMU.h"
#include "Core/System.h"

namespace Duo
{
namespace
{
constexpr u64 NOTICE_FIELDS = 240;  // About four seconds.

class DolphinMemory final : public MemorySource
{
public:
  explicit DolphinMemory(const Core::CPUThreadGuard& guard) : m_guard(guard) {}

  std::optional<u64> Read(u32 address, u32 size) override
  {
    switch (size)
    {
    case 1:
      if (const auto result = PowerPC::MMU::HostTryRead<u8>(m_guard, address))
        return result->value;
      break;
    case 2:
      if (const auto result = PowerPC::MMU::HostTryRead<u16>(m_guard, address))
        return result->value;
      break;
    case 4:
      if (const auto result = PowerPC::MMU::HostTryRead<u32>(m_guard, address))
        return result->value;
      break;
    case 8:
      if (const auto result = PowerPC::MMU::HostTryRead<u64>(m_guard, address))
        return result->value;
      break;
    default:
      break;
    }
    return std::nullopt;
  }

  bool Write(u32 address, u32 size, u64 value) override
  {
    switch (size)
    {
    case 1:
      return PowerPC::MMU::HostTryWrite<u8>(m_guard, static_cast<u8>(value), address).has_value();
    case 2:
      return PowerPC::MMU::HostTryWrite<u16>(m_guard, static_cast<u16>(value), address).has_value();
    case 4:
      return PowerPC::MMU::HostTryWrite<u32>(m_guard, static_cast<u32>(value), address).has_value();
    case 8:
      // Big-endian: the high word lives at the lower address.
      return Write(address, 4, value >> 32) && Write(address + 4, 4, value & 0xFFFFFFFF);
    default:
      return false;
    }
  }

  bool IsRAM(u32 address) override { return PowerPC::MMU::HostIsRAMAddress(m_guard, address); }

private:
  const Core::CPUThreadGuard& m_guard;
};

std::string DecodeGameText(TextEncoding encoding, std::span<const u8> bytes)
{
  const std::string_view raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (encoding == TextEncoding::ShiftJIS)
    return SHIFTJISToUTF8(raw);
  return std::string(raw);
}

bool IsSafeName(std::string_view name)
{
  return !name.empty() && name.size() <= 64 &&
         std::ranges::all_of(name,
                             [](char c) {
                               return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
                                      c == '_' || c == '.';
                             }) &&
         name != "." && name != "..";
}

// Game IDs a manifest claims to target, read without full validation so a companion with
// mistakes can still report its errors on the game it was written for.
std::vector<std::string> TargetGameIds(const std::string& json)
{
  std::vector<std::string> ids;
  picojson::value root;
  if (!picojson::parse(root, json).empty() || !root.is<picojson::object>())
    return ids;
  const picojson::value& targets = root.get("targets");
  if (!targets.is<picojson::array>())
    return ids;
  for (const picojson::value& target : targets.get<picojson::array>())
  {
    if (target.is<picojson::object>() && target.get("game_id").is<std::string>())
      ids.push_back(target.get("game_id").get<std::string>());
  }
  return ids;
}

struct Watch
{
  std::string label;
  std::string type;
  u32 length = 0;
  std::vector<u32> chain;  // address, then offsets
};

// <User>/Duo/Watches/<GAMEID>.txt, one watch per line:
//   <type>[:length] <address> [offset ...] <label>
// for example
//   u32 0x80123450 Money
//   u8 0x80123460 0x10 0x4 Leader HP
//   string:16 0x80400000 Player name
std::vector<Watch> LoadWatches(const std::string& game_id)
{
  std::vector<Watch> watches;
  std::string text;
  if (!File::ReadFileToString(Runtime::GetWatchesDirectory() + game_id + ".txt", text))
    return watches;

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line) && watches.size() < 64)
  {
    line = std::string(StripWhitespace(line));
    if (line.empty() || line.starts_with('#'))
      continue;

    std::istringstream words(line);
    Watch watch;
    std::string type;
    words >> type;
    const size_t colon = type.find(':');
    if (colon != std::string::npos)
    {
      TryParse(type.substr(colon + 1), &watch.length);
      type = type.substr(0, colon);
    }
    watch.type = type;

    std::string word;
    while (words >> word)
    {
      u32 number = 0;
      const bool is_number =
          (word.starts_with("0x") || word.starts_with("0X")) ?
              TryParse(word, &number) :
              (std::ranges::all_of(
                   word, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }) &&
               TryParse(word, &number));
      if (!is_number || watch.chain.size() > MAX_POINTER_DEPTH)
      {
        std::string rest;
        std::getline(words, rest);
        watch.label = word + rest;
        break;
      }
      watch.chain.push_back(number);
    }
    if (watch.chain.empty())
      continue;
    if (watch.label.empty())
      watch.label = fmt::format("0x{:08X}", watch.chain.front());
    watches.push_back(std::move(watch));
  }
  return watches;
}

// The built-in page shown when no companion is installed for the running game: game details,
// plus any memory watches the player listed. It is a regular manifest, so it exercises the same
// rendering path as real companions.
std::string BuildInspectorManifest(const std::string& game_id, u16 revision,
                                   const std::string& status_text)
{
  const std::vector<Watch> watches = LoadWatches(game_id);

  picojson::array values;
  picojson::array watch_items;
  for (size_t i = 0; i < watches.size(); ++i)
  {
    const Watch& watch = watches[i];
    picojson::object value;
    const std::string name = fmt::format("w{}", i);
    value["name"] = picojson::value(name);
    value["type"] = picojson::value(watch.type);
    value["address"] = picojson::value(fmt::format("0x{:08X}", watch.chain.front()));
    if (watch.type == "string")
      value["length"] = picojson::value(static_cast<double>(watch.length ? watch.length : 32));
    picojson::array offsets;
    for (size_t k = 1; k < watch.chain.size(); ++k)
      offsets.emplace_back(fmt::format("0x{:X}", watch.chain[k]));
    if (!offsets.empty())
      value["offsets"] = picojson::value(std::move(offsets));
    values.emplace_back(std::move(value));

    picojson::object item;
    item["label"] = picojson::value(watch.label);
    const bool is_integer = watch.type != "string" && watch.type != "f32" && watch.type != "f64";
    item["value"] = picojson::value(is_integer ? fmt::format("{{{0}}}  ·  0x{{{0}:X}}", name) :
                                                 fmt::format("{{{}}}", name));
    watch_items.emplace_back(std::move(item));
  }

  picojson::array widgets;
  const auto add_widget =
      [&](std::initializer_list<std::pair<const std::string, picojson::value>> fields) {
        picojson::object widget;
        for (const auto& [key, value] : fields)
          widget[key] = value;
        widgets.emplace_back(std::move(widget));
      };

  add_widget({{"type", picojson::value("heading")}, {"text", picojson::value("{$title}")}});
  {
    picojson::array info;
    const auto stat = [&](const std::string& label, const std::string& value) {
      picojson::object item;
      item["label"] = picojson::value(label);
      item["value"] = picojson::value(value);
      info.emplace_back(std::move(item));
    };
    stat("Game ID", "{$game_id}");
    stat("Revision", "{$revision}");
    stat("Platform", "{$platform == 'wii' ? 'Wii' : 'GameCube'}");
    stat("Frame", "{$frame}");
    picojson::object widget;
    widget["type"] = picojson::value("stats");
    widget["columns"] = picojson::value(2.0);
    widget["items"] = picojson::value(std::move(info));
    widgets.emplace_back(std::move(widget));
  }
  add_widget({{"type", picojson::value("text")},
              {"style", picojson::value("caption")},
              {"text", picojson::value(status_text)}});

  if (!watch_items.empty())
  {
    add_widget({{"type", picojson::value("divider")}});
    add_widget({{"type", picojson::value("heading")}, {"text", picojson::value("Watches")}});
    picojson::object widget;
    widget["type"] = picojson::value("stats");
    widget["columns"] = picojson::value(1.0);
    widget["items"] = picojson::value(std::move(watch_items));
    widgets.emplace_back(std::move(widget));
  }
  else
  {
    add_widget({{"type", picojson::value("text")},
                {"style", picojson::value("caption")},
                {"text", picojson::value(fmt::format(
                             "Tip: list addresses in Duo/Watches/{}.txt to watch them live here.",
                             game_id))}});
  }

  picojson::object page;
  page["id"] = picojson::value("inspector");
  page["title"] = picojson::value("Inspector");
  page["widgets"] = picojson::value(std::move(widgets));

  picojson::object target;
  target["game_id"] = picojson::value(game_id);
  picojson::array revisions;
  revisions.emplace_back(static_cast<double>(revision));
  target["revisions"] = picojson::value(std::move(revisions));

  picojson::object root;
  root["format"] = picojson::value(static_cast<double>(MANIFEST_FORMAT_VERSION));
  root["id"] = picojson::value("dolphin-duo-inspector");
  root["name"] = picojson::value("Inspector");
  root["version"] = picojson::value("1");
  root["targets"] = picojson::value(picojson::array{picojson::value(std::move(target))});
  root["poll_hz"] = picojson::value(10.0);
  root["values"] = picojson::value(std::move(values));
  root["pages"] = picojson::value(picojson::array{picojson::value(std::move(page))});
  return picojson::value(std::move(root)).serialize();
}
}  // namespace

Runtime& Runtime::GetInstance()
{
  static Runtime runtime;
  return runtime;
}

Runtime::Runtime() = default;

std::string Runtime::GetRootDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "Duo" DIR_SEP;
}

std::string Runtime::GetCompanionsDirectory()
{
  return GetRootDirectory() + "Companions" DIR_SEP;
}

std::string Runtime::GetWatchesDirectory()
{
  return GetRootDirectory() + "Watches" DIR_SEP;
}

std::vector<InstalledCompanion> Runtime::ScanCompanions()
{
  std::vector<InstalledCompanion> companions;
  const std::string root = GetCompanionsDirectory();
  if (!File::IsDirectory(root))
    return companions;

  const File::FSTEntry tree = File::ScanDirectoryTree(root, false);
  for (const File::FSTEntry& entry : tree.children)
  {
    if (!entry.isDirectory)
      continue;
    const std::string manifest_path = entry.physicalName + DIR_SEP "manifest.json";
    std::string json;
    if (!File::ReadFileToString(manifest_path, json))
      continue;

    InstalledCompanion companion;
    companion.directory = entry.physicalName + DIR_SEP;
    ManifestParseResult result = ParseManifest(json);
    if (result.manifest)
    {
      const Manifest& manifest = *result.manifest;
      companion.id = manifest.id;
      companion.name = manifest.name;
      companion.version = manifest.version;
      companion.author = manifest.author;
      companion.description = manifest.description;
      companion.targets = manifest.targets;
    }
    else
    {
      companion.id = entry.virtualName;
      companion.name = entry.virtualName;
      companion.errors = std::move(result.errors);
      for (std::string& game_id : TargetGameIds(json))
        companion.targets.push_back(TargetSpec{std::move(game_id), {}});
    }
    companions.push_back(std::move(companion));
  }
  std::ranges::sort(companions, {}, &InstalledCompanion::name);
  return companions;
}

std::string Runtime::SelectedCompanionFor(const std::string& game_id) const
{
  std::string selected;
  File::ReadFileToString(GetRootDirectory() + "Selected" DIR_SEP + game_id + ".txt", selected);
  return std::string(StripWhitespace(selected));
}

void Runtime::Unload()
{
  m_runner.Cancel(m_input.get(), m_loaded ? m_loaded->manifest->controller : 0);
  m_loaded.reset();
  m_errors.clear();
  m_page.clear();
}

void Runtime::LoadForGame(const std::string& game_id, u16 revision)
{
  Unload();
  m_available.clear();
  m_status = "inspector";
  m_message.clear();

  const std::string selected = SelectedCompanionFor(game_id);
  std::vector<std::string> wanted_revisions;
  const InstalledCompanion* broken = nullptr;
  const InstalledCompanion* chosen = nullptr;
  std::vector<InstalledCompanion> companions = ScanCompanions();

  for (const InstalledCompanion& companion : companions)
  {
    const bool for_this_game = std::ranges::any_of(
        companion.targets, [&](const TargetSpec& t) { return t.game_id == game_id; });
    if (!for_this_game)
      continue;
    if (!companion.errors.empty())
    {
      if (!broken)
        broken = &companion;
      continue;
    }
    const bool matches = std::ranges::any_of(companion.targets, [&](const TargetSpec& t) {
      return t.game_id == game_id && (t.revisions.empty() || t.revisions.contains(revision));
    });
    if (!matches)
    {
      for (const TargetSpec& t : companion.targets)
      {
        if (t.game_id != game_id)
          continue;
        for (u16 r : t.revisions)
          wanted_revisions.push_back(fmt::format("{}", r));
      }
      continue;
    }
    m_available.push_back(companion);
  }

  for (const InstalledCompanion& companion : m_available)
  {
    if (companion.id == selected)
      chosen = &companion;
  }
  if (!chosen && !m_available.empty())
    chosen = &m_available.front();

  std::string json;
  std::string inspector_text;
  if (chosen && File::ReadFileToString(chosen->directory + "manifest.json", json))
  {
    ManifestParseResult result = ParseManifest(json);
    if (result.manifest)
    {
      m_loaded = Loaded{std::make_unique<Manifest>(std::move(*result.manifest)), nullptr,
                        chosen->directory, false};
      m_status = "active";
      INFO_LOG_FMT(CORE, "Dolphin Duo: loaded companion '{}' {} for {} rev {}",
                   m_loaded->manifest->name, m_loaded->manifest->version, game_id, revision);
    }
    else
    {
      m_errors = std::move(result.errors);
    }
  }

  if (!m_loaded)
  {
    if (broken)
    {
      m_status = "error";
      m_errors = broken->errors;
      m_message = fmt::format("\"{}\" could not be loaded", broken->name);
      inspector_text = m_message + ". See the errors below.";
    }
    else if (!wanted_revisions.empty())
    {
      std::ranges::sort(wanted_revisions);
      const auto [first, last] = std::ranges::unique(wanted_revisions);
      wanted_revisions.erase(first, last);
      m_status = "mismatch";
      m_message = fmt::format("A companion for {} is installed, but it supports revision {} and "
                              "this disc is revision {}.",
                              game_id, fmt::join(wanted_revisions, "/"), revision);
      inspector_text = m_message;
    }
    else
    {
      inspector_text = "No companion installed for this game.";
    }

    ManifestParseResult inspector =
        ParseManifest(BuildInspectorManifest(game_id, revision, inspector_text));
    if (!inspector.manifest)
    {
      // Only reachable if the watches file produced something invalid; show those errors.
      for (std::string& error : inspector.errors)
        m_errors.push_back("Watches: " + error);
      inspector = ParseManifest(BuildInspectorManifest("", 0, inspector_text));
    }
    if (inspector.manifest)
    {
      m_loaded =
          Loaded{std::make_unique<Manifest>(std::move(*inspector.manifest)), nullptr, {}, true};
    }
  }

  if (m_loaded)
  {
    m_loaded->evaluator = std::make_unique<Evaluator>(*m_loaded->manifest, DecodeGameText);
    m_page = m_loaded->manifest->pages.empty() ? std::string() : m_loaded->manifest->pages[0].id;
  }
  m_next_sample_frame = m_frame;
}

void Runtime::OnFrameEnd(const Core::CPUThreadGuard& guard)
{
  ++m_frame;
  Core::System& system = guard.GetSystem();

  if (!m_attached.load(std::memory_order_relaxed))
  {
    if (m_active)
    {
      // The companion screen went away: let go of everything until it comes back.
      Shutdown();
      m_active = false;
    }
    return;
  }
  m_active = true;

  Pending pending;
  {
    std::lock_guard lock(m_mutex);
    pending = std::exchange(m_pending, Pending{});
    if (m_has_pending_input)
    {
      if (m_input)
        m_runner.Cancel(m_input.get(), m_loaded ? m_loaded->manifest->controller : 0);
      m_input = std::move(m_pending_input);
      m_has_pending_input = false;
    }
  }

  const SConfig& config = SConfig::GetInstance();
  const std::string game_id = config.GetGameID();
  const u16 revision = config.GetRevision();
  if (pending.select)
  {
    const std::string directory = GetRootDirectory() + "Selected" DIR_SEP;
    File::CreateFullPath(directory);
    File::WriteStringToFile(directory + game_id + ".txt", *pending.select);
    pending.reload = true;
  }
  if (!m_game_known || game_id != m_game_id || revision != m_revision || pending.reload)
  {
    m_game_id = game_id;
    m_revision = revision;
    m_game_known = true;
    LoadForGame(game_id, revision);
  }
  if (!m_loaded)
    return;

  DolphinMemory memory(guard);
  bool render_now = false;
  if (pending.page)
  {
    m_page = *pending.page;
    render_now = true;
  }
  if (pending.viewport)
  {
    std::tie(m_viewport_width, m_viewport_height) = *pending.viewport;
    render_now = true;
  }

  std::vector<std::string> messages;
  for (ActionRequest& request : pending.actions)
  {
    if (!m_runner.Enqueue(std::move(request)))
      messages.push_back("Too many actions queued; ignoring the extra tap.");
  }
  if (m_runner.IsBusy())
  {
    m_runner.Step(*m_loaded->manifest, *m_loaded->evaluator, memory, m_input.get(),
                  Config::Get(Config::MAIN_DUO_ALLOW_WRITES) &&
                      !AchievementManager::GetInstance().IsHardcoreModeActive(),
                  m_page, messages);
    render_now |= m_runner.TakeDirty();
  }
  for (std::string& message : messages)
  {
    WARN_LOG_FMT(CORE, "Dolphin Duo: {}", message);
    m_notices.emplace_back(m_frame + NOTICE_FIELDS, std::move(message));
    render_now = true;
  }
  std::erase_if(m_notices, [this](const auto& notice) { return notice.first <= m_frame; });

  if (m_frame < m_next_sample_frame && !render_now)
    return;

  const double refresh_rate = system.GetVideoInterface().GetTargetRefreshRate();
  const double fields_per_second = refresh_rate > 1.0 ? refresh_rate : 60.0;
  const u32 interval = std::max<u32>(
      1, static_cast<u32>(std::lround(fields_per_second / m_loaded->manifest->poll_hz)));
  m_next_sample_frame = m_frame + interval;

  SystemInfo info;
  info.game_id = game_id;
  info.revision = revision;
  info.title = config.GetTitleName();
  info.is_wii = system.IsWii();
  info.frame = m_frame;
  info.time_seconds = static_cast<double>(m_frame) / fields_per_second;
  info.has_disc = !SConfig::GetInstance().m_disc_path.empty();
  m_loaded->evaluator->Sample(memory, info);
  Publish();
}

void Runtime::Publish()
{
  picojson::object root;
  root["status"] = picojson::value(m_status);
  root["message"] = picojson::value(m_message);

  picojson::object game;
  game["id"] = picojson::value(m_game_id);
  game["revision"] = picojson::value(static_cast<double>(m_revision));
  game["title"] = picojson::value(SConfig::GetInstance().GetTitleName());
  root["game"] = picojson::value(std::move(game));

  picojson::array errors;
  for (const std::string& error : m_errors)
    errors.emplace_back(error);
  root["errors"] = picojson::value(std::move(errors));

  picojson::array notices;
  for (const auto& [expiry, text] : m_notices)
    notices.emplace_back(text);
  root["notices"] = picojson::value(std::move(notices));

  picojson::array available;
  for (const InstalledCompanion& companion : m_available)
  {
    picojson::object entry;
    entry["id"] = picojson::value(companion.id);
    entry["name"] = picojson::value(companion.name);
    entry["version"] = picojson::value(companion.version);
    available.emplace_back(std::move(entry));
  }
  root["available"] = picojson::value(std::move(available));

  if (m_loaded && !m_loaded->is_inspector)
  {
    const Manifest& manifest = *m_loaded->manifest;
    picojson::object companion;
    companion["id"] = picojson::value(manifest.id);
    companion["name"] = picojson::value(manifest.name);
    companion["version"] = picojson::value(manifest.version);
    companion["author"] = picojson::value(manifest.author);
    companion["directory"] = picojson::value(m_loaded->directory);
    root["companion"] = picojson::value(std::move(companion));
  }
  else
  {
    root["companion"] = picojson::value();
  }

  if (m_loaded && m_loaded->evaluator && m_loaded->evaluator->HasSample())
  {
    const LayoutSpec* layout =
        m_loaded->manifest->ChooseLayout(m_viewport_width, m_viewport_height);
    picojson::value rendered =
        m_loaded->evaluator->Render(m_page, layout ? std::string_view(layout->id) : "");
    for (auto& [key, value] : rendered.get<picojson::object>())
      root[key] = std::move(value);
  }

  std::string json = picojson::value(std::move(root)).serialize();
  std::lock_guard lock(m_mutex);
  if (json != m_snapshot)
  {
    m_snapshot = std::move(json);
    ++m_sequence;
  }
}

void Runtime::Shutdown()
{
  Unload();
  m_available.clear();
  m_notices.clear();
  m_game_known = false;
  m_game_id.clear();
  m_status = "idle";
  m_message.clear();

  std::lock_guard lock(m_mutex);
  m_pending = Pending{};
  m_snapshot.clear();
  ++m_sequence;
}

std::optional<std::string> Runtime::ResolveDiscImage(std::string_view source, std::string* error)
{
  const SConfig& config = SConfig::GetInstance();
  const std::string game_id = config.GetGameID();
  if (game_id.empty() || !IsSafeName(game_id))
  {
    if (error)
      *error = "no game is running";
    return std::nullopt;
  }
  return MaterializeDiscImage(config.m_disc_path,
                              GetRootDirectory() + "Cache" DIR_SEP + game_id + DIR_SEP, source,
                              error);
}

std::optional<std::pair<u64, std::string>> Runtime::GetSnapshot(u64 after) const
{
  std::lock_guard lock(m_mutex);
  if (m_sequence <= after)
    return std::nullopt;
  return std::make_pair(m_sequence, m_snapshot);
}

void Runtime::SetPage(std::string page)
{
  std::lock_guard lock(m_mutex);
  m_pending.page = std::move(page);
}

void Runtime::SetViewport(u32 width, u32 height)
{
  std::lock_guard lock(m_mutex);
  m_pending.viewport = std::make_pair(width, height);
}

void Runtime::TriggerAction(ActionRequest request)
{
  std::lock_guard lock(m_mutex);
  if (m_pending.actions.size() < ActionRunner::MAX_QUEUED)
    m_pending.actions.push_back(std::move(request));
}

void Runtime::SelectCompanion(std::string companion_id)
{
  if (!IsSafeName(companion_id))
    return;
  std::lock_guard lock(m_mutex);
  m_pending.select = std::move(companion_id);
}

void Runtime::RequestReload()
{
  std::lock_guard lock(m_mutex);
  m_pending.reload = true;
}

void Runtime::SetFrontendAttached(bool attached)
{
  m_attached.store(attached, std::memory_order_relaxed);
}

void Runtime::SetInputSink(std::unique_ptr<InputSink> sink)
{
  std::lock_guard lock(m_mutex);
  m_pending_input = std::move(sink);
  m_has_pending_input = true;
}
}  // namespace Duo
