// SynthyBoyVST - core: locale independent, allocation free number formatting and parsing.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "number.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace synthyboy
{
namespace number
{
namespace
{
constexpr double kPow10[23] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12,
                                1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

// ---------------------------------------------------------------------------------------------
// Exact decimal expansion of a float: mantissa * 2^e is converted with a small big integer
// (e >= 0: shift; e < 0: multiply by 5^-e and scale by 10^e). At most ~110 digits.
// ---------------------------------------------------------------------------------------------
struct Big
{
    static constexpr int maxLimbs = 14; // 448 bits, a float needs at most 24 + 150 * log2 (5) = 372
    uint32_t limb[maxLimbs] = {};
    int count = 1;

    void mulSmall (uint32_t factor) noexcept
    {
        uint64_t carry = 0;
        for (int i = 0; i < count; ++i)
        {
            const uint64_t v = static_cast<uint64_t> (limb[i]) * factor + carry;
            limb[i] = static_cast<uint32_t> (v);
            carry = v >> 32;
        }
        if (carry != 0 && count < maxLimbs)
            limb[count++] = static_cast<uint32_t> (carry);
    }

    void shiftLeft (int bits) noexcept
    {
        while (bits >= 32)
        {
            if (count < maxLimbs)
            {
                for (int i = count; i > 0; --i)
                    limb[i] = limb[i - 1];
                limb[0] = 0;
                ++count;
            }
            bits -= 32;
        }
        if (bits > 0)
        {
            uint32_t carry = 0;
            for (int i = 0; i < count; ++i)
            {
                const uint32_t next = limb[i] >> (32 - bits);
                limb[i] = (limb[i] << bits) | carry;
                carry = next;
            }
            if (carry != 0 && count < maxLimbs)
                limb[count++] = carry;
        }
    }

    /** Divides by 1e9 and returns the remainder. */
    uint32_t divBillion() noexcept
    {
        uint64_t rem = 0;
        for (int i = count - 1; i >= 0; --i)
        {
            const uint64_t v = (rem << 32) | limb[i];
            limb[i] = static_cast<uint32_t> (v / 1000000000u);
            rem = v % 1000000000u;
        }
        while (count > 1 && limb[count - 1] == 0)
            --count;
        return static_cast<uint32_t> (rem);
    }

    bool isZero() const noexcept { return count == 1 && limb[0] == 0; }
};

struct Decimal
{
    char digits[168];
    int len = 0;     // 0 = value is zero
    int pointExp = 0; // decimal exponent of digits[0]; digit i has exponent pointExp - i
    bool negative = false;
};

void toDecimal (float value, Decimal& d) noexcept
{
    uint32_t bits = 0;
    std::memcpy (&bits, &value, sizeof bits);
    d.negative = (bits >> 31) != 0;
    d.len = 0;
    d.pointExp = 0;

    const uint32_t expField = (bits >> 23) & 0xffu;
    const uint32_t frac = bits & 0x7fffffu;
    if (expField == 0xffu)
        return; // non-finite: treated as zero

    const uint32_t m = expField == 0 ? frac : (frac | 0x800000u);
    const int e = expField == 0 ? -149 : static_cast<int> (expField) - 150;
    if (m == 0)
        return;

    Big big;
    big.limb[0] = m;
    int scale = 0;
    if (e >= 0)
    {
        big.shiftLeft (e);
    }
    else
    {
        for (int k = -e; k > 0;)
        {
            if (k >= 13) { big.mulSmall (1220703125u); k -= 13; } // 5^13
            else         { big.mulSmall (5u); --k; }
        }
        scale = e;
    }

    char tmp[sizeof d.digits];
    int pos = static_cast<int> (sizeof tmp);
    while (! big.isZero() && pos >= 9)
    {
        uint32_t chunk = big.divBillion();
        for (int i = 0; i < 9; ++i)
        {
            tmp[--pos] = static_cast<char> ('0' + chunk % 10);
            chunk /= 10;
        }
    }
    while (pos < static_cast<int> (sizeof tmp) && tmp[pos] == '0')
        ++pos;

    d.len = static_cast<int> (sizeof tmp) - pos;
    std::memcpy (d.digits, tmp + pos, static_cast<size_t> (d.len));
    d.pointExp = d.len - 1 + scale;

    while (d.len > 0 && d.digits[d.len - 1] == '0')
        --d.len;
    if (d.len == 0)
        d.pointExp = 0;
}

void stripTrailingZeros (Decimal& d) noexcept
{
    while (d.len > 0 && d.digits[d.len - 1] == '0')
        --d.len;
    if (d.len == 0)
        d.pointExp = 0;
}

/** Keep the first `keep` (>= 1) significant digits, rounding half to even on the exact value. */
void roundSignificant (Decimal& d, int keep) noexcept
{
    if (d.len <= keep)
        return;

    const char first = d.digits[keep];
    const bool restNonZero = d.len > keep + 1; // trailing zeros are stripped, so any further digit is non-zero
    const bool up = first > '5' || (first == '5' && (restNonZero || ((d.digits[keep - 1] - '0') & 1) != 0));
    d.len = keep;

    if (up)
    {
        int i = keep - 1;
        for (; i >= 0; --i)
        {
            if (d.digits[i] == '9') { d.digits[i] = '0'; }
            else { ++d.digits[i]; break; }
        }
        if (i < 0) // all nines: 999 -> 1000
        {
            d.digits[0] = '1';
            d.len = 1;
            ++d.pointExp;
        }
    }
    stripTrailingZeros (d);
}

size_t emit (const char* text, size_t n, char* buf, size_t cap) noexcept
{
    if (cap == 0)
        return 0;
    const size_t k = n < cap - 1 ? n : cap - 1;
    std::memcpy (buf, text, k);
    buf[k] = '\0';
    return k;
}

struct Writer
{
    char text[96];
    size_t n = 0;
    void put (char c) noexcept { if (n < sizeof text) text[n++] = c; }
    char digit (const Decimal& d, int exponent) const noexcept
    {
        const int idx = d.pointExp - exponent;
        return idx >= 0 && idx < d.len ? d.digits[idx] : '0';
    }
};
} // namespace

size_t formatG9 (float value, char* buf, size_t cap) noexcept
{
    Decimal d;
    toDecimal (value, d);
    roundSignificant (d, 9);

    Writer w;
    if (d.len == 0)
    {
        w.put ('0');
        return emit (w.text, w.n, buf, cap);
    }

    if (d.negative)
        w.put ('-');

    const int x = d.pointExp;
    if (x < -4 || x >= 9) // exponential form d[.ddd]e+XX
    {
        w.put (d.digits[0]);
        if (d.len > 1)
        {
            w.put ('.');
            for (int i = 1; i < d.len; ++i)
                w.put (d.digits[i]);
        }
        w.put ('e');
        w.put (x < 0 ? '-' : '+');
        const int ax = x < 0 ? -x : x;
        if (ax >= 100)
            w.put (static_cast<char> ('0' + ax / 100));
        w.put (static_cast<char> ('0' + (ax / 10) % 10));
        w.put (static_cast<char> ('0' + ax % 10));
    }
    else if (x >= 0)
    {
        for (int e = x; e >= 0; --e)
            w.put (w.digit (d, e));
        if (d.len > x + 1)
        {
            w.put ('.');
            for (int i = x + 1; i < d.len; ++i)
                w.put (d.digits[i]);
        }
    }
    else // -4 <= x < 0
    {
        w.put ('0');
        w.put ('.');
        for (int i = 0; i < -x - 1; ++i)
            w.put ('0');
        for (int i = 0; i < d.len; ++i)
            w.put (d.digits[i]);
    }
    return emit (w.text, w.n, buf, cap);
}

size_t formatFixed1 (float value, char* buf, size_t cap) noexcept
{
    Decimal d;
    toDecimal (value, d);

    if (d.len > 0)
    {
        const int keep = d.pointExp + 2; // digits down to the first decimal place
        if (keep >= 1)
        {
            roundSignificant (d, keep);
        }
        else if (keep == 0) // 0.0X: rounds to 0.1 above the tie, to 0.0 on the tie (even)
        {
            const bool up = d.digits[0] > '5' || (d.digits[0] == '5' && d.len > 1);
            d.len = up ? 1 : 0;
            d.digits[0] = '1';
            d.pointExp = up ? -1 : 0;
        }
        else
        {
            d.len = 0;
            d.pointExp = 0;
        }
    }

    Writer w;
    if (d.len > 0 && d.negative)
        w.put ('-');
    for (int e = d.pointExp > 0 ? d.pointExp : 0; e >= 0; --e)
        w.put (d.len > 0 ? w.digit (d, e) : '0');
    w.put ('.');
    w.put (d.len > 0 ? w.digit (d, -1) : '0');
    return emit (w.text, w.n, buf, cap);
}

bool parseDecimal (const char* begin, const char* end, bool strictJson, double& value, const char*& next) noexcept
{
    const char* p = begin;
    bool negative = false;
    if (p < end && (*p == '-' || (! strictJson && *p == '+')))
    {
        negative = *p == '-';
        ++p;
    }

    uint64_t mantissa = 0;
    int sigDigits = 0; // digits accumulated into mantissa, counted from the first non-zero one (max 19)
    int exp10 = 0;
    int intDigits = 0;
    int fracDigits = 0;

    if (strictJson && p < end && *p == '0')
    {
        ++p;
        intDigits = 1;
        if (p < end && *p >= '0' && *p <= '9')
            return false; // leading zeros are not JSON
    }
    else
    {
        for (; p < end && *p >= '0' && *p <= '9'; ++p, ++intDigits)
        {
            if (sigDigits < 19)
            {
                mantissa = mantissa * 10u + static_cast<uint64_t> (*p - '0');
                if (mantissa != 0)
                    ++sigDigits;
            }
            else
            {
                ++exp10; // integer digit that no longer fits: scale instead
            }
        }
    }

    if (p < end && *p == '.')
    {
        const char* q = p + 1;
        for (; q < end && *q >= '0' && *q <= '9'; ++q, ++fracDigits)
        {
            if (sigDigits < 19)
            {
                mantissa = mantissa * 10u + static_cast<uint64_t> (*q - '0');
                if (mantissa != 0)
                    ++sigDigits;
                --exp10;
            }
        }
        if (strictJson && (intDigits == 0 || fracDigits == 0))
            return false;
        p = q;
    }

    if (intDigits == 0 && fracDigits == 0)
        return false;

    if (p < end && (*p == 'e' || *p == 'E'))
    {
        const char* q = p + 1;
        bool expNeg = false;
        if (q < end && (*q == '+' || *q == '-'))
        {
            expNeg = *q == '-';
            ++q;
        }
        if (q < end && *q >= '0' && *q <= '9')
        {
            int e = 0;
            for (; q < end && *q >= '0' && *q <= '9'; ++q)
                if (e < 100000)
                    e = e * 10 + (*q - '0');
            exp10 += expNeg ? -e : e;
            p = q;
        }
        else if (strictJson)
        {
            return false;
        }
    }

    next = p;

    double result = 0.0;
    if (mantissa != 0)
    {
        result = static_cast<double> (mantissa);
        int e = exp10;
        if (e > 0)
        {
            if (e + sigDigits > 330)
            {
                result = HUGE_VAL;
            }
            else
            {
                for (; e > 22; e -= 22) result *= kPow10[22];
                result *= kPow10[e];
            }
        }
        else if (e < 0)
        {
            if (e + sigDigits < -340)
            {
                result = 0.0;
            }
            else
            {
                for (; e < -22; e += 22) result /= kPow10[22];
                result /= kPow10[-e];
            }
        }
    }
    value = negative ? -result : result;
    return true;
}
} // namespace number
} // namespace synthyboy
