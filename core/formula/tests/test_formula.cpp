// SynthyBoy - core: tests for formula parser and wavetable frame evaluator.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "synthyboy/formula.h"
#include "test_runner.h"

using namespace synthyboy::formula;

TEST (formula_arithmetic_and_precedence)
{
    Formula f;
    Error err;

    // Basic arithmetic
    err = Formula::compile ("1 + 2 * 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 7.0f, 1e-5f);

    err = Formula::compile ("(1 + 2) * 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 9.0f, 1e-5f);

    err = Formula::compile ("10 - 4 - 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.0f, 1e-5f);

    err = Formula::compile ("12 / 3 / 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.0f, 1e-5f);

    // Modulo %
    err = Formula::compile ("10 % 4", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.0f, 1e-5f);

    err = Formula::compile ("14 % 5 % 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // Exponentiation ^ (right-associative)
    err = Formula::compile ("2 ^ 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 8.0f, 1e-5f);

    // 2 ^ 3 ^ 2 should be 2 ^ (3 ^ 2) = 2 ^ 9 = 512
    err = Formula::compile ("2 ^ 3 ^ 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 512.0f, 1e-5f);

    // Precedence: ^ binds tighter than *
    err = Formula::compile ("2 * 3 ^ 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 18.0f, 1e-5f);

    // Relational comparisons
    err = Formula::compile ("3 < 5", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("5 < 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    err = Formula::compile ("3 <= 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("4 > 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("2 >= 2", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("5 == 5", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("5 != 4", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // Logical operators
    err = Formula::compile ("1 && 1", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("1 && 0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    err = Formula::compile ("0 || 1", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("0 || 0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Precedence: && binds tighter than ||
    err = Formula::compile ("1 || 0 && 0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("0 && 0 || 1", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // Comparisons bind tighter than &&
    err = Formula::compile ("1 < 2 && 3 < 4", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // Unary operators
    err = Formula::compile ("-5 + 8", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 3.0f, 1e-5f);

    err = Formula::compile ("+7", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 7.0f, 1e-5f);

    err = Formula::compile ("!0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    err = Formula::compile ("!1", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Ternary conditional ?:
    err = Formula::compile ("1 ? 10 : 20", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 10.0f, 1e-5f);

    err = Formula::compile ("0 ? 10 : 20", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 20.0f, 1e-5f);

    // Right-associative ternary: 0 ? 1 : 1 ? 2 : 3 -> 2
    err = Formula::compile ("0 ? 1 : 1 ? 2 : 3", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.0f, 1e-5f);

    // Short-circuiting check: division by zero in unselected branch
    err = Formula::compile ("1 ? 42 : (1 / 0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 42.0f, 1e-5f);
}

TEST (formula_each_function_in_manual)
{
    Formula f;
    Error err;

    // 1. sin
    err = Formula::compile ("sin(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);
    err = Formula::compile ("sin(pi / 2)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // 2. cos
    err = Formula::compile ("cos(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);
    err = Formula::compile ("cos(pi)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), -1.0f, 1e-5f);

    // 3. tan
    err = Formula::compile ("tan(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 4. asin
    err = Formula::compile ("asin(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);
    err = Formula::compile ("asin(1)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), static_cast<float> (3.141592653589793 / 2.0), 1e-5f);

    // 5. acos
    err = Formula::compile ("acos(1)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 6. atan
    err = Formula::compile ("atan(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 7. sinh
    err = Formula::compile ("sinh(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 8. cosh
    err = Formula::compile ("cosh(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // 9. tanh
    err = Formula::compile ("tanh(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 10. asinh
    err = Formula::compile ("asinh(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 11. acosh
    err = Formula::compile ("acosh(1)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 12. atanh
    err = Formula::compile ("atanh(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 13. log2
    err = Formula::compile ("log2(8)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 3.0f, 1e-5f);

    // 14. log10
    err = Formula::compile ("log10(100)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.0f, 1e-5f);

    // 15. log (logarithm base 10 per Serum manual p. 305)
    err = Formula::compile ("log(1000)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 3.0f, 1e-5f);

    // 16. ln (natural logarithm base e)
    err = Formula::compile ("ln(e)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // 17. exp
    err = Formula::compile ("exp(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // 18. sqrt
    err = Formula::compile ("sqrt(16)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.0f, 1e-5f);
    err = Formula::compile ("sqrt(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 19. sign
    err = Formula::compile ("sign(5.2)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);
    err = Formula::compile ("sign(-3.1)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), -1.0f, 1e-5f);
    err = Formula::compile ("sign(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // 20. rint
    err = Formula::compile ("rint(1.2)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);
    err = Formula::compile ("rint(1.8)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.0f, 1e-5f);
    err = Formula::compile ("rint(-1.8)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), -2.0f, 1e-5f);

    // 21. abs
    err = Formula::compile ("abs(-4.5)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.5f, 1e-5f);
    err = Formula::compile ("abs(4.5)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.5f, 1e-5f);

    // 22. min (variadic)
    err = Formula::compile ("min(5)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 5.0f, 1e-5f);
    err = Formula::compile ("min(3, 1, 4, 2)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 1.0f, 1e-5f);

    // 23. max (variadic)
    err = Formula::compile ("max(5)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 5.0f, 1e-5f);
    err = Formula::compile ("max(3, 1, 4, 2)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.0f, 1e-5f);

    // 24. sum (variadic)
    err = Formula::compile ("sum(10)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 10.0f, 1e-5f);
    err = Formula::compile ("sum(1, 2, 3, 4)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 10.0f, 1e-5f);

    // 25. avg (variadic)
    err = Formula::compile ("avg(10)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 10.0f, 1e-5f);
    err = Formula::compile ("avg(2, 4, 6)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 4.0f, 1e-5f);
}

TEST (formula_constants_and_variables)
{
    Formula f;
    Error err;

    // Constants
    err = Formula::compile ("pi", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 3.14159265f, 1e-5f);

    err = Formula::compile ("e", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 2.7182818f, 1e-5f);

    // Variables inspection
    err = Formula::compile ("x + 1", f);
    CHECK (err.ok);
    CHECK (f.usesX ());
    CHECK (! f.usesW ());
    CHECK (! f.usesY ());
    CHECK (! f.usesZ ());
    CHECK (! f.usesQ ());
    CHECK (! f.isMultiFrame ());

    err = Formula::compile ("w * 2", f);
    CHECK (err.ok);
    CHECK (f.usesW ());

    err = Formula::compile ("y + z", f);
    CHECK (err.ok);
    CHECK (f.usesY ());
    CHECK (f.usesZ ());
    CHECK (f.isMultiFrame ());

    err = Formula::compile ("q < 17", f);
    CHECK (err.ok);
    CHECK (f.usesQ ());

    err = Formula::compile ("in + sel + rand", f);
    CHECK (err.ok);
    CHECK (f.usesIn ());
    CHECK (f.usesSel ());
    CHECK (f.usesRand ());

    // Value evaluation with context
    EvalContext ctx;
    ctx.x = 0.5;
    ctx.w = 0.75;
    ctx.y = 0.25;
    ctx.z = -0.5;
    ctx.q = 12.0;
    ctx.inVal = 0.3;
    ctx.selVal = -0.4;
    ctx.randVal = 0.1;

    err = Formula::compile ("x + w + y + z + in + sel + rand", f);
    CHECK (err.ok);
    float expected = static_cast<float> (0.5 + 0.75 + 0.25 - 0.5 + 0.3 - 0.4 + 0.1);
    CHECK_NEAR (f.evaluate (ctx), expected, 1e-5f);
}

TEST (formula_manual_examples)
{
    Formula f;
    Error err;

    // Saw wave from manual p. 306: typing "x" generates linear saw
    err = Formula::compile ("x", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (-1.0), -1.0f, 1e-5f);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);
    CHECK_NEAR (f.evaluate (1.0), 1.0f, 1e-5f);

    // Sine wave from manual
    err = Formula::compile ("sin(pi * x)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);
    CHECK_NEAR (f.evaluate (0.5), 1.0f, 1e-5f);
    CHECK_NEAR (f.evaluate (-0.5), -1.0f, 1e-5f);

    // Sine wave using w
    err = Formula::compile ("sin(2 * pi * w)", f);
    CHECK (err.ok);
    EvalContext ctx;
    ctx.w = 0.0;
    CHECK_NEAR (f.evaluate (ctx), 0.0f, 1e-5f);
    ctx.w = 0.25;
    CHECK_NEAR (f.evaluate (ctx), 1.0f, 1e-5f);
    ctx.w = 0.75;
    CHECK_NEAR (f.evaluate (ctx), -1.0f, 1e-5f);

    // FFT mode: q < 17 (p. 309: first 16 harmonics)
    err = Formula::compile ("q < 17", f);
    CHECK (err.ok);
    CHECK (f.usesQ ());
    ctx.q = 1.0;
    CHECK_NEAR (f.evaluate (ctx), 1.0f, 1e-5f);
    ctx.q = 16.0;
    CHECK_NEAR (f.evaluate (ctx), 1.0f, 1e-5f);
    ctx.q = 17.0;
    CHECK_NEAR (f.evaluate (ctx), 0.0f, 1e-5f);
    ctx.q = 512.0;
    CHECK_NEAR (f.evaluate (ctx), 0.0f, 1e-5f);

    // FFT mode: q < z * 256 (p. 310)
    err = Formula::compile ("q < z * 256", f);
    CHECK (err.ok);
    CHECK (f.usesQ ());
    CHECK (f.usesZ ());

    // FFT mode: (1/q)^0.25 (p. 310)
    err = Formula::compile ("(1/q)^0.25", f);
    CHECK (err.ok);
    ctx.q = 1.0;
    CHECK_NEAR (f.evaluate (ctx), 1.0f, 1e-5f);
    ctx.q = 16.0;
    CHECK_NEAR (f.evaluate (ctx), 0.5f, 1e-5f); // 16^(-0.25) = 1/2 = 0.5

    // FFT mode: ((q%2)==1)?in:0 (p. 310 Squarify)
    err = Formula::compile ("((q%2)==1)?in:0", f);
    CHECK (err.ok);
    ctx.inVal = 0.8;
    ctx.q = 1.0;
    CHECK_NEAR (f.evaluate (ctx), 0.8f, 1e-5f);
    ctx.q = 2.0;
    CHECK_NEAR (f.evaluate (ctx), 0.0f, 1e-5f);
    ctx.q = 3.0;
    CHECK_NEAR (f.evaluate (ctx), 0.8f, 1e-5f);
    ctx.q = 4.0;
    CHECK_NEAR (f.evaluate (ctx), 0.0f, 1e-5f);
}

TEST (formula_error_positions)
{
    Formula f;
    Error err;

    // Empty expression
    err = Formula::compile ("", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 0u);

    // Unexpected character '@'
    err = Formula::compile ("1 + @ 2", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 4u);

    // Trailing unexpected token
    err = Formula::compile ("1 + 2 foo", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 6u);

    // Incomplete expression
    err = Formula::compile ("1 +", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 3u);

    // Unexpected operator
    err = Formula::compile ("1 + * 2", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 4u);

    // Missing closing paren
    err = Formula::compile ("(1 + 2", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 6u);

    // Unexpected closing paren
    err = Formula::compile ("1 + 2)", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 5u);

    // Unknown function
    err = Formula::compile ("unknownFunc(x)", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 0u);

    // Arity mismatch: sin with 0 arguments
    err = Formula::compile ("sin()", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 4u);

    // Arity mismatch: sin with 2 arguments
    err = Formula::compile ("sin(1, 2)", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 5u);

    // Variadic function with 0 arguments
    err = Formula::compile ("min()", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 4u);

    // Missing colon in ternary
    err = Formula::compile ("x ? 1", f);
    CHECK (! err.ok);
    CHECK_EQ (err.position, 5u);
}

TEST (formula_safety_and_clamping)
{
    Formula f;
    Error err;

    // Division by zero does not crash, evaluates safely to 0.0
    err = Formula::compile ("1 / 0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Modulo zero
    err = Formula::compile ("5 % 0", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Sqrt negative
    err = Formula::compile ("sqrt(-9)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Log zero / negative
    err = Formula::compile ("ln(0)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    err = Formula::compile ("log(-10)", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 0.0f, 1e-5f);

    // Large overflow is bounded and clamped
    err = Formula::compile ("exp(1000)", f);
    CHECK (err.ok);
    CHECK (std::isfinite (f.evaluate (0.0)));
    CHECK (f.evaluate (0.0) <= 1e9f);

    err = Formula::compile ("100", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), 100.0f, 1e-5f);

    err = Formula::compile ("-50", f);
    CHECK (err.ok);
    CHECK_NEAR (f.evaluate (0.0), -50.0f, 1e-5f);

    // Recursion depth limit: deeply nested parens
    std::string deep;
    for (int i = 0; i < 70; ++i) deep += "(";
    deep += "1";
    for (int i = 0; i < 70; ++i) deep += ")";
    err = Formula::compile (deep, f);
    CHECK (! err.ok);

    // Expression length limit (> 4096 characters)
    std::string longExpr (5000, 'x');
    err = Formula::compile (longExpr, f);
    CHECK (! err.ok);
}

TEST (formula_render_frames)
{
    // Render 4 frames in time domain
    constexpr int kFrames = 4;
    std::vector<float> buffer (kFrames * kSamplesPerFrame, 0.0f);

    Error err = renderFrames ("x * y", kFrames, buffer.data ());
    CHECK (err.ok);

    // Frame 0 (y = 0.0): all samples should be 0.0
    for (int s = 0; s < kSamplesPerFrame; ++s)
        CHECK_NEAR (buffer[s], 0.0f, 1e-5f);

    // Frame 3 (y = 1.0): saw wave from -1.0 to 1.0
    CHECK_NEAR (buffer[3 * kSamplesPerFrame + 0], -1.0f, 1e-3f);
    CHECK_NEAR (buffer[3 * kSamplesPerFrame + 2047], 1.0f, 1e-3f);

    // Render with existing frames ('in' variable)
    std::vector<float> existing (kFrames * kSamplesPerFrame, 0.5f);
    std::vector<float> modified (kFrames * kSamplesPerFrame, 0.0f);
    err = renderFrames ("in * 0.5", kFrames, modified.data (), existing.data ());
    CHECK (err.ok);
    for (int s = 0; s < kSamplesPerFrame; ++s)
        CHECK_NEAR (modified[s], 0.25f, 1e-5f);

    // Render FFT mode with built-in additive fallback
    std::vector<float> fftBuffer (kFrames * kSamplesPerFrame, 0.0f);
    err = renderFrames ("q < 17", kFrames, fftBuffer.data ());
    CHECK (err.ok);
    for (size_t i = 0; i < fftBuffer.size (); ++i)
    {
        CHECK (std::isfinite (fftBuffer[i]));
        CHECK (fftBuffer[i] >= -1.0f && fftBuffer[i] <= 1.0f);
    }

    // Render FFT mode with custom FFTRenderHook
    struct HookContext
    {
        int invocationCount = 0;
    } hookCtx;

    FFTRenderHook hook = [] (const Formula& formula, int frameIndex, int numFrames,
                             float* outFrame, void* userData) {
        auto* ctx = static_cast<HookContext*> (userData);
        ++ctx->invocationCount;
        CHECK (formula.usesQ ());
        CHECK (frameIndex >= 0 && frameIndex < numFrames);
        for (int s = 0; s < kSamplesPerFrame; ++s)
            outFrame[s] = 0.123f;
    };

    err = renderFrames ("q < 17", kFrames, fftBuffer.data (), nullptr, 0, hook, &hookCtx);
    CHECK (err.ok);
    CHECK_EQ (hookCtx.invocationCount, kFrames);
    CHECK_NEAR (fftBuffer[0], 0.123f, 1e-5f);
}

TEST (formula_fuzz_1000_strings)
{
    std::mt19937 rng (42); // fixed seed for deterministic reproducible fuzzing
    std::uniform_int_distribution<int> lenDist (1, 80);
    std::uniform_int_distribution<int> charTypeDist (0, 3);
    std::uniform_int_distribution<int> asciiDist (32, 126);

    const char* kTokenSnippets[] = {
        "x", "w", "y", "z", "q", "in", "sel", "rand", "pi", "e",
        "sin(", "cos(", "tan(", "log(", "sqrt(", "abs(", "min(",
        "+", "-", "*", "/", "%", "^", "?", ":", "==", "!=", "<", ">", "&&", "||", "!",
        "0", "1", "0.5", "10", "(", ")", ",", " "
    };
    constexpr size_t kSnippetCount = sizeof (kTokenSnippets) / sizeof (kTokenSnippets[0]);
    std::uniform_int_distribution<size_t> snippetDist (0, kSnippetCount - 1);

    auto startTime = std::chrono::steady_clock::now ();

    for (int iteration = 0; iteration < 1000; ++iteration)
    {
        std::string testString;
        int targetLen = lenDist (rng);

        if (iteration % 2 == 0)
        {
            // Pure random ASCII characters
            for (int i = 0; i < targetLen; ++i)
                testString.push_back (static_cast<char> (asciiDist (rng)));
        }
        else
        {
            // Token snippet soup
            while (static_cast<int> (testString.size ()) < targetLen)
                testString += kTokenSnippets[snippetDist (rng)];
        }

        Formula f;
        Error err = Formula::compile (testString, f);
        if (err.ok)
        {
            // If compilation unexpectedly or legitimately succeeded, evaluate must be safe
            EvalContext ctx;
            ctx.x = 0.25;
            ctx.w = 0.625;
            ctx.y = 0.5;
            ctx.z = 0.0;
            ctx.q = 5.0;
            ctx.inVal = 0.1;
            ctx.selVal = 0.2;
            ctx.randVal = -0.3;

            float val1 = f.evaluate (ctx);
            CHECK (std::isfinite (val1));
            CHECK (val1 >= -1e9f && val1 <= 1e9f);

            float val2 = f.evaluate (0.5, 2, 4);
            CHECK (std::isfinite (val2));
            CHECK (val2 >= -1e9f && val2 <= 1e9f);
        }
    }

    auto elapsed = std::chrono::steady_clock::now () - startTime;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (elapsed).count ();

    // Fuzz of 1000 iterations must finish well within bounded time (< 2000 ms)
    CHECK (elapsedMs < 2000);
}
