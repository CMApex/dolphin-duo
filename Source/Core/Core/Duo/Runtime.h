// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Connects Dolphin Duo companions to the running emulator.
//
// Threading: OnFrameEnd() and Shutdown() run on the CPU thread. Everything else may be called
// from any thread (in practice the Android UI thread); those calls only exchange small requests
// and the latest snapshot under a mutex, so the frontend never touches emulated memory itself.

#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Duo/ActionRunner.h"
#include "Core/Duo/DiscAssets.h"
#include "Core/Duo/Evaluator.h"
#include "Core/Duo/Manifest.h"

namespace Core
{
class CPUThreadGuard;
class System;
}  // namespace Core

namespace Duo
{
struct InstalledCompanion
{
  std::string id;
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string directory;
  std::vector<TargetSpec> targets;
  std::vector<std::string> errors;  // Non-empty when the manifest does not parse.
};

class Runtime
{
public:
  static Runtime& GetInstance();

  // --- CPU thread -------------------------------------------------------------------------
  // Called once per emulated video field from Core::OnFrameEnd.
  void OnFrameEnd(const Core::CPUThreadGuard& guard);
  // Called when emulation stops. Releases held buttons and unloads the companion.
  void Shutdown();

  // --- Any thread -------------------------------------------------------------------------
  // Returns the latest snapshot as JSON if its sequence number is newer than `after`.
  std::optional<std::pair<u64, std::string>> GetSnapshot(u64 after) const;

  void SetPage(std::string page);
  // Size of the companion screen in pixels, used to pick the closest canvas layout.
  void SetViewport(u32 width, u32 height);
  void TriggerAction(ActionRequest request);
  void SelectCompanion(std::string companion_id);
  void RequestReload();

  void SetInputSink(std::unique_ptr<InputSink> sink);

  // The runtime does no work at all until a frontend with a companion screen attaches, so
  // single-screen devices and desktop builds pay nothing for it.
  void SetFrontendAttached(bool attached);
  bool IsFrontendAttached() const { return m_attached.load(std::memory_order_relaxed); }

  // Folder layout under the Dolphin user directory.
  static std::string GetRootDirectory();        // <User>/Duo/
  static std::string GetCompanionsDirectory();  // <User>/Duo/Companions/
  static std::string GetWatchesDirectory();     // <User>/Duo/Watches/

  static std::vector<InstalledCompanion> ScanCompanions();

  // Returns a PNG file for a "disc:" image of the running game, decoding it into
  // <User>/Duo/Cache/<game id>_r<revision>/ on first use. Any thread; may read the disc, so call it
  // off the UI thread.
  std::optional<std::string> ResolveDiscImage(std::string_view source, std::string* error) const;

private:
  Runtime();

  struct Loaded
  {
    std::unique_ptr<Manifest> manifest;
    std::unique_ptr<Evaluator> evaluator;
    std::string directory;
    bool is_inspector = false;
  };

  struct Pending
  {
    std::optional<std::string> page;
    std::vector<ActionRequest> actions;
    std::optional<std::string> select;
    std::optional<std::pair<u32, u32>> viewport;
    bool reload = false;
  };

  struct TableLoad
  {
    TextTables tables;
    std::vector<std::string> errors;
  };

  void LoadForGame(const std::string& game_id, u16 revision);
  // Creates the evaluator once the companion's text tables are in (see m_table_load).
  void FinishLoading();
  void Unload();
  void Publish();
  std::string SelectedCompanionFor(const std::string& game_id) const;

  // CPU-thread state.
  std::string m_game_id;
  u16 m_revision = 0;
  bool m_game_known = false;
  bool m_active = false;
  std::atomic<bool> m_attached{false};
  std::optional<Loaded> m_loaded;
  std::vector<InstalledCompanion> m_available;
  std::string m_status = "idle";
  std::string m_message;
  std::vector<std::string> m_errors;
  std::vector<std::pair<u64, std::string>> m_notices;  // (expiry frame, text)
  ActionRunner m_runner;
  std::string m_page;
  // Text tables being read from the disc on a worker thread, so a companion loading mid-game
  // does not stall emulation while the disc is opened and the message file decompressed. The
  // companion samples nothing until they arrive.
  std::future<TableLoad> m_table_load;
  // The running game's disc, copied when it loads (under m_mutex) so other threads never read
  // SConfig while a new game boots.
  std::string m_disc_path;
  std::string m_disc_game_id;
  u16 m_disc_revision = 0;
  u32 m_viewport_width = 0;
  u32 m_viewport_height = 0;
  u64 m_frame = 0;
  u64 m_next_sample_frame = 0;
  std::unique_ptr<InputSink> m_input;

  // Shared with other threads.
  mutable std::mutex m_mutex;
  Pending m_pending;
  std::unique_ptr<InputSink> m_pending_input;
  bool m_has_pending_input = false;
  u64 m_sequence = 0;
  std::string m_snapshot;
};
}  // namespace Duo
