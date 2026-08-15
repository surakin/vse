#include "string/utf8_codec.h"

namespace engine_placeholder {

namespace {

constexpr DecodedCodepoint kInvalid = {0xFFFD, 1};

bool IsContinuation(u8 b) {
    return (b & 0xC0) == 0x80;
}

} // namespace

DecodedCodepoint DecodeUtf8At(const char* data, usize length, usize offset, bool* out_valid) {
    auto Fail = [&]() -> DecodedCodepoint {
        if (out_valid != nullptr) {
            *out_valid = false;
        }
        return kInvalid;
    };
    if (out_valid != nullptr) {
        *out_valid = true;
    }

    u8 lead = static_cast<u8>(data[offset]);
    usize remaining = length - offset;

    if ((lead & 0x80) == 0x00) {
        // 1-byte: 0xxxxxxx
        return {lead, 1};
    }
    if ((lead & 0xE0) == 0xC0) {
        // 2-byte: 110xxxxx 10xxxxxx
        if (remaining < 2 || !IsContinuation(static_cast<u8>(data[offset + 1]))) {
            return Fail();
        }
        u32 cp = (static_cast<u32>(lead & 0x1F) << 6) | (static_cast<u32>(data[offset + 1]) & 0x3F);
        if (cp < 0x80) { // overlong
            return Fail();
        }
        return {cp, 2};
    }
    if ((lead & 0xF0) == 0xE0) {
        // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
        if (remaining < 3 || !IsContinuation(static_cast<u8>(data[offset + 1])) ||
            !IsContinuation(static_cast<u8>(data[offset + 2]))) {
            return Fail();
        }
        u32 cp = (static_cast<u32>(lead & 0x0F) << 12) |
                 (static_cast<u32>(data[offset + 1] & 0x3F) << 6) |
                 (static_cast<u32>(data[offset + 2]) & 0x3F);
        if (cp < 0x800) { // overlong
            return Fail();
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) { // surrogate range
            return Fail();
        }
        return {cp, 3};
    }
    if ((lead & 0xF8) == 0xF0) {
        // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if (remaining < 4 || !IsContinuation(static_cast<u8>(data[offset + 1])) ||
            !IsContinuation(static_cast<u8>(data[offset + 2])) ||
            !IsContinuation(static_cast<u8>(data[offset + 3]))) {
            return Fail();
        }
        u32 cp = (static_cast<u32>(lead & 0x07) << 18) |
                 (static_cast<u32>(data[offset + 1] & 0x3F) << 12) |
                 (static_cast<u32>(data[offset + 2] & 0x3F) << 6) |
                 (static_cast<u32>(data[offset + 3]) & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) { // overlong or out of range
            return Fail();
        }
        return {cp, 4};
    }
    // Lone continuation byte, or a 0xF8-0xFF lead byte (never valid UTF-8).
    return Fail();
}

usize EncodeUtf8(u32 codepoint, char out[4]) {
    if ((codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
        codepoint = 0xFFFD;
    }
    if (codepoint < 0x80) {
        out[0] = static_cast<char>(codepoint);
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (codepoint >> 18));
    out[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
    return 4;
}

} // namespace engine_placeholder
