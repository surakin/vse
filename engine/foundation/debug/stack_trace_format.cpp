// FormatStackTrace, the same on every platform (Phase 13 Day 154): it is built on ResolveStackTrace, which each platform implements
// (debug/stack_trace_backtrace.cpp with libbacktrace on Linux, os/windows/debug/stack_trace_dbghelp.cpp with DbgHelp on Windows).
#include "debug/stack_trace.h"

namespace engine_placeholder::debug {

namespace {

// Small appender for FormatStackTrace: never writes past the buffer, always leaves room for the null byte.
class TextWriter {
public:
    TextWriter(char* out, usize capacity) : out_(out), capacity_(capacity) {
        if (capacity_ > 0) {
            out_[0] = '\0';
        }
    }
    void Add(const char* text) {
        if (text == nullptr) {
            return;
        }
        for (; *text != '\0'; ++text) {
            AddChar(*text);
        }
    }
    void AddChar(char c) {
        if (capacity_ > 0 && length_ + 1 < capacity_) {
            out_[length_++] = c;
            out_[length_] = '\0';
        }
    }
    void AddDecimal(u64 value) {
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value > 0);
        while (n > 0) {
            AddChar(digits[--n]);
        }
    }
    void AddHex(u64 value, int digits) {
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
            AddChar("0123456789abcdef"[(value >> shift) & 0xf]);
        }
    }
    usize Length() const { return length_; }

private:
    char* out_;
    usize capacity_;
    usize length_ = 0;
};

// The number of hex digits `value` needs, at least one.
int HexDigits(u64 value) {
    int digits = 1;
    while (digits < 16 && (value >> (digits * 4)) != 0) ++digits;
    return digits;
}

constexpr usize kEntriesPerFrame = 8; // inlined functions shown per address; more are dropped (and the last shown is marked inlined)

} // namespace

usize FormatStackTrace(const StackTrace& trace, char* out, usize capacity, const FormatOptions& options) {
    TextWriter writer(out, capacity);
    FrameInfo entries[kEntriesPerFrame];
    for (u32 frame = 0; frame < trace.count; ++frame) {
        StackTrace one;
        one.frames[0] = trace.frames[frame];
        one.count = 1;
        usize count = ResolveStackTrace(one, entries, kEntriesPerFrame);
        if (count == 0) { // no resolver: only the address is known
            entries[0] = FrameInfo();
            entries[0].address = trace.frames[frame];
            count = 1;
        }
        for (usize e = 0; e < count; ++e) {
            const FrameInfo& info = entries[e];
            writer.AddChar('#');
            writer.AddDecimal(frame);
            writer.Add("  0x");
            writer.AddHex(info.address, 16);
            writer.Add("  ");
            if (info.function[0] != '\0') {
                char readable[kMaxSymbolBytes];
                if (options.demangle && DemangleSymbol(info.function, readable, sizeof(readable))) {
                    writer.Add(readable);
                } else {
                    writer.Add(info.function);
                }
            } else if (info.has_module) {
                writer.Add(info.module);
                writer.Add("+0x");
                writer.AddHex(info.module_offset, HexDigits(info.module_offset)); // every digit (it once printed only the last one)
            } else {
                writer.Add("??");
            }
            if (info.file[0] != '\0') {
                writer.Add("  ");
                writer.Add(info.file);
                writer.AddChar(':');
                writer.AddDecimal(static_cast<u64>(info.line));
            }
            if (info.inlined) {
                writer.Add(" [inlined]");
            }
            writer.AddChar('\n');
        }
    }
    if (trace.truncated) {
        writer.Add("  ... more frames\n");
    }
    return writer.Length();
}

} // namespace engine_placeholder::debug
