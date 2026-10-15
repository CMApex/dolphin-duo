// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Evaluator.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

#include <fmt/format.h>
#include <picojson.h>

namespace Duo
{
namespace
{
u32 TypeSize(MemoryType type)
{
  switch (type)
  {
  case MemoryType::U8:
  case MemoryType::S8:
  case MemoryType::Bool:
    return 1;
  case MemoryType::U16:
  case MemoryType::S16:
    return 2;
  case MemoryType::U32:
  case MemoryType::S32:
  case MemoryType::F32:
    return 4;
  case MemoryType::F64:
    return 8;
  case MemoryType::String:
    return 1;
  }
  return 4;
}

// Replaces anything that is not well-formed UTF-8 with '?', so game text can never produce an
// invalid snapshot for the Java side.
std::string SanitizeUtf8(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size())
  {
    const u8 c = static_cast<u8>(text[i]);
    size_t length = 0;
    if (c < 0x80)
      length = 1;
    else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
      length = 2;
    else if ((c & 0xF0) == 0xE0)
      length = 3;
    else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
      length = 4;

    bool valid = length != 0 && i + length <= text.size();
    for (size_t k = 1; valid && k < length; ++k)
      valid = (static_cast<u8>(text[i + k]) & 0xC0) == 0x80;

    if (!valid)
    {
      out.push_back('?');
      ++i;
      continue;
    }
    if (length == 1 && c < 0x20 && c != '\n' && c != '\t')
      out.push_back(' ');
    else
      out.append(text.substr(i, length));
    i += length;
  }
  return out;
}

void AppendUtf8(std::string& out, u32 code_point)
{
  if (code_point < 0x80)
  {
    out.push_back(static_cast<char>(code_point));
  }
  else if (code_point < 0x800)
  {
    out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
  else if (code_point < 0x10000)
  {
    out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
  else
  {
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

std::string DecodeUtf16BE(std::span<const u8> bytes)
{
  std::string out;
  for (size_t i = 0; i + 1 < bytes.size(); i += 2)
  {
    u32 unit = (u32{bytes[i]} << 8) | bytes[i + 1];
    if (unit == 0)
      break;
    if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size())
    {
      const u32 low = (u32{bytes[i + 2]} << 8) | bytes[i + 3];
      if (low >= 0xDC00 && low <= 0xDFFF)
      {
        unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
        i += 2;
      }
      else
      {
        unit = '?';
      }
    }
    else if (unit >= 0xD800 && unit <= 0xDFFF)
    {
      unit = '?';
    }
    AppendUtf8(out, unit);
  }
  return out;
}

picojson::value Json(const std::string& text)
{
  return picojson::value(text);
}

picojson::value Json(double number)
{
  return picojson::value(number);
}

picojson::value Json(bool boolean)
{
  return picojson::value(boolean);
}

picojson::value JsonFraction(const Value& value, const Value& max)
{
  const std::optional<double> v = value.AsNumber();
  const std::optional<double> m = max.AsNumber();
  if (!v || !m || *m <= 0.0)
    return picojson::value();
  return Json(std::clamp(*v / *m, 0.0, 1.0));
}

std::string KeyText(const Value& key)
{
  if (key.IsString())
    return key.String();
  if (key.IsNumber())
  {
    const double n = key.Number();
    if (std::trunc(n) == n && std::abs(n) < 1e15)
      return fmt::format("{}", static_cast<s64>(n));
    return fmt::format("{}", n);
  }
  return {};
}
}  // namespace

class Evaluator::GlobalScope final : public Scope
{
public:
  explicit GlobalScope(const Evaluator& evaluator, std::string_view page = {}, s64 index = -1)
      : m_evaluator(evaluator), m_page(page), m_index(index)
  {
  }

  Value Lookup(std::string_view name) const override
  {
    if (name.starts_with('$'))
      return LookupSystem(name);
    const auto it = m_evaluator.m_values.find(name);
    if (it != m_evaluator.m_values.end())
      return it->second;
    const auto state = m_evaluator.m_state.find(name);
    return state == m_evaluator.m_state.end() ? Value::Null() : state->second;
  }

  Value LookupElement(std::string_view array, s64 index, std::string_view field) const override
  {
    const std::vector<ArrayElement>* elements = m_evaluator.EnsureArray(array);
    const ArraySpec* spec = m_evaluator.m_manifest.FindArray(array);
    if (!elements || !spec || index < 0 || index >= static_cast<s64>(elements->size()))
      return Value::Null();
    const auto it = std::ranges::find(spec->fields, field, &FieldSpec::name);
    if (it == spec->fields.end())
      return Value::Null();
    const size_t field_index = static_cast<size_t>(it - spec->fields.begin());
    const ArrayElement& element = (*elements)[static_cast<size_t>(index)];
    return field_index < element.fields.size() ? element.fields[field_index] : Value::Null();
  }

  Value Count(std::string_view array) const override
  {
    const std::vector<ArrayElement>* elements = m_evaluator.EnsureArray(array);
    return elements ? Value(static_cast<s64>(elements->size())) : Value::Null();
  }

  Value Table(std::string_view table, const Value& key) const override
  {
    const auto table_it = m_evaluator.m_manifest.tables.find(table);
    if (table_it == m_evaluator.m_manifest.tables.end())
      return Value::Null();
    const auto entry = table_it->second.find(KeyText(key));
    return entry == table_it->second.end() ? Value::Null() : Value(entry->second);
  }

  Value LookupSystem(std::string_view name) const
  {
    const SystemInfo& system = m_evaluator.m_system;
    if (name == "$game_id")
      return Value(system.game_id);
    if (name == "$revision")
      return Value(static_cast<u32>(system.revision));
    if (name == "$title")
      return Value(system.title);
    if (name == "$frame")
      return Value(static_cast<double>(system.frame));
    if (name == "$time")
      return Value(system.time_seconds);
    if (name == "$platform")
      return Value(system.is_wii ? "wii" : "gc");
    if (name == "$page")
      return Value(std::string(m_page));
    if (name == "$index")
      return m_index >= 0 ? Value(m_index) : Value::Null();
    if (name == "$layout")
      return Value(m_evaluator.m_layout);
    return Value::Null();
  }

private:
  const Evaluator& m_evaluator;
  std::string_view m_page;
  s64 m_index;
};

class Evaluator::ElementScope final : public Scope
{
public:
  ElementScope(const GlobalScope& parent, const ArraySpec& spec, const ArrayElement& element,
               s64 index)
      : m_parent(parent), m_spec(spec), m_element(element), m_index(index)
  {
  }

  Value Lookup(std::string_view name) const override
  {
    if (name == "$index")
      return Value(m_index);
    if (name == "$slot")
      return Value(m_element.slot);
    if (name == "$address")
      return Value(m_element.address);
    const auto it = std::ranges::find(m_spec.fields, name, &FieldSpec::name);
    if (it != m_spec.fields.end())
    {
      const size_t field_index = static_cast<size_t>(it - m_spec.fields.begin());
      return field_index < m_element.fields.size() ? m_element.fields[field_index] : Value::Null();
    }
    return m_parent.Lookup(name);
  }

  Value LookupElement(std::string_view array, s64 index, std::string_view field) const override
  {
    return m_parent.LookupElement(array, index, field);
  }
  Value Count(std::string_view array) const override { return m_parent.Count(array); }
  Value Table(std::string_view table, const Value& key) const override
  {
    return m_parent.Table(table, key);
  }

private:
  const GlobalScope& m_parent;
  const ArraySpec& m_spec;
  const ArrayElement& m_element;
  s64 m_index;
};

Evaluator::Evaluator(const Manifest& manifest, TextDecoder decoder)
    : m_manifest(manifest), m_decoder(std::move(decoder))
{
  // Initial state is evaluated once, in order, so a state entry may build on an earlier one.
  for (const StateSpec& state : m_manifest.state)
  {
    const GlobalScope scope(*this);
    m_state.insert_or_assign(state.name, state.initial.Evaluate(scope));
  }
}

void Evaluator::SetState(std::string_view name, Value value)
{
  const auto it = m_state.find(name);
  if (it != m_state.end())
    it->second = std::move(value);
}

Value Evaluator::GetState(std::string_view name) const
{
  const auto it = m_state.find(name);
  return it == m_state.end() ? Value::Null() : it->second;
}

Value Evaluator::EvaluateForRow(const Expression& expression, std::string_view row_array,
                                s64 row) const
{
  const GlobalScope global(*this, {}, row);
  const ArraySpec* spec = row_array.empty() ? nullptr : m_manifest.FindArray(row_array);
  const std::vector<ArrayElement>* elements = spec ? GetArray(row_array) : nullptr;
  if (elements && row >= 0 && row < static_cast<s64>(elements->size()))
  {
    const ElementScope scope(global, *spec, (*elements)[static_cast<size_t>(row)], row);
    return expression.Evaluate(scope);
  }
  return expression.Evaluate(global);
}

std::optional<u32> Evaluator::ResolveAddress(MemorySource& memory, u32 address,
                                             std::span<const u32> offsets)
{
  if (offsets.empty())
    return address;

  const std::optional<u64> first = memory.Read(address, 4);
  if (!first || !memory.IsRAM(static_cast<u32>(*first)))
    return std::nullopt;
  u32 pointer = static_cast<u32>(*first);

  for (size_t i = 0; i + 1 < offsets.size(); ++i)
  {
    const std::optional<u64> next = memory.Read(pointer + offsets[i], 4);
    if (!next || !memory.IsRAM(static_cast<u32>(*next)))
      return std::nullopt;
    pointer = static_cast<u32>(*next);
  }
  return pointer + offsets.back();
}

Value Evaluator::ReadField(MemorySource& memory, const FieldSpec& field, u32 base,
                           const TextDecoder& decoder)
{
  const std::optional<u32> address = ResolveAddress(memory, base + field.address, field.offsets);
  if (!address)
    return Value::Null();

  if (field.type == MemoryType::String)
  {
    std::vector<u8> bytes;
    bytes.reserve(field.length);
    for (u32 i = 0; i < field.length; ++i)
    {
      const std::optional<u64> byte = memory.Read(*address + i, 1);
      if (!byte)
        return Value::Null();
      bytes.push_back(static_cast<u8>(*byte));
    }

    std::string text;
    switch (field.encoding)
    {
    case TextEncoding::Utf16BE:
      text = DecodeUtf16BE(bytes);
      break;
    case TextEncoding::ShiftJIS:
    {
      const auto end = std::ranges::find(bytes, u8{0});
      const std::span<const u8> used(bytes.begin(), end);
      text =
          decoder ? decoder(TextEncoding::ShiftJIS, used) : std::string(used.begin(), used.end());
      break;
    }
    case TextEncoding::Ascii:
    {
      const auto end = std::ranges::find(bytes, u8{0});
      text.assign(bytes.begin(), end);
      break;
    }
    }
    return Value(SanitizeUtf8(text));
  }

  const u32 size = TypeSize(field.type);
  const std::optional<u64> raw_read = memory.Read(*address, size);
  if (!raw_read)
    return Value::Null();
  u64 raw = *raw_read;

  if (field.type == MemoryType::F32)
  {
    const float f = std::bit_cast<float>(static_cast<u32>(raw));
    return std::isfinite(f) ? Value(static_cast<double>(f)) : Value::Null();
  }
  if (field.type == MemoryType::F64)
  {
    const double d = std::bit_cast<double>(raw);
    return std::isfinite(d) ? Value(d) : Value::Null();
  }

  if (field.mask)
    raw &= *field.mask;
  raw >>= field.shift;

  switch (field.type)
  {
  case MemoryType::U8:
  case MemoryType::U16:
  case MemoryType::U32:
    return Value(static_cast<double>(raw));
  case MemoryType::S8:
    return Value(static_cast<double>(static_cast<s8>(raw)));
  case MemoryType::S16:
    return Value(static_cast<double>(static_cast<s16>(raw)));
  case MemoryType::S32:
    return Value(static_cast<double>(static_cast<s32>(raw)));
  case MemoryType::Bool:
    return Value(raw != 0);
  default:
    return Value::Null();
  }
}

void Evaluator::Sample(MemorySource& memory, const SystemInfo& system)
{
  m_system = system;
  m_values.clear();
  m_arrays.clear();
  m_array_status.clear();
  m_sampling_memory = &memory;

  const GlobalScope scope(*this);
  for (const FieldSpec& field : m_manifest.values)
  {
    Value value;
    if (field.computed)
    {
      value = field.expression.Evaluate(scope);
    }
    else if (field.address_expression)
    {
      const std::optional<s64> address = field.address_expression->Evaluate(scope).AsInteger();
      if (address && *address > 0 && *address <= 0xFFFFFFFF)
        value = ReadField(memory, field, static_cast<u32>(*address), m_decoder);
    }
    else
    {
      value = ReadField(memory, field, 0, m_decoder);
    }
    m_values.insert_or_assign(field.name, std::move(value));
  }
  for (const ArraySpec& array : m_manifest.arrays)
    EnsureArray(array.name);

  m_sampling_memory = nullptr;
  m_has_sample = true;
}

const std::vector<ArrayElement>* Evaluator::EnsureArray(std::string_view name) const
{
  const ArraySpec* spec = m_manifest.FindArray(name);
  if (!spec)
    return nullptr;

  const auto status = m_array_status.find(name);
  if (status != m_array_status.end())
  {
    if (status->second == ArrayStatus::Sampling)
      return nullptr;  // An expression inside the array referred back to the array itself.
    const auto it = m_arrays.find(name);
    return it == m_arrays.end() ? nullptr : &it->second;
  }

  if (!m_sampling_memory)
    return nullptr;
  SampleArray(*spec);
  const auto it = m_arrays.find(name);
  return it == m_arrays.end() ? nullptr : &it->second;
}

void Evaluator::SampleArray(const ArraySpec& spec) const
{
  MemorySource& memory = *m_sampling_memory;
  m_array_status.insert_or_assign(spec.name, ArrayStatus::Sampling);
  std::vector<ArrayElement> elements;

  const GlobalScope scope(*this);
  u32 count = spec.count;
  if (spec.count_expression)
  {
    const std::optional<s64> evaluated = spec.count_expression->Evaluate(scope).AsInteger();
    count = evaluated ? static_cast<u32>(std::clamp<s64>(*evaluated, 0, spec.max_count)) : 0;
  }
  count = std::min(count, spec.max_count);

  u32 start = spec.address;
  if (spec.address_expression)
  {
    const std::optional<s64> address = spec.address_expression->Evaluate(scope).AsInteger();
    start = address && *address > 0 && *address <= 0xFFFFFFFF ? static_cast<u32>(*address) : 0;
  }
  const std::optional<u32> base =
      start ? ResolveAddress(memory, start, spec.offsets) : std::optional<u32>();
  if (base)
  {
    for (u32 slot = 0; slot < count; ++slot)
    {
      ArrayElement element;
      element.slot = slot;
      const u32 slot_address = *base + slot * spec.stride;
      if (spec.pointer_table)
      {
        const std::optional<u64> pointer = memory.Read(slot_address, 4);
        if (!pointer || !memory.IsRAM(static_cast<u32>(*pointer)))
          continue;  // Empty slot.
        element.address = static_cast<u32>(*pointer);
      }
      else
      {
        element.address = slot_address;
      }

      element.fields.resize(spec.fields.size());
      const s64 index = static_cast<s64>(elements.size());
      for (size_t f = 0; f < spec.fields.size(); ++f)
      {
        const FieldSpec& field = spec.fields[f];
        if (field.computed)
        {
          const ElementScope element_scope(scope, spec, element, index);
          element.fields[f] = field.expression.Evaluate(element_scope);
        }
        else if (field.address_expression)
        {
          // An absolute address computed from earlier fields, e.g. an index into another table.
          const ElementScope element_scope(scope, spec, element, index);
          const std::optional<s64> address =
              field.address_expression->Evaluate(element_scope).AsInteger();
          if (address && *address > 0 && *address <= 0xFFFFFFFF)
            element.fields[f] = ReadField(memory, field, static_cast<u32>(*address), m_decoder);
        }
        else
        {
          element.fields[f] = ReadField(memory, field, element.address, m_decoder);
        }
      }

      if (spec.where)
      {
        const ElementScope element_scope(scope, spec, element, index);
        if (!spec.where->Evaluate(element_scope).Truthy())
          continue;
      }
      elements.push_back(std::move(element));
    }
  }

  m_arrays.insert_or_assign(spec.name, std::move(elements));
  m_array_status.insert_or_assign(spec.name, ArrayStatus::Sampled);
}

Value Evaluator::GetValue(std::string_view name) const
{
  const GlobalScope scope(*this);
  return scope.Lookup(name);
}

const std::vector<ArrayElement>* Evaluator::GetArray(std::string_view name) const
{
  const auto it = m_arrays.find(name);
  return it == m_arrays.end() ? nullptr : &it->second;
}

struct Evaluator::CanvasContext
{
  const GlobalScope* global = nullptr;  // The page scope that element scopes chain to.
  picojson::array out;
  size_t budget = 6000;  // Hard cap on emitted elements per frame.
};

picojson::value Evaluator::Render(std::string& page_id, std::string_view layout_id) const
{
  picojson::object result;
  const LayoutSpec* layout = m_manifest.FindLayout(layout_id);
  if (!layout && !m_manifest.layouts.empty())
    layout = &m_manifest.layouts.front();
  m_layout = layout ? layout->id : std::string();
  picojson::array pages;

  const PageSpec* active = nullptr;
  const PageSpec* first_visible = nullptr;
  for (const PageSpec& page : m_manifest.pages)
  {
    const GlobalScope scope(*this, page.id);
    if (page.visible_if && !page.visible_if->Evaluate(scope).Truthy())
      continue;
    if (!first_visible)
      first_visible = &page;
    if (page.id == page_id)
      active = &page;

    picojson::object entry;
    entry["id"] = Json(page.id);
    entry["title"] = Json(page.title.Render(scope));
    pages.emplace_back(std::move(entry));
  }
  if (!active)
    active = first_visible;

  result["pages"] = picojson::value(std::move(pages));
  picojson::array widgets;
  if (active)
  {
    page_id = active->id;
    const GlobalScope scope(*this, active->id);
    for (const WidgetSpec& widget : active->widgets)
    {
      if (widget.visible_if && !widget.visible_if->Evaluate(scope).Truthy())
        continue;
      widgets.push_back(RenderWidget(widget));
    }
  }
  result["page"] = Json(page_id);
  result["widgets"] = picojson::value(std::move(widgets));

  const std::vector<ElementSpec>* page_elements = active ? active->ElementsFor(m_layout) : nullptr;
  if (layout && (page_elements || !layout->elements.empty()))
  {
    const GlobalScope scope(*this, page_id);
    CanvasContext context;
    context.global = &scope;
    RenderElements(layout->elements, scope, 0, 0, {}, -1, context);
    if (page_elements)
      RenderElements(*page_elements, scope, 0, 0, {}, -1, context);

    picojson::object canvas;
    canvas["layout"] = Json(layout->id);
    canvas["w"] = Json(static_cast<double>(layout->width));
    canvas["h"] = Json(static_cast<double>(layout->height));
    canvas["background"] =
        layout->background ? Json(ResolveColor(layout->background->Render(scope))) :
                             picojson::value();
    picojson::object fonts;
    for (const auto& [name, file] : m_manifest.theme.fonts)
      fonts[name] = Json(file);
    canvas["fonts"] = picojson::value(std::move(fonts));
    picojson::array layouts;
    for (const LayoutSpec& entry : m_manifest.layouts)
      layouts.push_back(Json(entry.id));
    canvas["layouts"] = picojson::value(std::move(layouts));
    result["canvas"] = picojson::value(std::move(canvas));
    result["elements"] = picojson::value(std::move(context.out));
  }
  return picojson::value(std::move(result));
}

// ---------------------------------------------------------------------------------------------
// Canvas pages

namespace
{
constexpr std::string_view COLOR_PROPS[] = {"fill",  "fill2",  "stroke",  "color", "trim",
                                            "edge",  "track",  "track2",  "off",   "shadow",
                                            "outline", "tint", "grid_color", "glow"};

bool IsColorProp(std::string_view key)
{
  return std::ranges::find(COLOR_PROPS, key) != std::end(COLOR_PROPS);
}
}  // namespace

std::string Evaluator::ResolveColor(const std::string& color) const
{
  if (!color.starts_with('@'))
    return color;
  const auto it = m_manifest.theme.colors.find(std::string_view(color).substr(1));
  return it == m_manifest.theme.colors.end() ? std::string() : it->second;
}

void Evaluator::RenderElements(const std::vector<ElementSpec>& elements, const Scope& scope,
                               double origin_x, double origin_y, const std::string& row_array,
                               s64 row, CanvasContext& context) const
{
  for (const ElementSpec& element : elements)
    RenderElement(element, scope, origin_x, origin_y, row_array, row, context);
}

void Evaluator::RenderElement(const ElementSpec& element, const Scope& scope, double origin_x,
                              double origin_y, const std::string& row_array, s64 row,
                              CanvasContext& context) const
{
  if (context.out.size() >= context.budget)
    return;
  if (element.visible_if && !element.visible_if->Evaluate(scope).Truthy())
    return;

  const auto number = [&](std::string_view key) -> std::optional<double> {
    const auto it = element.numbers.find(key);
    if (it == element.numbers.end())
      return std::nullopt;
    return it->second.Evaluate(scope).AsNumber();
  };
  const double x = origin_x + number("x").value_or(0.0);
  const double y = origin_y + number("y").value_or(0.0);

  switch (element.type)
  {
  case ElementType::Group:
    RenderElements(element.children, scope, x, y, row_array, row, context);
    return;

  case ElementType::Repeat:
  {
    const ArraySpec* spec = m_manifest.FindArray(element.source);
    const std::vector<ArrayElement>* items = GetArray(element.source);
    if (!spec || !items)
      return;
    const double dx = number("dx").value_or(0.0);
    const double dy = number("dy").value_or(0.0);
    const s64 columns = std::max<s64>(
        1, static_cast<s64>(number("columns").value_or(dy != 0.0 ? 1.0 : 1e9)));
    const s64 start = std::max<s64>(0, static_cast<s64>(number("start").value_or(0.0)));
    const s64 limit = static_cast<s64>(number("limit").value_or(1e9));
    const GlobalScope& parent = *context.global;
    s64 shown = 0;
    s64 skipped = 0;
    for (s64 i = 0; i < static_cast<s64>(items->size()) && shown < limit; ++i)
    {
      const ElementScope item_scope(parent, *spec, (*items)[static_cast<size_t>(i)], i);
      if (element.where && !element.where->Evaluate(item_scope).Truthy())
        continue;
      // `start` skips matching elements, for paging through a long list.
      if (skipped++ < start)
        continue;
      RenderElements(element.children, item_scope, x + dx * static_cast<double>(shown % columns),
                     y + dy * static_cast<double>(shown / columns), element.source, i, context);
      ++shown;
    }
    return;
  }

  default:
    break;
  }

  picojson::object out;
  static constexpr std::array<const char*, 10> TYPE_NAMES = {
      "rect", "frame", "text", "bar", "pips", "image", "path", "grid", "repeat", "group"};
  out["t"] = Json(std::string(TYPE_NAMES[static_cast<size_t>(element.type)]));
  out["x"] = Json(x);
  out["y"] = Json(y);
  for (const auto& [key, expression] : element.numbers)
  {
    if (key == "x" || key == "y")
      continue;
    const std::optional<double> value = expression.Evaluate(scope).AsNumber();
    out[key] = value && std::isfinite(*value) ? Json(*value) : picojson::value();
  }
  for (const auto& [key, text] : element.props)
  {
    const std::string rendered = text.Render(scope);
    out[key] = Json(IsColorProp(key) ? ResolveColor(rendered) : rendered);
  }
  if (!element.viewbox.empty())
  {
    picojson::array viewbox;
    for (double v : element.viewbox)
      viewbox.push_back(Json(v));
    out["viewbox"] = picojson::value(std::move(viewbox));
  }
  if (element.type == ElementType::Bar)
  {
    const auto value = element.numbers.find("value");
    const auto max = element.numbers.find("max");
    out["fraction"] = JsonFraction(value->second.Evaluate(scope), max->second.Evaluate(scope));
  }
  if (!element.action.empty())
  {
    out["action"] = Json(element.action);
    if (!row_array.empty() && row >= 0)
    {
      out["source"] = Json(row_array);
      out["index"] = Json(static_cast<double>(row));
    }
  }

  if (element.type == ElementType::Grid)
  {
    const s64 cols = std::clamp<s64>(static_cast<s64>(number("cols").value_or(0)), 0, 256);
    const s64 rows = std::clamp<s64>(static_cast<s64>(number("rows").value_or(0)), 0, 256);
    const s64 view_cols =
        std::clamp<s64>(static_cast<s64>(number("view_cols").value_or(cols)), 1, std::max<s64>(cols, 1));
    const s64 view_rows =
        std::clamp<s64>(static_cast<s64>(number("view_rows").value_or(rows)), 1, std::max<s64>(rows, 1));
    const double center_x = number("center_x").value_or(view_cols / 2.0);
    const double center_y = number("center_y").value_or(view_rows / 2.0);
    const s64 left = std::clamp<s64>(static_cast<s64>(std::floor(center_x - view_cols / 2.0 + 0.5)),
                                     0, std::max<s64>(cols - view_cols, 0));
    const s64 top = std::clamp<s64>(static_cast<s64>(std::floor(center_y - view_rows / 2.0 + 0.5)),
                                    0, std::max<s64>(rows - view_rows, 0));
    out["cols"] = Json(static_cast<double>(view_cols));
    out["rows"] = Json(static_cast<double>(view_rows));
    out["left"] = Json(static_cast<double>(left));
    out["top"] = Json(static_cast<double>(top));
    out.erase("cell");

    // Cells: colour per cell from the "cell" template, as indices into a small palette.
    const auto cell_template = element.props.find("cell");
    const ArraySpec* spec = m_manifest.FindArray(element.source);
    const std::vector<ArrayElement>* cells = spec ? GetArray(element.source) : nullptr;
    if (cell_template != element.props.end() && cells && cols > 0)
    {
      std::vector<std::string> palette;
      std::vector<int> indices(static_cast<size_t>(view_cols * view_rows), -1);
      const GlobalScope& parent = *context.global;
      for (size_t i = 0; i < cells->size(); ++i)
      {
        const ArrayElement& cell = (*cells)[i];
        const s64 cx = static_cast<s64>(cell.slot) % cols - left;
        const s64 cy = static_cast<s64>(cell.slot) / cols - top;
        if (cx < 0 || cy < 0 || cx >= view_cols || cy >= view_rows)
          continue;
        const ElementScope cell_scope(parent, *spec, cell, static_cast<s64>(i));
        const std::string color = ResolveColor(cell_template->second.Render(cell_scope));
        if (color.empty())
          continue;
        auto it = std::ranges::find(palette, color);
        if (it == palette.end())
        {
          if (palette.size() >= 64)
            continue;
          palette.push_back(color);
          it = palette.end() - 1;
        }
        indices[static_cast<size_t>(cy * view_cols + cx)] = static_cast<int>(it - palette.begin());
      }
      picojson::array palette_json;
      for (const std::string& color : palette)
        palette_json.push_back(Json(color));
      picojson::array cells_json;
      for (int index : indices)
        cells_json.push_back(Json(static_cast<double>(index)));
      out["palette"] = picojson::value(std::move(palette_json));
      out["cells"] = picojson::value(std::move(cells_json));
    }

    // Marks: units, cursor and anything else placed in cell coordinates.
    picojson::array marks;
    const auto add_mark = [&](const GridLayerSpec& layer, const Scope& s, const std::string& source,
                              s64 index) {
      if (layer.where && !layer.where->Evaluate(s).Truthy())
        return;
      const std::optional<double> mx = layer.x.Evaluate(s).AsNumber();
      const std::optional<double> my = layer.y.Evaluate(s).AsNumber();
      if (!mx || !my)
        return;
      const double wx = *mx - static_cast<double>(left);
      const double wy = *my - static_cast<double>(top);
      if (wx < 0 || wy < 0 || wx >= static_cast<double>(view_cols) ||
          wy >= static_cast<double>(view_rows))
      {
        return;
      }
      picojson::object mark;
      mark["x"] = Json(wx);
      mark["y"] = Json(wy);
      for (const auto& [key, text] : layer.props)
      {
        const std::string rendered = text.Render(s);
        mark[key] = Json(IsColorProp(key) ? ResolveColor(rendered) : rendered);
      }
      if (layer.size)
      {
        if (const std::optional<double> size = layer.size->Evaluate(s).AsNumber())
          mark["size"] = Json(*size);
      }
      if (!layer.action.empty())
      {
        mark["action"] = Json(layer.action);
        if (!source.empty())
        {
          mark["source"] = Json(source);
          mark["index"] = Json(static_cast<double>(index));
        }
      }
      marks.emplace_back(std::move(mark));
    };
    for (const GridLayerSpec& layer : element.layers)
    {
      if (layer.source.empty())
      {
        add_mark(layer, scope, {}, -1);
        continue;
      }
      const ArraySpec* layer_spec = m_manifest.FindArray(layer.source);
      const std::vector<ArrayElement>* items = layer_spec ? GetArray(layer.source) : nullptr;
      if (!items)
        continue;
      const GlobalScope& parent = *context.global;
      for (size_t i = 0; i < items->size(); ++i)
      {
        const ElementScope item_scope(parent, *layer_spec, (*items)[i], static_cast<s64>(i));
        add_mark(layer, item_scope, layer.source, static_cast<s64>(i));
      }
    }
    out["marks"] = picojson::value(std::move(marks));
  }

  context.out.emplace_back(std::move(out));
}

picojson::value Evaluator::RenderWidget(const WidgetSpec& widget) const
{
  const GlobalScope scope(*this);
  picojson::object out;

  const auto render = [](const std::optional<Template>& text, const Scope& s) {
    return text ? Json(text->Render(s)) : picojson::value();
  };

  switch (widget.type)
  {
  case WidgetType::Heading:
  case WidgetType::Text:
    out["type"] = Json(std::string("text"));
    out["style"] = Json(widget.type == WidgetType::Heading ? std::string("heading") : widget.style);
    out["text"] = render(widget.text, scope);
    out["color"] = render(widget.color, scope);
    break;

  case WidgetType::Bar:
  {
    const Value value = widget.value ? widget.value->Evaluate(scope) : Value::Null();
    const Value max = widget.max ? widget.max->Evaluate(scope) : Value::Null();
    out["type"] = Json(std::string("bar"));
    out["label"] = render(widget.label, scope);
    out["text"] = render(widget.text, scope);
    out["fraction"] = JsonFraction(value, max);
    out["color"] = render(widget.color, scope);
    break;
  }

  case WidgetType::Stats:
  {
    out["type"] = Json(std::string("stats"));
    out["columns"] = Json(static_cast<double>(widget.columns));
    picojson::array items;
    for (const StatSpec& stat : widget.stats)
    {
      picojson::object item;
      item["label"] = Json(stat.label.Render(scope));
      item["value"] = Json(stat.value.Render(scope));
      items.emplace_back(std::move(item));
    }
    out["items"] = picojson::value(std::move(items));
    break;
  }

  case WidgetType::List:
  {
    out["type"] = Json(std::string("list"));
    out["source"] = Json(widget.source);
    out["action"] = widget.action.empty() ? picojson::value() : Json(widget.action);
    out["empty"] = render(widget.empty_text, scope);
    picojson::array rows;
    const ArraySpec* spec = m_manifest.FindArray(widget.source);
    const std::vector<ArrayElement>* elements = GetArray(widget.source);
    if (spec && elements)
    {
      for (size_t i = 0; i < elements->size(); ++i)
      {
        const ElementScope element_scope(scope, *spec, (*elements)[i], static_cast<s64>(i));
        picojson::object row;
        row["index"] = Json(static_cast<double>(i));
        row["text"] = render(widget.text, element_scope);
        row["detail"] = render(widget.detail, element_scope);
        row["badge"] = render(widget.badge, element_scope);
        row["color"] = render(widget.color, element_scope);
        if (widget.bar && widget.bar_max)
        {
          row["fraction"] = JsonFraction(widget.bar->Evaluate(element_scope),
                                         widget.bar_max->Evaluate(element_scope));
        }
        rows.emplace_back(std::move(row));
      }
    }
    out["rows"] = picojson::value(std::move(rows));
    break;
  }

  case WidgetType::Buttons:
  {
    out["type"] = Json(std::string("buttons"));
    picojson::array buttons;
    for (const ButtonSpec& button : widget.buttons)
    {
      picojson::object entry;
      entry["label"] = Json(button.label.Render(scope));
      entry["action"] = Json(button.action);
      entry["enabled"] = Json(!button.enabled_if || button.enabled_if->Evaluate(scope).Truthy());
      buttons.emplace_back(std::move(entry));
    }
    out["buttons"] = picojson::value(std::move(buttons));
    break;
  }

  case WidgetType::Map:
  {
    out["type"] = Json(std::string("map"));
    out["aspect"] = Json(static_cast<double>(widget.aspect));
    out["image"] = widget.image.empty() ? picojson::value() : Json(widget.image);

    std::array<double, 4> bounds{};
    bool bounds_valid = true;
    for (size_t i = 0; i < 4; ++i)
    {
      const std::optional<double> bound = widget.bounds[i].Evaluate(scope).AsNumber();
      bounds_valid = bounds_valid && bound.has_value();
      bounds[i] = bound.value_or(0.0);
    }
    const double width = bounds[2] - bounds[0];
    const double height = bounds[3] - bounds[1];
    bounds_valid = bounds_valid && width != 0.0 && height != 0.0;

    picojson::array points;
    const auto add_point = [&](const MapLayerSpec& layer, const Scope& s, s64 index) {
      if (layer.where && !layer.where->Evaluate(s).Truthy())
        return;
      const std::optional<double> x = layer.x.Evaluate(s).AsNumber();
      const std::optional<double> y = layer.y.Evaluate(s).AsNumber();
      if (!x || !y)
        return;
      const double nx = (*x - bounds[0]) / width;
      double ny = (*y - bounds[1]) / height;
      if (widget.flip_y)
        ny = 1.0 - ny;
      if (nx < -0.02 || nx > 1.02 || ny < -0.02 || ny > 1.02)
        return;
      picojson::object point;
      point["x"] = Json(std::clamp(nx, 0.0, 1.0));
      point["y"] = Json(std::clamp(ny, 0.0, 1.0));
      point["label"] = render(layer.label, s);
      point["color"] = render(layer.color, s);
      point["shape"] = Json(layer.shape);
      point["action"] = layer.action.empty() ? picojson::value() : Json(layer.action);
      point["source"] = layer.source.empty() ? picojson::value() : Json(layer.source);
      point["index"] = index >= 0 ? Json(static_cast<double>(index)) : picojson::value();
      points.emplace_back(std::move(point));
    };

    if (bounds_valid)
    {
      for (const MapLayerSpec& layer : widget.layers)
      {
        if (layer.source.empty())
        {
          add_point(layer, scope, -1);
          continue;
        }
        const ArraySpec* spec = m_manifest.FindArray(layer.source);
        const std::vector<ArrayElement>* elements = GetArray(layer.source);
        if (!spec || !elements)
          continue;
        for (size_t i = 0; i < elements->size(); ++i)
        {
          const ElementScope element_scope(scope, *spec, (*elements)[i], static_cast<s64>(i));
          add_point(layer, element_scope, static_cast<s64>(i));
        }
      }
    }
    out["points"] = picojson::value(std::move(points));
    break;
  }

  case WidgetType::Divider:
    out["type"] = Json(std::string("divider"));
    break;
  }
  return picojson::value(std::move(out));
}

std::optional<std::string> Evaluator::Write(MemorySource& memory, const ActionStep& step,
                                            std::string_view row_array, s64 row) const
{
  if (step.kind != ActionStep::Kind::Write)
    return "not a write step";

  const GlobalScope global(*this, {}, row);
  const ArraySpec* row_spec = row_array.empty() ? nullptr : m_manifest.FindArray(row_array);
  const std::vector<ArrayElement>* row_elements = row_spec ? GetArray(row_array) : nullptr;
  const bool has_row = row_elements && row >= 0 && row < static_cast<s64>(row_elements->size());

  std::optional<ElementScope> row_scope;
  if (has_row)
    row_scope.emplace(global, *row_spec, (*row_elements)[static_cast<size_t>(row)], row);
  const Scope& scope =
      row_scope ? static_cast<const Scope&>(*row_scope) : static_cast<const Scope&>(global);

  const FieldSpec* field = nullptr;
  u32 base = 0;
  if (step.field.empty())
  {
    field = m_manifest.FindValue(step.target);
  }
  else
  {
    const ArraySpec* spec = m_manifest.FindArray(step.target);
    const std::vector<ArrayElement>* elements = GetArray(step.target);
    if (!spec || !elements)
      return fmt::format("array \"{}\" has not been read yet", step.target);
    const std::optional<s64> index =
        step.index ? step.index->Evaluate(scope).AsInteger() : std::optional<s64>(row);
    if (!index || *index < 0 || *index >= static_cast<s64>(elements->size()))
      return fmt::format("no element {} in \"{}\"", index.value_or(-1), step.target);
    const auto it = std::ranges::find(spec->fields, step.field, &FieldSpec::name);
    if (it != spec->fields.end())
      field = &*it;
    base = (*elements)[static_cast<size_t>(*index)].address;
  }
  if (!field || field->computed || field->type == MemoryType::String)
    return fmt::format("\"{}\" is not writable", step.target);

  const std::optional<double> number = step.value.Evaluate(scope).AsNumber();
  if (!number)
    return fmt::format("value \"{}\" did not evaluate to a number", step.value.Source());

  const std::optional<u32> address = ResolveAddress(memory, base + field->address, field->offsets);
  if (!address)
    return "pointer chain did not resolve";

  const u32 size = TypeSize(field->type);
  u64 raw = 0;
  switch (field->type)
  {
  case MemoryType::F32:
    raw = std::bit_cast<u32>(static_cast<float>(*number));
    break;
  case MemoryType::F64:
    raw = std::bit_cast<u64>(*number);
    break;
  case MemoryType::Bool:
    raw = *number != 0.0 ? 1 : 0;
    break;
  default:
  {
    // Clamp to the type's range rather than wrapping, so "add 10 to HP" at 250 becomes 255
    // instead of 4.
    double low = 0, high = 0;
    switch (field->type)
    {
    case MemoryType::U8:
      high = 0xFF;
      break;
    case MemoryType::S8:
      low = -128;
      high = 127;
      break;
    case MemoryType::U16:
      high = 0xFFFF;
      break;
    case MemoryType::S16:
      low = -32768;
      high = 32767;
      break;
    case MemoryType::U32:
      high = 4294967295.0;
      break;
    case MemoryType::S32:
      low = -2147483648.0;
      high = 2147483647.0;
      break;
    default:
      break;
    }
    if (field->mask)
    {
      // A masked field can only hold as many bits as the mask has above the shift.
      const u64 field_max = *field->mask >> field->shift;
      low = 0;
      high = std::min(high, static_cast<double>(field_max));
    }
    const s64 clamped = static_cast<s64>(std::clamp(std::round(*number), low, high));
    raw = static_cast<u64>(clamped) & (size == 8 ? ~u64{0} : ((u64{1} << (size * 8)) - 1));
    break;
  }
  }

  if (field->mask && field->type != MemoryType::F32 && field->type != MemoryType::F64)
  {
    const std::optional<u64> current = memory.Read(*address, size);
    if (!current)
      return "could not read the current value to merge the mask";
    raw = (*current & ~*field->mask) | ((raw << field->shift) & *field->mask);
  }
  else if (field->shift)
  {
    raw <<= field->shift;
  }

  if (!memory.Write(*address, size, raw))
    return fmt::format("write to 0x{:08X} failed", *address);
  return std::nullopt;
}
}  // namespace Duo
