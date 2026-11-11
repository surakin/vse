#include "ui/widgets/text_field.h"

#include "lang/scalar.h"

#include "string/grapheme.h"
#include "text/cursor.h"
#include "ui/surface.h"
#include "ui/text_words.h"
#include "ui/widgets/look.h"

namespace engine_placeholder::ui
{

namespace
{

bool IsControl(const Event& e)
{
    if (e.text_length == 0)
    {
        return true;
    }
    const u8 c = static_cast<u8>(e.text[0]);
    return c < static_cast<u8>(' ') || c == 0x7F;
}

bool IsSpaceText(StringView s)
{
    return s.Length() > 0 && ClassOf(DecodeUtf8At(s.Data(), s.Length(), 0).codepoint) == CharClass::Space;
}

void Append(Vector<char>* out, StringView s)
{
    for (usize i = 0; i < s.Length(); ++i)
    {
        out->Push(s[i]);
    }
}

}  // namespace

TextField::TextField(StringView name) : Widget(name)
{
    SetFocusable(true);
    SetAcceptsText(true);
    SetCursor(os::CursorShape::IBeam);
}

TextField::~TextField()
{
    if (paste_ticket_ != 0 && paste_clipboard_ != nullptr)
    {
        paste_clipboard_->CancelPaste(paste_ticket_);
    }
}

f32 TextField::PaddingX(const ResolvedStyle& style) const
{
    return style.padding_x / GetSurface()->Scale();
}

bool TextField::Prepare()
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return false;
    }
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(),
                                                          surface->Scale());
    run_.SetText(Text());
    bool ok = run_.EnsureForStyle(surface, style, 0);
    if (password_)
    {
        const usize clusters = GraphemeClusterCount(Text());
        if (mask_text_.Size() != clusters * kBulletBytes)
        {
            mask_text_.Clear();
            for (usize i = 0; i < clusters; ++i)
            {
                Append(&mask_text_, StringView(kBullet));
            }
        }
        mask_.SetText(StringView(mask_text_.Data(), mask_text_.Size()));
        ok = mask_.EnsureForStyle(surface, style, 0) && ok;
    }
    return ok;
}

u32 TextField::NextCluster(u32 offset)
{
    if (offset >= Length())
    {
        return Length();
    }
    if (Prepare())
    {
        return text::NextBoundary(run_.Full(), offset);
    }
    u32 o = offset + 1;
    while (o < Length() && (static_cast<u8>(text_[o]) & 0xC0) == 0x80)
    {
        ++o;
    }
    return o;
}

u32 TextField::PreviousCluster(u32 offset)
{
    if (offset == 0)
    {
        return 0;
    }
    if (Prepare())
    {
        return text::PreviousBoundary(run_.Full(), offset);
    }
    u32 o = offset - 1;
    while (o > 0 && (static_cast<u8>(text_[o]) & 0xC0) == 0x80)
    {
        --o;
    }
    return o;
}

u32 TextField::Snap(u32 offset)
{
    if (offset >= Length())
    {
        return Length();
    }
    if (offset == 0)
    {
        return 0;
    }
    return PreviousCluster(NextCluster(offset));
}

u32 TextField::WordLeftTarget()
{
    return password_ ? 0 : Snap(WordStart(Text(), caret_));
}

u32 TextField::WordRightTarget()
{
    return password_ ? Length() : Snap(WordEnd(Text(), caret_));
}

u32 TextField::ToShown(u32 offset) const
{
    if (!password_)
    {
        return offset;
    }
    return static_cast<u32>(GraphemeClusterCount(Text().Substring(0, offset))) * kBulletBytes;
}

u32 TextField::FromShown(u32 shown) const
{
    if (!password_)
    {
        return shown;
    }
    usize index = shown / kBulletBytes;
    for (StringView cluster : GraphemeClusters(Text()))
    {
        if (index == 0)
        {
            return static_cast<u32>(cluster.Data() - text_.Data());
        }
        --index;
    }
    return Length();
}

void TextField::SetText(StringView text)
{
    text_.Clear();
    Append(&text_, text);
    if (max_length_ > 0)
    {
        usize count = 0;
        usize cut = text_.Size();
        for (StringView cluster : GraphemeClusters(Text()))
        {
            if (count == max_length_)
            {
                cut = static_cast<usize>(cluster.Data() - text_.Data());
                break;
            }
            ++count;
        }
        text_.Resize(cut);
    }
    caret_ = anchor_ = Length();
    undo_.Clear();
    redo_.Clear();
    chain_ = false;
    scroll_x_ = 0;
    MarkDirty(kDrawDirty);
    if (GetSurface() != nullptr)
    {
        EnsureCaretVisible();
    }
}

void TextField::SetPlaceholder(StringView text)
{
    placeholder_.Clear();
    Append(&placeholder_, text);
    MarkDirty(kDrawDirty);
}

void TextField::SetPassword(bool password)
{
    password_ = password;
    mask_text_.Clear();
    MarkDirty(kDrawDirty);
}

void TextField::SetFrame(bool frame)
{
    frame_ = frame;
    MarkDirty(kDrawDirty);
}

void TextField::SetSelection(u32 anchor, u32 caret)
{
    anchor_ = Snap(anchor);
    caret_ = Snap(caret);
    chain_ = false;
    ResetBlink();
    if (GetSurface() != nullptr)
    {
        EnsureCaretVisible();
    }
    MarkDirty(kDrawDirty);
}

void TextField::SelectAll()
{
    SetSelection(0, Length());
}

StringView TextField::SelectedText() const
{
    const u32 lo = caret_ < anchor_ ? caret_ : anchor_;
    const u32 hi = caret_ < anchor_ ? anchor_ : caret_;
    return Text().Substring(lo, hi - lo);
}

void TextField::ResetBlink()
{
    Surface* surface = GetSurface();
    blink_epoch_ = surface != nullptr ? surface->Time() : 0.0;
    blink_shown_ = true;
}

bool TextField::CaretOn() const
{
    const Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return false;
    }
    const f64 phase = Fmod(surface->Time() - blink_epoch_, kBlinkSeconds * 2);
    return phase < kBlinkSeconds;
}

bool TextField::WantsUpdate() const
{
    return (State() & kStateFocused) != 0;
}

void TextField::Update(f32)
{
    const bool on = CaretOn();
    if (on != blink_shown_)
    {
        blink_shown_ = on;
        MarkDirty(kDrawDirty);
    }
}

Rect TextField::CaretRect()
{
    Surface* surface = GetSurface();
    if (surface == nullptr || !Prepare())
    {
        return Rect();
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    const text::CursorPosition pos = text::PositionOfOffset(Shown().Full(), ToShown(caret_));
    const Rect& b = Bounds();
    Rect r;
    r.x = b.x + PaddingX(style) - scroll_x_ + pos.x / scale;
    r.y = b.y + (b.h - Shown().Height()) / 2 + pos.y / scale;
    r.w = kCaretWidth;
    r.h = pos.height / scale;
    return r;
}

void TextField::EnsureCaretVisible()
{
    Surface* surface = GetSurface();
    if (surface == nullptr || !Prepare())
    {
        return;
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    const f32 inner = Bounds().w - PaddingX(style) * 2;
    if (!(inner > 0))
    {
        return;
    }
    const f32 text_w = Shown().Width();
    if (text_w + kCaretWidth <= inner)
    {
        scroll_x_ = 0;
        return;
    }
    const f32 caret_x = text::PositionOfOffset(Shown().Full(), ToShown(caret_)).x / scale;
    const f32 margin = FMin(kScrollMargin, inner / 2);
    if (caret_x - scroll_x_ < margin)
    {
        scroll_x_ = caret_x - margin;
    }
    if (caret_x - scroll_x_ > inner - margin)
    {
        scroll_x_ = caret_x - inner + margin;
    }
    const f32 max_scroll = text_w + kCaretWidth - inner;
    if (scroll_x_ > max_scroll)
    {
        scroll_x_ = max_scroll;
    }
    if (scroll_x_ < 0)
    {
        scroll_x_ = 0;
    }
}

u32 TextField::OffsetAtX(f32 local_x)
{
    Surface* surface = GetSurface();
    if (surface == nullptr || !Prepare())
    {
        return caret_;
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), State(), scale);
    const f32 x = (local_x - PaddingX(style) + scroll_x_) * scale;
    return Snap(FromShown(text::OffsetAtPoint(Shown().Full(), x, 0)));
}

Size TextField::Measure(const Constraints&)
{
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return Size();
    }
    const f32 scale = surface->Scale();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), 0, scale);
    TextRun probe;
    probe.SetText(StringView("Ag"));
    f32 line = 0;
    if (probe.EnsureForStyle(surface, style, 0))
    {
        line = probe.Height();
    }
    Size size;
    size.w = surface->Themes().Metric(MetricId::IconSize, 1.0f) * kWidthInIcons;
    size.h = line + style.padding_y / scale * 2;
    return size;
}

void TextField::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const f32 scale = surface->Scale();
    const u32 state = State();
    const ResolvedStyle style = surface->Themes().Resolve(widget_kind::kTextField, VariantId(0), state, scale);
    const Rect& b = Bounds();
    if (frame_)
    {
        DrawStyleBox(canvas, b, style, scale);
    }
    if (!Prepare())
    {
        return;
    }
    const bool focused = (state & kStateFocused) != 0;
    const f32 pad = PaddingX(style);
    canvas.PushClip(Rect{b.x + pad / 2, b.y, b.w - pad, b.h});
    const text::TextLayout& layout = Shown().Full();
    const f32 ox = b.x + pad - scroll_x_;
    const f32 oy = b.y + (b.h - layout.height / scale) / 2;
    if (text_.Empty() && !focused && placeholder_.Size() > 0)
    {
        placeholder_run_.SetText(StringView(placeholder_.Data(), placeholder_.Size()));
        if (placeholder_run_.EnsureForStyle(surface, style, 0))
        {
            const text::TextLayout& pl = placeholder_run_.Full();
            canvas.DrawText(pl, placeholder_run_.Style(), Point{b.x + pad, b.y + (b.h - pl.height / scale) / 2},
                            surface->Themes().Color(ColorId::TextDisabled));
        }
    }
    if (focused && HasSelection())
    {
        Vector<text::SelectionRect> rects;
        if (text::SelectionRects(layout, ToShown(anchor_), ToShown(caret_), &rects).Ok())
        {
            const u32 color = WithAlpha(surface->Themes().Color(ColorId::Accent), kSelectionAlpha);
            for (usize i = 0; i < rects.Size(); ++i)
            {
                canvas.DrawRect(
                    Rect{ox + rects[i].x / scale, oy + rects[i].y / scale, rects[i].w / scale, rects[i].h / scale},
                    color);
            }
        }
    }
    if (!text_.Empty())
    {
        canvas.DrawText(layout, Shown().Style(), Point{ox, oy}, style.text_color);
    }
    if (focused && CaretOn() && drop_caret_ == kNoCaret)
    {
        const text::CursorPosition pos = text::PositionOfOffset(layout, ToShown(caret_));
        canvas.DrawRect(Rect{ox + pos.x / scale, oy + pos.y / scale, kCaretWidth, pos.height / scale},
                        style.text_color);
    }
    if (drop_caret_ != kNoCaret)
    {  // where a dragged text would land
        const text::CursorPosition pos = text::PositionOfOffset(
            layout, ToShown(drop_caret_ > Length() ? Length() : drop_caret_));
        const ResolvedStyle marker = surface->Themes().Resolve(widget_kind::kDropMarker, VariantId(0), 0, scale);
        canvas.DrawRect(Rect{ox + pos.x / scale, oy + pos.y / scale, kDropMarkerThickness, pos.height / scale},
                        marker.background);
    }
    canvas.PopClip();
}

void TextField::Notify()
{
    EnsureCaretVisible();
    MarkDirty(kDrawDirty);
    on_changed_.Invoke(this);
}

void TextField::MoveCaret(u32 to, bool extend)
{
    caret_ = to;
    if (!extend)
    {
        anchor_ = to;
    }
    chain_ = false;
    ResetBlink();
    EnsureCaretVisible();
    MarkDirty(kDrawDirty);
}

void TextField::Apply(u32 at, u32 remove_length, StringView insert)
{
    Vector<char> next;
    for (u32 i = 0; i < at; ++i)
    {
        next.Push(text_[i]);
    }
    Append(&next, insert);
    for (u32 i = at + remove_length; i < Length(); ++i)
    {
        next.Push(text_[i]);
    }
    text_ = static_cast<Vector<char>&&>(next);
}

bool TextField::Replace(u32 begin, u32 end, StringView insert, EditKind kind)
{
    if (read_only_)
    {
        return false;
    }
    if (begin > end)
    {
        const u32 t = begin;
        begin = end;
        end = t;
    }
    if (end > Length())
    {
        end = Length();
    }
    if (begin > end)
    {
        begin = end;
    }
    const StringView removed = Text().Substring(begin, end - begin);
    if (max_length_ > 0)
    {
        const usize have = GraphemeClusterCount(Text()) - GraphemeClusterCount(removed);
        usize room = have >= max_length_ ? 0 : max_length_ - have;
        usize cut = 0;
        for (StringView cluster : GraphemeClusters(insert))
        {
            if (room == 0)
            {
                break;
            }
            --room;
            cut = static_cast<usize>(cluster.Data() - insert.Data()) + cluster.Length();
        }
        insert = insert.Substring(0, cut);
    }
    if (removed.Length() == 0 && insert.Length() == 0)
    {
        return false;
    }
    if (removed == insert)
    {  // nothing changes, but typing over a selection still ends the selection after what was typed
        caret_ = anchor_ = begin + static_cast<u32>(insert.Length());
        ResetBlink();
        EnsureCaretVisible();
        MarkDirty(kDrawDirty);
        return false;
    }
    if (validator_ != nullptr)
    {
        Vector<char> proposed;
        Append(&proposed, Text().Substring(0, begin));
        Append(&proposed, insert);
        Append(&proposed, Text().Substring(end, Length() - end));
        if (!validator_(validator_user_, this, StringView(proposed.Data(), proposed.Size())))
        {
            return false;
        }
    }
    redo_.Clear();
    bool joined = false;
    if (chain_ && !undo_.Empty())
    {
        Edit& last = undo_[undo_.Size() - 1];
        const u32 last_end = last.at + static_cast<u32>(last.inserted.Size());
        if (kind == EditKind::Typing && last.kind == EditKind::Typing && removed.Length() == 0 && begin == last_end &&
            IsSpaceText(insert) == IsSpaceText(StringView(last.inserted.Data(), last.inserted.Size())))
        {
            Append(&last.inserted, insert);
            joined = true;
        }
        else if (kind == EditKind::Backspace && last.kind == EditKind::Backspace && last.inserted.Empty() &&
                 end == last.at)
        {
            Vector<char> joined_removed;
            Append(&joined_removed, removed);
            Append(&joined_removed, StringView(last.removed.Data(), last.removed.Size()));
            last.removed = static_cast<Vector<char>&&>(joined_removed);
            last.at = begin;
            joined = true;
        }
        else if (kind == EditKind::Delete && last.kind == EditKind::Delete && last.inserted.Empty() && begin == last.at)
        {
            Append(&last.removed, removed);
            joined = true;
        }
    }
    if (!joined)
    {
        Edit edit;
        edit.at = begin;
        Append(&edit.removed, removed);
        Append(&edit.inserted, insert);
        edit.caret_before = caret_;
        edit.anchor_before = anchor_;
        edit.kind = kind;
        undo_.Push(static_cast<Edit&&>(edit));
        if (undo_.Size() > kMaxUndo)
        {
            undo_.RemoveRange(0, 1);
        }
    }
    chain_ = kind != EditKind::Other;
    Apply(begin, end - begin, insert);
    caret_ = anchor_ = begin + static_cast<u32>(insert.Length());
    ResetBlink();
    Notify();
    return true;
}

bool TextField::Undo()
{
    if (undo_.Empty())
    {
        return false;
    }
    Edit edit = static_cast<Edit&&>(undo_[undo_.Size() - 1]);
    undo_.Pop();
    Apply(edit.at, static_cast<u32>(edit.inserted.Size()), StringView(edit.removed.Data(), edit.removed.Size()));
    caret_ = edit.caret_before > Length() ? Length() : edit.caret_before;
    anchor_ = edit.anchor_before > Length() ? Length() : edit.anchor_before;
    redo_.Push(static_cast<Edit&&>(edit));
    chain_ = false;
    ResetBlink();
    Notify();
    return true;
}

bool TextField::Redo()
{
    if (redo_.Empty())
    {
        return false;
    }
    Edit edit = static_cast<Edit&&>(redo_[redo_.Size() - 1]);
    redo_.Pop();
    Apply(edit.at, static_cast<u32>(edit.removed.Size()), StringView(edit.inserted.Data(), edit.inserted.Size()));
    caret_ = anchor_ = edit.at + static_cast<u32>(edit.inserted.Size());
    undo_.Push(static_cast<Edit&&>(edit));
    chain_ = false;
    ResetBlink();
    Notify();
    return true;
}

void TextField::DeleteSelectionOrStep(bool forward, bool by_word)
{
    if (HasSelection())
    {
        Replace(caret_, anchor_, StringView(), EditKind::Other);
        return;
    }
    u32 to = caret_;
    if (forward)
    {
        to = by_word ? WordRightTarget() : NextCluster(caret_);
    }
    else
    {
        to = by_word ? WordLeftTarget() : PreviousCluster(caret_);
    }
    if (to == caret_)
    {
        return;
    }
    Replace(caret_, to, StringView(), by_word ? EditKind::Other : (forward ? EditKind::Delete : EditKind::Backspace));
}

void TextField::Copy(bool cut)
{
    if (password_ || !HasSelection())
    {
        return;
    }
    Surface* surface = GetSurface();
    if (surface == nullptr)
    {
        return;
    }
    surface->GetClipboard().SetText(SelectedText());
    if (cut)
    {
        Replace(caret_, anchor_, StringView(), EditKind::Other);
    }
}

void TextField::PasteReady(void* user, StringView text)
{
    TextField* field = static_cast<TextField*>(user);
    field->paste_ticket_ = 0;
    field->InsertPasted(text);
}

void TextField::InsertPasted(StringView text)
{
    usize end = 0;
    while (end < text.Length() && text[end] != '\n' && text[end] != '\r')
    {
        ++end;
    }
    if (end == 0)
    {
        return;  // nothing to insert must not delete the selection
    }
    // Typed text never holds control characters; pasted text does not either (a tab or an escape would sit in the
    // single line).
    String<HeapAllocator> clean{HeapAllocator::Default()};
    for (usize i = 0; i < end; ++i)
    {
        const u8 c = static_cast<u8>(text[i]);
        if (c >= 0x20 && c != 0x7F)
        {
            clean.Append(text[i]);
        }
    }
    if (clean.Length() == 0)
    {
        return;
    }
    Replace(caret_, anchor_, clean.View(), EditKind::Other);
}

void TextField::Paste()
{
    Surface* surface = GetSurface();
    if (surface == nullptr || read_only_)
    {
        return;
    }
    if (paste_ticket_ != 0 && paste_clipboard_ != nullptr)
    {
        paste_clipboard_->CancelPaste(paste_ticket_);
    }
    paste_clipboard_ = &surface->GetClipboard();
    paste_ticket_ = paste_clipboard_->RequestPaste(&TextField::PasteReady, this);
}

void TextField::PastePrimary()
{
    Surface* surface = GetSurface();
    if (surface == nullptr || read_only_ || !surface->GetClipboard().SupportsPrimary())
    {
        return;
    }
    if (paste_ticket_ != 0 && paste_clipboard_ != nullptr)
    {
        paste_clipboard_->CancelPaste(paste_ticket_);
    }
    paste_clipboard_ = &surface->GetClipboard();
    paste_ticket_ = paste_clipboard_->RequestPrimaryPaste(&TextField::PasteReady, this);
}

void TextField::PublishPrimary()
{
    Surface* surface = GetSurface();
    if (surface == nullptr || password_ || !HasSelection())
    {
        return;
    }
    surface->GetClipboard().SetPrimaryText(SelectedText());  // selecting text is copying it to the primary selection
}

bool TextField::CanEdit(EditCommand command) const
{
    const Surface* surface = GetSurface();
    switch (command)
    {
    case EditCommand::Cut: return !password_ && !read_only_ && HasSelection();
    case EditCommand::Copy: return !password_ && HasSelection();
    case EditCommand::Paste:
        return !read_only_ && surface != nullptr && const_cast<Surface*>(surface)->GetClipboard().HasText();
    case EditCommand::SelectAll: return !text_.Empty();
    }
    return false;
}

void TextField::RunEdit(EditCommand command)
{
    if (!CanEdit(command))
    {
        return;
    }
    switch (command)
    {
    case EditCommand::Cut: Copy(true); break;
    case EditCommand::Copy: Copy(false); break;
    case EditCommand::Paste: Paste(); break;
    case EditCommand::SelectAll: SelectAll(); break;
    }
}

DragAction TextField::CanAccept(const DragPayload& payload, Point position)
{
    if (read_only_ || password_ || !payload.Has(DragEntryKind::Text))
    {
        return DragAction::Nothing;
    }
    const bool own = payload.PointerOf(StringView(kDragTag)) == this;
    DragAction action = payload.Requested();
    if (action == DragAction::Link)
    {
        action = payload.Allows(DragAction::Copy) ? DragAction::Copy : DragAction::Nothing;
    }
    if (own)
    {
        const u32 at = OffsetAtX(position.x);
        if (at >= drag_lo_ && at <= drag_hi_)
        {
            return DragAction::Nothing;  // onto the text being dragged
        }
    }
    return action;
}

void TextField::OnDragOver(const DragPayload&, Point position, DragAction)
{
    const u32 at = OffsetAtX(position.x);
    if (at != drop_caret_)
    {
        drop_caret_ = at;
        MarkDirty(kDrawDirty);
    }
}

void TextField::OnDragLeave()
{
    drop_caret_ = kNoCaret;
    MarkDirty(kDrawDirty);
}

void TextField::OnDrop(const DragPayload& payload, Point position, DragAction action)
{
    drop_caret_ = kNoCaret;
    MarkDirty(kDrawDirty);
    StringView text = payload.Text();
    usize end = 0;
    while (end < text.Length() && text[end] != '\n' && text[end] != '\r')
    {
        ++end;
    }
    text = text.Substring(0, end);  // a field holds one line
    if (text.Length() == 0)
    {
        return;
    }
    u32 at = OffsetAtX(position.x);
    const bool own = payload.PointerOf(StringView(kDragTag)) == this;
    if (own && action == DragAction::Move)
    {
        const u32 lo = drag_lo_;
        const u32 hi = drag_hi_;
        if (at >= lo && at <= hi)
        {
            moved_in_place_ = true;  // dropped onto itself: nothing moves, and the source must not remove it
            return;
        }
        Replace(lo, hi, StringView(), EditKind::Other);
        if (at > hi)
        {
            at -= hi - lo;
        }
        moved_in_place_ = true;
    }
    const u32 before = Length();
    if (!Replace(at, at, text, EditKind::Other))
    {
        return;
    }
    SetSelection(at, at + (Length() - before));
}

void TextField::OnDragEnded(DragAction action)
{
    drag_pending_ = false;
    if (action == DragAction::Move && !moved_in_place_ && !read_only_ && drag_hi_ <= Length() &&
        drag_hi_ - drag_lo_ == drag_text_.Size() &&
        StringView(text_.Data() + drag_lo_, drag_text_.Size()) == StringView(drag_text_.Data(), drag_text_.Size()))
    {
        Replace(drag_lo_, drag_hi_, StringView(),
                EditKind::Other);  // the text went elsewhere, and is still where it was taken from
    }
    moved_in_place_ = false;
    drop_caret_ = kNoCaret;
    MarkDirty(kDrawDirty);
}

void TextField::Press(Event& event)
{
    RequestFocus();
    RequestCapture();
    const bool near = Abs(event.position.x - last_press_.x) <= kClickSlop &&
                      Abs(event.position.y - last_press_.y) <= kClickSlop;
    if (event.time - last_press_time_ <= kMultiClickSeconds && near)
    {
        ++press_count_;
    }
    else
    {
        press_count_ = 1;
    }
    last_press_time_ = event.time;
    last_press_ = event.position;
    const u32 at = OffsetAtX(event.position.x);
    drag_pending_ = false;
    if (press_count_ == 1 && !password_ && HasSelection() && (event.modifiers & os::kModShift) == 0)
    {
        const u32 lo = caret_ < anchor_ ? caret_ : anchor_;
        const u32 hi = caret_ > anchor_ ? caret_ : anchor_;
        if (at >= lo && at < hi)
        {  // a press on the selection: it may start a drag, and is a click if it does not
            drag_pending_ = true;
            drag_press_ = event.position;
            drag_lo_ = lo;
            drag_hi_ = hi;
            return;
        }
    }
    if (press_count_ == 2)
    {
        u32 lo = 0;
        u32 hi = 0;
        if (password_)
        {
            hi = Length();  // a password is one word: where its spaces are must not show
        }
        else
        {
            WordRangeAt(Text(), at, &lo, &hi);
        }
        SetSelection(lo, hi);
        PublishPrimary();
    }
    else if (press_count_ >= 2)
    {
        SelectAll();
        PublishPrimary();
        press_count_ = 0;
    }
    else
    {
        MoveCaret(at, (event.modifiers & os::kModShift) != 0);
    }
}

void TextField::OnEvent(Event& event)
{
    switch (event.type)
    {
    case EventType::PointerDown:
        if (event.button == os::kMouseMiddle)
        {  // a middle click pastes the primary selection at the pointer
            event.handled = true;
            RequestFocus();
            MoveCaret(OffsetAtX(event.position.x), false);
            PastePrimary();
            break;
        }
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        event.handled = true;
        Press(event);
        break;
    case EventType::PointerUp:
        if (event.button != os::kMouseLeft)
        {
            break;
        }
        if (drag_pending_)
        {  // the press on the selection was a click: the caret goes there
            drag_pending_ = false;
            MoveCaret(OffsetAtX(event.position.x), false);
        }
        else if (press_count_ == 1 && HasSelection())
        {
            PublishPrimary();
        }
        event.handled = true;
        break;
    case EventType::PointerMove:
        if (drag_pending_ && GetSurface()->Capture() == this && GetSurface()->PastDragSlop(drag_press_, event.position))
        {
            drag_pending_ = false;
            DragPayload payload;
            drag_text_.Clear();
            const StringView taken = SelectedText();
            for (usize i = 0; i < taken.Length(); ++i)
            {
                drag_text_.Push(taken[i]);
            }
            payload.AddText(SelectedText());
            payload.AddPointer(StringView(kDragTag), this);
            payload.SetAllowed(read_only_ ? os::DragActionBit(DragAction::Copy)
                                          : static_cast<u8>(os::DragActionBit(DragAction::Copy) |
                                                            os::DragActionBit(DragAction::Move)));
            moved_in_place_ = false;
            GetSurface()->BeginDrag(static_cast<DragPayload&&>(payload));
            event.handled = true;
            break;
        }
        if (GetSurface()->Capture() == this && press_count_ == 1 && !drag_pending_)
        {
            MoveCaret(OffsetAtX(event.position.x), true);
            event.handled = true;
        }
        break;
    case EventType::FocusGained:
        ResetBlink();
        chain_ = false;
        MarkDirty(kDrawDirty);
        break;
    case EventType::FocusLost:
        chain_ = false;
        MarkDirty(kDrawDirty);
        on_focus_lost_.Invoke(this);
        break;
    case EventType::Text:
        event.handled = true;
        if (IsControl(event) || read_only_)
        {
            break;
        }
        Replace(caret_, anchor_, StringView(event.text, event.text_length), EditKind::Typing);
        break;
    case EventType::KeyDown:
    {
        const bool ctrl = (event.modifiers & os::kModCtrl) != 0;
        const bool shift = (event.modifiers & os::kModShift) != 0;
        event.handled = true;
        switch (event.key)
        {
        case Key::Left:
        {
            const u32 lo = caret_ < anchor_ ? caret_ : anchor_;
            if (HasSelection() && !shift && !ctrl)
            {
                MoveCaret(lo, false);
            }
            else
            {
                MoveCaret(ctrl ? WordLeftTarget() : PreviousCluster(caret_), shift);
            }
            break;
        }
        case Key::Right:
        {
            const u32 hi = caret_ > anchor_ ? caret_ : anchor_;
            if (HasSelection() && !shift && !ctrl)
            {
                MoveCaret(hi, false);
            }
            else
            {
                MoveCaret(ctrl ? WordRightTarget() : NextCluster(caret_), shift);
            }
            break;
        }
        case Key::Home: MoveCaret(0, shift); break;
        case Key::End: MoveCaret(Length(), shift); break;
        case Key::Backspace: DeleteSelectionOrStep(false, ctrl); break;
        case Key::Delete:
            if (shift)
            {
                Copy(true);
            }
            else
            {
                DeleteSelectionOrStep(true, ctrl);
            }
            break;
        case Key::Insert:
            if (ctrl)
            {
                Copy(false);
            }
            else if (shift)
            {
                Paste();
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::A:
            if (ctrl)
            {
                SelectAll();
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::C:
            if (ctrl)
            {
                Copy(false);
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::X:
            if (ctrl)
            {
                Copy(true);
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::V:
            if (ctrl)
            {
                Paste();
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::Z:
            if (!ctrl)
            {
                event.handled = false;
            }
            else if (shift)
            {
                if (!read_only_)
                {
                    Redo();
                }
            }
            else if (!read_only_)
            {
                Undo();
            }
            break;
        case Key::Y:
            if (ctrl)
            {
                if (!read_only_)
                {
                    Redo();
                }
            }
            else
            {
                event.handled = false;
            }
            break;
        case Key::Enter: on_submit_.Invoke(this); break;
        case Key::Escape: on_cancel_.Invoke(this); break;
        default: event.handled = false; break;
        }
        break;
    }
    default: break;
    }
}

}  // namespace engine_placeholder::ui
