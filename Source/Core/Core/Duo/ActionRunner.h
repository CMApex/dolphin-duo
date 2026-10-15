// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Runs companion actions one step at a time on the emulation thread, so button presses are held
// for an exact number of emulated frames regardless of how fast the UI thread is.

#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Duo/Evaluator.h"
#include "Core/Duo/Manifest.h"
#include "Core/Duo/MemorySource.h"

namespace Duo
{
// Where button presses go. On Android this is Dolphin's touch InputOverrider.
class InputSink
{
public:
  virtual ~InputSink() = default;
  virtual void Press(int controller, Control control, double amount) = 0;
  virtual void Release(int controller, Control control) = 0;
};

struct ActionRequest
{
  std::string action;
  std::string row_array;  // The list/map the action came from, if any.
  s64 row = -1;
};

class ActionRunner
{
public:
  static constexpr size_t MAX_QUEUED = 8;

  // Returns false (and drops the request) when the queue is full.
  bool Enqueue(ActionRequest request);

  // Advances by one emulated frame. `page` may be changed by page steps. Problems are appended to
  // `messages` for the companion screen to show.
  void Step(const Manifest& manifest, Evaluator& evaluator, MemorySource& memory,
            InputSink* input, bool writes_allowed, std::string& page,
            std::vector<std::string>& messages);

  // Releases anything being held and forgets queued work (companion unloaded, game stopped).
  void Cancel(InputSink* input, int controller);

  bool IsBusy() const { return m_running.has_value() || !m_queue.empty(); }

  // True after a step changed something the UI shows (a page switch or a write), so the runtime
  // can sample and render again right away. Reading it clears it.
  bool TakeDirty();

private:
  struct Running
  {
    const ActionSpec* action = nullptr;
    ActionRequest request;
    size_t step = 0;
    u32 frames_left = 0;
    std::optional<Control> held;
  };

  std::deque<ActionRequest> m_queue;
  std::optional<Running> m_running;
  bool m_dirty = false;
};
}  // namespace Duo
