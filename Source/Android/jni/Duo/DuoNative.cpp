// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// JNI bridge for org.dolphinemu.dolphinemu.features.duo.DuoNative.

#include <algorithm>
#include <jni.h>

#include <memory>
#include <string>

#include <fmt/format.h>
#include <picojson.h>

#include "Core/Duo/Manifest.h"
#include "Core/Duo/Runtime.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/Touch/InputOverrider.h"
#include "jni/AndroidCommon/AndroidCommon.h"

namespace
{
using ciface::Touch::ControlID;
static_assert(static_cast<int>(Duo::Control::GC_A) == ControlID::GCPAD_A_BUTTON);
static_assert(static_cast<int>(Duo::Control::GC_C_STICK_Y) == ControlID::GCPAD_C_STICK_Y);
static_assert(static_cast<int>(Duo::Control::WII_A) == ControlID::WIIMOTE_A_BUTTON);
static_assert(static_cast<int>(Duo::Control::WII_IR_Y) == ControlID::WIIMOTE_IR_Y);
static_assert(static_cast<int>(Duo::Control::NUNCHUK_C) == ControlID::NUNCHUK_C_BUTTON);
static_assert(static_cast<int>(Duo::Control::CLASSIC_A) == ControlID::CLASSIC_A_BUTTON);
static_assert(static_cast<int>(Duo::Control::CLASSIC_RIGHT_STICK_Y) ==
              ControlID::CLASSIC_RIGHT_STICK_Y);

// Presses buttons through the same override mechanism as the touchscreen overlay. Runs on the
// CPU thread, which is also the thread that reads controller state, so registering here cannot
// race with an input poll.
class AndroidInputSink final : public Duo::InputSink
{
public:
  void Press(int controller, Duo::Control control, double amount) override
  {
    {
      const auto lock = ControllerEmu::EmulatedController::GetStateLock();
      // Registering again is harmless (the overlay registers the same function) and makes
      // presses work even when the touch overlay is hidden, as it usually is on handhelds.
      if (Duo::IsGameCubeControl(control))
        ciface::Touch::RegisterGameCubeInputOverrider(controller);
      else
        ciface::Touch::RegisterWiiInputOverrider(controller);
    }
    ciface::Touch::SetControlState(controller, static_cast<ControlID>(control), amount);
  }

  void Release(int controller, Duo::Control control) override
  {
    ciface::Touch::ClearControlState(controller, static_cast<ControlID>(control));
  }
};

picojson::value TargetsToJson(const std::vector<Duo::TargetSpec>& targets)
{
  picojson::array out;
  for (const Duo::TargetSpec& target : targets)
  {
    picojson::object entry;
    entry["game_id"] = picojson::value(target.game_id);
    picojson::array revisions;
    for (u16 revision : target.revisions)
      revisions.emplace_back(static_cast<double>(revision));
    entry["revisions"] = picojson::value(std::move(revisions));
    out.emplace_back(std::move(entry));
  }
  return picojson::value(std::move(out));
}

picojson::value ErrorsToJson(const std::vector<std::string>& errors)
{
  picojson::array out;
  for (const std::string& error : errors)
    out.emplace_back(error);
  return picojson::value(std::move(out));
}
}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_setAttached(
    JNIEnv*, jobject, jboolean attached)
{
  static bool s_sink_installed = false;
  Duo::Runtime& runtime = Duo::Runtime::GetInstance();
  if (attached && !s_sink_installed)
  {
    runtime.SetInputSink(std::make_unique<AndroidInputSink>());
    s_sink_installed = true;
  }
  runtime.SetFrontendAttached(attached);
}

// Returns "<sequence>\n<json>" when a snapshot newer than `after` exists, otherwise null.
JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_getSnapshot(JNIEnv* env, jobject, jlong after)
{
  const auto snapshot = Duo::Runtime::GetInstance().GetSnapshot(static_cast<u64>(after));
  if (!snapshot)
    return nullptr;
  return ToJString(env, fmt::format("{}\n{}", snapshot->first, snapshot->second));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_setPage(JNIEnv* env,
                                                                                     jobject,
                                                                                     jstring page)
{
  Duo::Runtime::GetInstance().SetPage(GetJString(env, page));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_setViewport(
    JNIEnv*, jobject, jint width, jint height)
{
  Duo::Runtime::GetInstance().SetViewport(static_cast<u32>(std::max(width, 0)),
                                          static_cast<u32>(std::max(height, 0)));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_triggerAction(
    JNIEnv* env, jobject, jstring action, jstring row_array, jint row)
{
  Duo::Runtime::GetInstance().TriggerAction(
      Duo::ActionRequest{GetJString(env, action), GetJString(env, row_array), row});
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_selectCompanion(
    JNIEnv* env, jobject, jstring companion_id)
{
  Duo::Runtime::GetInstance().SelectCompanion(GetJString(env, companion_id));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_reload(JNIEnv*,
                                                                                    jobject)
{
  Duo::Runtime::GetInstance().RequestReload();
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_getCompanionsDirectory(JNIEnv* env, jobject)
{
  return ToJString(env, Duo::Runtime::GetCompanionsDirectory());
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_getWatchesDirectory(JNIEnv* env, jobject)
{
  return ToJString(env, Duo::Runtime::GetWatchesDirectory());
}

// Parses and validates a manifest for the installer. Returns a JSON object with "errors" plus,
// when valid, the companion's identity, targets and requested capabilities.
JNIEXPORT jstring JNICALL Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_inspectManifest(
    JNIEnv* env, jobject, jstring json)
{
  const Duo::ManifestParseResult result = Duo::ParseManifest(GetJString(env, json));
  picojson::object out;
  out["errors"] = ErrorsToJson(result.errors);
  if (result.manifest)
  {
    const Duo::Manifest& manifest = *result.manifest;
    out["id"] = picojson::value(manifest.id);
    out["name"] = picojson::value(manifest.name);
    out["version"] = picojson::value(manifest.version);
    out["author"] = picojson::value(manifest.author);
    out["description"] = picojson::value(manifest.description);
    out["targets"] = TargetsToJson(manifest.targets);
    out["write_memory"] = picojson::value(manifest.allow_write);
    out["input"] = picojson::value(manifest.allow_input);
  }
  return ToJString(env, picojson::value(std::move(out)).serialize());
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_features_duo_DuoNative_listInstalled(JNIEnv* env, jobject)
{
  picojson::array out;
  for (const Duo::InstalledCompanion& companion : Duo::Runtime::ScanCompanions())
  {
    picojson::object entry;
    entry["id"] = picojson::value(companion.id);
    entry["name"] = picojson::value(companion.name);
    entry["version"] = picojson::value(companion.version);
    entry["author"] = picojson::value(companion.author);
    entry["description"] = picojson::value(companion.description);
    entry["directory"] = picojson::value(companion.directory);
    entry["targets"] = TargetsToJson(companion.targets);
    entry["errors"] = ErrorsToJson(companion.errors);
    out.emplace_back(std::move(entry));
  }
  return ToJString(env, picojson::value(std::move(out)).serialize());
}
}
