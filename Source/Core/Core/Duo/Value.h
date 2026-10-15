// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// A small dynamic value used by Dolphin Duo companions. A value is either unavailable (a read
// failed, a pointer was null, an expression referenced something missing), a number or a string.

#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "Common/CommonTypes.h"

namespace Duo
{
class Value
{
public:
  Value() = default;
  Value(double number) : m_data(number) {}
  Value(int number) : m_data(static_cast<double>(number)) {}
  Value(s64 number) : m_data(static_cast<double>(number)) {}
  Value(u32 number) : m_data(static_cast<double>(number)) {}
  Value(bool boolean) : m_data(boolean ? 1.0 : 0.0) {}
  Value(std::string text) : m_data(std::move(text)) {}
  Value(const char* text) : m_data(std::string(text)) {}

  static Value Null() { return {}; }

  bool IsNull() const { return std::holds_alternative<std::monostate>(m_data); }
  bool IsNumber() const { return std::holds_alternative<double>(m_data); }
  bool IsString() const { return std::holds_alternative<std::string>(m_data); }

  double Number() const { return std::get<double>(m_data); }
  const std::string& String() const { return std::get<std::string>(m_data); }

  // Numbers convert as-is; numeric strings are not coerced, so a typo in a manifest shows up as
  // an unavailable value instead of silently becoming 0.
  std::optional<double> AsNumber() const
  {
    if (IsNumber())
      return Number();
    return std::nullopt;
  }

  // Integer view used by bitwise operators, indexing and counts. Non-finite numbers are null.
  std::optional<s64> AsInteger() const
  {
    if (!IsNumber() || !std::isfinite(Number()))
      return std::nullopt;
    return static_cast<s64>(std::trunc(Number()));
  }

  bool Truthy() const
  {
    if (IsNumber())
      return Number() != 0.0 && !std::isnan(Number());
    if (IsString())
      return !String().empty();
    return false;
  }

  bool operator==(const Value& other) const = default;

private:
  std::variant<std::monostate, double, std::string> m_data;
};
}  // namespace Duo
