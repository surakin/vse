#include "ui/debug_overlay.h"

#include "cstr/format.h"
#include "cstr/cstr.h"

#include "log/log.h"
#include "ui/surface.h"

namespace engine_placeholder::ui
{

namespace
{

String<HeapAllocator> OwnedText(StringView text)
{
    return String<HeapAllocator>(HeapAllocator::Default(), text);
}

StringView TrimmedText(StringView text)
{
    usize begin = 0;
    usize end = text.Length();
    while (begin < end && text[begin] == ' ')
    {
        ++begin;
    }
    while (end > begin && text[end - 1] == ' ')
    {
        --end;
    }
    return text.Substring(begin, end - begin);
}

bool HasText(StringView text, StringView needle)
{
    if (needle.Length() == 0)
    {
        return true;
    }
    return text.Find(needle) != text.Length();
}

}  // namespace

// ---- LogCapture ----

LogCapture::LogCapture() : filter_(OwnedText(StringView()))
{
    lines_.Reserve(kMaxLines);
    // Start at the lines logged from now on: reading nothing moves `next_` to the end of the ring.
    next_ = LogLinesWritten();
}

void LogCapture::Take(const char* line, usize length, void* context)
{
    LogCapture* self = static_cast<LogCapture*>(context);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    {
        --length;
    }
    self->Append(StringView(line, length));
}

usize LogCapture::Poll()
{
    const u64 before = count_ + dropped_;
    next_ = ReadLogLinesSince(next_, &LogCapture::Take, this);
    return static_cast<usize>(count_ + dropped_ - before);
}

void LogCapture::Clear()
{
    visible_dirty_ = true;
    lines_.Clear();
    first_ = 0;
    count_ = 0;
    Poll();
    lines_.Clear();
    first_ = 0;
    count_ = 0;
}

void LogCapture::Append(StringView line)
{
    visible_dirty_ = true;
    if (count_ < kMaxLines)
    {
        lines_.Push(OwnedText(line));
        ++count_;
        return;
    }
    lines_[first_] = OwnedText(line);
    first_ = (first_ + 1) % kMaxLines;
    ++dropped_;
}

void LogCapture::SetFilter(StringView text)
{
    filter_ = OwnedText(text);
    visible_dirty_ = true;
}

void LogCapture::Refilter() const
{
    if (!visible_dirty_)
    {
        return;
    }
    visible_.Clear();
    for (usize i = 0; i < count_; ++i)
    {
        if (Passes(At(i)))
        {
            visible_.Push(static_cast<u32>(i));
        }
    }
    visible_dirty_ = false;
}

const String<HeapAllocator>& LogCapture::At(usize index) const
{
    return lines_[(first_ + index) % kMaxLines];
}

bool LogCapture::Passes(const String<HeapAllocator>& line) const
{
    return HasText(line.View(), filter_.View());
}

usize LogCapture::VisibleCount() const
{
    if (filter_.Length() == 0)
    {
        return count_;
    }
    Refilter();
    return visible_.Size();
}

StringView LogCapture::VisibleLine(usize index) const
{
    if (filter_.Length() == 0)
    {
        ENGINE_ASSERT(index < count_, "LogCapture::VisibleLine: the index is out of range");
        return At(index).View();
    }
    Refilter();
    ENGINE_ASSERT(index < visible_.Size(), "LogCapture::VisibleLine: the index is out of range");
    return At(visible_[index]).View();
}

// ---- FrameGraph ----

FrameGraph::FrameGraph(StringView name) : Widget(name) {}

void FrameGraph::Push(f32 ms)
{
    if (!(ms > 0))
    {
        ms = 0;
    }
    samples_[head_] = ms;
    head_ = (head_ + 1) % kSamples;
    if (count_ < kSamples)
    {
        ++count_;
    }
    MarkDirty(kDrawDirty);
}

f32 FrameGraph::Max() const
{
    f32 m = 0;
    for (usize i = 0; i < count_; ++i)
    {
        if (samples_[i] > m)
        {
            m = samples_[i];
        }
    }
    return m;
}

f32 FrameGraph::Average() const
{
    if (count_ == 0)
    {
        return 0;
    }
    f32 sum = 0;
    for (usize i = 0; i < count_; ++i)
    {
        sum += samples_[i];
    }
    return sum / static_cast<f32>(count_);
}

Size FrameGraph::Measure(const Constraints&)
{
    Size s;
    s.w = static_cast<f32>(kSamples);
    s.h = kHeight;
    return s;
}

void FrameGraph::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const Rect& b = Bounds();
    if (b.Empty())
    {
        return;
    }
    const u32 border = surface->Themes().Color(ColorId::Border);
    const u32 good = surface->Themes().Color(ColorId::Accent);
    const u32 bad = surface->Themes().Color(ColorId::Danger);
    canvas.PushClip(b);
    const f32 bar = b.w / static_cast<f32>(kSamples);
    // The oldest sample is at the left, so a graph that is not full starts at the right.
    const usize empty = kSamples - count_;
    for (usize i = 0; i < count_; ++i)
    {
        const usize at = (head_ + kSamples - count_ + i) % kSamples;
        f32 fraction = samples_[at] / kFullScaleMs;
        if (fraction > 1.0f)
        {
            fraction = 1.0f;
        }
        Rect r;
        r.w = bar;
        r.h = b.h * fraction;
        r.x = b.x + bar * static_cast<f32>(empty + i);
        r.y = b.y + b.h - r.h;
        canvas.DrawRect(r, samples_[at] > kFullScaleMs ? bad : good);
    }
    canvas.DrawLine(Point{b.x, b.y + b.h}, Point{b.x + b.w, b.y + b.h}, 1.0f, border);
    canvas.PopClip();
}

// ---- DebugOverlay ----

class DebugOverlay::ConsoleModel final : public ListModel
{
public:
    explicit ConsoleModel(LogCapture* capture) : capture_(capture) {}
    usize ItemCount() const override { return capture_->VisibleCount(); }
    StringView ItemText(usize index) override { return capture_->VisibleLine(index); }

private:
    LogCapture* capture_;
};

DebugOverlay::DebugOverlay(StringView name) : BoxLayout(Orientation::Vertical, name)
{
    SetSpacing(4.0f);
    Insets pad;
    pad.left = pad.top = pad.right = pad.bottom = 6.0f;
    SetPadding(pad);
    LayoutProps self;
    self.width = kWidth;
    self.height = kPanelHeight;
    self.align_x = Align::Start;
    self.align_y = Align::Start;
    SetProps(self);

    UniquePtr<FrameGraph> graph = MakeUnique<FrameGraph>(StringView("graph"));
    graph_ = graph.Get();
    AddChild(UniquePtr<Widget>(graph.Release()));

    UniquePtr<Label> counters = MakeUnique<Label>(StringView("counters"), StringView("no frames yet"));
    counters_ = counters.Get();
    AddChild(UniquePtr<Widget>(counters.Release()));

    UniquePtr<TextField> filter = MakeUnique<TextField>(StringView("filter"));
    filter->SetPlaceholder(StringView("filter the log (for example WARN)"));
    filter->OnChanged().Connect(&DebugOverlay::FilterChanged, this);
    filter_ = filter.Get();
    AddChild(UniquePtr<Widget>(filter.Release()));

    model_ = MakeUnique<ConsoleModel>(&capture_);
    UniquePtr<ListBox> console = MakeUnique<ListBox>(StringView("console"));
    console->SetModel(model_.Get());
    LayoutProps grow;
    grow.weight = 1;
    console->SetProps(grow);
    console_ = console.Get();
    AddChild(UniquePtr<Widget>(console.Release()));

    UniquePtr<TextField> line = MakeUnique<TextField>(StringView("command_line"));
    line->SetPlaceholder(StringView("command (help lists them)"));
    line->OnSubmit().Connect(&DebugOverlay::Submitted, this);
    command_line_ = line.Get();
    AddChild(UniquePtr<Widget>(line.Release()));

    SetCollapsed(true);
}

DebugOverlay::~DebugOverlay()
{
    if (surface_ != nullptr && accelerator_ != 0)
    {
        surface_->UnregisterAccelerator(accelerator_);
    }
}

bool DebugOverlay::Attach(Surface* surface, UniquePtr<DebugOverlay> overlay, const KeyChord& toggle)
{
    DebugOverlay* raw = overlay.Get();
    raw->surface_ = surface;
    surface->Root()->AddChild(UniquePtr<Widget>(overlay.Release()));
    raw->accelerator_ = surface->RegisterAccelerator(toggle, &DebugOverlay::Toggle, raw);
    return raw->accelerator_ != 0;
}

void DebugOverlay::Toggle(void* user)
{
    DebugOverlay* self = static_cast<DebugOverlay*>(user);
    self->Show(self->Collapsed());
}

void DebugOverlay::Show(bool show)
{
    if (show == !Collapsed())
    {
        return;
    }
    SetCollapsed(!show);
    if (show)
    {
        Refresh();
    }
}

void DebugOverlay::SetStats(const DebugStats& stats)
{
    graph_->Push(stats.frame_ms);
    const f32 fps = stats.frame_ms > 0 ? 1000.0f / stats.frame_ms : 0.0f;
    char text[160];
    FormatTo(text, sizeof(text), "%.1f fps  %.2f ms  gpu %.2f ms  draws %u  jobs %u/%u  gpu mem %llu MB",
             static_cast<double>(fps), static_cast<double>(stats.frame_ms), static_cast<double>(stats.gpu_ms),
             stats.draw_calls, stats.jobs_pending, stats.job_workers,
             static_cast<unsigned long long>(stats.gpu_bytes >> 20));
    counters_->SetText(StringView(text, CStrLength(text)));
}

bool DebugOverlay::AddCommand(StringView name, StringView help, CommandFn function, void* user)
{
    if (name.Length() == 0 || function == nullptr || commands_.Size() >= kMaxCommands)
    {
        return false;
    }
    if (name.Find(StringView(" ")) != name.Length())
    {
        return false;
    }
    if (name == StringView("help") || name == StringView("clear"))
    {
        return false;
    }
    for (usize i = 0; i < commands_.Size(); ++i)
    {
        if (commands_[i].name == name)
        {
            return false;
        }
    }
    commands_.Push(Command(OwnedText(name), OwnedText(help), function, user));
    return true;
}

bool DebugOverlay::Run(StringView line)
{
    line = TrimmedText(line);
    if (line.Length() == 0)
    {
        return false;
    }
    char echo[1024];
    const int n = FormatTo(echo, sizeof(echo), "> %.*s", static_cast<int>(line.Length()), line.Data());
    capture_.Append(StringView(
        echo, n > 0 ? (static_cast<usize>(n) < sizeof(echo) ? static_cast<usize>(n) : sizeof(echo) - 1) : 0));
    const usize space = line.Find(StringView(" "));
    const StringView name = line.Substring(0, space);
    const StringView argument = space < line.Length() ? TrimmedText(line.Substring(space)) : StringView();
    bool ran = false;
    if (name == StringView("clear"))
    {
        capture_.Clear();
        ran = true;
    }
    else if (name == StringView("help"))
    {
        capture_.Append(StringView("help: list the commands"));
        capture_.Append(StringView("clear: empty the console"));
        for (usize i = 0; i < commands_.Size(); ++i)
        {
            char buf[256];
            const int len = FormatTo(buf, sizeof(buf), "%s: %s", commands_[i].name.CStr(), commands_[i].help.CStr());
            capture_.Append(StringView(
                buf,
                len > 0 ? (static_cast<usize>(len) < sizeof(buf) ? static_cast<usize>(len) : sizeof(buf) - 1) : 0));
        }
        ran = true;
    }
    else
    {
        for (usize i = 0; i < commands_.Size(); ++i)
        {
            if (commands_[i].name == name)
            {
                commands_[i].function(commands_[i].user, argument, capture_);
                ran = true;
                break;
            }
        }
        if (!ran)
        {
            capture_.Append(StringView("unknown command (help lists them)"));
        }
    }
    Refresh();
    return ran;
}

void DebugOverlay::Submitted(void* user, TextField* field)
{
    DebugOverlay* self = static_cast<DebugOverlay*>(user);
    self->Run(field->Text());
    field->SetText(StringView());
}

void DebugOverlay::FilterChanged(void* user, TextField* field)
{
    DebugOverlay* self = static_cast<DebugOverlay*>(user);
    self->capture_.SetFilter(field->Text());
    self->Refresh();
}

void DebugOverlay::Refresh()
{
    // Follow the newest line only while the user has not scrolled up from where the last refresh left the list.
    const bool follow = console_->Offset() >= bottom_offset_ - 0.5f;
    console_->ModelChanged();
    const usize visible = capture_.VisibleCount();
    if (follow && visible > 0)
    {
        console_->ScrollToIndex(visible - 1);
        bottom_offset_ = console_->Offset();
    }
    ++refreshes_;
}

void DebugOverlay::Update(f32)
{
    if (capture_.Poll() > 0)
    {
        Refresh();
    }
}

void DebugOverlay::Arrange(const Rect& rect)
{
    Rect r = rect;
    Surface* surface = GetSurface();
    if (surface != nullptr)
    {
        const Rect& root = surface->Root()->Bounds();
        r.w = kWidth;
        r.h = kPanelHeight;
        r.x = root.x + root.w - kWidth - kMargin;
        r.y = root.y + kMargin;
    }
    BoxLayout::Arrange(r);
}

void DebugOverlay::Draw(Canvas& canvas)
{
    Surface* surface = GetSurface();
    const Rect& b = Bounds();
    canvas.DrawRect(b, surface->Themes().Color(ColorId::WindowBackground));
    canvas.DrawBorder(b, CornerRadii(0), 1.0f, surface->Themes().Color(ColorId::Border));
}

}  // namespace engine_placeholder::ui
