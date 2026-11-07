#include "settings/key.h"

namespace engine_placeholder::settings {

const char* KeyErrorName(KeyError error) {
    switch (error) {
    case KeyError::None: return "none";
    case KeyError::Empty: return "empty";
    case KeyError::TooLong: return "too_long";
    case KeyError::BadCharacter: return "bad_character";
    case KeyError::EmptySegment: return "empty_segment";
    case KeyError::TooManySegments: return "too_many_segments";
    case KeyError::SegmentTooLong: return "segment_too_long";
    }
    return "unknown";
}

KeyError Key::Parse(StringView text, Key* out) {
    if (text.Length() == 0) return KeyError::Empty;
    if (text.Length() > kMaxKeyBytes) return KeyError::TooLong;
    u32 segments = 1;
    usize segment_start = 0;
    for (usize i = 0; i <= text.Length(); ++i) {
        const bool end = i == text.Length();
        const char c = end ? '.' : text[i];
        if (c == '.') {
            if (i == segment_start) return KeyError::EmptySegment;
            if (i - segment_start > kMaxSegmentBytes) return KeyError::SegmentTooLong;
            if (!end) {
                if (++segments > kMaxKeySegments) return KeyError::TooManySegments;
            }
            segment_start = i + 1;
            continue;
        }
        const bool letter = c >= 'a' && c <= 'z';
        const bool digit = c >= '0' && c <= '9';
        const bool first = i == segment_start;
        if (!(letter || (!first && (digit || c == '_')))) return KeyError::BadCharacter;
    }
    if (out != nullptr) {
        for (usize i = 0; i < text.Length(); ++i) out->text_[i] = text[i];
        out->text_[text.Length()] = '\0';
        out->length_ = static_cast<u8>(text.Length());
    }
    return KeyError::None;
}

u32 Key::Segments() const {
    if (length_ == 0) return 0;
    u32 n = 1;
    for (usize i = 0; i < length_; ++i) n += text_[i] == '.' ? 1 : 0;
    return n;
}

StringView Key::Section() const {
    const StringView v = View();
    const usize dot = v.LastIndexOf('.');
    return dot == v.Length() ? StringView() : v.Substring(0, dot);
}

StringView Key::Name() const {
    const StringView v = View();
    const usize dot = v.LastIndexOf('.');
    return dot == v.Length() ? v : v.Substring(dot + 1);
}

usize Key::Hash() const {
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < length_; ++i) h = (h ^ static_cast<u8>(text_[i])) * 1099511628211ull;
    return static_cast<usize>(h);
}

} // namespace engine_placeholder::settings
