// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A scripting harness for the headless frontend, used to author Dolphin Duo companions without a
// person at the controls. Enabled by setting DUO_HARNESS to a directory: Dolphin creates the FIFO
// <dir>/cmd and appends one line per finished command to <dir>/log.
//
// Commands (one per line):
//   press <A|B|X|Y|Z|START|UP|DOWN|LEFT|RIGHT|L|R>[+<...>] [frames]  hold buttons (default 4)
//   stick <x> <y> [frames]         main stick, -1..1
//   wait <frames>                  let N video fields pass with nothing held
//   mem1 <path>                    write MEM1 to a file while the CPU is paused
//   shot <name>                    save a screenshot as ScreenShots/<game>/<name>.png
//   save <path> | load <path>      save state
//   speed <factor>                 emulation speed, 0 = unlimited
//   echo <text>                    write <text> to the log (a barrier for scripts)
//   duo attach <W>x<H>             attach the companion runtime as an Android screen of that size
//   duo page <id>                  switch the companion page, as a tab tap would
//   duo snap <path>                write the latest companion snapshot (JSON, plus "disc_images"
//                                  mapping each disc: image to its decoded PNG, for the previewer)
//   duo detach                     detach the companion runtime

#include "DolphinNoGUI/DuoHarness.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <fmt/format.h>
#include <picojson.h>

#include "Common/Config/Config.h"
#include "Common/StringUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/Duo/Runtime.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/Memmap.h"
#include "Core/Movie.h"
#include "Core/State.h"
#include "Core/System.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/StickGate.h"
#include "InputCommon/InputConfig.h"

namespace DuoHarness
{
namespace
{
std::thread s_thread;
std::atomic<bool> s_running{false};
std::string s_dir;

std::mutex s_input_mutex;
std::condition_variable s_input_cv;
std::map<std::pair<std::string, std::string>, double> s_held;  // (group, control) -> state
bool s_active = false;

const std::map<std::string, std::pair<std::string, std::string>, std::less<>> BUTTONS = {
    {"A", {GCPad::BUTTONS_GROUP, GCPad::A_BUTTON}},
    {"B", {GCPad::BUTTONS_GROUP, GCPad::B_BUTTON}},
    {"X", {GCPad::BUTTONS_GROUP, GCPad::X_BUTTON}},
    {"Y", {GCPad::BUTTONS_GROUP, GCPad::Y_BUTTON}},
    {"Z", {GCPad::BUTTONS_GROUP, GCPad::Z_BUTTON}},
    {"START", {GCPad::BUTTONS_GROUP, GCPad::START_BUTTON}},
    {"UP", {GCPad::DPAD_GROUP, "Up"}},
    {"DOWN", {GCPad::DPAD_GROUP, "Down"}},
    {"LEFT", {GCPad::DPAD_GROUP, "Left"}},
    {"RIGHT", {GCPad::DPAD_GROUP, "Right"}},
    {"L", {GCPad::TRIGGERS_GROUP, GCPad::L_DIGITAL}},
    {"R", {GCPad::TRIGGERS_GROUP, GCPad::R_DIGITAL}},
};

void Log(const std::string& line)
{
  std::ofstream out(s_dir + "/log", std::ios::app);
  out << line << '\n';
  std::fprintf(stderr, "[duo-harness] %s\n", line.c_str());
}

u64 CurrentFrame()
{
  return Core::System::GetInstance().GetMovie().GetCurrentFrame();
}

std::optional<ControlState> Override(std::string_view group, std::string_view control,
                                     ControlState state)
{
  std::lock_guard lock(s_input_mutex);
  if (!s_active)
    return std::nullopt;
  const auto it = s_held.find({std::string(group), std::string(control)});
  if (it != s_held.end())
    return it->second;
  return std::nullopt;
}

// Holds `held` for `frames` video fields (60 per second on NTSC), then releases everything.
bool HoldFor(std::map<std::pair<std::string, std::string>, double> held, int frames)
{
  {
    std::lock_guard lock(s_input_mutex);
    s_held = std::move(held);
    s_active = true;
  }
  const u64 start = CurrentFrame();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
  bool done = false;
  while (s_running && std::chrono::steady_clock::now() < deadline)
  {
    if (CurrentFrame() >= start + static_cast<u64>(std::max(frames, 1)))
    {
      done = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  std::lock_guard lock(s_input_mutex);
  s_held.clear();
  s_active = false;
  return done;
}

void Execute(const std::string& line)
{
  std::istringstream in(line);
  std::string cmd;
  in >> cmd;
  auto& system = Core::System::GetInstance();

  if (cmd == "press")
  {
    std::string names;
    int polls = 4;
    in >> names >> polls;
    std::map<std::pair<std::string, std::string>, double> held;
    for (const std::string& name : SplitString(names, '+'))
    {
      const auto it = BUTTONS.find(name);
      if (it == BUTTONS.end())
        return Log("error unknown button " + name);
      held[it->second] = 1.0;
    }
    // Release for a few frames afterwards so repeated presses register as separate presses.
    if (!HoldFor(std::move(held), polls) || !HoldFor({}, 4))
      return Log("error timeout " + line);
  }
  else if (cmd == "stick")
  {
    double x = 0, y = 0;
    int polls = 4;
    in >> x >> y >> polls;
    std::map<std::pair<std::string, std::string>, double> held;
    held[{GCPad::MAIN_STICK_GROUP, ControllerEmu::ReshapableInput::X_INPUT_OVERRIDE}] = x;
    held[{GCPad::MAIN_STICK_GROUP, ControllerEmu::ReshapableInput::Y_INPUT_OVERRIDE}] = y;
    if (!HoldFor(std::move(held), polls) || !HoldFor({}, 4))
      return Log("error timeout " + line);
  }
  else if (cmd == "wait")
  {
    int polls = 1;
    in >> polls;
    if (!HoldFor({}, polls))
      return Log("error timeout " + line);
  }
  else if (cmd == "mem1")
  {
    std::string path;
    in >> path;
    const Core::CPUThreadGuard guard(system);
    auto& memory = system.GetMemory();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(memory.GetRAM()), memory.GetRamSizeReal());
  }
  else if (cmd == "shot")
  {
    std::string name;
    in >> name;
    Core::SaveScreenShot(name);
  }
  else if (cmd == "save" || cmd == "load")
  {
    std::string path;
    in >> path;
    // Save states are driven from the host thread, as the GUI frontends do.
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done = false;
    Core::QueueHostJob([&, path, save = cmd == "save"](Core::System& s) {
      if (save)
        State::SaveAs(s, path);
      else
        State::LoadAs(s, path);
      std::lock_guard lock(done_mutex);
      done = true;
      done_cv.notify_all();
    });
    std::unique_lock lock(done_mutex);
    if (!done_cv.wait_for(lock, std::chrono::seconds(60), [&] { return done; }))
      return Log("error timeout " + line);
  }
  else if (cmd == "speed")
  {
    float speed = 1.0f;
    in >> speed;
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, speed);
  }
  else if (cmd == "duo")
  {
    std::string sub, arg;
    in >> sub >> arg;
    Duo::Runtime& runtime = Duo::Runtime::GetInstance();
    if (sub == "attach")
    {
      u32 width = 0, height = 0;
      if (std::sscanf(arg.c_str(), "%ux%u", &width, &height) != 2)
        return Log("error usage: duo attach <W>x<H>");
      runtime.SetViewport(width, height);
      runtime.SetFrontendAttached(true);
    }
    else if (sub == "detach")
    {
      runtime.SetFrontendAttached(false);
    }
    else if (sub == "page")
    {
      runtime.SetPage(arg);
    }
    else if (sub == "snap")
    {
      const auto snapshot = runtime.GetSnapshot(0);
      if (!snapshot)
        return Log("error no snapshot yet");
      // Resolve disc images the way the Android frontend does, so the previewer can draw them.
      picojson::value root;
      const std::string parse_error = picojson::parse(root, snapshot->second);
      if (!parse_error.empty() || !root.is<picojson::object>())
        return Log("error bad snapshot: " + parse_error);
      picojson::object images;
      if (root.contains("elements") && root.get("elements").is<picojson::array>())
      {
        for (const picojson::value& element : root.get("elements").get<picojson::array>())
        {
          for (const char* key : {"src", "fallback"})
          {
            if (!element.contains(key) || !element.get(key).is<std::string>())
              continue;
            const std::string& source = element.get(key).get<std::string>();
            if (!source.starts_with("disc:") || images.contains(source))
              continue;
            std::string error;
            if (const std::optional<std::string> png = runtime.ResolveDiscImage(source, &error))
              images[source] = picojson::value(*png);
            else if (std::string_view(key) == "fallback" || !element.contains("fallback"))
              Log("warning " + source + ": " + error);
          }
        }
      }
      root.get<picojson::object>()["disc_images"] = picojson::value(std::move(images));
      std::ofstream out(arg);
      out << root.serialize();
    }
    else
    {
      return Log("error unknown duo command " + sub);
    }
  }
  else if (cmd != "echo" && !cmd.empty())
  {
    return Log("error unknown command " + cmd);
  }
  Log("done " + std::to_string(CurrentFrame()) + " " + line);
}

void ThreadFunc()
{
#ifndef _WIN32
  const std::string fifo = s_dir + "/cmd";
  unlink(fifo.c_str());
  mkfifo(fifo.c_str(), 0666);
  // Opened read-write so the read never sees EOF when a writer closes.
  const int fd = open(fifo.c_str(), O_RDWR);
  std::string buffer;
  std::array<char, 4096> chunk;
  // Wait until the game is actually running before accepting commands.
  auto& system = Core::System::GetInstance();
  while (s_running && (!Core::IsRunning(system) || CurrentFrame() < 2))
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Log("ready");
  while (s_running)
  {
    const ssize_t n = read(fd, chunk.data(), chunk.size());
    if (n <= 0)
      continue;
    buffer.append(chunk.data(), n);
    size_t newline;
    while ((newline = buffer.find('\n')) != std::string::npos)
    {
      const std::string line = buffer.substr(0, newline);
      buffer.erase(0, newline + 1);
      Execute(line);
    }
  }
  close(fd);
#endif
}
}  // namespace

void Start()
{
  const char* dir = std::getenv("DUO_HARNESS");
  if (!dir || !*dir)
    return;
  s_dir = dir;
  Pad::GetConfig()->GetController(0)->SetInputOverrideFunction(Override);
  s_running = true;
  s_thread = std::thread(ThreadFunc);
  s_thread.detach();
}

void Stop()
{
  s_running = false;
  s_input_cv.notify_all();
}
}  // namespace DuoHarness
