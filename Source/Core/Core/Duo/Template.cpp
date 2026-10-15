// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Template.h"

#include <cctype>
#include <cmath>

#include <fmt/format.h>

namespace Duo
{
namespace
{
constexpr std::string_view PLACEHOLDER = "?";

// Finds the ':' separating an expression from its format, ignoring colons inside strings,
// parentheses, brackets and the ':' of a ?: conditional.
size_t FindFormatSeparator(std::string_view inner)
{
  int depth = 0;
  int pending_conditionals = 0;
  char quote = 0;
  size_t separator = std::string_view::npos;
  for (size_t i = 0; i < inner.size(); ++i)
  {
    const char c = inner[i];
    if (quote)
    {
      if (c == '\\')
        ++i;
      else if (c == quote)
        quote = 0;
      continue;
    }
    if (c == '\'' || c == '"')
      quote = c;
    else if (c == '(' || c == '[')
      ++depth;
    else if (c == ')' || c == ']')
      --depth;
    else if (depth == 0 && c == '?')
      ++pending_conditionals;
    else if (depth == 0 && c == ':')
    {
      if (pending_conditionals > 0)
        --pending_conditionals;
      else
        separator = i;
    }
  }
  return separator;
}

std::string Pad(std::string text, int width, bool left_align, bool zero_pad)
{
  if (width <= 0 || static_cast<int>(text.size()) >= width)
    return text;
  const size_t fill = static_cast<size_t>(width) - text.size();
  if (left_align)
    return text + std::string(fill, ' ');
  if (zero_pad)
  {
    const bool negative = !text.empty() && text[0] == '-';
    return (negative ? "-" : "") + std::string(fill, '0') + text.substr(negative ? 1 : 0);
  }
  return std::string(fill, ' ') + text;
}
}  // namespace

std::optional<Template::Format> Template::ParseFormat(std::string_view spec)
{
  Format format;
  size_t i = 0;
  if (i < spec.size() && spec[i] == '-')
  {
    format.left_align = true;
    ++i;
  }
  if (i < spec.size() && spec[i] == '0')
  {
    format.zero_pad = true;
    ++i;
  }
  while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
  {
    format.width = format.width * 10 + (spec[i] - '0');
    if (format.width > 64)
      return std::nullopt;
    ++i;
  }
  if (i < spec.size() && spec[i] == '.')
  {
    ++i;
    format.precision = 0;
    if (i >= spec.size() || !std::isdigit(static_cast<unsigned char>(spec[i])))
      return std::nullopt;
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
    {
      format.precision = format.precision * 10 + (spec[i] - '0');
      if (format.precision > 12)
        return std::nullopt;
      ++i;
    }
  }
  if (i < spec.size())
  {
    const char type = spec[i];
    if (type != 'x' && type != 'X' && type != 'd' && type != 'f' && type != '%')
      return std::nullopt;
    format.type = type;
    ++i;
  }
  if (i != spec.size())
    return std::nullopt;
  return format;
}

std::string Template::FormatValue(const Value& value, const Format& format)
{
  std::string text;
  if (value.IsNull())
  {
    text = std::string(PLACEHOLDER);
    return Pad(std::move(text), format.width, format.left_align, false);
  }

  if (value.IsString())
    return Pad(value.String(), format.width, format.left_align, false);

  const double number = value.Number();
  switch (format.type)
  {
  case 'x':
  case 'X':
  {
    const s64 integer = static_cast<s64>(std::trunc(number));
    const u64 bits = static_cast<u64>(integer);
    text = format.type == 'x' ? fmt::format("{:x}", bits) : fmt::format("{:X}", bits);
    break;
  }
  case 'd':
    text = fmt::format("{}", static_cast<s64>(std::trunc(number)));
    break;
  case '%':
    text = fmt::format("{:.{}f}%", number * 100.0, format.precision < 0 ? 0 : format.precision);
    break;
  case 'f':
    text = fmt::format("{:.{}f}", number, format.precision < 0 ? 2 : format.precision);
    break;
  default:
    if (format.precision >= 0)
      text = fmt::format("{:.{}f}", number, format.precision);
    else if (std::trunc(number) == number && std::abs(number) < 1e15)
      text = fmt::format("{}", static_cast<s64>(number));
    else
    {
      text = fmt::format("{:.2f}", number);
      while (text.ends_with('0'))
        text.pop_back();
      if (text.ends_with('.'))
        text.pop_back();
    }
    break;
  }
  return Pad(std::move(text), format.width, format.left_align, format.zero_pad);
}

std::optional<Template> Template::Parse(std::string_view source, std::string* error)
{
  Template result;
  result.m_source = std::string(source);

  std::string literal;
  size_t i = 0;
  while (i < source.size())
  {
    const char c = source[i];
    if (c == '{' && i + 1 < source.size() && source[i + 1] == '{')
    {
      literal.push_back('{');
      i += 2;
      continue;
    }
    if (c == '}' && i + 1 < source.size() && source[i + 1] == '}')
    {
      literal.push_back('}');
      i += 2;
      continue;
    }
    if (c == '}')
    {
      if (error)
        *error =
            fmt::format("unmatched '}}' at column {} (write '}}}}' for a literal brace)", i + 1);
      return std::nullopt;
    }
    if (c != '{')
    {
      literal.push_back(c);
      ++i;
      continue;
    }

    // Find the matching '}', skipping string literals.
    size_t end = i + 1;
    char quote = 0;
    int depth = 0;
    for (; end < source.size(); ++end)
    {
      const char d = source[end];
      if (quote)
      {
        if (d == '\\')
          ++end;
        else if (d == quote)
          quote = 0;
        continue;
      }
      if (d == '\'' || d == '"')
        quote = d;
      else if (d == '{')
        ++depth;
      else if (d == '}')
      {
        if (depth == 0)
          break;
        --depth;
      }
    }
    if (end >= source.size())
    {
      if (error)
        *error = fmt::format("unterminated '{{' at column {}", i + 1);
      return std::nullopt;
    }

    const std::string_view inner = source.substr(i + 1, end - i - 1);
    const size_t separator = FindFormatSeparator(inner);
    const std::string_view expression_text =
        separator == std::string_view::npos ? inner : inner.substr(0, separator);
    const std::string_view format_text =
        separator == std::string_view::npos ? std::string_view{} : inner.substr(separator + 1);

    std::string expression_error;
    std::optional<Expression> expression = Expression::Parse(expression_text, &expression_error);
    if (!expression)
    {
      if (error)
        *error = fmt::format("in '{{{}}}': {}", inner, expression_error);
      return std::nullopt;
    }
    std::optional<Format> format = ParseFormat(format_text);
    if (!format)
    {
      if (error)
        *error = fmt::format("in '{{{}}}': bad format '{}'", inner, format_text);
      return std::nullopt;
    }

    if (!literal.empty())
    {
      result.m_parts.push_back(Part{std::move(literal), std::nullopt, {}});
      literal.clear();
    }
    result.m_parts.push_back(Part{{}, std::move(expression), *format});
    i = end + 1;
  }
  if (!literal.empty())
    result.m_parts.push_back(Part{std::move(literal), std::nullopt, {}});
  return result;
}

std::string Template::Render(const Scope& scope) const
{
  std::string out;
  for (const Part& part : m_parts)
  {
    if (part.expression)
      out += FormatValue(part.expression->Evaluate(scope), part.format);
    else
      out += part.literal;
  }
  return out;
}

bool Template::IsConstant() const
{
  for (const Part& part : m_parts)
  {
    if (part.expression)
      return false;
  }
  return true;
}
}  // namespace Duo
