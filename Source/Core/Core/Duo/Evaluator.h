// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Samples a manifest's values from emulated memory and renders companion pages. This class is
// independent of Dolphin's core so it can be unit tested against a fake memory.
//
// Evaluation order within a sample:
//   1. "values" in declaration order. A computed value can use any value declared before it.
//   2. Arrays, lazily: the first expression that touches count(a) or a[i].f samples array `a`.
//      Any array nobody touched is sampled afterwards so lists and maps can show it.
// Arrays may therefore refer to values, and values may refer to arrays.

#pragma once

#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/Duo/Manifest.h"
#include "Core/Duo/MemorySource.h"
#include "Core/Duo/Value.h"

namespace picojson
{
class value;
}

namespace Duo
{
class Scope;
}

namespace Duo
{
struct SystemInfo
{
  std::string game_id;
  u16 revision = 0;
  std::string title;
  bool is_wii = false;
  u64 frame = 0;
  double time_seconds = 0.0;
  bool has_disc = false;  // "disc:" images can be loaded ($disc).
};

// Converts game text in a non-trivial encoding to UTF-8. Ascii/UTF-8 and UTF-16BE are handled
// internally; Dolphin supplies a Shift-JIS decoder.
using TextDecoder = std::function<std::string(TextEncoding, std::span<const u8>)>;

struct ArrayElement
{
  u32 address = 0;
  u32 slot = 0;  // Position in memory, before filtering with "where".
  std::vector<Value> fields;
};

class Evaluator
{
public:
  explicit Evaluator(const Manifest& manifest, TextDecoder decoder = {});

  // Reads everything from memory. Call on the emulation thread between frames.
  void Sample(MemorySource& memory, const SystemInfo& system);

  // Renders the page with the given id (or the first visible page) to the snapshot form the
  // Android companion screen draws. `page_id` is updated to the page actually rendered.
  // For a companion with canvas layouts, `layout_id` picks the layout (empty: the first one).
  picojson::value Render(std::string& page_id, std::string_view layout_id = {}) const;

  // UI state ("state" in the manifest), changed by "set" steps. Not game memory.
  void SetState(std::string_view name, Value value);
  Value GetState(std::string_view name) const;

  // Evaluates an expression the way an action triggered from `row` of `row_array` sees it.
  Value EvaluateForRow(const Expression& expression, std::string_view row_array, s64 row) const;

  // Performs a write step against the most recent sample. `row` is the list row an action was
  // triggered from, or -1. Returns an error message on failure.
  // `row_array` names the list's array (empty when the action did not come from a list row).
  std::optional<std::string> Write(MemorySource& memory, const ActionStep& step,
                                   std::string_view row_array, s64 row) const;

  // Read access for the runtime and tests.
  Value GetValue(std::string_view name) const;
  const std::vector<ArrayElement>* GetArray(std::string_view name) const;
  const SystemInfo& GetSystem() const { return m_system; }
  bool HasSample() const { return m_has_sample; }

  // Reads one field from memory. Exposed for tests and tools.
  static Value ReadField(MemorySource& memory, const FieldSpec& field, u32 base,
                         const TextDecoder& decoder);
  static std::optional<u32> ResolveAddress(MemorySource& memory, u32 address,
                                           std::span<const u32> offsets);

private:
  class GlobalScope;
  class ElementScope;
  friend class GlobalScope;
  friend class ElementScope;

  enum class ArrayStatus
  {
    NotSampled,
    Sampling,
    Sampled,
  };

  void SampleArray(const ArraySpec& spec) const;
  const std::vector<ArrayElement>* EnsureArray(std::string_view name) const;

  picojson::value RenderWidget(const WidgetSpec& widget) const;

  struct CanvasContext;
  void RenderElements(const std::vector<ElementSpec>& elements, const Scope& scope, double origin_x,
                      double origin_y, const std::string& row_array, s64 row,
                      CanvasContext& context) const;
  void RenderElement(const ElementSpec& element, const Scope& scope, double origin_x,
                     double origin_y, const std::string& row_array, s64 row,
                     CanvasContext& context) const;
  std::string ResolveColor(const std::string& color) const;

  std::map<std::string, Value, std::less<>> m_state;
  mutable std::string m_layout;  // Layout being rendered, for $layout.

  const Manifest& m_manifest;
  TextDecoder m_decoder;
  SystemInfo m_system;
  bool m_has_sample = false;

  std::map<std::string, Value, std::less<>> m_values;
  mutable std::map<std::string, std::vector<ArrayElement>, std::less<>> m_arrays;
  mutable std::map<std::string, ArrayStatus, std::less<>> m_array_status;

  // Only set while Sample() runs, so lazily sampled arrays can read memory.
  MemorySource* m_sampling_memory = nullptr;
};
}  // namespace Duo
