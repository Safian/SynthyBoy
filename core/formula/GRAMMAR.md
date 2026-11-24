# Serum-Style Wavetable Formula Grammar Specification

This document specifies the lexical structure, grammar, operator precedence, built-in functions, variables, and evaluation semantics for the SynthyBoy wavetable formula parser (`synthyboy::formula`), based on the Xfer Serum 2 manual (Chapter *Using the Formula Parser*, pages 304–311) and the UI design decisions (Q11: full grammar).

> [!NOTE]
> All descriptions in this document paraphrase the manual's concepts and syntax. No copyrighted text is copied verbatim. Ambiguities or omissions in the manual are explicitly designated with `[S unclear]`.

---

## 1. Overview & Execution Model

The formula parser enables generative wavetable creation and modification using mathematical expressions. Formulas are entered as text and evaluated to generate single-cycle wavetable frames of 2048 samples (`kSamplesPerFrame = 2048`), across 1 to 256 frames (`kMaxFrames = 256`).

Evaluation operates in one of two modes:
1. **Time Domain (Waveform Display Mode)**: The expression is evaluated for each audio sample `s` (from `0` to `2047`) in the target wavetable frame.
2. **Frequency Domain (FFT Bins Mode)**: Triggered whenever the variable `q` is referenced in the formula. The expression is evaluated for each harmonic bin `q = 1, ..., 512`. The manual notes that in this mode, expressions plot to the FFT bins rather than directly as audio `[S unclear]`.

Formulas are also categorized by scope:
- **Singles**: Expressions that do not contain the multi-frame variables `y` or `z`. Only the currently selected frame is generated or modified.
- **Multis**: Expressions referencing `y` or `z`. All frames across the wavetable are generated or replaced.

---

## 2. Lexical Grammar

### 2.1 Whitespace & Comments
- Whitespace characters (`' '`, `'\t'`, `'\r'`, `'\n'`) separate tokens and are otherwise ignored.

### 2.2 Literals
- **Floating-point and integer numbers**:
  - Digits with optional decimal point and optional exponent:
    `[0-9]+(\.[0-9]*)?([eE][+-]?[0-9]+)?` or `\.[0-9]+([eE][+-]?[0-9]+)?`
  - Examples: `0`, `1`, `16`, `0.25`, `.5`, `1e-3`, `2.5e2`.

### 2.3 Identifiers
- Names consist of letters, digits, and underscores, beginning with a letter or underscore: `[a-zA-Z_][a-zA-Z0-9_]*`.
- Keywords, function names, and variable names are case-insensitive (e.g., `sin`, `SIN`, `Sin`).

---

## 3. Variables and Constants

### 3.1 Constants
| Constant | Value | Description |
| :--- | :--- | :--- |
| `pi` | `3.14159265358979323846` | Archimedes' constant $\pi$ (ratio of circumference to diameter) |
| `e` | `2.71828182845904523536` | Euler's number $e$ (base of the natural logarithm) |

> `[S unclear]`: The Serum manual lists `pi` as `3.141592658979323846264338` (note the digits `58979...` instead of `53589...`). SynthyBoy provides the mathematically correct standard $\pi$ constant, which matches standard mathematical libraries.

### 3.2 Variables
| Symbol | Domain | Description |
| :--- | :--- | :--- |
| `x` | `[-1.0, 1.0]` | Normalized sample time coordinate across the frame (left to right). Sample $s \in [0, 2047]$ maps linearly from $-1.0$ to $+1.0$. |
| `w` | `[0.0, 1.0]` | Normalized sample time coordinate from $0.0$ to $1.0$, equivalent to `(x + 1) / 2`. |
| `y` | `[0.0, 1.0]` | Normalized table/frame index across the wavetable ($0.0$ at first frame, $1.0$ at final frame). |
| `z` | `[-1.0, 1.0]` | Normalized table/frame index mapped to $[-1.0, 1.0]$, equivalent to `(y * 2) - 1`. |
| `q` | `[1.0, 512.0]` | Harmonic bin index for FFT spectral editing ($1 \le q \le 512$). Its presence triggers FFT mode. |
| `in` | `[-1.0, 1.0]` | Current/previous sample value of the frame being processed (in FFT mode: current harmonic amplitude). |
| `sel` | `[-1.0, 1.0]` | Sample value of the designated/selected frame (fixed across all frames when evaluating multi-frame formulas). |
| `rand` | `[-1.0, 1.0]` | Precalculated pseudo-random value per sample/bin position, invariant across all frames of the wavetable. |

---

## 4. Built-in Functions

### 4.1 Single-Argument Mathematical Functions
All single-argument functions accept one expression argument: `func(expr)`.
1. `sin(x)`: Trigonometric sine.
2. `cos(x)`: Trigonometric cosine.
3. `tan(x)`: Trigonometric tangent.
4. `asin(x)`: Inverse sine (clamped to domain $[-1, 1]$).
5. `acos(x)`: Inverse cosine (clamped to domain $[-1, 1]$).
6. `atan(x)`: Inverse tangent.
7. `sinh(x)`: Hyperbolic sine (input bounded to prevent overflow).
8. `cosh(x)`: Hyperbolic cosine (input bounded to prevent overflow).
9. `tanh(x)`: Hyperbolic tangent.
10. `asinh(x)`: Inverse hyperbolic sine.
11. `acosh(x)`: Inverse hyperbolic cosine ($x \ge 1$; returns $0$ if $x < 1$).
12. `atanh(x)`: Inverse hyperbolic tangent ($|x| < 1$; clamped to prevent singularity).
13. `log2(x)`: Binary logarithm base 2 ($x > 0$; returns $0$ if $x \le 0$).
14. `log10(x)`: Decimal logarithm base 10 ($x > 0$; returns $0$ if $x \le 0$).
15. `log(x)`: Decimal logarithm base 10 (manual explicitly specifies `log` as logarithm base 10).
16. `ln(x)`: Natural logarithm base $e \approx 2.71828$ ($x > 0$; returns $0$ if $x \le 0$).
17. `exp(x)`: Exponential function $e^x$ (input bounded to prevent overflow).
18. `sqrt(x)`: Square root ($x \ge 0$; returns $0$ if $x < 0$).
19. `sign(x)`: Signum: $-1.0$ if $x < 0$, $+1.0$ if $x > 0$, and $0.0$ if $x = 0$.
20. `rint(x)`: Round to nearest integer.
21. `abs(x)`: Absolute value $|x|$.

### 4.2 Variadic Functions
Accept one or more comma-separated arguments: `func(arg1, arg2, ...)`.
1. `min(a, b, ...)`: Minimum value among all arguments.
2. `max(a, b, ...)`: Maximum value among all arguments.
3. `sum(a, b, ...)`: Sum of all arguments.
4. `avg(a, b, ...)`: Arithmetic mean (average) of all arguments: $\frac{1}{N}\sum_{i=1}^N a_i$.

---

## 5. Operators and Precedence

The operator precedence hierarchy is defined as follows (ordered from lowest priority to highest priority):

| Level | Operator | Description | Associativity |
| :---: | :---: | :--- | :---: |
| 1 | `? :` | Ternary conditional: `cond ? expr_true : expr_false` | Right |
| 2 | `\|\|` | Logical OR (truthy if $\|v\| > 10^{-9}$) | Left |
| 3 | `&&` | Logical AND (truthy if $\|v\| > 10^{-9}$) | Left |
| 4 | `==`, `!=` | Equality and inequality (fuzzy comparison $\epsilon = 10^{-9}$) | Left |
| 4 | `<`, `<=`, `>`, `>=` | Relational comparisons | Left |
| 5 | `+`, `-` | Binary addition and subtraction | Left |
| 6 | `*`, `/`, `%` | Multiplication, division, and modulo (floating-point remainder) | Left |
| 7 | `+`, `-`, `!` | Unary plus, negation, and logical NOT | Right (prefix) |
| 8 | `^` | Power / exponentiation ($x^y$) | Right |
| 9 | `( )`, `f(...)` | Primary: groupings, constants, variables, function calls | N/A |

### Precedence and Grammar Clarifications `[S unclear]`
1. **Operator Precedence of `&&` and `||`**:
   The Serum manual table (p. 305) lists `&&` with Priority 1 and `||` with Priority 2. In standard logic, C/C++, and the underlying muParser engine (`prLOR = 1`, `prLAND = 2`), logical OR has *lower* precedence than logical AND. The manual's table numbering inverted the convention (listing lower binding as 1 or reversing the pair). SynthyBoy implements standard boolean binding where `&&` binds tighter than `||`, and both bind below comparisons.
2. **Modulo `%` and Ternary `?:`**:
   The operator table on p. 305 of the manual omits `%` and `?:`. However, both operators appear explicitly in example formulas on p. 310 (e.g., `((q%2)==1)?in:0`). SynthyBoy implements `%` at multiplicative precedence (Level 6) and `?:` at ternary conditional precedence (Level 1, right-associative with short-circuit evaluation), matching C/muParser.
3. **FFT Reconstruction & Phase `[S unclear]`**:
   The manual describes `q` as selecting the FFT harmonic bin (1 to 512). When `q` is present, the formula outputs bin magnitudes. However, the manual does not specify phase reconstruction (e.g., whether phases default to zero / sine phase, or preserve input phase from `in`), nor does it define DC bin 0 handling. SynthyBoy provides a default additive synthesis reconstructor (sine phase harmonics with output normalization) and provides an `FFTRenderHook` callback so the host / wavetable editor can route the computed 512 partials into a specialized inverse FFT engine.

---

## 6. Formal EBNF Grammar

```ebnf
Expression      ::= Conditional ;

Conditional     ::= LogicalOr ( '?' Conditional ':' Conditional )? ;

LogicalOr       ::= LogicalAnd ( '||' LogicalAnd )* ;

LogicalAnd      ::= Equality ( '&&' Equality )* ;

Equality        ::= Relational ( ( '==' | '!=' ) Relational )* ;

Relational      ::= Additive ( ( '<' | '<=' | '>' | '>=' ) Additive )* ;

Additive        ::= Multiplicative ( ( '+' | '-' ) Multiplicative )* ;

Multiplicative  ::= Unary ( ( '*' | '/' | '%' ) Unary )* ;

Unary           ::= ( '+' | '-' | '!' ) Unary
                  | Power ;

Power           ::= Primary ( '^' Unary )? ;

Primary         ::= Number
                  | VariableOrConstant
                  | FunctionCall
                  | '(' Expression ')' ;

FunctionCall    ::= Identifier '(' ArgumentList? ')' ;

ArgumentList    ::= Expression ( ',' Expression )* ;

VariableOrConstant ::= Identifier ;
```

---

## 7. Safety, Constraints & Error Handling

To ensure robust real-time editor operation and prevent malicious or accidental crashes:
1. **No C++ Exceptions**: All compilation and evaluation errors are returned via structured error codes/types.
2. **Bounded Parse Depth**: Recursion depth is capped at 64 to prevent stack overflow on deeply nested parenthesized expressions.
3. **Bounded Expression Length**: Input expressions exceeding 4096 characters are rejected.
4. **Bounded Instructions**: Compiled bytecode programs cannot exceed 1024 instructions.
5. **No Allocation During Evaluation**: Bytecode execution uses a fixed-size stack of 64 elements on the thread stack.
6. **NaN and Infinity Protection**: All operations (such as division by zero, $\log(\le 0)$, $\sqrt{<0}$, exponent overflow) safely return finite values ($0.0$). The final output samples are clamped to $[-1.0, 1.0]$.
7. **Error Position Reporting**: Lexical and syntax errors report the exact 0-based character offset and a descriptive message.
