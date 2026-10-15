// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/Expression.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace Duo
{
namespace
{
enum class TokenType
{
  Number,
  String,
  Name,
  Operator,
  End,
};

struct Token
{
  TokenType type = TokenType::End;
  std::string text;
  double number = 0.0;
  size_t position = 0;
};

// Dolphin builds without exceptions, so parse errors are recorded here and every parsing step
// checks Failed() before continuing. Only the first error is kept.
class ErrorSink
{
public:
  void Fail(size_t position, std::string message)
  {
    if (!m_failed)
    {
      m_failed = true;
      m_position = position;
      m_message = std::move(message);
    }
  }
  bool Failed() const { return m_failed; }
  size_t Position() const { return m_position; }
  const std::string& Message() const { return m_message; }

private:
  bool m_failed = false;
  size_t m_position = 0;
  std::string m_message;
};

bool IsNameStart(char c)
{
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool IsNameChar(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::vector<Token> Tokenize(std::string_view source, ErrorSink& errors)
{
  static constexpr std::string_view two_char_ops[] = {
      "||", "&&", "==", "!=", "<=", ">=", "<<", ">>"};
  static constexpr std::string_view one_char_ops = "?:|^&<>+-*/%!~()[],.";

  std::vector<Token> tokens;
  size_t i = 0;
  while (i < source.size())
  {
    const char c = source[i];
    if (std::isspace(static_cast<unsigned char>(c)))
    {
      ++i;
      continue;
    }

    Token token;
    token.position = i;

    const bool starts_number = std::isdigit(static_cast<unsigned char>(c)) ||
                               (c == '.' && i + 1 < source.size() &&
                                std::isdigit(static_cast<unsigned char>(source[i + 1])) &&
                                (tokens.empty() || tokens.back().text != "]"));
    if (starts_number)
    {
      size_t end = i;
      if (c == '0' && i + 1 < source.size() && (source[i + 1] == 'x' || source[i + 1] == 'X'))
      {
        end = i + 2;
        while (end < source.size() && std::isxdigit(static_cast<unsigned char>(source[end])))
          ++end;
        if (end == i + 2)
        {
          errors.Fail(i, "hexadecimal literal has no digits");
          return {};
        }
        token.number = static_cast<double>(
            std::strtoull(std::string(source.substr(i + 2, end - i - 2)).c_str(), nullptr, 16));
      }
      else
      {
        while (end < source.size() &&
               (std::isdigit(static_cast<unsigned char>(source[end])) || source[end] == '.'))
        {
          ++end;
        }
        const std::string text(source.substr(i, end - i));
        char* parse_end = nullptr;
        token.number = std::strtod(text.c_str(), &parse_end);
        if (parse_end != text.c_str() + text.size())
        {
          errors.Fail(i, fmt::format("malformed number '{}'", text));
          return {};
        }
      }
      if (end < source.size() && IsNameChar(source[end]))
      {
        errors.Fail(end, "unexpected character after number");
        return {};
      }
      token.type = TokenType::Number;
      token.text = std::string(source.substr(i, end - i));
      tokens.push_back(std::move(token));
      i = end;
      continue;
    }

    if (IsNameStart(c))
    {
      size_t end = i + 1;
      while (end < source.size() && IsNameChar(source[end]))
        ++end;
      token.type = TokenType::Name;
      token.text = std::string(source.substr(i, end - i));
      tokens.push_back(std::move(token));
      i = end;
      continue;
    }

    if (c == '\'' || c == '"')
    {
      std::string text;
      size_t end = i + 1;
      while (end < source.size() && source[end] != c)
      {
        if (source[end] == '\\' && end + 1 < source.size())
          ++end;
        text.push_back(source[end]);
        ++end;
      }
      if (end >= source.size())
      {
        errors.Fail(i, "unterminated string literal");
        return {};
      }
      token.type = TokenType::String;
      token.text = std::move(text);
      tokens.push_back(std::move(token));
      i = end + 1;
      continue;
    }

    bool matched = false;
    for (std::string_view op : two_char_ops)
    {
      if (source.substr(i, 2) == op)
      {
        token.type = TokenType::Operator;
        token.text = std::string(op);
        tokens.push_back(std::move(token));
        i += 2;
        matched = true;
        break;
      }
    }
    if (matched)
      continue;

    if (one_char_ops.find(c) != std::string_view::npos)
    {
      token.type = TokenType::Operator;
      token.text = std::string(1, c);
      tokens.push_back(std::move(token));
      ++i;
      continue;
    }

    {
      errors.Fail(i, fmt::format("unexpected character '{}'", c));
      return {};
    }
  }

  Token end;
  end.type = TokenType::End;
  end.position = source.size();
  tokens.push_back(std::move(end));
  return tokens;
}
}  // namespace

struct Expression::Node
{
  enum class Kind
  {
    Constant,
    Name,
    Element,
    Unary,
    Binary,
    Conditional,
    Call,
  };

  Kind kind = Kind::Constant;
  Value constant;
  std::string name;   // Name, Element (array), Call (function), Unary/Binary (operator)
  std::string field;  // Element
  std::vector<std::shared_ptr<const Node>> children;
};

namespace
{
using NodePtr = std::shared_ptr<const Expression::Node>;
using Kind = Expression::Node::Kind;

NodePtr MakeNode(Kind kind, std::string name = {}, std::vector<NodePtr> children = {})
{
  auto node = std::make_shared<Expression::Node>();
  node->kind = kind;
  node->name = std::move(name);
  node->children = std::move(children);
  return node;
}

NodePtr MakeConstant(Value value)
{
  auto node = std::make_shared<Expression::Node>();
  node->kind = Kind::Constant;
  node->constant = std::move(value);
  return node;
}

constexpr std::string_view KNOWN_FUNCTIONS[] = {"min",   "max",     "abs", "floor", "ceil",
                                                "round", "clamp",   "bit", "bits",  "count",
                                                "table", "default", "str"};

class Parser
{
public:
  Parser(std::vector<Token> tokens, ErrorSink& errors)
      : m_tokens(std::move(tokens)), m_errors(errors)
  {
  }

  NodePtr ParseAll()
  {
    NodePtr node = ParseConditional();
    if (Failed())
      return {};
    if (Peek().type != TokenType::End)
      return Fail(Peek().position, fmt::format("unexpected '{}'", Peek().text));
    return node;
  }

private:
  bool Failed() const { return m_errors.Failed(); }

  NodePtr Fail(size_t position, std::string message)
  {
    m_errors.Fail(position, std::move(message));
    return {};
  }

  const Token& Peek() const { return m_tokens[m_index]; }

  // Never moves past the End token, so a failed parse cannot read out of bounds.
  const Token& Next()
  {
    const Token& token = m_tokens[m_index];
    if (m_index + 1 < m_tokens.size())
      ++m_index;
    return token;
  }

  bool AcceptOperator(std::string_view op)
  {
    if (Peek().type == TokenType::Operator && Peek().text == op)
    {
      Next();
      return true;
    }
    return false;
  }

  bool ExpectOperator(std::string_view op)
  {
    if (AcceptOperator(op))
      return true;
    const std::string found = Peek().type == TokenType::End ? "end of expression" : Peek().text;
    Fail(Peek().position, fmt::format("expected '{}' but found '{}'", op, found));
    return false;
  }

  NodePtr ParseConditional()
  {
    NodePtr condition = ParseBinary(0);
    if (Failed() || !AcceptOperator("?"))
      return condition;
    NodePtr if_true = ParseConditional();
    if (Failed() || !ExpectOperator(":"))
      return {};
    NodePtr if_false = ParseConditional();
    if (Failed())
      return {};
    return MakeNode(Kind::Conditional, {}, {condition, if_true, if_false});
  }

  static int Precedence(std::string_view op)
  {
    static constexpr std::pair<std::string_view, int> table[] = {
        {"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5},  {"==", 6},
        {"!=", 6}, {"<", 7},  {"<=", 7}, {">", 7},  {">=", 7}, {"<<", 8},
        {">>", 8}, {"+", 9},  {"-", 9},  {"*", 10}, {"/", 10}, {"%", 10},
    };
    for (const auto& [name, precedence] : table)
    {
      if (name == op)
        return precedence;
    }
    return -1;
  }

  NodePtr ParseBinary(int min_precedence)
  {
    NodePtr left = ParseUnary();
    while (!Failed() && Peek().type == TokenType::Operator)
    {
      const std::string op = Peek().text;
      const int precedence = Precedence(op);
      if (precedence < 0 || precedence <= min_precedence)
        break;
      Next();
      NodePtr right = ParseBinary(precedence);
      if (Failed())
        return {};
      left = MakeNode(Kind::Binary, op, {left, right});
    }
    return Failed() ? NodePtr{} : left;
  }

  NodePtr ParseUnary()
  {
    for (std::string_view op : {"!", "-", "~", "+"})
    {
      if (AcceptOperator(op))
      {
        NodePtr operand = ParseUnary();
        if (Failed())
          return {};
        if (op == "+")
          return operand;
        return MakeNode(Kind::Unary, std::string(op), {operand});
      }
    }
    return ParsePrimary();
  }

  NodePtr ParsePrimary()
  {
    const Token& token = Next();
    switch (token.type)
    {
    case TokenType::Number:
      return MakeConstant(Value(token.number));
    case TokenType::String:
      return MakeConstant(Value(token.text));
    case TokenType::Name:
      return ParseName(token);
    case TokenType::Operator:
      if (token.text == "(")
      {
        NodePtr inner = ParseConditional();
        if (Failed() || !ExpectOperator(")"))
          return {};
        return inner;
      }
      return Fail(token.position, fmt::format("unexpected '{}'", token.text));
    case TokenType::End:
      return Fail(token.position, "unexpected end of expression");
    }
    return Fail(token.position, "unexpected token");
  }

  NodePtr ParseName(const Token& token)
  {
    if (token.text == "true")
      return MakeConstant(Value(true));
    if (token.text == "false")
      return MakeConstant(Value(false));
    if (token.text == "null")
      return MakeConstant(Value::Null());

    if (AcceptOperator("("))
    {
      if (std::ranges::find(KNOWN_FUNCTIONS, token.text) == std::end(KNOWN_FUNCTIONS))
        return Fail(token.position, fmt::format("unknown function '{}'", token.text));

      std::vector<NodePtr> arguments;
      if (!AcceptOperator(")"))
      {
        do
        {
          // count() and table() take a bare name as their first argument.
          if (arguments.empty() && (token.text == "count" || token.text == "table") &&
              Peek().type == TokenType::Name)
          {
            arguments.push_back(MakeConstant(Value(Next().text)));
          }
          else
          {
            arguments.push_back(ParseConditional());
            if (Failed())
              return {};
          }
        } while (AcceptOperator(","));
        if (!ExpectOperator(")"))
          return {};
      }
      if (!ValidateArity(token, arguments.size()))
        return {};
      return MakeNode(Kind::Call, token.text, std::move(arguments));
    }

    if (AcceptOperator("["))
    {
      NodePtr index = ParseConditional();
      if (Failed() || !ExpectOperator("]") || !ExpectOperator("."))
        return {};
      const Token& field = Next();
      if (field.type != TokenType::Name)
        return Fail(field.position, "expected a field name after '.'");
      auto node = std::make_shared<Expression::Node>();
      node->kind = Kind::Element;
      node->name = token.text;
      node->field = field.text;
      node->children = {index};
      return node;
    }

    return MakeNode(Kind::Name, token.text);
  }

  bool ValidateArity(const Token& token, size_t count)
  {
    const std::string& f = token.text;
    size_t min_args = 1, max_args = 1;
    if (f == "min" || f == "max")
    {
      min_args = 1;
      max_args = 64;
    }
    else if (f == "clamp" || f == "bits")
    {
      min_args = max_args = 3;
    }
    else if (f == "bit" || f == "table" || f == "default")
    {
      min_args = max_args = 2;
    }
    if (count >= min_args && count <= max_args)
      return true;
    Fail(token.position, fmt::format("{}() takes {} argument(s), got {}", f,
                                     min_args == max_args ? fmt::format("{}", min_args) :
                                                            fmt::format("{}+", min_args),
                                     count));
    return false;
  }

  std::vector<Token> m_tokens;
  size_t m_index = 0;
  ErrorSink& m_errors;
};

Value NumberOrNull(double value)
{
  return std::isfinite(value) ? Value(value) : Value::Null();
}

std::string ToText(const Value& value)
{
  if (value.IsString())
    return value.String();
  if (value.IsNumber())
  {
    const double n = value.Number();
    if (std::trunc(n) == n && std::abs(n) < 1e15)
      return fmt::format("{}", static_cast<s64>(n));
    return fmt::format("{}", n);
  }
  return {};
}

Value Evaluate(const Expression::Node& node, const Scope& scope);

Value EvaluateBinary(const Expression::Node& node, const Scope& scope)
{
  const std::string& op = node.name;

  // Short-circuiting logic operators.
  if (op == "&&")
  {
    const Value left = Evaluate(*node.children[0], scope);
    if (!left.Truthy())
      return Value(false);
    return Value(Evaluate(*node.children[1], scope).Truthy());
  }
  if (op == "||")
  {
    const Value left = Evaluate(*node.children[0], scope);
    if (left.Truthy())
      return Value(true);
    return Value(Evaluate(*node.children[1], scope).Truthy());
  }

  const Value left = Evaluate(*node.children[0], scope);
  const Value right = Evaluate(*node.children[1], scope);

  if (op == "==" || op == "!=")
  {
    // Comparing against null is allowed so manifests can test whether a read succeeded.
    const bool equal = left == right;
    return Value(op == "==" ? equal : !equal);
  }

  if (op == "+" && (left.IsString() || right.IsString()))
  {
    if (left.IsNull() || right.IsNull())
      return Value::Null();
    return Value(ToText(left) + ToText(right));
  }

  if (left.IsString() && right.IsString())
  {
    const int comparison = left.String().compare(right.String());
    if (op == "<")
      return Value(comparison < 0);
    if (op == "<=")
      return Value(comparison <= 0);
    if (op == ">")
      return Value(comparison > 0);
    if (op == ">=")
      return Value(comparison >= 0);
    return Value::Null();
  }

  const std::optional<double> a = left.AsNumber();
  const std::optional<double> b = right.AsNumber();
  if (!a || !b)
    return Value::Null();

  if (op == "+")
    return NumberOrNull(*a + *b);
  if (op == "-")
    return NumberOrNull(*a - *b);
  if (op == "*")
    return NumberOrNull(*a * *b);
  if (op == "/")
    return *b == 0.0 ? Value::Null() : NumberOrNull(*a / *b);
  if (op == "%")
    return *b == 0.0 ? Value::Null() : NumberOrNull(std::fmod(*a, *b));
  if (op == "<")
    return Value(*a < *b);
  if (op == "<=")
    return Value(*a <= *b);
  if (op == ">")
    return Value(*a > *b);
  if (op == ">=")
    return Value(*a >= *b);

  const std::optional<s64> x = left.AsInteger();
  const std::optional<s64> y = right.AsInteger();
  if (!x || !y)
    return Value::Null();
  if (op == "&")
    return Value(*x & *y);
  if (op == "|")
    return Value(*x | *y);
  if (op == "^")
    return Value(*x ^ *y);
  if (op == "<<")
    return (*y < 0 || *y > 62) ? Value::Null() : Value(*x << *y);
  if (op == ">>")
    return (*y < 0 || *y > 62) ? Value::Null() : Value(*x >> *y);

  return Value::Null();
}

Value EvaluateCall(const Expression::Node& node, const Scope& scope)
{
  const std::string& f = node.name;
  const auto& args = node.children;

  if (f == "count")
  {
    const Value name = Evaluate(*args[0], scope);
    return name.IsString() ? scope.Count(name.String()) : Value::Null();
  }
  if (f == "table")
  {
    const Value name = Evaluate(*args[0], scope);
    if (!name.IsString())
      return Value::Null();
    return scope.Table(name.String(), Evaluate(*args[1], scope));
  }
  if (f == "default")
  {
    Value value = Evaluate(*args[0], scope);
    return value.IsNull() ? Evaluate(*args[1], scope) : value;
  }
  if (f == "str")
  {
    const Value value = Evaluate(*args[0], scope);
    return value.IsNull() ? Value::Null() : Value(ToText(value));
  }

  std::vector<double> numbers;
  numbers.reserve(args.size());
  for (const auto& arg : args)
  {
    const std::optional<double> number = Evaluate(*arg, scope).AsNumber();
    if (!number)
      return Value::Null();
    numbers.push_back(*number);
  }

  if (f == "min")
    return Value(*std::ranges::min_element(numbers));
  if (f == "max")
    return Value(*std::ranges::max_element(numbers));
  if (f == "abs")
    return Value(std::abs(numbers[0]));
  if (f == "floor")
    return Value(std::floor(numbers[0]));
  if (f == "ceil")
    return Value(std::ceil(numbers[0]));
  if (f == "round")
    return Value(std::round(numbers[0]));
  if (f == "clamp")
    return Value(
        std::clamp(numbers[0], std::min(numbers[1], numbers[2]), std::max(numbers[1], numbers[2])));
  if (f == "bit")
  {
    const s64 bit = static_cast<s64>(numbers[1]);
    if (bit < 0 || bit > 62)
      return Value::Null();
    return Value(((static_cast<s64>(numbers[0]) >> bit) & 1) != 0);
  }
  if (f == "bits")
  {
    const s64 shift = static_cast<s64>(numbers[1]);
    const s64 width = static_cast<s64>(numbers[2]);
    if (shift < 0 || width <= 0 || shift + width > 63)
      return Value::Null();
    return Value((static_cast<s64>(numbers[0]) >> shift) & ((s64{1} << width) - 1));
  }
  return Value::Null();
}

Value Evaluate(const Expression::Node& node, const Scope& scope)
{
  switch (node.kind)
  {
  case Kind::Constant:
    return node.constant;
  case Kind::Name:
    return scope.Lookup(node.name);
  case Kind::Element:
  {
    const std::optional<s64> index = Evaluate(*node.children[0], scope).AsInteger();
    if (!index)
      return Value::Null();
    return scope.LookupElement(node.name, *index, node.field);
  }
  case Kind::Unary:
  {
    const Value operand = Evaluate(*node.children[0], scope);
    if (node.name == "!")
      return Value(!operand.Truthy());
    if (node.name == "-")
    {
      const std::optional<double> number = operand.AsNumber();
      return number ? Value(-*number) : Value::Null();
    }
    if (node.name == "~")
    {
      const std::optional<s64> integer = operand.AsInteger();
      return integer ? Value(~*integer) : Value::Null();
    }
    return Value::Null();
  }
  case Kind::Binary:
    return EvaluateBinary(node, scope);
  case Kind::Conditional:
    return Evaluate(*node.children[0], scope).Truthy() ? Evaluate(*node.children[1], scope) :
                                                         Evaluate(*node.children[2], scope);
  case Kind::Call:
    return EvaluateCall(node, scope);
  }
  return Value::Null();
}
}  // namespace

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(const Expression&) = default;
Expression& Expression::operator=(const Expression&) = default;
Expression::Expression(Expression&&) noexcept = default;
Expression& Expression::operator=(Expression&&) noexcept = default;

std::optional<Expression> Expression::Parse(std::string_view source, std::string* error)
{
  ErrorSink errors;
  std::vector<Token> tokens = Tokenize(source, errors);
  NodePtr root;
  if (!errors.Failed())
  {
    Parser parser(std::move(tokens), errors);
    root = parser.ParseAll();
  }
  if (errors.Failed() || !root)
  {
    if (error)
    {
      *error = errors.Failed() ?
                   fmt::format("{} (at column {})", errors.Message(), errors.Position() + 1) :
                   std::string("empty expression");
    }
    return std::nullopt;
  }

  Expression expression;
  expression.m_root = std::move(root);
  expression.m_source = std::string(source);
  return expression;
}

Expression Expression::Constant(Value value)
{
  Expression expression;
  expression.m_root = MakeConstant(value);
  expression.m_source = ToText(value);
  return expression;
}

Value Expression::Evaluate(const Scope& scope) const
{
  if (!m_root)
    return Value::Null();
  return Duo::Evaluate(*m_root, scope);
}
}  // namespace Duo
