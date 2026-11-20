#include "asset/load_buffer_pool.h"

#include "asset/blob.h"
#include "cstr/mem_ops.h"
#include "mem/heap_allocator.h"

namespace vse::asset
{
namespace
{

constexpr usize kMinClassBytes = 256;
constexpr usize kLargeBytes = usize(1) << 20;

usize AlignmentFor(usize capacity)
{
    return capacity >= kLargeBytes ? kPageAlignment : usize(64);
}

}  // namespace

LoadBuffer::LoadBuffer(LoadBuffer&& other) noexcept
    : pool_(other.pool_), data_(other.data_), size_(other.size_), capacity_(other.capacity_)
{
    other.pool_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

LoadBuffer& LoadBuffer::operator=(LoadBuffer&& other) noexcept
{
    if (this != &other)
    {
        Release();
        pool_ = other.pool_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.pool_ = nullptr;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

void LoadBuffer::Release()
{
    if (data_ != nullptr)
    {
        pool_->Give(data_, capacity_);
    }
    pool_ = nullptr;
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

LoadBufferPool::LoadBufferPool(u64 budget_bytes) : budget_(budget_bytes) {}

LoadBufferPool::~LoadBufferPool()
{
    LockGuard<Mutex> lock(mutex_);
    FreeIdleLocked();
}

usize LoadBufferPool::ClassOf(usize capacity)
{
    usize index = 0;
    for (usize c = kMinClassBytes; c < capacity; c <<= 1)
    {
        ++index;
    }
    return index;
}

void LoadBufferPool::FreeIdleLocked()
{
    for (usize c = 0; c < kClassCount; ++c)
    {
        const usize capacity = kMinClassBytes << c;
        for (usize i = 0; i < free_[c].Size(); ++i)
        {
            HeapAllocator::Default().Free(free_[c][i], capacity, AlignmentFor(capacity));
        }
        free_[c].Clear();
    }
    idle_ = 0;
}

Status LoadBufferPool::Acquire(usize size, LoadBuffer* out)
{
    out->Release();
    if (size == 0)
    {
        return Status::Make(ErrorCode::Invalid, "a load buffer of zero bytes was asked for");
    }
    if (size >= kMaxBlobBytes)
    {
        return Status::Make(ErrorCode::TooLarge, "the load buffer would be larger than the blob size limit");
    }
    usize capacity = kMinClassBytes;
    while (capacity < size)
    {
        capacity <<= 1;
    }
    const usize cls = ClassOf(capacity);

    LockGuard<Mutex> lock(mutex_);
    if (free_[cls].Size() > 0)
    {
        u8* data = free_[cls][free_[cls].Size() - 1];
        free_[cls].Pop();
        idle_ -= capacity;
        in_use_ += capacity;
        ++reuses_;
        out->pool_ = this;
        out->data_ = data;
        out->size_ = size;
        out->capacity_ = capacity;
        return Status::Success();
    }
    if (in_use_ + capacity > budget_)
    {
        return Status::Make(ErrorCode::LimitExceeded, "the load buffer pool budget is exhausted");
    }
    if (in_use_ + idle_ + capacity > budget_)
    {
        FreeIdleLocked();  // idle buffers of other sizes make room
    }
    u8* data = static_cast<u8*>(HeapAllocator::Default().Alloc(capacity, AlignmentFor(capacity)));
    if (data == nullptr)
    {
        return Status::Make(ErrorCode::Other, "memory for a load buffer ran out");
    }
    MemSet(data, 0, capacity);  // touch every page once, here, so the read into it does not fault
    in_use_ += capacity;
    ++allocations_;
    out->pool_ = this;
    out->data_ = data;
    out->size_ = size;
    out->capacity_ = capacity;
    return Status::Success();
}

void LoadBufferPool::Give(u8* data, usize capacity)
{
    LockGuard<Mutex> lock(mutex_);
    in_use_ -= capacity;
    free_[ClassOf(capacity)].Push(data);
    idle_ += capacity;
}

u64 LoadBufferPool::AllocationCount() const
{
    LockGuard<Mutex> lock(mutex_);
    return allocations_;
}

u64 LoadBufferPool::ReuseCount() const
{
    LockGuard<Mutex> lock(mutex_);
    return reuses_;
}

u64 LoadBufferPool::BytesInUse() const
{
    LockGuard<Mutex> lock(mutex_);
    return in_use_;
}

u64 LoadBufferPool::BytesIdle() const
{
    LockGuard<Mutex> lock(mutex_);
    return idle_;
}

}  // namespace vse::asset
