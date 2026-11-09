#pragma once

// The data conversions the Win32 clipboard and drag and drop share (Phase 15 Day 205): global memory blocks, UTF-8 and UTF-16, CF_HDROP file
// lists and registered format names. Internal to engine/platform/windows; defined in win32_clipboard.cpp.

#include "containers/vector.h"
#include "os/windows/win32.h"
#include "string_view.h"
#include "types.h"

#include "unity/windows_enable.h"
#include <shellapi.h> // HDROP
#include "unity/windows_disable.h"

namespace engine_placeholder::os::win32::detail {

// Copies bytes into a movable global block (for SetClipboardData and STGMEDIUM); null on failure. The caller owns the block until it hands it over.
HGLOBAL GlobalCopy(const void* data, usize size);

// UTF-8 to a NUL-terminated UTF-16 buffer.
void ToWide(StringView utf8, Vector<wchar_t>* out);

// UTF-16 code units to UTF-8 bytes (no terminator).
void ToUtf8(const wchar_t* text, usize length, Vector<byte>* out);

// The registered clipboard format of a custom name, or 0 for an empty or too long name.
UINT CustomFormat(StringView name);

// A CF_HDROP block (DROPFILES and the wide paths) from UTF-8 paths separated by '\n'; null if a path is refused by AcceptLocalPath or the
// block cannot be made.
HGLOBAL MakeHdrop(const byte* paths, usize size);

// The paths of a CF_HDROP that AcceptLocalPath allows, as UTF-8 separated by '\n'; the others are counted in `*rejected`. Returns false if the
// list grows past kMaxClipboardBytes.
bool ReadHdrop(HDROP drop, Vector<byte>* out, u32* rejected);

// The text of a CF_UNICODETEXT block of `size` bytes, up to its NUL, as valid UTF-8 (replacements counted). Returns false if it is too large.
bool ReadUnicodeText(const void* block, usize size, Vector<byte>* out, u32* replaced);

} // namespace engine_placeholder::os::win32::detail
