#include "ui/clipboard.h"

namespace engine_placeholder::ui {

void LocalClipboard::SetText(StringView text) {
    text_.Clear();
    for (usize i = 0; i < text.Length(); ++i) text_.Push(text[i]);
}

bool LocalClipboard::HasText() { return !text_.Empty(); }

Clipboard::Ticket LocalClipboard::RequestPaste(PasteFunction function, void* user) {
    if (function != nullptr && !text_.Empty()) function(user, Text());
    return 0;
}

void LocalClipboard::CancelPaste(Ticket) {}

} // namespace engine_placeholder::ui
