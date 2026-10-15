// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The expression language used by Dolphin Duo manifests for computed values, visibility
// conditions and text templates. It is deliberately tiny and side-effect free:
//
//   literals     12  0x1F  1.5  'text'  "text"  true  false  null
//   names        hp  $frame  max_hp            (resolved through a Scope)
//   elements     party[2].hp                   (array element field)
//   operators    ?:  ||  &&  |  ^  &  ==  !=  <  <=  >  >=  <<  >>  +  -  *  /  %  !  ~  unary -
//   functions    min max abs floor ceil round clamp(x, lo, hi) bit(x, n) bits(x, shift, width)
//                count(array) table(name, key) default(x, fallback) str(x)
//
// Arithmetic on an unavailable value yields an unavailable value, so a failed memory read flows
// through to the UI as "?" instead of becoming a misleading 0. `default(x, y)` recovers from it.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Core/Duo/Value.h"

namespace Duo
{
class Scope
{
public:
  virtual ~Scope() = default;

  // A plain name such as `hp`.
  virtual Value Lookup(std::string_view name) const = 0;

  // `array[index].field`. Out-of-range indices return null.
  virtual Value LookupElement(std::string_view array, s64 index, std::string_view field) const
  {
    return {};
  }

  // `count(array)`: the number of elements currently in the array.
  virtual Value Count(std::string_view array) const { return {}; }

  // `table(name, key)`: a lookup in one of the manifest's text tables.
  virtual Value Table(std::string_view table, const Value& key) const { return {}; }
};

class Expression
{
public:
  struct Node;

  Expression();
  ~Expression();
  Expression(const Expression&);
  Expression& operator=(const Expression&);
  Expression(Expression&&) noexcept;
  Expression& operator=(Expression&&) noexcept;

  // Returns nullopt and fills `error` (if given) when `source` does not parse.
  static std::optional<Expression> Parse(std::string_view source, std::string* error = nullptr);

  // An expression that always evaluates to `value`.
  static Expression Constant(Value value);

  Value Evaluate(const Scope& scope) const;

  bool IsEmpty() const { return m_root == nullptr; }
  const std::string& Source() const { return m_source; }

private:
  std::shared_ptr<const Node> m_root;
  std::string m_source;
};
}  // namespace Duo
