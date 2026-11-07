#include "envelope.h"

#include <cstring>

namespace crash_extract {

namespace {

struct Cursor {
    const char* data;
    std::size_t size;
    std::size_t pos;
};

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

void SkipSpace(const char* line, std::size_t length, std::size_t& i) {
    while (i < length && IsSpace(line[i])) {
        ++i;
    }
}

// Reads a JSON string starting at line[i] == '"'; leaves i after the closing quote. Returns false if it does not close.
// The text (without quotes, escapes left as they are) is [begin, begin + length).
bool ReadString(const char* line, std::size_t length, std::size_t& i, const char*& begin, std::size_t& text_length) {
    if (i >= length || line[i] != '"') {
        return false;
    }
    ++i;
    begin = line + i;
    while (i < length) {
        if (line[i] == '\\') {
            i += 2; // skip the escaped character, whatever it is
            continue;
        }
        if (line[i] == '"') {
            text_length = static_cast<std::size_t>(line + i - begin);
            ++i;
            return true;
        }
        ++i;
    }
    return false;
}

// Skips one JSON value (a string, a number, a literal, or a nested object or array) starting at line[i].
bool SkipValue(const char* line, std::size_t length, std::size_t& i) {
    if (i >= length) {
        return false;
    }
    if (line[i] == '"') {
        const char* unused = nullptr;
        std::size_t unused_length = 0;
        return ReadString(line, length, i, unused, unused_length);
    }
    if (line[i] == '{' || line[i] == '[') {
        int depth = 0;
        while (i < length) {
            const char c = line[i];
            if (c == '"') {
                const char* unused = nullptr;
                std::size_t unused_length = 0;
                if (!ReadString(line, length, i, unused, unused_length)) {
                    return false;
                }
                continue;
            }
            if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
                if (depth == 0) {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }
    // A number or a literal: runs to the next delimiter.
    const std::size_t start = i;
    while (i < length && line[i] != ',' && line[i] != '}' && !IsSpace(line[i])) {
        ++i;
    }
    return i > start;
}

struct ItemHeader {
    bool has_length = false;
    std::size_t payload_length = 0;
    bool is_minidump = false;
    const char* filename = nullptr; // the attachment's file name, escapes left as they are; null if the header names none
    std::size_t filename_length = 0;
};

// Parses a flat item header line such as {"type":"attachment","length":123,"attachment_type":"event.minidump"}.
bool ParseItemHeader(const char* line, std::size_t length, ItemHeader& header) {
    std::size_t i = 0;
    SkipSpace(line, length, i);
    if (i >= length || line[i] != '{') {
        return false;
    }
    ++i;
    for (;;) {
        SkipSpace(line, length, i);
        if (i < length && line[i] == '}') {
            return true;
        }
        const char* key = nullptr;
        std::size_t key_length = 0;
        if (!ReadString(line, length, i, key, key_length)) {
            return false;
        }
        SkipSpace(line, length, i);
        if (i >= length || line[i] != ':') {
            return false;
        }
        ++i;
        SkipSpace(line, length, i);
        if (key_length == 6 && std::memcmp(key, "length", 6) == 0) {
            // A non-negative integer that fits; anything else makes the item unusable.
            std::size_t value = 0;
            std::size_t digits = 0;
            while (i < length && line[i] >= '0' && line[i] <= '9') {
                const std::size_t digit = static_cast<std::size_t>(line[i] - '0');
                if (value > (static_cast<std::size_t>(-1) - digit) / 10) {
                    return false; // overflow
                }
                value = value * 10 + digit;
                ++i;
                ++digits;
            }
            if (digits == 0) {
                return false;
            }
            header.has_length = true;
            header.payload_length = value;
        } else if (key_length == 15 && std::memcmp(key, "attachment_type", 15) == 0) {
            const char* text = nullptr;
            std::size_t text_length = 0;
            if (!ReadString(line, length, i, text, text_length)) {
                return false;
            }
            header.is_minidump = text_length == 14 && std::memcmp(text, "event.minidump", 14) == 0;
        } else if (key_length == 8 && std::memcmp(key, "filename", 8) == 0) {
            if (!ReadString(line, length, i, header.filename, header.filename_length)) {
                return false;
            }
        } else if (!SkipValue(line, length, i)) {
            return false;
        }
        SkipSpace(line, length, i);
        if (i < length && line[i] == ',') {
            ++i;
            continue;
        }
        SkipSpace(line, length, i);
        if (i < length && line[i] == '}') {
            return true;
        }
        return false;
    }
}

// The index of the first '\n' at or after `from`, or `size` if there is none.
std::size_t FindNewline(const char* data, std::size_t size, std::size_t from) {
    for (std::size_t i = from; i < size; ++i) {
        if (data[i] == '\n') {
            return i;
        }
    }
    return size;
}

// Walks every item; the first that `wanted` accepts is returned in `out_data`/`out_size`. The whole envelope is checked either way.
template <typename Wanted>
EnvelopeResult FindItem(const char* data, std::size_t size, Wanted wanted, const char** out_data, std::size_t* out_size) {
    *out_data = nullptr;
    *out_size = 0;
    if (data == nullptr || size == 0) {
        return EnvelopeResult::Malformed;
    }
    // The envelope header line: skipped (it has nested JSON we do not need).
    std::size_t pos = FindNewline(data, size, 0);
    if (pos >= size) {
        return EnvelopeResult::Malformed; // no newline at all
    }
    ++pos;
    bool found = false;
    while (pos < size) {
        const std::size_t line_end = FindNewline(data, size, pos);
        if (line_end >= size) {
            return EnvelopeResult::Malformed; // an item header with no end of line
        }
        ItemHeader header;
        if (!ParseItemHeader(data + pos, line_end - pos, header)) {
            return EnvelopeResult::Malformed;
        }
        std::size_t payload_start = line_end + 1;
        std::size_t payload_length = 0;
        if (header.has_length) {
            if (header.payload_length > size - payload_start) {
                return EnvelopeResult::Malformed; // the payload is longer than the data
            }
            payload_length = header.payload_length;
        } else {
            payload_length = FindNewline(data, size, payload_start) - payload_start; // runs to the end of the line
        }
        if (!found && wanted(header)) {
            *out_data = data + payload_start;
            *out_size = payload_length;
            found = true;
        }
        pos = payload_start + payload_length;
        if (pos < size) {
            if (data[pos] != '\n') {
                return EnvelopeResult::Malformed; // an item must end with a newline
            }
            ++pos;
        }
    }
    return found ? EnvelopeResult::Ok : EnvelopeResult::NoMinidump;
}

} // namespace

EnvelopeResult FindMinidump(const char* data, std::size_t size, Minidump* out) {
    return FindItem(data, size, [](const ItemHeader& h) { return h.is_minidump; }, &out->data, &out->size);
}

EnvelopeResult FindAttachment(const char* data, std::size_t size, const char* filename, Attachment* out) {
    const std::size_t name_length = filename != nullptr ? std::strlen(filename) : 0;
    return FindItem(
        data, size,
        [&](const ItemHeader& h) {
            return !h.is_minidump && h.filename != nullptr && h.filename_length == name_length && std::memcmp(h.filename, filename, name_length) == 0;
        },
        &out->data, &out->size);
}

} // namespace crash_extract
