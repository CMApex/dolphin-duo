// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/ActionRunner.h"

#include <utility>

#include <fmt/format.h>

namespace Duo
{
bool ActionRunner::Enqueue(ActionRequest request)
{
  if (m_queue.size() >= MAX_QUEUED)
    return false;
  m_queue.push_back(std::move(request));
  return true;
}

bool ActionRunner::TakeDirty()
{
  return std::exchange(m_dirty, false);
}

void ActionRunner::Cancel(InputSink* input, int controller)
{
  if (m_running && m_running->held && input)
    input->Release(controller, *m_running->held);
  m_running.reset();
  m_queue.clear();
}

void ActionRunner::Step(const Manifest& manifest, Evaluator& evaluator, MemorySource& memory,
                        InputSink* input, bool writes_allowed, std::string& page,
                        std::vector<std::string>& messages)
{
  // A frame-consuming step (press/wait) that is still in progress.
  if (m_running && m_running->frames_left > 0)
  {
    if (--m_running->frames_left > 0)
      return;
    if (m_running->held)
    {
      if (input)
        input->Release(manifest.controller, *m_running->held);
      m_running->held.reset();
    }
    ++m_running->step;
  }

  // Bound the instantaneous steps run in one frame so a malformed action cannot stall the
  // emulation thread.
  for (int budget = 0; budget < 256; ++budget)
  {
    if (!m_running)
    {
      if (m_queue.empty())
        return;
      ActionRequest request = std::move(m_queue.front());
      m_queue.pop_front();

      const ActionSpec* action = manifest.FindAction(request.action);
      if (!action)
      {
        messages.push_back(fmt::format("Unknown action \"{}\"", request.action));
        continue;
      }
      if (action->needs_write && !writes_allowed)
      {
        messages.push_back(fmt::format(
            "\"{}\" changes game memory, which is turned off in Dolphin Duo settings", action->id));
        continue;
      }
      if (action->needs_input && !input)
      {
        messages.push_back(
            fmt::format("\"{}\" presses buttons, which this build cannot do", action->id));
        continue;
      }
      m_running = Running{action, std::move(request), 0, 0, std::nullopt};
    }

    Running& running = *m_running;
    if (running.step >= running.action->steps.size())
    {
      m_running.reset();
      continue;
    }

    const ActionStep& step = running.action->steps[running.step];
    switch (step.kind)
    {
    case ActionStep::Kind::Press:
      input->Press(manifest.controller, step.control, step.amount);
      running.held = step.control;
      running.frames_left = step.frames;
      return;

    case ActionStep::Kind::Wait:
      running.frames_left = step.frames;
      return;

    case ActionStep::Kind::Write:
      if (std::optional<std::string> error =
              evaluator.Write(memory, step, running.request.row_array, running.request.row))
      {
        messages.push_back(fmt::format("{}: {}", running.action->id, *error));
        // Abandon the rest of the action; later steps usually depend on the write.
        m_running.reset();
        continue;
      }
      m_dirty = true;
      ++running.step;
      break;

    case ActionStep::Kind::Page:
      page = step.page;
      m_dirty = true;
      ++running.step;
      break;

    case ActionStep::Kind::Set:
      evaluator.SetState(step.target,
                         evaluator.EvaluateForRow(step.value, running.request.row_array,
                                                  running.request.row));
      m_dirty = true;
      ++running.step;
      break;
    }
  }
}
}  // namespace Duo
