// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Manifest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <utility>

#include <fmt/format.h>
#include <picojson.h>

namespace Duo
{
namespace
{
constexpr std::pair<std::string_view, Control> CONTROL_NAMES[] = {
    {"A", Control::GC_A},
    {"B", Control::GC_B},
    {"X", Control::GC_X},
    {"Y", Control::GC_Y},
    {"Z", Control::GC_Z},
    {"START", Control::GC_START},
    {"UP", Control::GC_DPAD_UP},
    {"DOWN", Control::GC_DPAD_DOWN},
    {"LEFT", Control::GC_DPAD_LEFT},
    {"RIGHT", Control::GC_DPAD_RIGHT},
    {"L", Control::GC_L},
    {"R", Control::GC_R},
    {"L_ANALOG", Control::GC_L_ANALOG},
    {"R_ANALOG", Control::GC_R_ANALOG},
    {"STICK_X", Control::GC_STICK_X},
    {"STICK_Y", Control::GC_STICK_Y},
    {"C_STICK_X", Control::GC_C_STICK_X},
    {"C_STICK_Y", Control::GC_C_STICK_Y},
    {"WII_A", Control::WII_A},
    {"WII_B", Control::WII_B},
    {"WII_1", Control::WII_1},
    {"WII_2", Control::WII_2},
    {"WII_PLUS", Control::WII_PLUS},
    {"WII_MINUS", Control::WII_MINUS},
    {"WII_HOME", Control::WII_HOME},
    {"WII_UP", Control::WII_DPAD_UP},
    {"WII_DOWN", Control::WII_DPAD_DOWN},
    {"WII_LEFT", Control::WII_DPAD_LEFT},
    {"WII_RIGHT", Control::WII_DPAD_RIGHT},
    {"WII_IR_X", Control::WII_IR_X},
    {"WII_IR_Y", Control::WII_IR_Y},
    {"NUNCHUK_C", Control::NUNCHUK_C},
    {"NUNCHUK_Z", Control::NUNCHUK_Z},
    {"NUNCHUK_STICK_X", Control::NUNCHUK_STICK_X},
    {"NUNCHUK_STICK_Y", Control::NUNCHUK_STICK_Y},
    {"CLASSIC_A", Control::CLASSIC_A},
    {"CLASSIC_B", Control::CLASSIC_B},
    {"CLASSIC_X", Control::CLASSIC_X},
    {"CLASSIC_Y", Control::CLASSIC_Y},
    {"CLASSIC_ZL", Control::CLASSIC_ZL},
    {"CLASSIC_ZR", Control::CLASSIC_ZR},
    {"CLASSIC_PLUS", Control::CLASSIC_PLUS},
    {"CLASSIC_MINUS", Control::CLASSIC_MINUS},
    {"CLASSIC_HOME", Control::CLASSIC_HOME},
    {"CLASSIC_UP", Control::CLASSIC_DPAD_UP},
    {"CLASSIC_DOWN", Control::CLASSIC_DPAD_DOWN},
    {"CLASSIC_LEFT", Control::CLASSIC_DPAD_LEFT},
    {"CLASSIC_RIGHT", Control::CLASSIC_DPAD_RIGHT},
    {"CLASSIC_L", Control::CLASSIC_L},
    {"CLASSIC_R", Control::CLASSIC_R},
    {"CLASSIC_L_ANALOG", Control::CLASSIC_L_ANALOG},
    {"CLASSIC_R_ANALOG", Control::CLASSIC_R_ANALOG},
    {"CLASSIC_LEFT_STICK_X", Control::CLASSIC_LEFT_STICK_X},
    {"CLASSIC_LEFT_STICK_Y", Control::CLASSIC_LEFT_STICK_Y},
    {"CLASSIC_RIGHT_STICK_X", Control::CLASSIC_RIGHT_STICK_X},
    {"CLASSIC_RIGHT_STICK_Y", Control::CLASSIC_RIGHT_STICK_Y},
};

bool IsIdentifier(std::string_view name)
{
  if (name.empty() || name.size() > 64)
    return false;
  if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')
    return false;
  return std::ranges::all_of(
      name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

bool IsReservedName(std::string_view name)
{
  return name == "true" || name == "false" || name == "null";
}

// Reads typed fields out of a JSON object, collecting errors with their location.
class Reader
{
public:
  explicit Reader(std::vector<std::string>* errors) : m_errors(errors) {}

  void Error(std::string_view path, std::string_view message)
  {
    m_errors->push_back(fmt::format("{}: {}", path, message));
  }

  const picojson::value* Get(const picojson::object& object, const std::string& key)
  {
    const auto it = object.find(key);
    if (it == object.end() || it->second.is<picojson::null>())
      return nullptr;
    return &it->second;
  }

  std::optional<std::string> String(const picojson::object& object, const std::string& key,
                                    std::string_view path, bool required)
  {
    const picojson::value* value = Get(object, key);
    if (!value)
    {
      if (required)
        Error(path, fmt::format("missing required \"{}\"", key));
      return std::nullopt;
    }
    if (!value->is<std::string>())
    {
      Error(path, fmt::format("\"{}\" must be a string", key));
      return std::nullopt;
    }
    return value->get<std::string>();
  }

  // Accepts JSON numbers and strings like "0x80123456" or "1234".
  std::optional<u64> Unsigned(const picojson::value& value, std::string_view path,
                              std::string_view what)
  {
    if (value.is<double>())
    {
      const double number = value.get<double>();
      if (number < 0 || number != std::trunc(number) || number > 18446744073709551615.0)
      {
        Error(path, fmt::format("{} must be a non-negative integer", what));
        return std::nullopt;
      }
      return static_cast<u64>(number);
    }
    if (value.is<std::string>())
    {
      std::string text = value.get<std::string>();
      text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
      const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
      const std::string digits = hex ? text.substr(2) : text;
      const bool valid = !digits.empty() && std::ranges::all_of(digits, [hex](char c) {
        return hex ? std::isxdigit(static_cast<unsigned char>(c)) != 0 :
                     std::isdigit(static_cast<unsigned char>(c)) != 0;
      });
      if (!valid || digits.size() > 16)
      {
        Error(path, fmt::format("{} \"{}\" is not a valid number", what, value.get<std::string>()));
        return std::nullopt;
      }
      return std::strtoull(digits.c_str(), nullptr, hex ? 16 : 10);
    }
    Error(path, fmt::format("{} must be a number or a \"0x...\" string", what));
    return std::nullopt;
  }

  std::optional<u32> U32(const picojson::object& object, const std::string& key,
                         std::string_view path, bool required,
                         std::optional<u32> default_value = {})
  {
    const picojson::value* value = Get(object, key);
    if (!value)
    {
      if (required)
        Error(path, fmt::format("missing required \"{}\"", key));
      return default_value;
    }
    const std::optional<u64> number = Unsigned(*value, path, fmt::format("\"{}\"", key));
    if (!number)
      return std::nullopt;
    if (*number > 0xFFFFFFFFull)
    {
      Error(path, fmt::format("\"{}\" does not fit in 32 bits", key));
      return std::nullopt;
    }
    return static_cast<u32>(*number);
  }

  bool Bool(const picojson::object& object, const std::string& key, std::string_view path,
            bool default_value)
  {
    const picojson::value* value = Get(object, key);
    if (!value)
      return default_value;
    if (!value->is<bool>())
    {
      Error(path, fmt::format("\"{}\" must be true or false", key));
      return default_value;
    }
    return value->get<bool>();
  }

  std::optional<Expression> Expr(const picojson::value& value, std::string_view path,
                                 std::string_view what)
  {
    if (value.is<double>() || value.is<bool>())
      return Expression::Constant(value.is<bool>() ? Value(value.get<bool>()) :
                                                     Value(value.get<double>()));
    if (!value.is<std::string>())
    {
      Error(path, fmt::format("{} must be an expression string", what));
      return std::nullopt;
    }
    std::string error;
    std::optional<Expression> expression = Expression::Parse(value.get<std::string>(), &error);
    if (!expression)
      Error(path, fmt::format("{} \"{}\": {}", what, value.get<std::string>(), error));
    return expression;
  }

  std::optional<Expression> OptionalExpr(const picojson::object& object, const std::string& key,
                                         std::string_view path)
  {
    const picojson::value* value = Get(object, key);
    if (!value)
      return std::nullopt;
    return Expr(*value, path, fmt::format("\"{}\"", key));
  }

  std::optional<Template> OptionalTemplate(const picojson::object& object, const std::string& key,
                                           std::string_view path)
  {
    const picojson::value* value = Get(object, key);
    if (!value)
      return std::nullopt;
    if (!value->is<std::string>())
    {
      Error(path, fmt::format("\"{}\" must be a text template string", key));
      return std::nullopt;
    }
    std::string error;
    std::optional<Template> result = Template::Parse(value->get<std::string>(), &error);
    if (!result)
      Error(path, fmt::format("\"{}\": {}", key, error));
    return result;
  }

  const picojson::array* Array(const picojson::object& object, const std::string& key,
                               std::string_view path)
  {
    const picojson::value* value = Get(object, key);
    if (!value)
      return nullptr;
    if (!value->is<picojson::array>())
    {
      Error(path, fmt::format("\"{}\" must be an array", key));
      return nullptr;
    }
    return &value->get<picojson::array>();
  }

  const picojson::object* Object(const picojson::value& value, std::string_view path)
  {
    if (!value.is<picojson::object>())
    {
      Error(path, "must be an object");
      return nullptr;
    }
    return &value.get<picojson::object>();
  }

  // Warns about keys this version does not understand, which are almost always typos.
  void CheckKeys(const picojson::object& object, std::string_view path,
                 std::initializer_list<std::string_view> known)
  {
    for (const auto& [key, value] : object)
    {
      if (key.starts_with("//") || key.starts_with('_'))
        continue;  // Comments.
      if (std::ranges::find(known, key) == known.end())
        Error(path, fmt::format("unknown key \"{}\"", key));
    }
  }

private:
  std::vector<std::string>* m_errors;
};

std::optional<MemoryType> ParseMemoryType(std::string_view type)
{
  static constexpr std::pair<std::string_view, MemoryType> types[] = {
      {"u8", MemoryType::U8},         {"s8", MemoryType::S8},   {"u16", MemoryType::U16},
      {"s16", MemoryType::S16},       {"u32", MemoryType::U32}, {"s32", MemoryType::S32},
      {"f32", MemoryType::F32},       {"f64", MemoryType::F64}, {"bool", MemoryType::Bool},
      {"string", MemoryType::String},
  };
  for (const auto& [name, value] : types)
  {
    if (name == type)
      return value;
  }
  return std::nullopt;
}

std::optional<FieldSpec> ParseField(Reader& r, const picojson::value& json, std::string_view path,
                                    bool is_array_field)
{
  const picojson::object* object = r.Object(json, path);
  if (!object)
    return std::nullopt;

  r.CheckKeys(*object, path,
              {"name", "type", "address", "offset", "offsets", "mask", "shift", "length",
               "encoding", "expr", "address_expr"});

  FieldSpec field;
  const std::optional<std::string> name = r.String(*object, "name", path, true);
  if (!name)
    return std::nullopt;
  const std::string field_path = fmt::format("{} \"{}\"", path, *name);
  if (!IsIdentifier(*name) || IsReservedName(*name))
  {
    r.Error(path, fmt::format("\"{}\" is not a valid name (letters, digits and _ only)", *name));
    return std::nullopt;
  }
  field.name = *name;

  if (const picojson::value* expr = r.Get(*object, "expr"))
  {
    if (r.Get(*object, "type") || r.Get(*object, "address") || r.Get(*object, "offset"))
      r.Error(field_path, "a computed value (\"expr\") cannot also have a type or address");
    std::optional<Expression> expression = r.Expr(*expr, field_path, "\"expr\"");
    if (!expression)
      return std::nullopt;
    field.computed = true;
    field.expression = std::move(*expression);
    return field;
  }

  const std::optional<std::string> type_name = r.String(*object, "type", field_path, true);
  if (!type_name)
    return std::nullopt;
  const std::optional<MemoryType> type = ParseMemoryType(*type_name);
  if (!type)
  {
    r.Error(field_path, fmt::format("unknown type \"{}\" (expected u8, s8, u16, s16, u32, s32, "
                                    "f32, f64, bool or string)",
                                    *type_name));
    return std::nullopt;
  }
  field.type = *type;

  const std::string address_key = is_array_field ? "offset" : "address";
  if (const picojson::value* address_expr = r.Get(*object, "address_expr"))
  {
    if (r.Get(*object, address_key))
      r.Error(field_path, fmt::format("use either \"{}\" or \"address_expr\"", address_key));
    field.address_expression = r.Expr(*address_expr, field_path, "\"address_expr\"");
    if (!field.address_expression)
      return std::nullopt;
  }
  else
  {
    const std::optional<u32> address = r.U32(*object, address_key, field_path, true);
    if (!address)
      return std::nullopt;
    field.address = *address;
  }

  if (const picojson::array* offsets = r.Array(*object, "offsets", field_path))
  {
    if (offsets->size() > MAX_POINTER_DEPTH)
      r.Error(field_path,
              fmt::format("pointer chains are limited to {} offsets", MAX_POINTER_DEPTH));
    for (const picojson::value& offset : *offsets)
    {
      const std::optional<u64> value = r.Unsigned(offset, field_path, "offset");
      if (!value)
        return std::nullopt;
      field.offsets.push_back(static_cast<u32>(*value));
    }
  }

  if (const picojson::value* mask = r.Get(*object, "mask"))
  {
    field.mask = r.Unsigned(*mask, field_path, "\"mask\"");
    if (!field.mask)
      return std::nullopt;
  }
  field.shift = r.U32(*object, "shift", field_path, false, 0).value_or(0);
  if (field.shift > 63)
    r.Error(field_path, "\"shift\" must be below 64");
  if ((field.mask || field.shift) &&
      (field.type == MemoryType::F32 || field.type == MemoryType::F64 ||
       field.type == MemoryType::String))
  {
    r.Error(field_path, "\"mask\"/\"shift\" only apply to integer and bool types");
  }

  if (field.type == MemoryType::String)
  {
    field.length = r.U32(*object, "length", field_path, true).value_or(0);
    if (field.length == 0 || field.length > MAX_STRING_LENGTH)
      r.Error(field_path, fmt::format("string \"length\" must be 1-{}", MAX_STRING_LENGTH));
    if (const std::optional<std::string> encoding =
            r.String(*object, "encoding", field_path, false))
    {
      if (*encoding == "ascii" || *encoding == "utf8")
        field.encoding = TextEncoding::Ascii;
      else if (*encoding == "sjis" || *encoding == "shift_jis")
        field.encoding = TextEncoding::ShiftJIS;
      else if (*encoding == "utf16" || *encoding == "utf16be")
        field.encoding = TextEncoding::Utf16BE;
      else
        r.Error(field_path,
                fmt::format("unknown encoding \"{}\" (ascii, sjis or utf16)", *encoding));
    }
  }
  return field;
}

std::optional<ArraySpec> ParseArray(Reader& r, const picojson::value& json, std::string_view path)
{
  const picojson::object* object = r.Object(json, path);
  if (!object)
    return std::nullopt;
  r.CheckKeys(*object, path,
              {"name", "address", "address_expr", "offsets", "stride", "pointer_table", "count",
               "count_expr", "max_count", "where", "fields"});

  ArraySpec array;
  const std::optional<std::string> name = r.String(*object, "name", path, true);
  if (!name)
    return std::nullopt;
  if (!IsIdentifier(*name) || IsReservedName(*name))
  {
    r.Error(path, fmt::format("\"{}\" is not a valid name", *name));
    return std::nullopt;
  }
  array.name = *name;
  const std::string array_path = fmt::format("array \"{}\"", *name);

  std::optional<u32> address;
  if (const picojson::value* address_expr = r.Get(*object, "address_expr"))
  {
    if (r.Get(*object, "address"))
      r.Error(array_path, "use either \"address\" or \"address_expr\"");
    array.address_expression = r.Expr(*address_expr, array_path, "\"address_expr\"");
    if (!array.address_expression)
      return std::nullopt;
    address = 0;
  }
  else
  {
    address = r.U32(*object, "address", array_path, true);
  }
  const std::optional<u32> stride = r.U32(*object, "stride", array_path, true);
  if (!address || !stride)
    return std::nullopt;
  array.address = *address;
  array.stride = *stride;
  array.pointer_table = r.Bool(*object, "pointer_table", array_path, false);
  if (array.stride == 0)
    r.Error(array_path, "\"stride\" must be greater than 0");

  if (const picojson::array* offsets = r.Array(*object, "offsets", array_path))
  {
    for (const picojson::value& offset : *offsets)
    {
      if (const std::optional<u64> value = r.Unsigned(offset, array_path, "offset"))
        array.offsets.push_back(static_cast<u32>(*value));
    }
  }

  array.max_count = r.U32(*object, "max_count", array_path, false, 256).value_or(256);
  if (array.max_count == 0 || array.max_count > MAX_ARRAY_ELEMENTS)
  {
    r.Error(array_path, fmt::format("\"max_count\" must be 1-{}", MAX_ARRAY_ELEMENTS));
    array.max_count = std::clamp<u32>(array.max_count, 1, MAX_ARRAY_ELEMENTS);
  }

  array.count_expression = r.OptionalExpr(*object, "count_expr", array_path);
  if (!array.count_expression)
  {
    const std::optional<u32> count = r.U32(*object, "count", array_path, true);
    if (!count)
      return std::nullopt;
    array.count = *count;
    if (array.count > array.max_count)
      r.Error(array_path, fmt::format("\"count\" exceeds \"max_count\" ({})", array.max_count));
  }
  array.where = r.OptionalExpr(*object, "where", array_path);

  const picojson::array* fields = r.Array(*object, "fields", array_path);
  if (!fields || fields->empty())
  {
    r.Error(array_path, "needs a non-empty \"fields\" list");
    return std::nullopt;
  }
  std::set<std::string> names;
  for (size_t i = 0; i < fields->size(); ++i)
  {
    std::optional<FieldSpec> field =
        ParseField(r, (*fields)[i], fmt::format("{} field #{}", array_path, i + 1), true);
    if (!field)
      continue;
    if (!names.insert(field->name).second)
      r.Error(array_path, fmt::format("duplicate field \"{}\"", field->name));
    array.fields.push_back(std::move(*field));
  }
  return array;
}

std::optional<MapLayerSpec> ParseMapLayer(Reader& r, const picojson::value& json,
                                          std::string_view path)
{
  const picojson::object* object = r.Object(json, path);
  if (!object)
    return std::nullopt;
  r.CheckKeys(*object, path, {"source", "x", "y", "label", "color", "shape", "where", "action"});

  MapLayerSpec layer;
  layer.source = r.String(*object, "source", path, false).value_or("");
  const picojson::value* x = r.Get(*object, "x");
  const picojson::value* y = r.Get(*object, "y");
  if (!x || !y)
  {
    r.Error(path, "map points need \"x\" and \"y\"");
    return std::nullopt;
  }
  std::optional<Expression> x_expr = r.Expr(*x, path, "\"x\"");
  std::optional<Expression> y_expr = r.Expr(*y, path, "\"y\"");
  if (!x_expr || !y_expr)
    return std::nullopt;
  layer.x = std::move(*x_expr);
  layer.y = std::move(*y_expr);
  layer.label = r.OptionalTemplate(*object, "label", path);
  layer.color = r.OptionalTemplate(*object, "color", path);
  layer.shape = r.String(*object, "shape", path, false).value_or("dot");
  if (layer.shape != "dot" && layer.shape != "player" && layer.shape != "square" &&
      layer.shape != "diamond")
  {
    r.Error(path, fmt::format("unknown shape \"{}\" (dot, player, square, diamond)", layer.shape));
  }
  layer.where = r.OptionalExpr(*object, "where", path);
  layer.action = r.String(*object, "action", path, false).value_or("");
  return layer;
}

std::optional<WidgetSpec> ParseWidget(Reader& r, const picojson::value& json, std::string_view path)
{
  const picojson::object* object = r.Object(json, path);
  if (!object)
    return std::nullopt;

  WidgetSpec widget;
  const std::optional<std::string> type = r.String(*object, "type", path, true);
  if (!type)
    return std::nullopt;
  widget.visible_if = r.OptionalExpr(*object, "visible_if", path);
  const std::string widget_path = fmt::format("{} ({})", path, *type);

  if (*type == "heading" || *type == "text")
  {
    r.CheckKeys(*object, widget_path, {"type", "visible_if", "text", "style", "color"});
    widget.type = *type == "heading" ? WidgetType::Heading : WidgetType::Text;
    widget.text = r.OptionalTemplate(*object, "text", widget_path);
    if (!widget.text)
      r.Error(widget_path, "needs \"text\"");
    widget.style = r.String(*object, "style", widget_path, false).value_or("body");
    if (widget.style != "body" && widget.style != "caption" && widget.style != "title" &&
        widget.style != "mono")
    {
      r.Error(widget_path,
              fmt::format("unknown style \"{}\" (body, caption, title, mono)", widget.style));
    }
    widget.color = r.OptionalTemplate(*object, "color", widget_path);
  }
  else if (*type == "bar")
  {
    r.CheckKeys(*object, widget_path,
                {"type", "visible_if", "label", "value", "max", "text", "color"});
    widget.type = WidgetType::Bar;
    widget.label = r.OptionalTemplate(*object, "label", widget_path);
    widget.value = r.OptionalExpr(*object, "value", widget_path);
    widget.max = r.OptionalExpr(*object, "max", widget_path);
    if (!widget.value || !widget.max)
      r.Error(widget_path, "needs \"value\" and \"max\"");
    widget.text = r.OptionalTemplate(*object, "text", widget_path);
    widget.color = r.OptionalTemplate(*object, "color", widget_path);
  }
  else if (*type == "stats")
  {
    r.CheckKeys(*object, widget_path, {"type", "visible_if", "columns", "items"});
    widget.type = WidgetType::Stats;
    widget.columns = r.U32(*object, "columns", widget_path, false, 2).value_or(2);
    if (widget.columns == 0 || widget.columns > 6)
      r.Error(widget_path, "\"columns\" must be 1-6");
    const picojson::array* items = r.Array(*object, "items", widget_path);
    if (!items || items->empty())
      r.Error(widget_path, "needs a non-empty \"items\" list");
    else
    {
      for (size_t i = 0; i < items->size(); ++i)
      {
        const std::string item_path = fmt::format("{} item #{}", widget_path, i + 1);
        const picojson::object* item = r.Object((*items)[i], item_path);
        if (!item)
          continue;
        r.CheckKeys(*item, item_path, {"label", "value"});
        std::optional<Template> label = r.OptionalTemplate(*item, "label", item_path);
        std::optional<Template> value = r.OptionalTemplate(*item, "value", item_path);
        if (!label || !value)
        {
          r.Error(item_path, "needs \"label\" and \"value\"");
          continue;
        }
        widget.stats.push_back(StatSpec{std::move(*label), std::move(*value)});
      }
    }
  }
  else if (*type == "list")
  {
    r.CheckKeys(*object, widget_path,
                {"type", "visible_if", "source", "text", "detail", "bar", "bar_max", "badge",
                 "color", "action", "empty"});
    widget.type = WidgetType::List;
    widget.source = r.String(*object, "source", widget_path, true).value_or("");
    widget.text = r.OptionalTemplate(*object, "text", widget_path);
    if (!widget.text)
      r.Error(widget_path, "needs \"text\"");
    widget.detail = r.OptionalTemplate(*object, "detail", widget_path);
    widget.bar = r.OptionalExpr(*object, "bar", widget_path);
    widget.bar_max = r.OptionalExpr(*object, "bar_max", widget_path);
    if (widget.bar.has_value() != widget.bar_max.has_value())
      r.Error(widget_path, "\"bar\" and \"bar_max\" go together");
    widget.badge = r.OptionalTemplate(*object, "badge", widget_path);
    widget.color = r.OptionalTemplate(*object, "color", widget_path);
    widget.action = r.String(*object, "action", widget_path, false).value_or("");
    widget.empty_text = r.OptionalTemplate(*object, "empty", widget_path);
  }
  else if (*type == "buttons")
  {
    r.CheckKeys(*object, widget_path, {"type", "visible_if", "buttons"});
    widget.type = WidgetType::Buttons;
    const picojson::array* buttons = r.Array(*object, "buttons", widget_path);
    if (!buttons || buttons->empty())
      r.Error(widget_path, "needs a non-empty \"buttons\" list");
    else
    {
      for (size_t i = 0; i < buttons->size(); ++i)
      {
        const std::string button_path = fmt::format("{} button #{}", widget_path, i + 1);
        const picojson::object* button = r.Object((*buttons)[i], button_path);
        if (!button)
          continue;
        r.CheckKeys(*button, button_path, {"label", "action", "enabled_if"});
        std::optional<Template> label = r.OptionalTemplate(*button, "label", button_path);
        std::optional<std::string> action = r.String(*button, "action", button_path, true);
        if (!label || !action)
        {
          if (!label)
            r.Error(button_path, "needs \"label\"");
          continue;
        }
        widget.buttons.push_back(ButtonSpec{std::move(*label), *action,
                                            r.OptionalExpr(*button, "enabled_if", button_path)});
      }
    }
  }
  else if (*type == "map")
  {
    r.CheckKeys(*object, widget_path,
                {"type", "visible_if", "bounds", "flip_y", "image", "points", "aspect"});
    widget.type = WidgetType::Map;
    const picojson::array* bounds = r.Array(*object, "bounds", widget_path);
    if (!bounds || bounds->size() != 4)
      r.Error(widget_path, "needs \"bounds\": [min_x, min_y, max_x, max_y]");
    else
    {
      for (size_t i = 0; i < 4; ++i)
      {
        if (std::optional<Expression> bound = r.Expr((*bounds)[i], widget_path, "bound"))
          widget.bounds[i] = std::move(*bound);
      }
    }
    widget.flip_y = r.Bool(*object, "flip_y", widget_path, false);
    widget.image = r.String(*object, "image", widget_path, false).value_or("");
    if (widget.image.find("..") != std::string::npos || widget.image.starts_with('/'))
      r.Error(widget_path, "\"image\" must be a relative path inside the package");
    if (const picojson::value* aspect = r.Get(*object, "aspect"))
    {
      if (!aspect->is<double>() || aspect->get<double>() <= 0.05 || aspect->get<double>() > 20)
        r.Error(widget_path, "\"aspect\" must be a number between 0.05 and 20");
      else
        widget.aspect = static_cast<float>(aspect->get<double>());
    }
    const picojson::array* points = r.Array(*object, "points", widget_path);
    if (!points || points->empty())
      r.Error(widget_path, "needs a non-empty \"points\" list");
    else
    {
      for (size_t i = 0; i < points->size(); ++i)
      {
        if (std::optional<MapLayerSpec> layer =
                ParseMapLayer(r, (*points)[i], fmt::format("{} point #{}", widget_path, i + 1)))
        {
          widget.layers.push_back(std::move(*layer));
        }
      }
    }
  }
  else if (*type == "divider")
  {
    r.CheckKeys(*object, widget_path, {"type", "visible_if"});
    widget.type = WidgetType::Divider;
  }
  else
  {
    r.Error(path, fmt::format("unknown widget type \"{}\" (heading, text, bar, stats, list, "
                              "buttons, map, divider)",
                              *type));
    return std::nullopt;
  }
  return widget;
}

std::optional<ActionStep> ParseStep(Reader& r, const picojson::value& json, std::string_view path)
{
  const picojson::object* object = r.Object(json, path);
  if (!object)
    return std::nullopt;

  ActionStep step;
  if (const std::optional<std::string> control = r.String(*object, "press", path, false))
  {
    r.CheckKeys(*object, path, {"press", "frames", "amount"});
    step.kind = ActionStep::Kind::Press;
    const std::optional<Control> parsed = ParseControl(*control);
    if (!parsed)
    {
      r.Error(path, fmt::format("unknown control \"{}\"", *control));
      return std::nullopt;
    }
    step.control = *parsed;
    step.frames = r.U32(*object, "frames", path, false, 4).value_or(4);
    if (step.frames == 0 || step.frames > 600)
      r.Error(path, "\"frames\" must be 1-600");
    if (const picojson::value* amount = r.Get(*object, "amount"))
    {
      if (!amount->is<double>() || amount->get<double>() < -1.0 || amount->get<double>() > 1.0)
        r.Error(path, "\"amount\" must be a number from -1 to 1");
      else
        step.amount = amount->get<double>();
    }
    return step;
  }
  if (const picojson::value* wait = r.Get(*object, "wait"))
  {
    r.CheckKeys(*object, path, {"wait"});
    step.kind = ActionStep::Kind::Wait;
    const std::optional<u64> frames = r.Unsigned(*wait, path, "\"wait\"");
    if (!frames || *frames == 0 || *frames > 3600)
    {
      r.Error(path, "\"wait\" must be 1-3600 frames");
      return std::nullopt;
    }
    step.frames = static_cast<u32>(*frames);
    return step;
  }
  if (const std::optional<std::string> target = r.String(*object, "write", path, false))
  {
    r.CheckKeys(*object, path, {"write", "value", "index"});
    step.kind = ActionStep::Kind::Write;
    // "name" for a value, "array[].field" for an array element field.
    const size_t brackets = target->find("[].");
    if (brackets != std::string::npos)
    {
      step.target = target->substr(0, brackets);
      step.field = target->substr(brackets + 3);
      step.index = r.OptionalExpr(*object, "index", path);
      if (!step.index)
        step.index = Expression::Parse("$index");
    }
    else
    {
      step.target = *target;
    }
    const picojson::value* value = r.Get(*object, "value");
    if (!value)
    {
      r.Error(path, "a write needs a \"value\"");
      return std::nullopt;
    }
    std::optional<Expression> expression = r.Expr(*value, path, "\"value\"");
    if (!expression)
      return std::nullopt;
    step.value = std::move(*expression);
    return step;
  }
  if (const std::optional<std::string> page = r.String(*object, "page", path, false))
  {
    r.CheckKeys(*object, path, {"page"});
    step.kind = ActionStep::Kind::Page;
    step.page = *page;
    return step;
  }
  if (const std::optional<std::string> target = r.String(*object, "set", path, false))
  {
    r.CheckKeys(*object, path, {"set", "value"});
    step.kind = ActionStep::Kind::Set;
    step.target = *target;
    const picojson::value* value = r.Get(*object, "value");
    if (!value)
    {
      r.Error(path, "a set step needs a \"value\"");
      return std::nullopt;
    }
    std::optional<Expression> expression = r.Expr(*value, path, "\"value\"");
    if (!expression)
      return std::nullopt;
    step.value = std::move(*expression);
    return step;
  }
  r.Error(path, "a step needs one of \"press\", \"wait\", \"write\", \"page\" or \"set\"");
  return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Canvas elements

constexpr std::pair<std::string_view, ElementType> ELEMENT_TYPES[] = {
    {"rect", ElementType::Rect},     {"frame", ElementType::Frame}, {"text", ElementType::Text},
    {"bar", ElementType::Bar},       {"pips", ElementType::Pips},   {"image", ElementType::Image},
    {"path", ElementType::Path},     {"grid", ElementType::Grid},   {"repeat", ElementType::Repeat},
    {"group", ElementType::Group},
};

// Numeric element properties, parsed as expressions.
constexpr std::string_view ELEMENT_NUMBERS[] = {
    "x",         "y",          "w",          "h",        "radius",       "stroke_width",
    "value",     "max",        "count",      "filled",   "size",         "opacity",
    "dx",        "dy",         "columns",    "limit",    "start",        "cols",
    "rows",      "view_cols",  "view_rows",  "center_x", "center_y",     "line_height",
    "trim_width", "inset",     "corner_size", "gap",     "outline_width", "shadow_dx",
    "shadow_dy", "min_size",   "fit",        "rotate",   "letter_spacing", "grid_width",
    "slice_scale", "flip_x",
};

// Text element properties, parsed as templates.
constexpr std::string_view ELEMENT_TEXTS[] = {
    "text",   "fill",   "fill2",  "stroke", "color",  "font",    "align", "valign",
    "src",    "d",      "shape",  "corner", "trim",   "edge",    "track", "track2",
    "off",    "shadow", "outline", "cell",  "weight", "tint",    "grid_color", "glow",
    "fill_angle", "case",
};

constexpr std::string_view ELEMENT_STRUCTURE[] = {"type",    "visible_if", "action", "source",
                                                  "elements", "layers",    "viewbox", "style",
                                                  "where",   "crop",       "slice"};

bool IsElementNumber(std::string_view key)
{
  return std::ranges::find(ELEMENT_NUMBERS, key) != std::end(ELEMENT_NUMBERS);
}

bool IsElementText(std::string_view key)
{
  return std::ranges::find(ELEMENT_TEXTS, key) != std::end(ELEMENT_TEXTS);
}

std::optional<ElementSpec> ParseElement(Reader& r, const picojson::value& json,
                                        std::string_view path,
                                        const std::map<std::string, picojson::object>& styles,
                                        int depth = 0);

std::vector<ElementSpec> ParseElementList(Reader& r, const picojson::value& json,
                                          std::string_view path,
                                          const std::map<std::string, picojson::object>& styles,
                                          int depth)
{
  std::vector<ElementSpec> result;
  if (!json.is<picojson::array>())
  {
    r.Error(path, "\"elements\" must be an array");
    return result;
  }
  const auto& list = json.get<picojson::array>();
  for (size_t i = 0; i < list.size(); ++i)
  {
    if (std::optional<ElementSpec> element =
            ParseElement(r, list[i], fmt::format("{} element #{}", path, i + 1), styles, depth))
    {
      result.push_back(std::move(*element));
    }
  }
  return result;
}

std::optional<ElementSpec> ParseElement(Reader& r, const picojson::value& json,
                                        std::string_view path,
                                        const std::map<std::string, picojson::object>& styles,
                                        int depth)
{
  if (depth > 16)
  {
    r.Error(path, "elements are nested too deeply");
    return std::nullopt;
  }
  const picojson::object* given = r.Object(json, path);
  if (!given)
    return std::nullopt;

  // A "style" names a preset from theme.styles; the element's own keys win.
  picojson::object merged;
  if (const std::optional<std::string> style = r.String(*given, "style", path, false))
  {
    const auto it = styles.find(*style);
    if (it == styles.end())
      r.Error(path, fmt::format("unknown style \"{}\"", *style));
    else
      merged = it->second;
  }
  for (const auto& [key, value] : *given)
    merged[key] = value;
  const picojson::object& object = merged;

  const std::optional<std::string> type_name = r.String(object, "type", path, true);
  if (!type_name)
    return std::nullopt;
  const auto type_it = std::ranges::find(ELEMENT_TYPES, *type_name,
                                         &std::pair<std::string_view, ElementType>::first);
  const std::string element_path = fmt::format("{} ({})", path, *type_name);
  if (type_it == std::end(ELEMENT_TYPES))
  {
    r.Error(element_path, "unknown element type (rect, frame, text, bar, pips, image, path, grid, "
                          "repeat, group)");
    return std::nullopt;
  }

  ElementSpec element;
  element.type = type_it->second;
  element.visible_if = r.OptionalExpr(object, "visible_if", element_path);
  element.action = r.String(object, "action", element_path, false).value_or("");
  element.source = r.String(object, "source", element_path, false).value_or("");
  element.where = r.OptionalExpr(object, "where", element_path);

  for (const auto& [key, value] : object)
  {
    if (key.starts_with("//") || key.starts_with('_') || value.is<picojson::null>())
      continue;
    if (IsElementNumber(key))
    {
      if (std::optional<Expression> expression =
              r.Expr(value, element_path, fmt::format("\"{}\"", key)))
      {
        element.numbers.emplace(key, std::move(*expression));
      }
    }
    else if (IsElementText(key))
    {
      if (std::optional<Template> text = r.OptionalTemplate(object, key, element_path))
        element.props.emplace(key, std::move(*text));
    }
    else if (std::ranges::find(ELEMENT_STRUCTURE, key) == std::end(ELEMENT_STRUCTURE))
    {
      r.Error(element_path, fmt::format("unknown key \"{}\"", key));
    }
  }

  if (const picojson::value* viewbox = r.Get(object, "viewbox"))
  {
    bool valid = viewbox->is<picojson::array>() && viewbox->get<picojson::array>().size() == 4;
    if (valid)
    {
      for (const picojson::value& number : viewbox->get<picojson::array>())
      {
        valid = valid && number.is<double>();
        if (number.is<double>())
          element.viewbox.push_back(number.get<double>());
      }
    }
    if (!valid)
      r.Error(element_path, "\"viewbox\" must be [min_x, min_y, width, height]");
  }

  for (const char* key : {"crop", "slice"})
  {
    const picojson::value* list = r.Get(object, key);
    if (!list)
      continue;
    if (!list->is<picojson::array>() || list->get<picojson::array>().size() != 4)
    {
      r.Error(element_path, fmt::format("\"{}\" must be a list of four numbers or expressions", key));
      continue;
    }
    std::vector<Expression> expressions;
    for (const picojson::value& entry : list->get<picojson::array>())
    {
      if (std::optional<Expression> expression =
              r.Expr(entry, element_path, fmt::format("\"{}\" entry", key)))
      {
        expressions.push_back(std::move(*expression));
      }
    }
    if (expressions.size() == 4)
      element.lists.emplace(key, std::move(expressions));
  }

  if (const picojson::value* children = r.Get(object, "elements"))
    element.children = ParseElementList(r, *children, element_path, styles, depth + 1);

  if (const picojson::array* layers = r.Array(object, "layers", element_path))
  {
    for (size_t i = 0; i < layers->size(); ++i)
    {
      const std::string layer_path = fmt::format("{} layer #{}", element_path, i + 1);
      const picojson::object* layer_object = r.Object((*layers)[i], layer_path);
      if (!layer_object)
        continue;
      r.CheckKeys(*layer_object, layer_path,
                  {"source", "x", "y", "where", "fill", "stroke", "shape", "label", "size",
                   "action"});
      GridLayerSpec layer;
      layer.source = r.String(*layer_object, "source", layer_path, false).value_or("");
      const picojson::value* x = r.Get(*layer_object, "x");
      const picojson::value* y = r.Get(*layer_object, "y");
      if (!x || !y)
      {
        r.Error(layer_path, "needs \"x\" and \"y\"");
        continue;
      }
      std::optional<Expression> x_expression = r.Expr(*x, layer_path, "\"x\"");
      std::optional<Expression> y_expression = r.Expr(*y, layer_path, "\"y\"");
      if (!x_expression || !y_expression)
        continue;
      layer.x = std::move(*x_expression);
      layer.y = std::move(*y_expression);
      layer.where = r.OptionalExpr(*layer_object, "where", layer_path);
      layer.size = r.OptionalExpr(*layer_object, "size", layer_path);
      for (const char* key : {"fill", "stroke", "shape", "label"})
      {
        if (std::optional<Template> text = r.OptionalTemplate(*layer_object, key, layer_path))
          layer.props.emplace(key, std::move(*text));
      }
      layer.action = r.String(*layer_object, "action", layer_path, false).value_or("");
      element.layers.push_back(std::move(layer));
    }
  }

  const auto require = [&](std::initializer_list<std::string_view> keys) {
    for (std::string_view key : keys)
    {
      if (!element.numbers.contains(key) && !element.props.contains(key))
        r.Error(element_path, fmt::format("needs \"{}\"", key));
    }
  };
  switch (element.type)
  {
  case ElementType::Rect:
  case ElementType::Frame:
    require({"x", "y", "w", "h"});
    break;
  case ElementType::Text:
    require({"x", "y", "w", "h", "text"});
    break;
  case ElementType::Bar:
    require({"x", "y", "w", "h", "value", "max"});
    break;
  case ElementType::Pips:
    require({"x", "y", "w", "h", "count", "filled"});
    break;
  case ElementType::Image:
    require({"x", "y", "w", "h", "src"});
    break;
  case ElementType::Path:
    require({"x", "y", "w", "h", "d"});
    if (element.viewbox.empty())
      r.Error(element_path, "needs \"viewbox\"");
    break;
  case ElementType::Grid:
    require({"x", "y", "w", "h", "cols", "rows"});
    break;
  case ElementType::Repeat:
    if (element.source.empty())
      r.Error(element_path, "needs a \"source\" array");
    break;
  case ElementType::Group:
    break;
  }
  if ((element.type == ElementType::Repeat || element.type == ElementType::Group) &&
      element.children.empty())
  {
    r.Error(element_path, "needs \"elements\"");
  }
  return element;
}

void ParseTargets(Reader& r, const picojson::object& root, Manifest& manifest)
{
  const picojson::array* targets = r.Array(root, "targets", "manifest");
  if (!targets || targets->empty())
  {
    r.Error("manifest", "needs a non-empty \"targets\" list of {\"game_id\": ...}");
    return;
  }
  for (size_t i = 0; i < targets->size(); ++i)
  {
    const std::string path = fmt::format("target #{}", i + 1);
    const picojson::object* object = r.Object((*targets)[i], path);
    if (!object)
      continue;
    r.CheckKeys(*object, path, {"game_id", "revisions"});
    TargetSpec target;
    const std::optional<std::string> game_id = r.String(*object, "game_id", path, true);
    if (!game_id)
      continue;
    if (game_id->size() != 6 && game_id->size() != 4)
      r.Error(path, fmt::format("\"{}\" is not a game ID like \"GZLE01\"", *game_id));
    target.game_id = *game_id;
    if (const picojson::array* revisions = r.Array(*object, "revisions", path))
    {
      for (const picojson::value& revision : *revisions)
      {
        const std::optional<u64> number = r.Unsigned(revision, path, "revision");
        if (number && *number <= 0xFFFF)
          target.revisions.insert(static_cast<u16>(*number));
        else if (number)
          r.Error(path, "revisions must be 0-65535");
      }
    }
    manifest.targets.push_back(std::move(target));
  }
}

// Checks references between sections once everything has been parsed.
void Validate(Reader& r, Manifest& manifest)
{
  std::set<std::string, std::less<>> value_names;
  for (const FieldSpec& value : manifest.values)
  {
    if (!value_names.insert(value.name).second)
      r.Error("values", fmt::format("duplicate value \"{}\"", value.name));
  }
  for (const ArraySpec& array : manifest.arrays)
  {
    if (value_names.contains(array.name))
      r.Error("arrays", fmt::format("\"{}\" is both a value and an array", array.name));
    if (!value_names.insert(array.name).second)
      r.Error("arrays", fmt::format("duplicate array \"{}\"", array.name));
  }

  std::set<std::string, std::less<>> page_ids;
  for (const PageSpec& page : manifest.pages)
  {
    if (!page_ids.insert(page.id).second)
      r.Error("pages", fmt::format("duplicate page id \"{}\"", page.id));
  }

  std::set<std::string, std::less<>> action_ids;
  for (const ActionSpec& action : manifest.actions)
  {
    if (!action_ids.insert(action.id).second)
      r.Error("actions", fmt::format("duplicate action id \"{}\"", action.id));
  }

  const auto check_action = [&](std::string_view where, const std::string& id) {
    if (!id.empty() && !action_ids.contains(id))
      r.Error(where, fmt::format("refers to unknown action \"{}\"", id));
  };
  const auto check_array = [&](std::string_view where, const std::string& name) {
    if (!manifest.FindArray(name))
      r.Error(where, fmt::format("refers to unknown array \"{}\"", name));
  };

  for (const PageSpec& page : manifest.pages)
  {
    for (size_t i = 0; i < page.widgets.size(); ++i)
    {
      const WidgetSpec& widget = page.widgets[i];
      const std::string where = fmt::format("page \"{}\" widget #{}", page.id, i + 1);
      if (widget.type == WidgetType::List)
      {
        check_array(where, widget.source);
        check_action(where, widget.action);
      }
      for (const ButtonSpec& button : widget.buttons)
        check_action(where, button.action);
      for (const MapLayerSpec& layer : widget.layers)
      {
        if (!layer.source.empty())
          check_array(where, layer.source);
        check_action(where, layer.action);
      }
    }
  }

  std::set<std::string, std::less<>> layout_ids;
  for (const LayoutSpec& layout : manifest.layouts)
  {
    if (!layout_ids.insert(layout.id).second)
      r.Error("layouts", fmt::format("duplicate layout id \"{}\"", layout.id));
  }

  std::set<std::string, std::less<>> state_names;
  for (const StateSpec& state : manifest.state)
  {
    if (value_names.contains(state.name))
      r.Error("state", fmt::format("\"{}\" is also a value or array", state.name));
    if (!state_names.insert(state.name).second)
      r.Error("state", fmt::format("duplicate state \"{}\"", state.name));
  }

  std::function<void(std::string_view, const std::vector<ElementSpec>&)> check_elements =
      [&](std::string_view where, const std::vector<ElementSpec>& elements) {
        for (const ElementSpec& element : elements)
        {
          check_action(where, element.action);
          if (!element.source.empty())
            check_array(where, element.source);
          for (const GridLayerSpec& layer : element.layers)
          {
            if (!layer.source.empty())
              check_array(where, layer.source);
            check_action(where, layer.action);
          }
          check_elements(where, element.children);
        }
      };
  for (const LayoutSpec& layout : manifest.layouts)
    check_elements(fmt::format("layout \"{}\"", layout.id), layout.elements);
  for (const PageSpec& page : manifest.pages)
  {
    for (const auto& [layout, elements] : page.elements)
    {
      const std::string where = fmt::format("page \"{}\" layout \"{}\"", page.id, layout);
      if (layout != "*" && !layout_ids.contains(layout))
        r.Error(where, "names a layout that is not in \"layouts\"");
      if (manifest.layouts.empty())
        r.Error(where, "canvas elements need at least one entry in \"layouts\"");
      check_elements(where, elements);
    }
  }

  for (ActionSpec& action : manifest.actions)
  {
    const std::string where = fmt::format("action \"{}\"", action.id);
    for (const ActionStep& step : action.steps)
    {
      switch (step.kind)
      {
      case ActionStep::Kind::Press:
        action.needs_input = true;
        if (!manifest.allow_input)
          r.Error(where, "presses buttons but the manifest does not list the \"input\" capability");
        break;
      case ActionStep::Kind::Write:
      {
        action.needs_write = true;
        if (!manifest.allow_write)
        {
          r.Error(where,
                  "writes memory but the manifest does not list the \"write_memory\" capability");
        }
        if (step.field.empty())
        {
          const FieldSpec* value = manifest.FindValue(step.target);
          if (!value)
            r.Error(where, fmt::format("writes to unknown value \"{}\"", step.target));
          else if (value->computed || value->type == MemoryType::String ||
                   value->address_expression)
            r.Error(where, fmt::format("cannot write to \"{}\" (computed, string or address_expr)",
                                       step.target));
        }
        else
        {
          const ArraySpec* array = manifest.FindArray(step.target);
          const auto field_it = array ?
                                    std::ranges::find(array->fields, step.field, &FieldSpec::name) :
                                    std::vector<FieldSpec>::const_iterator{};
          if (!array)
            r.Error(where, fmt::format("writes to unknown array \"{}\"", step.target));
          else if (field_it == array->fields.end())
            r.Error(where,
                    fmt::format("writes to unknown field \"{}.{}\"", step.target, step.field));
          else if (field_it->computed || field_it->type == MemoryType::String ||
                   field_it->address_expression)
            r.Error(where, fmt::format("cannot write to \"{}.{}\" (computed or string)",
                                       step.target, step.field));
        }
        break;
      }
      case ActionStep::Kind::Page:
        if (!page_ids.contains(step.page))
          r.Error(where, fmt::format("switches to unknown page \"{}\"", step.page));
        break;
      case ActionStep::Kind::Set:
        if (!state_names.contains(step.target))
          r.Error(where, fmt::format("sets unknown state \"{}\"", step.target));
        break;
      case ActionStep::Kind::Wait:
        break;
      }
    }
  }
}
}  // namespace

std::optional<Control> ParseControl(std::string_view name)
{
  std::string upper(name);
  std::ranges::transform(upper, upper.begin(),
                         [](char c) { return static_cast<char>(std::toupper(c)); });
  for (const auto& [control_name, control] : CONTROL_NAMES)
  {
    if (control_name == upper)
      return control;
  }
  return std::nullopt;
}

std::string_view ControlName(Control control)
{
  for (const auto& [control_name, value] : CONTROL_NAMES)
  {
    if (value == control)
      return control_name;
  }
  return "?";
}

bool IsGameCubeControl(Control control)
{
  return static_cast<int>(control) <= static_cast<int>(Control::GC_C_STICK_Y);
}

bool Manifest::Targets(std::string_view game_id, u16 revision) const
{
  return std::ranges::any_of(targets, [&](const TargetSpec& target) {
    return target.game_id == game_id &&
           (target.revisions.empty() || target.revisions.contains(revision));
  });
}

const std::vector<ElementSpec>* PageSpec::ElementsFor(std::string_view layout) const
{
  auto it = elements.find(layout);
  if (it == elements.end())
    it = elements.find("*");
  return it == elements.end() ? nullptr : &it->second;
}

const LayoutSpec* Manifest::ChooseLayout(u32 screen_width, u32 screen_height) const
{
  if (layouts.empty())
    return nullptr;
  if (screen_width == 0 || screen_height == 0)
    return &layouts.front();
  // Compare shapes on a log scale so 4:3 vs 16:9 and 3:4 vs 9:16 weigh the same.
  const double screen = std::log(static_cast<double>(screen_width) / screen_height);
  const LayoutSpec* best = nullptr;
  double best_distance = 0;
  for (const LayoutSpec& layout : layouts)
  {
    if (layout.width == 0 || layout.height == 0)
      continue;
    const double distance =
        std::abs(std::log(static_cast<double>(layout.width) / layout.height) - screen);
    if (!best || distance < best_distance)
    {
      best = &layout;
      best_distance = distance;
    }
  }
  return best ? best : &layouts.front();
}

const LayoutSpec* Manifest::FindLayout(std::string_view layout_id) const
{
  const auto it = std::ranges::find(layouts, layout_id, &LayoutSpec::id);
  return it == layouts.end() ? nullptr : &*it;
}

const ActionSpec* Manifest::FindAction(std::string_view action_id) const
{
  const auto it = std::ranges::find(actions, action_id, &ActionSpec::id);
  return it == actions.end() ? nullptr : &*it;
}

const ArraySpec* Manifest::FindArray(std::string_view array_name) const
{
  const auto it = std::ranges::find(arrays, array_name, &ArraySpec::name);
  return it == arrays.end() ? nullptr : &*it;
}

const FieldSpec* Manifest::FindValue(std::string_view value_name) const
{
  const auto it = std::ranges::find(values, value_name, &FieldSpec::name);
  return it == values.end() ? nullptr : &*it;
}

ManifestParseResult ParseManifest(std::string_view json)
{
  ManifestParseResult result;
  Reader r(&result.errors);

  picojson::value root_value;
  const std::string parse_error =
      picojson::parse(root_value, std::string(json.begin(), json.end()));
  if (!parse_error.empty())
  {
    r.Error("manifest.json", parse_error);
    return result;
  }
  const picojson::object* root = r.Object(root_value, "manifest");
  if (!root)
    return result;

  r.CheckKeys(*root, "manifest",
              {"format", "id", "name", "version", "author", "description", "targets", "controller",
               "poll_hz", "capabilities", "values", "arrays", "tables", "pages", "actions",
               "theme", "layouts", "state"});

  Manifest manifest;
  const std::optional<u32> format = r.U32(*root, "format", "manifest", true);
  if (format && *format != MANIFEST_FORMAT_VERSION)
  {
    r.Error("manifest", fmt::format("format {} is not supported by this Dolphin Duo (expects {})",
                                    *format, MANIFEST_FORMAT_VERSION));
    return result;
  }

  manifest.id = r.String(*root, "id", "manifest", true).value_or("");
  if (!manifest.id.empty() && !std::ranges::all_of(manifest.id, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
      }))
  {
    r.Error("manifest", "\"id\" may only contain letters, digits, '-', '_' and '.'");
  }
  if (manifest.id.starts_with('.') || manifest.id.size() > 64)
    r.Error("manifest", "\"id\" must be at most 64 characters and not start with '.'");
  manifest.name = r.String(*root, "name", "manifest", true).value_or("");
  manifest.version = r.String(*root, "version", "manifest", true).value_or("");
  manifest.author = r.String(*root, "author", "manifest", false).value_or("");
  manifest.description = r.String(*root, "description", "manifest", false).value_or("");
  ParseTargets(r, *root, manifest);

  manifest.controller =
      static_cast<int>(r.U32(*root, "controller", "manifest", false, 0).value_or(0));
  if (manifest.controller > 3)
    r.Error("manifest", "\"controller\" must be 0-3");
  manifest.poll_hz = r.U32(*root, "poll_hz", "manifest", false, 20).value_or(20);
  if (manifest.poll_hz == 0 || manifest.poll_hz > 60)
    r.Error("manifest", "\"poll_hz\" must be 1-60");

  if (const picojson::array* capabilities = r.Array(*root, "capabilities", "manifest"))
  {
    for (const picojson::value& capability : *capabilities)
    {
      if (!capability.is<std::string>())
      {
        r.Error("capabilities", "entries must be strings");
        continue;
      }
      const std::string& name = capability.get<std::string>();
      if (name == "write_memory")
        manifest.allow_write = true;
      else if (name == "input")
        manifest.allow_input = true;
      else
        r.Error("capabilities",
                fmt::format("unknown capability \"{}\" (write_memory, input)", name));
    }
  }

  if (const picojson::array* values = r.Array(*root, "values", "manifest"))
  {
    for (size_t i = 0; i < values->size(); ++i)
    {
      if (std::optional<FieldSpec> field =
              ParseField(r, (*values)[i], fmt::format("value #{}", i + 1), false))
      {
        manifest.values.push_back(std::move(*field));
      }
    }
  }

  if (const picojson::array* arrays = r.Array(*root, "arrays", "manifest"))
  {
    for (size_t i = 0; i < arrays->size(); ++i)
    {
      if (std::optional<ArraySpec> array =
              ParseArray(r, (*arrays)[i], fmt::format("array #{}", i + 1)))
      {
        manifest.arrays.push_back(std::move(*array));
      }
    }
  }

  if (const picojson::value* tables = r.Get(*root, "tables"))
  {
    if (const picojson::object* table_object = r.Object(*tables, "tables"))
    {
      for (const auto& [table_name, table_value] : *table_object)
      {
        const std::string path = fmt::format("table \"{}\"", table_name);
        if (!IsIdentifier(table_name))
          r.Error(path, "is not a valid name");
        const picojson::object* entries = r.Object(table_value, path);
        if (!entries)
          continue;
        auto& table = manifest.tables[table_name];
        for (const auto& [key, text] : *entries)
        {
          if (!text.is<std::string>())
          {
            r.Error(path, fmt::format("entry \"{}\" must be a string", key));
            continue;
          }
          // Normalise hex keys so table(class, 0x1A) finds "0x1A" as well as "26".
          std::string normalised = key;
          if (key.size() > 2 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X'))
          {
            if (const std::optional<u64> number = r.Unsigned(picojson::value(key), path, "key"))
              normalised = fmt::format("{}", *number);
          }
          table[normalised] = text.get<std::string>();
        }
      }
    }
  }

  // Theme: named colours, fonts shipped in the package and element style presets.
  std::map<std::string, picojson::object> styles;
  if (const picojson::value* theme = r.Get(*root, "theme"))
  {
    if (const picojson::object* theme_object = r.Object(*theme, "theme"))
    {
      r.CheckKeys(*theme_object, "theme", {"colors", "fonts", "styles"});
      const auto read_map = [&](const char* key, auto& out) {
        const picojson::value* value = r.Get(*theme_object, key);
        if (!value)
          return;
        const picojson::object* entries = r.Object(*value, fmt::format("theme.{}", key));
        if (!entries)
          return;
        for (const auto& [name, text] : *entries)
        {
          if (!text.is<std::string>())
            r.Error(fmt::format("theme.{}", key), fmt::format("\"{}\" must be a string", name));
          else
            out[name] = text.get<std::string>();
        }
      };
      read_map("colors", manifest.theme.colors);
      read_map("fonts", manifest.theme.fonts);
      for (const auto& [name, file] : manifest.theme.fonts)
      {
        if (file.find("..") != std::string::npos || file.starts_with('/'))
          r.Error("theme.fonts", fmt::format("\"{}\" must be a path inside the package", name));
      }
      if (const picojson::value* style_value = r.Get(*theme_object, "styles"))
      {
        if (const picojson::object* style_object = r.Object(*style_value, "theme.styles"))
        {
          for (const auto& [name, preset] : *style_object)
          {
            if (const picojson::object* preset_object =
                    r.Object(preset, fmt::format("theme.styles.{}", name)))
            {
              styles[name] = *preset_object;
            }
          }
        }
      }
    }
  }

  if (const picojson::array* layouts = r.Array(*root, "layouts", "manifest"))
  {
    for (size_t i = 0; i < layouts->size(); ++i)
    {
      const std::string path = fmt::format("layout #{}", i + 1);
      const picojson::object* object = r.Object((*layouts)[i], path);
      if (!object)
        continue;
      r.CheckKeys(*object, path, {"id", "canvas", "background", "elements"});
      LayoutSpec layout;
      layout.id = r.String(*object, "id", path, true).value_or("");
      if (!layout.id.empty() && !IsIdentifier(layout.id))
        r.Error(path, fmt::format("\"{}\" is not a valid layout id", layout.id));
      const std::string layout_path = fmt::format("layout \"{}\"", layout.id);
      const picojson::array* canvas = r.Array(*object, "canvas", layout_path);
      if (!canvas || canvas->size() != 2 || !(*canvas)[0].is<double>() ||
          !(*canvas)[1].is<double>() || (*canvas)[0].get<double>() < 16 ||
          (*canvas)[1].get<double>() < 16 || (*canvas)[0].get<double>() > 8192 ||
          (*canvas)[1].get<double>() > 8192)
      {
        r.Error(layout_path, "needs \"canvas\": [width, height] (16-8192)");
      }
      else
      {
        layout.width = static_cast<u32>((*canvas)[0].get<double>());
        layout.height = static_cast<u32>((*canvas)[1].get<double>());
      }
      layout.background = r.OptionalTemplate(*object, "background", layout_path);
      if (const picojson::value* elements = r.Get(*object, "elements"))
        layout.elements = ParseElementList(r, *elements, layout_path, styles, 0);
      manifest.layouts.push_back(std::move(layout));
    }
  }

  if (const picojson::value* state = r.Get(*root, "state"))
  {
    if (const picojson::object* state_object = r.Object(*state, "state"))
    {
      for (const auto& [name, initial] : *state_object)
      {
        if (!IsIdentifier(name) || IsReservedName(name))
        {
          r.Error("state", fmt::format("\"{}\" is not a valid name", name));
          continue;
        }
        if (std::optional<Expression> expression =
                r.Expr(initial, "state", fmt::format("\"{}\"", name)))
        {
          manifest.state.push_back({name, std::move(*expression)});
        }
      }
    }
  }

  if (const picojson::array* pages = r.Array(*root, "pages", "manifest"))
  {
    for (size_t i = 0; i < pages->size(); ++i)
    {
      const std::string path = fmt::format("page #{}", i + 1);
      const picojson::object* object = r.Object((*pages)[i], path);
      if (!object)
        continue;
      r.CheckKeys(*object, path, {"id", "title", "visible_if", "widgets", "elements"});
      PageSpec page;
      page.id = r.String(*object, "id", path, true).value_or("");
      if (!page.id.empty() && !IsIdentifier(page.id))
        r.Error(path, fmt::format("\"{}\" is not a valid page id", page.id));
      const std::string page_path = fmt::format("page \"{}\"", page.id);
      if (std::optional<Template> title = r.OptionalTemplate(*object, "title", page_path))
        page.title = std::move(*title);
      else
        r.Error(page_path, "needs a \"title\"");
      page.visible_if = r.OptionalExpr(*object, "visible_if", page_path);
      if (const picojson::array* widgets = r.Array(*object, "widgets", page_path))
      {
        for (size_t w = 0; w < widgets->size(); ++w)
        {
          if (std::optional<WidgetSpec> widget =
                  ParseWidget(r, (*widgets)[w], fmt::format("{} widget #{}", page_path, w + 1)))
          {
            page.widgets.push_back(std::move(*widget));
          }
        }
      }
      if (const picojson::value* elements = r.Get(*object, "elements"))
      {
        // Either one list for every layout, or {"layout id": [...], "*": [...]}.
        if (elements->is<picojson::array>())
        {
          page.elements["*"] = ParseElementList(r, *elements, page_path, styles, 0);
        }
        else if (const picojson::object* per_layout = r.Object(*elements, page_path))
        {
          for (const auto& [layout_id, list] : *per_layout)
          {
            page.elements[layout_id] = ParseElementList(
                r, list, fmt::format("{} layout \"{}\"", page_path, layout_id), styles, 0);
          }
        }
      }
      manifest.pages.push_back(std::move(page));
    }
  }
  if (manifest.pages.empty())
    r.Error("manifest", "needs at least one page");

  if (const picojson::array* actions = r.Array(*root, "actions", "manifest"))
  {
    for (size_t i = 0; i < actions->size(); ++i)
    {
      const std::string path = fmt::format("action #{}", i + 1);
      const picojson::object* object = r.Object((*actions)[i], path);
      if (!object)
        continue;
      r.CheckKeys(*object, path, {"id", "label", "steps"});
      ActionSpec action;
      action.id = r.String(*object, "id", path, true).value_or("");
      if (!action.id.empty() && !IsIdentifier(action.id))
        r.Error(path, fmt::format("\"{}\" is not a valid action id", action.id));
      const std::string action_path = fmt::format("action \"{}\"", action.id);
      action.label = r.OptionalTemplate(*object, "label", action_path);
      const picojson::array* steps = r.Array(*object, "steps", action_path);
      if (!steps || steps->empty())
        r.Error(action_path, "needs a non-empty \"steps\" list");
      else
      {
        if (steps->size() > 128)
          r.Error(action_path, "actions are limited to 128 steps");
        for (size_t s = 0; s < steps->size(); ++s)
        {
          if (std::optional<ActionStep> step =
                  ParseStep(r, (*steps)[s], fmt::format("{} step #{}", action_path, s + 1)))
          {
            action.steps.push_back(std::move(*step));
          }
        }
      }
      manifest.actions.push_back(std::move(action));
    }
  }

  Validate(r, manifest);

  if (result.errors.empty())
    result.manifest = std::move(manifest);
  return result;
}
}  // namespace Duo
