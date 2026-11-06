#pragma once

#include <cstddef>

// Reading the minidump out of a sentry-native "envelope" file (Phase 7 Day 95).
//
// The crash handler leaves one envelope per crash in its dump directory: a header line of JSON, then items, each an item
// header line of JSON followed by its payload of `length` bytes and a newline. The minidump is the item whose header has
// "attachment_type":"event.minidump". The parser is a pure function over a buffer: it reads nothing outside it, whatever
// the bytes are.
namespace crash_extract {

enum class EnvelopeResult {
    Ok,         // the minidump was found; `Minidump` points into the input
    NoMinidump, // a well-formed envelope with no minidump item
    Malformed   // not an envelope, or cut off, or an item longer than the data
};

struct Minidump {
    const char* data = nullptr; // inside the buffer passed to FindMinidump
    std::size_t size = 0;
};

// Finds the first minidump item. `data` is borrowed for the call and for as long as `out` is used.
EnvelopeResult FindMinidump(const char* data, std::size_t size, Minidump* out);

} // namespace crash_extract
