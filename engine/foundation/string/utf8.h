#pragma once

#include "string/string.h"
#include "types.h"
#include "string/utf8_codec.h"

// StringView-level UTF-8 helpers. Depends on both utf8_codec.h and
// string.h — string.h depends only on utf8_codec.h, never on this header,
// so there's no include cycle (see the file-split rationale in
// plans/phase-00-foundation/day-10-utf8-core.md).
namespace engine_placeholder {

inline usize CodepointCount(StringView sv) {
    usize count = 0;
    usize offset = 0;
    while (offset < sv.Length()) {
        DecodedCodepoint dc = DecodeUtf8At(sv.Data(), sv.Length(), offset, nullptr);
        offset += dc.byte_length;
        ++count;
    }
    return count;
}

// Loops DecodeUtf8At with out_valid, returns false on the first invalid step.
inline bool IsValidUtf8(StringView sv) {
    usize offset = 0;
    while (offset < sv.Length()) {
        bool valid = false;
        DecodedCodepoint dc = DecodeUtf8At(sv.Data(), sv.Length(), offset, &valid);
        if (!valid) {
            return false;
        }
        offset += dc.byte_length;
    }
    return true;
}

class Utf8CodepointIterator {
public:
    Utf8CodepointIterator(StringView sv, usize offset) : sv_(sv), offset_(offset) {}

    u32 operator*() const {
        DecodedCodepoint dc = DecodeUtf8At(sv_.Data(), sv_.Length(), offset_, nullptr);
        return dc.codepoint;
    }

    Utf8CodepointIterator& operator++() {
        DecodedCodepoint dc = DecodeUtf8At(sv_.Data(), sv_.Length(), offset_, nullptr);
        offset_ += dc.byte_length;
        return *this;
    }

    bool operator!=(const Utf8CodepointIterator& other) const { return offset_ != other.offset_; }

private:
    StringView sv_;
    usize offset_;
};

class Utf8CodepointRange {
public:
    explicit Utf8CodepointRange(StringView sv) : sv_(sv) {}

    Utf8CodepointIterator begin() const { return Utf8CodepointIterator(sv_, 0); }
    Utf8CodepointIterator end() const { return Utf8CodepointIterator(sv_, sv_.Length()); }

private:
    StringView sv_;
};

inline Utf8CodepointRange Utf8Codepoints(StringView sv) {
    return Utf8CodepointRange(sv);
}

} // namespace engine_placeholder
