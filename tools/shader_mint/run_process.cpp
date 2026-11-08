#include "run_process.h"

namespace engine_placeholder::shader_mint {

void AppendWindowsArgument(Text* line, StringView arg) {
    if (line->Length() != 0) {
        line->Append(' ');
    }
    bool plain = arg.Length() != 0;
    for (usize i = 0; i < arg.Length() && plain; ++i) {
        const char c = arg[i];
        plain = c != ' ' && c != '\t' && c != '\n' && c != '\v' && c != '"';
    }
    if (plain) {
        line->Append(arg);
        return;
    }
    line->Append('"');
    usize backslashes = 0;
    for (usize i = 0; i < arg.Length(); ++i) {
        const char c = arg[i];
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless a quote follows them: then each one is doubled and the quote is escaped.
        const usize copies = c == '"' ? backslashes * 2 + 1 : backslashes;
        for (usize k = 0; k < copies; ++k) {
            line->Append('\\');
        }
        backslashes = 0;
        line->Append(c);
    }
    // Before the closing quote, trailing backslashes are doubled so the quote stays a closing quote.
    for (usize k = 0; k < backslashes * 2; ++k) {
        line->Append('\\');
    }
    line->Append('"');
}

} // namespace engine_placeholder::shader_mint
