// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Evaluator.h"

#include <algorithm>
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
    return it == m_evaluator.m_values.end() ? Value::Null() : it->second;
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
    Value value =
        field.computed ? field.expression.Evaluate(scope) : ReadField(memory, field, 0, m_decoder);
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

  const std::optional<u32> base = ResolveAddress(memory, spec.address, spec.offsets);
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

picojson::value Evaluator::Render(std::string& page_id) const
{
  picojson::object result;
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
  return picojson::value(std::move(result));
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
