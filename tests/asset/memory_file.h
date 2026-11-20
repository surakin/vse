#pragma once

// A file in memory for the pak tests (Phase 22 Day 316): dense (every byte kept) or sparse (all-zero writes are
// dropped, so a file of several gigabytes costs only what is non-zero).

#include <catch2/catch_test_macros.hpp>

#include "containers/unique_ptr.h"
#include "containers/vector.h"
#include "lang/utility.h"
#include "cstr/mem_ops.h"
#include "io/file.h"

namespace vse::asset::test
{

class MemoryFile : public io::File
{
public:
    explicit MemoryFile(bool sparse = false) : sparse_(sparse) {}

    bool IsOpen() const override { return open_; }
    u32 Capabilities() const override { return io::kCapSeekable | io::kCapSizeKnown | io::kCapWritable; }

    io::Status Size(u64* out) override
    {
        *out = size_;
        return io::Status::Success();
    }

    io::Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) override
    {
        *bytes_read = 0;
        if (offset >= size_)
        {
            return io::Status::Success();
        }
        const usize n = static_cast<usize>(size_ - offset < length ? size_ - offset : length);
        MemSet(buffer, 0, n);
        for (usize i = 0; i < segments_.Size(); ++i)
        {
            const Segment& s = segments_[i];
            const u64 s_end = s.offset + s.data.Size();
            const u64 lo = s.offset > offset ? s.offset : offset;
            const u64 hi = s_end < offset + n ? s_end : offset + n;
            if (lo < hi)
            {
                MemCopy(static_cast<byte*>(buffer) + (lo - offset), s.data.Data() + (lo - s.offset),
                        static_cast<usize>(hi - lo));
            }
        }
        *bytes_read = n;
        return io::Status::Success();
    }

    io::Status Write(const void* data, usize length, u64 offset) override
    {
        if (offset + length > size_)
        {
            size_ = offset + length;
        }
        const byte* bytes = static_cast<const byte*>(data);
        if (sparse_)
        {
            bool zero = true;
            for (usize i = 0; i < length && zero; ++i)
            {
                zero = bytes[i] == 0;
            }
            if (zero)
            {
                return io::Status::Success();
            }
        }
        if (segments_.Size() > 0)
        {
            Segment& last = segments_[segments_.Size() - 1];
            if (last.offset + last.data.Size() == offset)
            {
                const usize old = last.data.Size();
                last.data.Resize(old + length);
                MemCopy(last.data.Data() + old, bytes, length);
                return io::Status::Success();
            }
        }
        Segment s;
        s.offset = offset;
        s.data.Resize(length);
        MemCopy(s.data.Data(), bytes, length);
        segments_.Push(Move(s));
        return io::Status::Success();
    }

    io::Status Append(const void* data, usize length) override { return Write(data, length, size_); }
    io::Status Flush() override { return io::Status::Success(); }
    io::Status Sync() override { return io::Status::Success(); }
    io::Status Truncate(u64 size) override
    {
        size_ = size;
        return io::Status::Success();
    }
    io::Status Close() override
    {
        open_ = false;
        return io::Status::Success();
    }

    /// The whole content as one buffer (dense files only).
    Vector<byte> Bytes() const
    {
        Vector<byte> out;
        out.Resize(static_cast<usize>(size_));
        MemSet(out.Data(), 0, out.Size());
        for (usize i = 0; i < segments_.Size(); ++i)
        {
            MemCopy(out.Data() + segments_[i].offset, segments_[i].data.Data(), segments_[i].data.Size());
        }
        return out;
    }

private:
    struct Segment
    {
        u64 offset = 0;
        Vector<byte> data;
    };

    bool sparse_;
    bool open_ = true;
    u64 size_ = 0;
    Vector<Segment> segments_;
};

/// A file made from `bytes` (copied), for reading.
inline io::FilePtr FileOf(const Vector<byte>& bytes, bool sparse = false)
{
    UniquePtr<MemoryFile> file = MakeUnique<MemoryFile>(sparse);
    REQUIRE(file->Write(bytes.Data(), bytes.Size(), 0).Ok());
    return io::FilePtr(file.Release());
}

}  // namespace vse::asset::test
