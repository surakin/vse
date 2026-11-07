#include "int128.h"

#include "log/assert.h"

namespace engine_placeholder {

inline namespace ENGINE_INT128_NAMESPACE {

namespace detail128 {

Parts PortableMul(Parts a, Parts b) {
    const u64 a0 = a.lo & 0xffffffffu, a1 = a.lo >> 32, b0 = b.lo & 0xffffffffu, b1 = b.lo >> 32;
    const u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const u64 mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    Parts r;
    r.lo = (mid << 32) | (p00 & 0xffffffffu);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    r.hi += a.lo * b.hi + a.hi * b.lo; // the cross terms only reach the high half
    return r;
}

namespace {
bool Less(Parts a, Parts b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }
u32 Clz(Parts a) { return u128::FromParts(a.hi, a.lo).CountLeadingZeros(); }
Parts Shl(Parts a, u32 n) { // 0 <= n < 128
    const u128 r = u128::FromParts(a.hi, a.lo) << n;
    return {r.lo, r.hi};
}
Parts Shr(Parts a, u32 n) {
    const u128 r = u128::FromParts(a.hi, a.lo) >> n;
    return {r.lo, r.hi};
}
} // namespace

void PortableDivMod(Parts n, Parts d, Parts* quotient, Parts* remainder) {
    ENGINE_ASSERT(d.lo != 0 || d.hi != 0, "PortableDivMod: division by zero");
    if (Less(n, d)) {
        *quotient = {0, 0};
        *remainder = n;
        return;
    }
    if (d.hi == 0 && n.hi == 0) {
        *quotient = {n.lo / d.lo, 0};
        *remainder = {n.lo % d.lo, 0};
        return;
    }
    if (d.hi == 0 && d.lo <= 0xffffffffu) { // a one-limb divisor: schoolbook over 32-bit limbs
        const u32 limbs[4] = {static_cast<u32>(n.lo), static_cast<u32>(n.lo >> 32), static_cast<u32>(n.hi), static_cast<u32>(n.hi >> 32)};
        u32 q[4];
        u64 carry = 0;
        for (int j = 3; j >= 0; --j) {
            const u64 cur = (carry << 32) | limbs[j];
            q[j] = static_cast<u32>(cur / d.lo);
            carry = cur % d.lo;
        }
        *quotient = {(static_cast<u64>(q[1]) << 32) | q[0], (static_cast<u64>(q[3]) << 32) | q[2]};
        *remainder = {carry, 0};
        return;
    }
    // Knuth algorithm D (Hacker's Delight divmnu) on 32-bit limbs, the divisor normalized so its top limb has its high bit set.
    const u32 s = Clz(d) & 31;
    const Parts dn = Shl(d, s);
    const Parts nn = Shl(n, s);
    const u32 vn[4] = {static_cast<u32>(dn.lo), static_cast<u32>(dn.lo >> 32), static_cast<u32>(dn.hi), static_cast<u32>(dn.hi >> 32)};
    u32 un[5] = {static_cast<u32>(nn.lo), static_cast<u32>(nn.lo >> 32), static_cast<u32>(nn.hi), static_cast<u32>(nn.hi >> 32),
                 s != 0 ? static_cast<u32>(n.hi >> (64 - s)) : 0u};
    int divisor_limbs = 4;
    while (divisor_limbs > 0 && vn[divisor_limbs - 1] == 0) --divisor_limbs;
    u32 q[5] = {};
    for (int j = 4 - divisor_limbs; j >= 0; --j) {
        const u64 numerator = (static_cast<u64>(un[j + divisor_limbs]) << 32) | un[j + divisor_limbs - 1];
        u64 qhat = numerator / vn[divisor_limbs - 1];
        u64 rhat = numerator % vn[divisor_limbs - 1];
        while (qhat >= (u64{1} << 32) || (divisor_limbs >= 2 && qhat * vn[divisor_limbs - 2] > ((rhat << 32) | un[j + divisor_limbs - 2]))) {
            --qhat;
            rhat += vn[divisor_limbs - 1];
            if (rhat >= (u64{1} << 32)) break;
        }
        i64 borrow = 0;
        u64 carry = 0;
        for (int i = 0; i < divisor_limbs; ++i) {
            const u64 product = qhat * vn[i] + carry;
            carry = product >> 32;
            const i64 t = static_cast<i64>(un[i + j]) - borrow - static_cast<i64>(product & 0xffffffffu);
            un[i + j] = static_cast<u32>(t);
            borrow = t < 0 ? 1 : 0;
        }
        const i64 t = static_cast<i64>(un[j + divisor_limbs]) - borrow - static_cast<i64>(carry);
        un[j + divisor_limbs] = static_cast<u32>(t);
        q[j] = static_cast<u32>(qhat);
        if (t < 0) { // qhat was one too large: add the divisor back
            --q[j];
            u64 c = 0;
            for (int i = 0; i < divisor_limbs; ++i) {
                c += static_cast<u64>(un[i + j]) + vn[i];
                un[i + j] = static_cast<u32>(c);
                c >>= 32;
            }
            un[j + divisor_limbs] += static_cast<u32>(c);
        }
    }
    *quotient = {(static_cast<u64>(q[1]) << 32) | q[0], (static_cast<u64>(q[3]) << 32) | q[2]};
    const Parts rem = {(static_cast<u64>(un[1]) << 32) | un[0], (static_cast<u64>(un[3]) << 32) | un[2]};
    *remainder = Shr(rem, s);
}

} // namespace detail128

u128& u128::operator/=(u128 other) {
    ENGINE_ASSERT(other.lo != 0 || other.hi != 0, "u128: division by zero");
#if ENGINE_HAS_NATIVE_INT128
    *this = FromNative(ToNative() / other.ToNative());
#else
    detail128::Parts q, r;
    detail128::PortableDivMod({lo, hi}, {other.lo, other.hi}, &q, &r);
    lo = q.lo;
    hi = q.hi;
#endif
    return *this;
}

u128& u128::operator%=(u128 other) {
    ENGINE_ASSERT(other.lo != 0 || other.hi != 0, "u128: remainder by zero");
#if ENGINE_HAS_NATIVE_INT128
    *this = FromNative(ToNative() % other.ToNative());
#else
    detail128::Parts q, r;
    detail128::PortableDivMod({lo, hi}, {other.lo, other.hi}, &q, &r);
    lo = r.lo;
    hi = r.hi;
#endif
    return *this;
}

i128& i128::operator/=(i128 other) {
    ENGINE_ASSERT(other.lo != 0 || other.hi != 0, "i128: division by zero");
    const bool negative = IsNegative() != other.IsNegative();
    const u128 q = Magnitude() / other.Magnitude();
    return *this = i128(negative ? -q : q);
}

i128& i128::operator%=(i128 other) {
    ENGINE_ASSERT(other.lo != 0 || other.hi != 0, "i128: remainder by zero");
    const bool negative = IsNegative();
    const u128 r = Magnitude() % other.Magnitude();
    return *this = i128(negative ? -r : r);
}

namespace {

const char kDigits[] = "0123456789abcdef";

usize WriteUnsigned(u128 value, bool negative, char* out, usize capacity, u32 base) {
    if (base < 2 || base > 16) {
        if (capacity != 0) out[0] = '\0';
        return 0;
    }
    char reversed[130];
    usize count = 0;
    if (!value) {
        reversed[count++] = '0';
    } else {
        const u128 divisor(base);
        while (value) {
            const u128 quotient = value / divisor;
            const u128 digit = value - quotient * divisor;
            reversed[count++] = kDigits[digit.lo];
            value = quotient;
        }
    }
    if (negative) reversed[count++] = '-';
    if (count + 1 > capacity) {
        if (capacity != 0) out[0] = '\0';
        return 0;
    }
    for (usize i = 0; i < count; ++i) out[i] = reversed[count - 1 - i];
    out[count] = '\0';
    return count;
}

// Reads digits into `value`; false on an empty run, a bad digit or overflow past 2^128 - 1.
bool ReadUnsigned(StringView text, u32 base, u128* value) {
    if (base < 2 || base > 16 || text.Empty()) return false;
    u128 result;
    const u128 max_before_multiply = u128::Max() / u128(base);
    for (usize i = 0; i < text.Length(); ++i) {
        const char c = text[i];
        u32 digit;
        if (c >= '0' && c <= '9') digit = static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<u32>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<u32>(c - 'A' + 10);
        else return false;
        if (digit >= base) return false;
        if (result > max_before_multiply) return false;
        result *= u128(base);
        const u128 next = result + u128(digit);
        if (next < result) return false;
        result = next;
    }
    *value = result;
    return true;
}

} // namespace

usize u128::ToText(char* out, usize capacity, u32 base) const { return WriteUnsigned(*this, false, out, capacity, base); }

bool u128::FromText(StringView text, u32 base, u128* out) { return ReadUnsigned(text, base, out); }

usize i128::ToText(char* out, usize capacity, u32 base) const { return WriteUnsigned(Magnitude(), IsNegative(), out, capacity, base); }

bool i128::FromText(StringView text, u32 base, i128* out) {
    bool negative = false;
    if (!text.Empty() && (text[0] == '-' || text[0] == '+')) {
        negative = text[0] == '-';
        text = text.Substring(1);
    }
    u128 magnitude;
    if (!ReadUnsigned(text, base, &magnitude)) return false;
    const u128 limit = u128::FromParts(u64{1} << 63, 0); // 2^127
    if (negative ? magnitude > limit : magnitude >= limit) return false;
    *out = i128(negative ? -magnitude : magnitude);
    return true;
}

} // inline namespace

} // namespace engine_placeholder
