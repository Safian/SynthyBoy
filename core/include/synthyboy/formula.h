// SynthyBoy - core: Serum-style wavetable formula parser and frame evaluator.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace synthyboy::formula
{
constexpr int kSamplesPerFrame = 2048;
constexpr int kMaxFrames = 256;
constexpr int kFFTBins = 512;
constexpr size_t kMaxExpressionLength = 4096;
constexpr int kMaxRecursionDepth = 64;
constexpr size_t kMaxBytecodeOps = 1024;
constexpr size_t kMaxStackSize = 64;

/** Variable usage bitmask flags. */
enum VariableFlags : uint32_t
{
    kVarNone = 0,
    kVarX    = 1u << 0,
    kVarW    = 1u << 1,
    kVarY    = 1u << 2,
    kVarZ    = 1u << 3,
    kVarQ    = 1u << 4,
    kVarIn   = 1u << 5,
    kVarSel  = 1u << 6,
    kVarRand = 1u << 7
};

/** Compilation error result reporting exact position and descriptive text. */
struct Error
{
    bool ok = true;
    size_t position = 0;
    const char* message = "";

    explicit operator bool () const noexcept { return ok; }
};

/** Evaluation context for calculating a formula sample or harmonic bin. */
struct EvalContext
{
    double x = 0.0;
    double w = 0.0;
    double y = 0.0;
    double z = -1.0;
    double q = 1.0;
    double inVal = 0.0;
    double selVal = 0.0;
    double randVal = 0.0;
};

/** Bytecode instruction opcodes. */
enum class Opcode : uint8_t
{
    PushConst,
    PushVar,
    Neg,
    Not,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,
    Eq,
    Neq,
    Lt,
    Lte,
    Gt,
    Gte,
    And,
    Or,
    JumpIfFalse,
    Jump,
    Call1,
    CallN,
    Halt
};

/** Function ID for built-in single and variadic functions. */
enum class FuncId : uint8_t
{
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Sinh,
    Cosh,
    Tanh,
    Asinh,
    Acosh,
    Atanh,
    Log2,
    Log10,
    Log,
    Ln,
    Exp,
    Sqrt,
    Sign,
    Rint,
    Abs,
    Min,
    Max,
    Sum,
    Avg
};

/** Variable identifiers for PushVar instruction. */
enum class VarId : uint8_t
{
    X = 0,
    W,
    Y,
    Z,
    Q,
    In,
    Sel,
    Rand
};

/** Compact bytecode instruction. */
struct Instruction
{
    Opcode op = Opcode::Halt;
    FuncId func = FuncId::Sin;
    VarId varId = VarId::X;
    uint8_t argCount = 0;
    uint16_t arg16 = 0;
};

class Formula;

/**
 * Optional FFT rendering hook.
 *
 * When formula contains 'q' and an FFT hook is supplied to renderFrames(),
 * this hook is invoked for each frame to perform frequency-to-time reconstruction
 * using an external FFT engine with custom phase data.
 */
using FFTRenderHook = void (*) (const Formula& formula, int frameIndex, int numFrames,
                                float* outFrame2048, void* userData);

/**
 * Compiled formula bytecode representation.
 *
 * Thread-safe for concurrent read-only evaluation once compiled.
 * Evaluation does not allocate memory on the heap.
 */
class Formula
{
public:
    Formula () = default;

    /** Compiles a formula string into bytecode. Returns an Error with position on failure. */
    static Error compile (std::string_view expression, Formula& out) noexcept;

    /** Returns true if the formula was successfully compiled and is ready for evaluation. */
    bool isValid () const noexcept { return valid_; }

    /** Returns the bitmask of all variables referenced by the formula. */
    uint32_t variableMask () const noexcept { return variableMask_; }

    bool usesX () const noexcept { return (variableMask_ & kVarX) != 0; }
    bool usesW () const noexcept { return (variableMask_ & kVarW) != 0; }
    bool usesY () const noexcept { return (variableMask_ & kVarY) != 0; }
    bool usesZ () const noexcept { return (variableMask_ & kVarZ) != 0; }
    bool usesQ () const noexcept { return (variableMask_ & kVarQ) != 0; }
    bool usesIn () const noexcept { return (variableMask_ & kVarIn) != 0; }
    bool usesSel () const noexcept { return (variableMask_ & kVarSel) != 0; }
    bool usesRand () const noexcept { return (variableMask_ & kVarRand) != 0; }

    /** Returns true if the formula references y or z (multi-frame formula). */
    bool isMultiFrame () const noexcept { return usesY () || usesZ (); }

    /** Evaluates the formula using a full evaluation context. Finite result with NaN/inf clamped. */
    float evaluate (const EvalContext& ctx) const noexcept;

    /**
     * Evaluates the formula for a specific time position x in [-1.0, 1.0].
     * Derives w = (x + 1) / 2, y = frameIndex / (numFrames - 1), z = y * 2 - 1.
     */
    float evaluate (double x, int frameIndex = 0, int numFrames = 1,
                    float inVal = 0.0f, float selVal = 0.0f, float randVal = 0.0f,
                    double q = 1.0) const noexcept;

    /** Clears the compiled program. */
    void clear () noexcept;

    /** Internal accessors for compiler / debugger. */
    const std::vector<Instruction>& instructions () const noexcept { return instructions_; }
    const std::vector<double>& constants () const noexcept { return constants_; }

private:
    friend class Compiler;

    std::vector<Instruction> instructions_;
    std::vector<double> constants_;
    uint32_t variableMask_ = 0;
    bool valid_ = false;
};

/** Free evaluation function matching prompt evaluate(f, x, frameIndex, ...). */
inline float evaluate (const Formula& f, double x, int frameIndex = 0, int numFrames = 1,
                       float inVal = 0.0f, float selVal = 0.0f, float randVal = 0.0f,
                       double q = 1.0) noexcept
{
    return f.evaluate (x, frameIndex, numFrames, inVal, selVal, randVal, q);
}

/**
 * Renders wavetable frames (numFrames * 2048 samples) into out.
 *
 * If formula references 'q' and fftHook is provided, invokes fftHook per frame.
 * If formula references 'q' and fftHook is nullptr, uses built-in additive synthesis.
 * If formula does not reference 'q', renders directly in time domain.
 *
 * @param formula         The compiled formula.
 * @param numFrames       Number of frames to render (1 .. 256).
 * @param out             Output buffer for numFrames * 2048 float samples.
 * @param existingFrames  Optional source buffer for 'in' and 'sel' samples (or nullptr).
 * @param selectedFrame   Index of currently selected frame for 'sel' variable.
 * @param fftHook         Optional hook for custom FFT reconstruction in 'q' mode.
 * @param userData        Opaque pointer passed to fftHook.
 */
Error renderFrames (const Formula& formula, int numFrames, float* out,
                    const float* existingFrames = nullptr, int selectedFrame = 0,
                    FFTRenderHook fftHook = nullptr, void* userData = nullptr) noexcept;

/** Helper overload that compiles formulaSource and renders frames. */
Error renderFrames (std::string_view formulaSource, int numFrames, float* out,
                    const float* existingFrames = nullptr, int selectedFrame = 0,
                    FFTRenderHook fftHook = nullptr, void* userData = nullptr) noexcept;

} // namespace synthyboy::formula
