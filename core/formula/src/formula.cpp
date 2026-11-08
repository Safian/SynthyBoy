// SynthyBoy - core: Serum-style wavetable formula parser and frame evaluator.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/formula.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace synthyboy::formula
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

inline bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool isAlpha (char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool isDigit (char c) noexcept
{
    return c >= '0' && c <= '9';
}

inline bool isAlphaNum (char c) noexcept
{
    return isAlpha (c) || isDigit (c);
}

inline char toLower (char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char> (c + ('a' - 'A')) : c;
}

bool iequals (std::string_view a, std::string_view b) noexcept
{
    if (a.size () != b.size ())
        return false;
    for (size_t i = 0; i < a.size (); ++i)
    {
        if (toLower (a[i]) != toLower (b[i]))
            return false;
    }
    return true;
}

// Token definitions
enum class TokenType
{
    EndOfFile,
    Number,
    Identifier,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Caret,
    Question,
    Colon,
    Comma,
    LParen,
    RParen,
    EqualEqual,
    ExclamationEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AmpAmp,
    PipePipe,
    Exclamation,
    Error
};

struct Token
{
    TokenType type = TokenType::EndOfFile;
    size_t position = 0;
    double numberValue = 0.0;
    std::string_view text;
    const char* errorMessage = "";
};

class Lexer
{
public:
    explicit Lexer (std::string_view src) noexcept : src_ (src) {}

    Token next () noexcept
    {
        skipWhitespace ();
        if (index_ >= src_.size ())
            return { TokenType::EndOfFile, src_.size (), 0.0, {}, "" };

        const size_t start = index_;
        const char c = src_[index_++];

        switch (c)
        {
            case '+': return { TokenType::Plus, start, 0.0, src_.substr (start, 1), "" };
            case '-': return { TokenType::Minus, start, 0.0, src_.substr (start, 1), "" };
            case '*': return { TokenType::Star, start, 0.0, src_.substr (start, 1), "" };
            case '/': return { TokenType::Slash, start, 0.0, src_.substr (start, 1), "" };
            case '%': return { TokenType::Percent, start, 0.0, src_.substr (start, 1), "" };
            case '^': return { TokenType::Caret, start, 0.0, src_.substr (start, 1), "" };
            case '?': return { TokenType::Question, start, 0.0, src_.substr (start, 1), "" };
            case ':': return { TokenType::Colon, start, 0.0, src_.substr (start, 1), "" };
            case ',': return { TokenType::Comma, start, 0.0, src_.substr (start, 1), "" };
            case '(': return { TokenType::LParen, start, 0.0, src_.substr (start, 1), "" };
            case ')': return { TokenType::RParen, start, 0.0, src_.substr (start, 1), "" };

            case '!':
                if (peek () == '=')
                {
                    ++index_;
                    return { TokenType::ExclamationEqual, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Exclamation, start, 0.0, src_.substr (start, 1), "" };

            case '=':
                if (peek () == '=')
                {
                    ++index_;
                    return { TokenType::EqualEqual, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Error, start, 0.0, src_.substr (start, 1), "Unexpected '=' (use '==' for comparison)" };

            case '<':
                if (peek () == '=')
                {
                    ++index_;
                    return { TokenType::LessEqual, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Less, start, 0.0, src_.substr (start, 1), "" };

            case '>':
                if (peek () == '=')
                {
                    ++index_;
                    return { TokenType::GreaterEqual, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Greater, start, 0.0, src_.substr (start, 1), "" };

            case '&':
                if (peek () == '&')
                {
                    ++index_;
                    return { TokenType::AmpAmp, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Error, start, 0.0, src_.substr (start, 1), "Unexpected '&' (use '&&' for logical AND)" };

            case '|':
                if (peek () == '|')
                {
                    ++index_;
                    return { TokenType::PipePipe, start, 0.0, src_.substr (start, 2), "" };
                }
                return { TokenType::Error, start, 0.0, src_.substr (start, 1), "Unexpected '|' (use '||' for logical OR)" };

            case '.':
                if (isDigit (peek ()))
                {
                    --index_;
                    return lexNumber ();
                }
                return { TokenType::Error, start, 0.0, src_.substr (start, 1), "Unexpected '.'" };

            default:
                if (isDigit (c))
                {
                    --index_;
                    return lexNumber ();
                }
                if (isAlpha (c))
                {
                    --index_;
                    return lexIdentifier ();
                }
                return { TokenType::Error, start, 0.0, src_.substr (start, 1), "Invalid character in expression" };
        }
    }

private:
    char peek () const noexcept
    {
        if (index_ < src_.size ())
            return src_[index_];
        return '\0';
    }

    void skipWhitespace () noexcept
    {
        while (index_ < src_.size () && isSpace (src_[index_]))
            ++index_;
    }

    Token lexIdentifier () noexcept
    {
        const size_t start = index_;
        while (index_ < src_.size () && isAlphaNum (src_[index_]))
            ++index_;
        return { TokenType::Identifier, start, 0.0, src_.substr (start, index_ - start), "" };
    }

    Token lexNumber () noexcept
    {
        const size_t start = index_;
        bool seenDot = false;
        bool seenExp = false;

        while (index_ < src_.size ())
        {
            const char c = src_[index_];
            if (isDigit (c))
            {
                ++index_;
            }
            else if (c == '.' && ! seenDot && ! seenExp)
            {
                seenDot = true;
                ++index_;
            }
            else if ((c == 'e' || c == 'E') && ! seenExp)
            {
                seenExp = true;
                seenDot = true;
                ++index_;
                if (index_ < src_.size () && (src_[index_] == '+' || src_[index_] == '-'))
                    ++index_;
            }
            else
            {
                break;
            }
        }

        std::string_view numText = src_.substr (start, index_ - start);
        double val = 0.0;
        char buf[64];
        if (numText.size () < sizeof (buf))
        {
            std::memcpy (buf, numText.data (), numText.size ());
            buf[numText.size ()] = '\0';
            char* endPtr = nullptr;
            val = std::strtod (buf, &endPtr);
        }
        return { TokenType::Number, start, val, numText, "" };
    }

    std::string_view src_;
    size_t index_ = 0;
};

// Safe math helpers
inline double safeSin (double x) noexcept { return std::sin (x); }
inline double safeCos (double x) noexcept { return std::cos (x); }
inline double safeTan (double x) noexcept
{
    const double c = std::cos (x);
    if (std::fabs (c) < 1e-12)
        return 0.0;
    const double r = std::tan (x);
    return std::isfinite (r) ? r : 0.0;
}
inline double safeAsin (double x) noexcept { return std::asin (std::clamp (x, -1.0, 1.0)); }
inline double safeAcos (double x) noexcept { return std::acos (std::clamp (x, -1.0, 1.0)); }
inline double safeAtan (double x) noexcept { return std::atan (x); }
inline double safeSinh (double x) noexcept { return std::sinh (std::clamp (x, -80.0, 80.0)); }
inline double safeCosh (double x) noexcept { return std::cosh (std::clamp (x, -80.0, 80.0)); }
inline double safeTanh (double x) noexcept { return std::tanh (x); }
inline double safeAsinh (double x) noexcept { return std::asinh (x); }
inline double safeAcosh (double x) noexcept { return (x < 1.0) ? 0.0 : std::acosh (x); }
inline double safeAtanh (double x) noexcept { return std::atanh (std::clamp (x, -0.9999999, 0.9999999)); }
inline double safeLog2 (double x) noexcept { return (x <= 0.0) ? 0.0 : std::log2 (x); }
inline double safeLog10 (double x) noexcept { return (x <= 0.0) ? 0.0 : std::log10 (x); }
inline double safeLn (double x) noexcept { return (x <= 0.0) ? 0.0 : std::log (x); }
inline double safeExp (double x) noexcept { return std::exp (std::clamp (x, -80.0, 80.0)); }
inline double safeSqrt (double x) noexcept { return (x < 0.0) ? 0.0 : std::sqrt (x); }
inline double safeSign (double x) noexcept { return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0); }
inline double safeRint (double x) noexcept { return std::round (x); }
inline double safeAbs (double x) noexcept { return std::fabs (x); }

inline double safeDiv (double a, double b) noexcept
{
    if (std::fabs (b) < 1e-15)
        return 0.0;
    const double r = a / b;
    return std::isfinite (r) ? r : 0.0;
}

inline double safeMod (double a, double b) noexcept
{
    if (std::fabs (b) < 1e-15)
        return 0.0;
    const double r = std::fmod (a, b);
    return std::isfinite (r) ? r : 0.0;
}

inline double safePow (double a, double b) noexcept
{
    if (std::fabs (b) < 1e-15)
        return 1.0;
    if (std::fabs (a) < 1e-15)
        return 0.0;
    if (a < 0.0 && std::floor (b) != b)
        return 0.0;
    const double r = std::pow (a, b);
    return std::isfinite (r) ? r : 0.0;
}

inline bool isTruthy (double v) noexcept
{
    return std::fabs (v) > 1e-9;
}

double evalFunc1 (FuncId id, double x) noexcept
{
    switch (id)
    {
        case FuncId::Sin:   return safeSin (x);
        case FuncId::Cos:   return safeCos (x);
        case FuncId::Tan:   return safeTan (x);
        case FuncId::Asin:  return safeAsin (x);
        case FuncId::Acos:  return safeAcos (x);
        case FuncId::Atan:  return safeAtan (x);
        case FuncId::Sinh:  return safeSinh (x);
        case FuncId::Cosh:  return safeCosh (x);
        case FuncId::Tanh:  return safeTanh (x);
        case FuncId::Asinh: return safeAsinh (x);
        case FuncId::Acosh: return safeAcosh (x);
        case FuncId::Atanh: return safeAtanh (x);
        case FuncId::Log2:  return safeLog2 (x);
        case FuncId::Log10: return safeLog10 (x);
        case FuncId::Log:   return safeLog10 (x);
        case FuncId::Ln:    return safeLn (x);
        case FuncId::Exp:   return safeExp (x);
        case FuncId::Sqrt:  return safeSqrt (x);
        case FuncId::Sign:  return safeSign (x);
        case FuncId::Rint:  return safeRint (x);
        case FuncId::Abs:   return safeAbs (x);
        default:            return 0.0;
    }
}

double evalFuncN (FuncId id, const double* args, int count) noexcept
{
    if (count < 1)
        return 0.0;
    switch (id)
    {
        case FuncId::Min:
        {
            double m = args[0];
            for (int i = 1; i < count; ++i)
                if (args[i] < m) m = args[i];
            return m;
        }
        case FuncId::Max:
        {
            double m = args[0];
            for (int i = 1; i < count; ++i)
                if (args[i] > m) m = args[i];
            return m;
        }
        case FuncId::Sum:
        {
            double s = 0.0;
            for (int i = 0; i < count; ++i)
                s += args[i];
            return s;
        }
        case FuncId::Avg:
        {
            double s = 0.0;
            for (int i = 0; i < count; ++i)
                s += args[i];
            return s / static_cast<double> (count);
        }
        default:
            return 0.0;
    }
}
} // namespace

class Compiler
{
public:
    Compiler (std::string_view src, Formula& out) noexcept
        : lexer_ (src), formula_ (out) {}

    Error compile () noexcept
    {
        formula_.clear ();
        advance ();
        if (current_.type == TokenType::EndOfFile)
            return { false, 0, "Empty expression" };

        Error err = parseExpression (0);
        if (! err.ok)
            return err;

        if (current_.type != TokenType::EndOfFile)
            return { false, current_.position, "Unexpected trailing token" };

        if (emitHalt () >= kMaxBytecodeOps)
            return { false, current_.position, "Expression produced too many instructions" };

        formula_.valid_ = true;
        return { true, 0, "" };
    }

private:
    void advance () noexcept
    {
        current_ = lexer_.next ();
    }

    uint16_t addConstant (double val) noexcept
    {
        for (size_t i = 0; i < formula_.constants_.size (); ++i)
        {
            if (formula_.constants_[i] == val)
                return static_cast<uint16_t> (i);
        }
        formula_.constants_.push_back (val);
        return static_cast<uint16_t> (formula_.constants_.size () - 1);
    }

    size_t emit (Opcode op) noexcept
    {
        Instruction inst;
        inst.op = op;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitJump (Opcode op) noexcept
    {
        Instruction inst;
        inst.op = op;
        inst.arg16 = 0;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitConst (double val) noexcept
    {
        Instruction inst;
        inst.op = Opcode::PushConst;
        inst.arg16 = addConstant (val);
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitVar (VarId v, uint32_t mask) noexcept
    {
        formula_.variableMask_ |= mask;
        Instruction inst;
        inst.op = Opcode::PushVar;
        inst.varId = v;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitCall1 (FuncId f) noexcept
    {
        Instruction inst;
        inst.op = Opcode::Call1;
        inst.func = f;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitCallN (FuncId f, uint8_t count) noexcept
    {
        Instruction inst;
        inst.op = Opcode::CallN;
        inst.func = f;
        inst.argCount = count;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    size_t emitHalt () noexcept
    {
        Instruction inst;
        inst.op = Opcode::Halt;
        formula_.instructions_.push_back (inst);
        return formula_.instructions_.size () - 1;
    }

    void patchJump (size_t index, size_t targetIP) noexcept
    {
        if (index < formula_.instructions_.size ())
            formula_.instructions_[index].arg16 = static_cast<uint16_t> (targetIP);
    }

    Error parseExpression (int depth) noexcept
    {
        return parseConditional (depth);
    }

    Error parseConditional (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseLogicalOr (depth + 1);
        if (! err.ok)
            return err;

        if (current_.type == TokenType::Question)
        {
            advance ();
            const size_t jumpIfFalse = emitJump (Opcode::JumpIfFalse);

            err = parseConditional (depth + 1);
            if (! err.ok)
                return err;

            if (current_.type != TokenType::Colon)
                return { false, current_.position, "Expected ':' in conditional expression" };
            advance ();

            const size_t jumpEnd = emitJump (Opcode::Jump);
            patchJump (jumpIfFalse, formula_.instructions_.size ());

            err = parseConditional (depth + 1);
            if (! err.ok)
                return err;

            patchJump (jumpEnd, formula_.instructions_.size ());
        }

        return { true, 0, "" };
    }

    Error parseLogicalOr (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseLogicalAnd (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::PipePipe)
        {
            advance ();
            err = parseLogicalAnd (depth + 1);
            if (! err.ok)
                return err;
            emit (Opcode::Or);
        }

        return { true, 0, "" };
    }

    Error parseLogicalAnd (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseEquality (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::AmpAmp)
        {
            advance ();
            err = parseEquality (depth + 1);
            if (! err.ok)
                return err;
            emit (Opcode::And);
        }

        return { true, 0, "" };
    }

    Error parseEquality (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseRelational (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::EqualEqual || current_.type == TokenType::ExclamationEqual)
        {
            const TokenType op = current_.type;
            advance ();
            err = parseRelational (depth + 1);
            if (! err.ok)
                return err;
            emit (op == TokenType::EqualEqual ? Opcode::Eq : Opcode::Neq);
        }

        return { true, 0, "" };
    }

    Error parseRelational (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseAdditive (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::Less || current_.type == TokenType::LessEqual ||
               current_.type == TokenType::Greater || current_.type == TokenType::GreaterEqual)
        {
            const TokenType op = current_.type;
            advance ();
            err = parseAdditive (depth + 1);
            if (! err.ok)
                return err;
            switch (op)
            {
                case TokenType::Less:         emit (Opcode::Lt); break;
                case TokenType::LessEqual:    emit (Opcode::Lte); break;
                case TokenType::Greater:      emit (Opcode::Gt); break;
                case TokenType::GreaterEqual: emit (Opcode::Gte); break;
                default: break;
            }
        }

        return { true, 0, "" };
    }

    Error parseAdditive (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseMultiplicative (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::Plus || current_.type == TokenType::Minus)
        {
            const TokenType op = current_.type;
            advance ();
            err = parseMultiplicative (depth + 1);
            if (! err.ok)
                return err;
            emit (op == TokenType::Plus ? Opcode::Add : Opcode::Sub);
        }

        return { true, 0, "" };
    }

    Error parseMultiplicative (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parseUnary (depth + 1);
        if (! err.ok)
            return err;

        while (current_.type == TokenType::Star || current_.type == TokenType::Slash || current_.type == TokenType::Percent)
        {
            const TokenType op = current_.type;
            advance ();
            err = parseUnary (depth + 1);
            if (! err.ok)
                return err;
            switch (op)
            {
                case TokenType::Star:    emit (Opcode::Mul); break;
                case TokenType::Slash:   emit (Opcode::Div); break;
                case TokenType::Percent: emit (Opcode::Mod); break;
                default: break;
            }
        }

        return { true, 0, "" };
    }

    Error parseUnary (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        if (current_.type == TokenType::Plus)
        {
            advance ();
            return parseUnary (depth + 1);
        }
        if (current_.type == TokenType::Minus)
        {
            advance ();
            Error err = parseUnary (depth + 1);
            if (! err.ok)
                return err;
            emit (Opcode::Neg);
            return { true, 0, "" };
        }
        if (current_.type == TokenType::Exclamation)
        {
            advance ();
            Error err = parseUnary (depth + 1);
            if (! err.ok)
                return err;
            emit (Opcode::Not);
            return { true, 0, "" };
        }

        return parsePower (depth + 1);
    }

    Error parsePower (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        Error err = parsePrimary (depth + 1);
        if (! err.ok)
            return err;

        if (current_.type == TokenType::Caret)
        {
            advance ();
            err = parseUnary (depth + 1);
            if (! err.ok)
                return err;
            emit (Opcode::Pow);
        }

        return { true, 0, "" };
    }

    Error parsePrimary (int depth) noexcept
    {
        if (depth > kMaxRecursionDepth)
            return { false, current_.position, "Recursion depth limit exceeded" };

        if (current_.type == TokenType::Number)
        {
            emitConst (current_.numberValue);
            advance ();
            return { true, 0, "" };
        }

        if (current_.type == TokenType::LParen)
        {
            advance ();
            Error err = parseExpression (depth + 1);
            if (! err.ok)
                return err;
            if (current_.type != TokenType::RParen)
                return { false, current_.position, "Expected closing ')'" };
            advance ();
            return { true, 0, "" };
        }

        if (current_.type == TokenType::Identifier)
        {
            const std::string_view name = current_.text;
            const size_t pos = current_.position;
            advance ();

            if (current_.type == TokenType::LParen)
            {
                advance ();
                return parseFunctionCall (name, pos, depth + 1);
            }

            return parseVariableOrConstant (name, pos);
        }

        if (current_.type == TokenType::Error)
            return { false, current_.position, current_.errorMessage };

        return { false, current_.position, "Unexpected token in expression" };
    }

    Error parseFunctionCall (std::string_view name, size_t pos, int depth) noexcept
    {
        // 1-argument functions
        constexpr struct { const char* name; FuncId id; } kFuncs1[] = {
            { "sin",   FuncId::Sin },
            { "cos",   FuncId::Cos },
            { "tan",   FuncId::Tan },
            { "asin",  FuncId::Asin },
            { "acos",  FuncId::Acos },
            { "atan",  FuncId::Atan },
            { "sinh",  FuncId::Sinh },
            { "cosh",  FuncId::Cosh },
            { "tanh",  FuncId::Tanh },
            { "asinh", FuncId::Asinh },
            { "acosh", FuncId::Acosh },
            { "atanh", FuncId::Atanh },
            { "log2",  FuncId::Log2 },
            { "log10", FuncId::Log10 },
            { "log",   FuncId::Log },
            { "ln",    FuncId::Ln },
            { "exp",   FuncId::Exp },
            { "sqrt",  FuncId::Sqrt },
            { "sign",  FuncId::Sign },
            { "rint",  FuncId::Rint },
            { "abs",   FuncId::Abs }
        };

        for (const auto& f : kFuncs1)
        {
            if (iequals (name, f.name))
            {
                if (current_.type == TokenType::RParen)
                    return { false, current_.position, "Function requires exactly 1 argument" };

                Error err = parseExpression (depth + 1);
                if (! err.ok)
                    return err;

                if (current_.type == TokenType::Comma)
                    return { false, current_.position, "Function requires exactly 1 argument" };

                if (current_.type != TokenType::RParen)
                    return { false, current_.position, "Expected closing ')' after function argument" };

                advance ();
                emitCall1 (f.id);
                return { true, 0, "" };
            }
        }

        // Variadic functions
        constexpr struct { const char* name; FuncId id; } kFuncsN[] = {
            { "min", FuncId::Min },
            { "max", FuncId::Max },
            { "sum", FuncId::Sum },
            { "avg", FuncId::Avg }
        };

        for (const auto& f : kFuncsN)
        {
            if (iequals (name, f.name))
            {
                if (current_.type == TokenType::RParen)
                    return { false, current_.position, "Function requires at least 1 argument" };

                uint8_t count = 0;
                while (true)
                {
                    Error err = parseExpression (depth + 1);
                    if (! err.ok)
                        return err;
                    ++count;

                    if (current_.type == TokenType::Comma)
                    {
                        advance ();
                        continue;
                    }
                    if (current_.type == TokenType::RParen)
                    {
                        advance ();
                        break;
                    }
                    return { false, current_.position, "Expected ',' or ')' in argument list" };
                }

                emitCallN (f.id, count);
                return { true, 0, "" };
            }
        }

        return { false, pos, "Unknown function" };
    }

    Error parseVariableOrConstant (std::string_view name, size_t pos) noexcept
    {
        if (iequals (name, "pi"))
        {
            emitConst (kPi);
            return { true, 0, "" };
        }
        if (iequals (name, "e"))
        {
            emitConst (kE);
            return { true, 0, "" };
        }
        if (iequals (name, "x"))
        {
            emitVar (VarId::X, kVarX);
            return { true, 0, "" };
        }
        if (iequals (name, "w"))
        {
            emitVar (VarId::W, kVarW);
            return { true, 0, "" };
        }
        if (iequals (name, "y"))
        {
            emitVar (VarId::Y, kVarY);
            return { true, 0, "" };
        }
        if (iequals (name, "z"))
        {
            emitVar (VarId::Z, kVarZ);
            return { true, 0, "" };
        }
        if (iequals (name, "q"))
        {
            emitVar (VarId::Q, kVarQ);
            return { true, 0, "" };
        }
        if (iequals (name, "in"))
        {
            emitVar (VarId::In, kVarIn);
            return { true, 0, "" };
        }
        if (iequals (name, "sel"))
        {
            emitVar (VarId::Sel, kVarSel);
            return { true, 0, "" };
        }
        if (iequals (name, "rand"))
        {
            emitVar (VarId::Rand, kVarRand);
            return { true, 0, "" };
        }

        return { false, pos, "Unknown variable or identifier" };
    }

    Lexer lexer_;
    Token current_;
    Formula& formula_;
};

void Formula::clear () noexcept
{
    instructions_.clear ();
    constants_.clear ();
    variableMask_ = 0;
    valid_ = false;
}

Error Formula::compile (std::string_view expression, Formula& out) noexcept
{
    out.clear ();
    if (expression.size () > kMaxExpressionLength)
        return { false, 0, "Expression exceeds maximum length of 4096 characters" };

    Compiler comp (expression, out);
    return comp.compile ();
}

float Formula::evaluate (const EvalContext& ctx) const noexcept
{
    if (! valid_ || instructions_.empty ())
        return 0.0f;

    double stack[kMaxStackSize];
    int sp = 0;
    size_t ip = 0;

    while (ip < instructions_.size ())
    {
        const Instruction& inst = instructions_[ip++];
        switch (inst.op)
        {
            case Opcode::PushConst:
                if (sp < static_cast<int> (kMaxStackSize))
                    stack[sp++] = constants_[inst.arg16];
                break;

            case Opcode::PushVar:
                if (sp < static_cast<int> (kMaxStackSize))
                {
                    switch (inst.varId)
                    {
                        case VarId::X:    stack[sp++] = ctx.x; break;
                        case VarId::W:    stack[sp++] = ctx.w; break;
                        case VarId::Y:    stack[sp++] = ctx.y; break;
                        case VarId::Z:    stack[sp++] = ctx.z; break;
                        case VarId::Q:    stack[sp++] = ctx.q; break;
                        case VarId::In:   stack[sp++] = ctx.inVal; break;
                        case VarId::Sel:  stack[sp++] = ctx.selVal; break;
                        case VarId::Rand: stack[sp++] = ctx.randVal; break;
                    }
                }
                break;

            case Opcode::Neg:
                if (sp >= 1)
                    stack[sp - 1] = -stack[sp - 1];
                break;

            case Opcode::Not:
                if (sp >= 1)
                    stack[sp - 1] = isTruthy (stack[sp - 1]) ? 0.0 : 1.0;
                break;

            case Opcode::Add:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] += stack[sp];
                }
                break;

            case Opcode::Sub:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] -= stack[sp];
                }
                break;

            case Opcode::Mul:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] *= stack[sp];
                }
                break;

            case Opcode::Div:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = safeDiv (stack[sp - 1], stack[sp]);
                }
                break;

            case Opcode::Mod:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = safeMod (stack[sp - 1], stack[sp]);
                }
                break;

            case Opcode::Pow:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = safePow (stack[sp - 1], stack[sp]);
                }
                break;

            case Opcode::Eq:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (std::fabs (stack[sp - 1] - stack[sp]) < 1e-9) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Neq:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (std::fabs (stack[sp - 1] - stack[sp]) >= 1e-9) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Lt:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (stack[sp - 1] < stack[sp]) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Lte:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (stack[sp - 1] <= stack[sp]) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Gt:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (stack[sp - 1] > stack[sp]) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Gte:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (stack[sp - 1] >= stack[sp]) ? 1.0 : 0.0;
                }
                break;

            case Opcode::And:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (isTruthy (stack[sp - 1]) && isTruthy (stack[sp])) ? 1.0 : 0.0;
                }
                break;

            case Opcode::Or:
                if (sp >= 2)
                {
                    --sp;
                    stack[sp - 1] = (isTruthy (stack[sp - 1]) || isTruthy (stack[sp])) ? 1.0 : 0.0;
                }
                break;

            case Opcode::JumpIfFalse:
                if (sp >= 1)
                {
                    const double cond = stack[--sp];
                    if (! isTruthy (cond))
                        ip = inst.arg16;
                }
                break;

            case Opcode::Jump:
                ip = inst.arg16;
                break;

            case Opcode::Call1:
                if (sp >= 1)
                    stack[sp - 1] = evalFunc1 (inst.func, stack[sp - 1]);
                break;

            case Opcode::CallN:
                if (sp >= inst.argCount && inst.argCount >= 1)
                {
                    sp -= inst.argCount;
                    stack[sp] = evalFuncN (inst.func, &stack[sp], inst.argCount);
                    ++sp;
                }
                break;

            case Opcode::Halt:
                goto done;
        }
    }

done:
    if (sp != 1)
        return 0.0f;

    double result = stack[0];
    if (std::isnan (result))
        result = 0.0;
    else if (result > 1e9)
        result = 1e9;
    else if (result < -1e9)
        result = -1e9;

    return static_cast<float> (result);
}

float Formula::evaluate (double x, int frameIndex, int numFrames,
                         float inVal, float selVal, float randVal, double q) const noexcept
{
    EvalContext ctx;
    ctx.x = x;
    ctx.w = (x + 1.0) * 0.5;
    ctx.y = (numFrames > 1) ? static_cast<double> (frameIndex) / static_cast<double> (numFrames - 1) : 0.0;
    ctx.z = (ctx.y * 2.0) - 1.0;
    ctx.q = q;
    ctx.inVal = inVal;
    ctx.selVal = selVal;
    ctx.randVal = randVal;
    return evaluate (ctx);
}

Error renderFrames (const Formula& formula, int numFrames, float* out,
                    const float* existingFrames, int selectedFrame,
                    FFTRenderHook fftHook, void* userData) noexcept
{
    if (! formula.isValid ())
        return { false, 0, "Formula is invalid or uncompiled" };
    if (numFrames < 1 || numFrames > kMaxFrames)
        return { false, 0, "Number of frames must be between 1 and 256" };
    if (out == nullptr)
        return { false, 0, "Output buffer pointer is null" };

    if (formula.usesQ ())
    {
        if (fftHook != nullptr)
        {
            for (int f = 0; f < numFrames; ++f)
                fftHook (formula, f, numFrames, &out[f * kSamplesPerFrame], userData);
            return { true, 0, "" };
        }

        // Built-in additive synthesis default for FFT mode
        float randBins[kFFTBins];
        uint32_t rng = 123456789u;
        for (int q = 0; q < kFFTBins; ++q)
        {
            rng = rng * 1664525u + 1013904223u;
            randBins[q] = static_cast<float> (static_cast<double> (rng) / 4294967296.0 * 2.0 - 1.0);
        }

        constexpr double kTwoPi = 6.28318530717958647692;

        for (int f = 0; f < numFrames; ++f)
        {
            const double y = (numFrames > 1) ? static_cast<double> (f) / static_cast<double> (numFrames - 1) : 0.0;
            const double z = (y * 2.0) - 1.0;
            float* frameOut = &out[f * kSamplesPerFrame];
            std::fill_n (frameOut, kSamplesPerFrame, 0.0f);

            float amps[kFFTBins];
            for (int q = 1; q <= kFFTBins; ++q)
            {
                EvalContext ctx;
                ctx.x = 0.0;
                ctx.w = 0.0;
                ctx.y = y;
                ctx.z = z;
                ctx.q = static_cast<double> (q);
                ctx.inVal = (existingFrames != nullptr) ? existingFrames[f * kSamplesPerFrame + (q - 1)] : 0.0;
                ctx.selVal = (existingFrames != nullptr && selectedFrame >= 0 && selectedFrame < numFrames)
                                 ? existingFrames[selectedFrame * kSamplesPerFrame + (q - 1)]
                                 : 0.0;
                ctx.randVal = randBins[q - 1];
                amps[q - 1] = formula.evaluate (ctx);
            }

            for (int q = 1; q <= kFFTBins; ++q)
            {
                const float a = amps[q - 1];
                if (std::fabs (a) < 1e-6f)
                    continue;

                const double step = kTwoPi * static_cast<double> (q) / static_cast<double> (kSamplesPerFrame);
                for (int s = 0; s < kSamplesPerFrame; ++s)
                    frameOut[s] += a * static_cast<float> (std::sin (step * s));
            }

            float peak = 0.0f;
            for (int s = 0; s < kSamplesPerFrame; ++s)
                if (std::fabs (frameOut[s]) > peak)
                    peak = std::fabs (frameOut[s]);

            if (peak > 1.0f)
            {
                const float invPeak = 1.0f / peak;
                for (int s = 0; s < kSamplesPerFrame; ++s)
                    frameOut[s] *= invPeak;
            }
        }

        return { true, 0, "" };
    }

    // Time domain rendering
    float randSamples[kSamplesPerFrame];
    uint32_t rng = 987654321u;
    for (int s = 0; s < kSamplesPerFrame; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        randSamples[s] = static_cast<float> (static_cast<double> (rng) / 4294967296.0 * 2.0 - 1.0);
    }

    for (int f = 0; f < numFrames; ++f)
    {
        const double y = (numFrames > 1) ? static_cast<double> (f) / static_cast<double> (numFrames - 1) : 0.0;
        const double z = (y * 2.0) - 1.0;
        float* frameOut = &out[f * kSamplesPerFrame];

        for (int s = 0; s < kSamplesPerFrame; ++s)
        {
            const double w = static_cast<double> (s) / static_cast<double> (kSamplesPerFrame - 1);
            const double x = -1.0 + 2.0 * w;

            EvalContext ctx;
            ctx.x = x;
            ctx.w = w;
            ctx.y = y;
            ctx.z = z;
            ctx.q = 1.0;
            ctx.inVal = (existingFrames != nullptr) ? existingFrames[f * kSamplesPerFrame + s] : 0.0;
            ctx.selVal = (existingFrames != nullptr && selectedFrame >= 0 && selectedFrame < numFrames)
                             ? existingFrames[selectedFrame * kSamplesPerFrame + s]
                             : 0.0;
            ctx.randVal = randSamples[s];

            frameOut[s] = std::clamp (formula.evaluate (ctx), -1.0f, 1.0f);
        }
    }

    return { true, 0, "" };
}

Error renderFrames (std::string_view formulaSource, int numFrames, float* out,
                    const float* existingFrames, int selectedFrame,
                    FFTRenderHook fftHook, void* userData) noexcept
{
    Formula formula;
    Error err = Formula::compile (formulaSource, formula);
    if (! err.ok)
        return err;
    return renderFrames (formula, numFrames, out, existingFrames, selectedFrame, fftHook, userData);
}

} // namespace synthyboy::formula
