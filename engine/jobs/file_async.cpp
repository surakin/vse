#include "jobs/file_async.h"


#include "jobs/scheduler.h"
#include "jobs/worker_pool.h"
#include "log/assert.h"
#include "cstr/mem_ops.h"

namespace engine_placeholder::jobs {

// ---- one operation ---------------------------------------------------------------------------------------------------

void FileIoAwaitable::OnDone(void* user, const io::AsyncResult& result) {
    FileIoAwaitable* self = static_cast<FileIoAwaitable*>(user);
    self->result_ = result;
    // Last touch: the awaiter may be freed once the coroutine resumes. Decrementing from a non-participant thread routes the
    // continuation through the injection queue.
    self->counter_.Decrement();
}

bool FileIoAwaitable::await_suspend(CoroutineHandle<> h) noexcept {
    counter_.Increment();
    switch (kind_) {
    case io::AsyncKind::Read: file_->ReadAsync(&request_, buffer_, length_, offset_, &OnDone, this); break;
    case io::AsyncKind::Write: file_->WriteAsync(&request_, data_, length_, offset_, &OnDone, this); break;
    case io::AsyncKind::Flush: file_->FlushAsync(&request_, &OnDone, this); break;
    }
    return counter_.operator co_await().await_suspend(h);
}

io::AsyncResult FileIoAwaitable::await_resume() noexcept {
    counter_.operator co_await().await_resume();
    ENGINE_ASSERT(WorkerPool::IsParticipantThread(), "async file I/O continuation resumed on a non-participant");
    return result_;
}

FileIoAwaitable ReadAsync(Scheduler& scheduler, io::File& file, void* buffer, usize length, u64 offset) {
    return FileIoAwaitable(scheduler, file, io::AsyncKind::Read, buffer, nullptr, length, offset);
}
FileIoAwaitable WriteAsync(Scheduler& scheduler, io::File& file, const void* data, usize length, u64 offset) {
    return FileIoAwaitable(scheduler, file, io::AsyncKind::Write, nullptr, data, length, offset);
}
FileIoAwaitable FlushAsync(Scheduler& scheduler, io::File& file) {
    return FileIoAwaitable(scheduler, file, io::AsyncKind::Flush, nullptr, nullptr, 0, 0);
}

// ---- whole file -------------------------------------------------------------------------------------------------------

void FileReadAllAwaitable::Issue() { file_->ReadAsync(&request_, chunk_.Data(), kChunk, offset_, &OnChunk, this); }

void FileReadAllAwaitable::OnChunk(void* user, const io::AsyncResult& result) {
    FileReadAllAwaitable* self = static_cast<FileReadAllAwaitable*>(user);
    if (result.status.Failed()) {
        self->status_ = result.status;
    } else if (result.bytes != 0) {
        self->offset_ += result.bytes;
        const usize old = self->bytes_.Size();
        if (self->offset_ > self->max_bytes_) {
            self->status_ = io::Status::Of(io::Error::TooLong);
        } else if (!self->bytes_.TryResizeUninitialized(old + result.bytes)) {
            self->status_ = io::Status::Of(io::Error::OutOfMemory);
        } else {
            MemCopy(self->bytes_.Data() + old, self->chunk_.Data(), result.bytes);
            self->Issue(); // the previous request has completed, so its storage is free to reuse
            return;
        }
    }
    self->counter_.Decrement(); // finished: the last touch
}

bool FileReadAllAwaitable::await_suspend(CoroutineHandle<> h) noexcept {
    u64 hint = 0;
    if (file_->Size(&hint).Ok() && hint > 0 && hint <= max_bytes_) {
        bytes_.TryReserve(static_cast<usize>(hint)); // a hint only: failure just means the vector grows as it reads
    }
    counter_.Increment();
    if (!chunk_.TryResizeUninitialized(kChunk)) {
        status_ = io::Status::Of(io::Error::OutOfMemory);
        counter_.Decrement();
    } else {
        Issue();
    }
    return counter_.operator co_await().await_suspend(h);
}

FileReadAllResult FileReadAllAwaitable::await_resume() noexcept {
    counter_.operator co_await().await_resume();
    ENGINE_ASSERT(WorkerPool::IsParticipantThread(), "async file I/O continuation resumed on a non-participant");
    FileReadAllResult r;
    r.status = status_;
    r.bytes = static_cast<Vector<u8>&&>(bytes_);
    return r;
}

FileReadAllAwaitable ReadAllAsync(Scheduler& scheduler, io::File& file, u64 max_bytes) {
    return FileReadAllAwaitable(scheduler, file, max_bytes);
}

} // namespace engine_placeholder::jobs
