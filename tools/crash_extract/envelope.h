#pragma once

#include "types.h"

// Reading the minidump out of a sentry-native "envelope" file (Phase 7 Day 95).
//
// The crash handler leaves one envelope per crash in its dump directory: a header line of JSON, then items, each an
// item header line of JSON followed by its payload of `length` bytes and a newline. The minidump is the item whose
// header has "attachment_type":"event.minidump". The parser is a pure function over a buffer: it reads nothing outside
// it, whatever the bytes are.
namespace crash_extract
{

enum class EnvelopeResult
{
    Ok,          // the minidump was found; `Minidump` points into the input
    NoMinidump,  // a well-formed envelope with no minidump item
    Malformed    // not an envelope, or cut off, or an item longer than the data
};

struct Minidump
{
    const char* data = nullptr;  // inside the buffer passed to FindMinidump
    engine_placeholder::usize size = 0;
};

// Finds the first minidump item. `data` is borrowed for the call and for as long as `out` is used.
EnvelopeResult FindMinidump(const char* data, engine_placeholder::usize size, Minidump* out);

// An attachment's payload, inside the buffer passed to FindAttachment.
struct Attachment
{
    const char* data = nullptr;
    engine_placeholder::usize size = 0;
};

// Finds the first attachment (not the minidump) whose header names `filename` exactly, such as "crash-report.txt", the
// report the crash handler attaches (Phase 13 Day 156). NoMinidump means: a well-formed envelope without it.
EnvelopeResult FindAttachment(const char* data, engine_placeholder::usize size, const char* filename, Attachment* out);

}  // namespace crash_extract
