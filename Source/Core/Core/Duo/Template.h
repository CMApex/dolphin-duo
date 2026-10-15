// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Text templates such as "HP {hp}/{max_hp}" or "Gold {gold:07}". Each {...} holds an expression
// (see Expression.h) and an optional format after the last top-level ':'.
//
//   {x}        default formatting (integers without decimals, other numbers to 2 places)
//   {x:5}      right-aligned in 5 columns          {x:-5}  left-aligned
//   {x:05}     zero-padded to 5 digits             {x:.1}  one decimal place
//   {x:x}      lowercase hex   {x:08X}  uppercase zero-padded hex
//   {x:%}      fraction as a percentage (0.5 -> 50%)
//
// Literal braces are written {{ and }}. An unavailable value renders as the placeholder "?".

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Duo/Expression.h"

namespace Duo
{
class Template
{
public:
  static std::optional<Template> Parse(std::string_view source, std::string* error = nullptr);

  std::string Render(const Scope& scope) const;

  // True when the template contains no expressions and renders the same text every time.
  bool IsConstant() const;
  const std::string& Source() const { return m_source; }

private:
  struct Format
  {
    bool left_align = false;
    bool zero_pad = false;
    int width = 0;
    int precision = -1;
    char type = 0;  // 0 (default), 'x', 'X', 'd', 'f', '%'
  };

  struct Part
  {
    std::string literal;
    std::optional<Expression> expression;
    Format format;
  };

  static std::optional<Format> ParseFormat(std::string_view spec);
  static std::string FormatValue(const Value& value, const Format& format);

  std::vector<Part> m_parts;
  std::string m_source;
};
}  // namespace Duo
