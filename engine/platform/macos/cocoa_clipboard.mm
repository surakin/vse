// The clipboard of CocoaWindow (Phase 26 Day 313; platform/macos/cocoa_window.h, BACKEND.md "macos"): NSPasteboard.
//
// Text is NSPasteboardTypeString, a file list is file URLs (written with writeObjects:, read with
// readObjectsForClasses:) checked by the shared `text/uri-list` rules (ParseUriList: absolute,
// local, no `..`, no control byte), a custom format is the pasteboard type named by the program. The calls are
// synchronous: a request is answered at once and announced by its ClipboardData event, as on Windows. Another
// program's change is seen by polling changeCount in PumpEvents. The system keeps the data after the window closes.
// There is no primary selection.
#include "platform/macos/cocoa_window.h"

#include "cstr/mem_ops.h"
#include "os/os.h"
#include "string/utf8_codec.h"
#include "platform/macos/cocoa_window_native.h"

#import <AppKit/AppKit.h>

namespace engine_placeholder::os::cocoa
{

NSString* StringOfBytes(const byte* data, usize size)
{
    NSString* text = [[NSString alloc] initWithBytes:data length:size encoding:NSUTF8StringEncoding];
    return text != nil ? text : @"";
}

namespace
{

void AppendBytes(Vector<byte>* out, const char* data, usize size)
{
    const usize at = out->Size();
    out->Resize(at + size);
    if (size != 0)
    {
        MemCopy(out->Data() + at, data, size);
    }
}

}  // namespace

void Utf8Of(NSString* text, Vector<byte>* out, u32* replaced)
{
    out->Clear();
    const NSUInteger length = text.length;
    Vector<unichar> units;
    units.Resize(length);
    if (length != 0)
    {
        [text getCharacters:units.Data() range:NSMakeRange(0, length)];
    }
    u32 bad = 0;
    for (NSUInteger i = 0; i < length; ++i)
    {
        u32 scalar = units[i];
        if (scalar >= 0xD800 && scalar < 0xDC00 && i + 1 < length && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000)
        {
            scalar = 0x10000u + ((scalar - 0xD800u) << 10) + (units[i + 1] - 0xDC00u);
            ++i;
        }
        else if (scalar >= 0xD800 && scalar < 0xE000)
        {
            scalar = 0xFFFD;  // a surrogate without its other half
            ++bad;
        }
        Array<char, 4> bytes = {};
        const usize n = EncodeUtf8(scalar, bytes.Data());
        AppendBytes(out, bytes.Data(), n);
    }
    if (replaced != nullptr)
    {
        *replaced = bad;
    }
}

NSString* PasteboardTypeOf(ClipboardFormat format, StringView custom_name)
{
    switch (format)
    {
    case ClipboardFormat::Text: return NSPasteboardTypeString;
    case ClipboardFormat::FileList: return NSPasteboardTypeFileURL;
    case ClipboardFormat::Custom:
        if (custom_name.Length() == 0 || custom_name.Length() > 255)
        {
            return nil;
        }
        return [[NSString alloc] initWithBytes:custom_name.Data()
                                        length:custom_name.Length()
                                      encoding:NSUTF8StringEncoding];
    }
    return nil;
}

ClipboardStatus ReadPasteboard(NSPasteboard* pasteboard, ClipboardFormat format, NSString* type, Vector<byte>* out,
                               u32* replaced, u32* rejected)
{
    out->Clear();
    if (pasteboard == nil || type == nil)
    {
        return ClipboardStatus::Missing;
    }
    if (format == ClipboardFormat::Text)
    {
        NSString* text = [pasteboard stringForType:type];
        if (text == nil)
        {
            return ClipboardStatus::Missing;
        }
        if (text.length > kMaxClipboardBytes)
        {
            return ClipboardStatus::TooLarge;  // before converting: every UTF-16 unit is at least one byte
        }
        Utf8Of(text, out, replaced);
        if (out->Size() > kMaxClipboardBytes)
        {
            out->Clear();
            return ClipboardStatus::TooLarge;
        }
        return ClipboardStatus::Ok;
    }
    if (format == ClipboardFormat::FileList)
    {
        // Every URL, not only file URLs: the shared rules reject the others and count them (rejected_entries), where
        // AppKit's file-URLs-only option would drop them unseen. No file URL at all is no file list.
        NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:@{}];
        bool any_file = false;
        for (NSURL* url in urls)
        {
            any_file = any_file || url.fileURL;
        }
        if (!any_file)
        {
            return ClipboardStatus::Missing;
        }
        // Through the shared text/uri-list rules, as on Linux: the same checks for every platform's file lists.
        // At most kMaxDropFileEntries entries go through the rules; the rest are counted as rejected, so a huge list is
        // cut, not refused whole.
        Vector<byte> list;
        u32 beyond = 0;
        usize taken = 0;
        for (NSURL* url in urls)
        {
            if (taken == kMaxDropFileEntries)
            {
                ++beyond;
                continue;
            }
            ++taken;
            NSURL* path_url = url.filePathURL != nil ? url.filePathURL : url;  // a file reference URL as a path
            NSString* text = path_url.absoluteString;
            const usize size = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
            if (list.Size() + size + 2 > kMaxClipboardBytes)
            {
                return ClipboardStatus::TooLarge;
            }
            AppendBytes(&list, text.UTF8String, size);
            AppendBytes(&list, "\r\n", 2);
        }
        ParseUriList(list.Data(), list.Size(), *out, rejected);
        *rejected += beyond + LimitPathList(*out, kMaxDropFileEntries);
        return ClipboardStatus::Ok;
    }
    NSData* data = [pasteboard dataForType:type];
    if (data == nil)
    {
        return ClipboardStatus::Missing;
    }
    if (data.length > kMaxClipboardBytes)
    {
        return ClipboardStatus::TooLarge;
    }
    out->Resize(data.length);
    if (data.length != 0)
    {
        MemCopy(out->Data(), data.bytes, data.length);
    }
    return ClipboardStatus::Ok;
}

NSArray<NSURL*>* FileUrlsOf(const byte* paths, usize size)
{
    // A path is offered only if it survives the rules a reader applies: built into a uri-list and parsed back whole.
    Vector<byte> uris;
    BuildUriList(paths, size, uris);
    Vector<byte> accepted;
    u32 rejected = 0;
    ParseUriList(uris.Data(), uris.Size(), accepted, &rejected);
    if (rejected != 0 || accepted.Size() == 0)
    {
        return nil;
    }
    // The URLs from the uri-list's own lines (percent-encoded bytes), not fileURLWithPath:, which rewrites a path into
    // the decomposed form (café as e and a combining accent): the program's bytes are offered as they were given.
    NSMutableArray<NSURL*>* urls = [NSMutableArray array];
    usize start = 0;
    while (start < uris.Size())
    {
        usize end = start;
        while (end < uris.Size() && uris[end] != '\r' && uris[end] != '\n')
        {
            ++end;
        }
        if (end > start)
        {
            NSURL* url = [NSURL URLWithString:StringOfBytes(uris.Data() + start, end - start)];
            if (url == nil || !url.fileURL)
            {
                return nil;
            }
            [urls addObject:url];
        }
        start = end + 1;
    }
    return urls;
}

bool CocoaWindow::SupportsClipboard(ClipboardKind kind) const
{
    return native_ != nullptr && kind == ClipboardKind::Clipboard;
}

bool CocoaWindow::SetClipboard(ClipboardKind kind, const ClipboardOffer& offer)
{
    if (!SupportsClipboard(kind) || offer.data == nullptr || offer.size == 0 || offer.size > kMaxClipboardBytes)
    {
        return false;
    }
    @autoreleasepool
    {
        NSPasteboard* pasteboard = native_->pasteboard;
        bool written = false;
        switch (offer.format)
        {
        case ClipboardFormat::Text:
        {
            Vector<byte> clean;
            SanitizeUtf8(offer.data, offer.size, clean, nullptr);
            [pasteboard clearContents];
            written = [pasteboard setString:StringOfBytes(clean.Data(), clean.Size()) forType:NSPasteboardTypeString];
            break;
        }
        case ClipboardFormat::FileList:
        {
            NSArray<NSURL*>* urls = FileUrlsOf(offer.data, offer.size);
            if (urls == nil)
            {
                return false;  // a path a reader would refuse: nothing is changed
            }
            [pasteboard clearContents];
            written = [pasteboard writeObjects:urls];
            break;
        }
        case ClipboardFormat::Custom:
        {
            NSString* type = PasteboardTypeOf(ClipboardFormat::Custom, offer.custom_name);
            if (type == nil)
            {
                return false;
            }
            [pasteboard clearContents];
            written = [pasteboard setData:[NSData dataWithBytes:offer.data length:offer.size] forType:type];
            break;
        }
        }
        // Our own change raises no ClipboardChanged. A write AppKit refused after clearContents (NSPasteboard has no
        // transaction) has emptied the pasteboard: that change is left for the poll to report.
        if (written)
        {
            pasteboard_count_ = pasteboard.changeCount;
        }
        return written;
    }
}

u32 CocoaWindow::RequestClipboard(ClipboardKind kind, ClipboardFormat format, StringView custom_name)
{
    if (!SupportsClipboard(kind))
    {
        return 0;
    }
    const u64 now = MonotonicTimeNanos();
    const u32 id = clipboard_requests_.Begin(kind, format, now);
    if (id == 0)
    {
        return 0;
    }
    @autoreleasepool
    {
        Vector<byte> bytes;
        u32 replaced = 0;
        u32 rejected = 0;
        const ClipboardStatus status = ReadPasteboard(
            native_->pasteboard, format, PasteboardTypeOf(format, custom_name), &bytes, &replaced, &rejected);
        if (status != ClipboardStatus::Ok)
        {
            bytes.Clear();
        }
        clipboard_requests_.Complete(id, status, static_cast<Vector<byte>&&>(bytes), replaced, rejected, now);
    }
    return id;
}

bool CocoaWindow::ClipboardHasFormat(ClipboardKind kind, ClipboardFormat format, StringView custom_name) const
{
    if (!SupportsClipboard(kind))
    {
        return false;
    }
    @autoreleasepool
    {
        NSPasteboard* pasteboard = native_->pasteboard;
        if (format == ClipboardFormat::FileList)
        {
            return [pasteboard canReadObjectForClasses:@[ [NSURL class] ]
                                               options:@{
                                                   NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                               }];
        }
        NSString* type = PasteboardTypeOf(format, custom_name);
        return type != nil && [pasteboard availableTypeFromArray:@[ type ]] != nil;
    }
}

void CocoaWindow::PollPasteboard()
{
    const i64 count = native_->pasteboard.changeCount;
    if (count == pasteboard_count_)
    {
        return;
    }
    pasteboard_count_ = count;
    InputEvent changed;
    changed.type = InputEventType::ClipboardChanged;
    changed.clipboard_kind = static_cast<u8>(ClipboardKind::Clipboard);
    QueueEvent(changed);
}

void CocoaWindow::SetPasteboardForTest(void* pasteboard)
{
    if (native_ == nullptr)
    {
        return;
    }
    native_->pasteboard = pasteboard != nullptr ? (__bridge NSPasteboard*)pasteboard : [NSPasteboard generalPasteboard];
    pasteboard_count_ = native_->pasteboard.changeCount;
}

}  // namespace engine_placeholder::os::cocoa
