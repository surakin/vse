#include "io/file_log_device.h"

#include "cstr/mem_ops.h"
#include "io/file_system.h"
#include "sync/mutex.h"

namespace vse::io
{

Status FileLogDevice::Open(const Path& path, const FileLogOptions& options, UniquePtr<FileLogDevice>* out)
{
    FilePtr file;
    Status status = File::Open(path, OpenMode::Append, kOpenNone, &file);
    if (!status.Ok())
    {
        return status;
    }
    u64 size = 0;
    status = file->Size(&size);
    if (!status.Ok())
    {
        return status;
    }
    *out = MakeUnique<FileLogDevice>(path, options, Move(file), size);
    return Status::Success();
}

FileLogDevice::FileLogDevice(const Path& path, const FileLogOptions& options, FilePtr file, u64 size)
    : path_(path), options_(options), file_(Move(file)), size_(size)
{
}

FileLogDevice::~FileLogDevice()
{
    LockGuard<Mutex> lock(mutex_);
    FlushLocked();
}

bool FileLogDevice::FlushLocked()
{
    if (buffered_ == 0)
    {
        return true;
    }
    const usize n = buffered_;
    buffered_ = 0;
    if (file_ == nullptr || !file_->Append(buffer_, n).Ok())
    {
        // The buffered lines are lost; they are counted as lines.
        for (usize i = 0; i < n; ++i)
        {
            if (buffer_[i] == '\n')
            {
                ++dropped_;
            }
        }
        return false;
    }
    return file_->Flush().Ok();
}

Path FileLogDevice::RotatedName(u32 index) const
{
    char name[kMaxPathBytes + 16];
    StringView base = path_.View();
    MemCopy(name, base.Data(), base.Length());
    usize n = base.Length();
    name[n++] = '.';
    char digits[10];
    int count = 0;
    do
    {
        digits[count++] = static_cast<char>('0' + index % 10);
        index /= 10;
    } while (index != 0);
    while (count > 0)
    {
        name[n++] = digits[--count];
    }
    return Path(StringView(name, n), path_.Style());
}

void FileLogDevice::RotateLocked()
{
    FlushLocked();
    file_.Reset();  // closed before it is renamed (Windows will not rename an open file)
    if (options_.keep == 0)
    {
        Remove(path_);
    }
    else
    {
        Remove(RotatedName(options_.keep));
        for (u32 i = options_.keep; i > 1; --i)
        {
            Rename(RotatedName(i - 1), RotatedName(i));
        }
        Rename(path_, RotatedName(1));
    }
    size_ = 0;
    ++rotations_;
    FilePtr fresh;
    if (File::Open(path_, OpenMode::Append, kOpenNone, &fresh).Ok())
    {
        file_ = Move(fresh);
        u64 size = 0;
        if (file_->Size(&size).Ok())
        {
            size_ = size;
        }
    }
}

void FileLogDevice::Write(const LogRecord& record)
{
    const usize line = record.line.Length();
    const usize trace = record.trace.Length();
    const usize total = line + trace;
    LockGuard<Mutex> lock(mutex_);
    if (options_.max_bytes != 0 && size_ != 0 && size_ + total > options_.max_bytes)
    {
        RotateLocked();
    }
    if (file_ == nullptr)
    {
        // The file could not be reopened after a rotation: try again, else drop the line.
        FilePtr fresh;
        if (File::Open(path_, OpenMode::Append, kOpenNone, &fresh).Ok())
        {
            file_ = Move(fresh);
        }
        else
        {
            ++dropped_;
            return;
        }
    }
    if (buffered_ + total > kBufferBytes)
    {
        FlushLocked();
    }
    if (total > kBufferBytes)
    {
        // Larger than the buffer: written straight through, the line and its trace in order.
        const bool ok = file_->Append(record.line.Data(), line).Ok() &&
                        (trace == 0 || file_->Append(record.trace.Data(), trace).Ok());
        if (!ok)
        {
            ++dropped_;
        }
        size_ += total;
        return;
    }
    MemCopy(buffer_ + buffered_, record.line.Data(), line);
    buffered_ += line;
    if (trace != 0)
    {
        MemCopy(buffer_ + buffered_, record.trace.Data(), trace);
        buffered_ += trace;
    }
    size_ += total;
    if (record.level <= options_.flush_level)
    {
        FlushLocked();
    }
}

void FileLogDevice::Flush()
{
    LockGuard<Mutex> lock(mutex_);
    FlushLocked();
}

u64 FileLogDevice::DroppedLines()
{
    LockGuard<Mutex> lock(mutex_);
    return dropped_;
}

u32 FileLogDevice::Rotations()
{
    LockGuard<Mutex> lock(mutex_);
    return rotations_;
}

}  // namespace vse::io
