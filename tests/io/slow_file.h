#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "algorithm.h"
#include "io/file.h"
#include "test_text.h"

namespace io_test {

using namespace engine_placeholder;

/// A file that behaves like one on a network: bytes live in memory, every request completes on the file's own thread after a delay,
/// reads come back in chunks of at most `chunk` bytes, the size is unknown until the first read has completed, the Nth request can
/// fail, and the synchronous operations only block on the asynchronous ones. Passing the conformance suite unchanged is the proof that
/// the `io::File` interface assumes nothing local.
class SlowFile final : public io::File {
public:
    struct Options {
        int delay_us = 100;
        usize chunk = 1000;
        int fail_nth = 0; // 1-based request number that fails with Io; 0 never
    };

    SlowFile(StringView content, Options options) : options_(options), data_(Copy(content)), thread_([this] { Loop(); }) {}

    ~SlowFile() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        thread_.join(); // requests still queued are completed with Interrupted by Loop
        WaitForAsyncRequests();
    }

    bool IsOpen() const override { return !closed_.load(); }
    u32 Capabilities() const override { return io::kCapSeekable | io::kCapWritable | io::kCapNativelyAsync | (size_known_.load() ? io::kCapSizeKnown : 0u); }

    io::Status Size(u64* out) override {
        if (closed_.load()) return io::Status::Of(io::Error::Invalid);
        if (!size_known_.load()) return io::Status::Of(io::Error::Unsupported);
        std::lock_guard<std::mutex> lock(mutex_);
        *out = data_.Size();
        return io::Status::Success();
    }

    io::Status Read(void* buffer, usize length, u64 offset, usize* bytes_read) override {
        io::AsyncRequest r;
        Waiter w;
        ReadAsync(&r, buffer, length, offset, &Waiter::Done, &w);
        const io::Status s = w.Wait();
        *bytes_read = s.Ok() ? w.bytes : 0;
        return s;
    }
    io::Status Write(const void* data, usize length, u64 offset) override {
        io::AsyncRequest r;
        Waiter w;
        WriteAsync(&r, data, length, offset, &Waiter::Done, &w);
        return w.Wait();
    }
    io::Status Append(const void*, usize) override { return io::Status::Of(io::Error::Unsupported); }
    io::Status Flush() override {
        io::AsyncRequest r;
        Waiter w;
        FlushAsync(&r, &Waiter::Done, &w);
        return w.Wait();
    }
    io::Status Sync() override { return Flush(); }
    io::Status Truncate(u64 size) override {
        if (closed_.load()) return io::Status::Of(io::Error::Invalid);
        std::lock_guard<std::mutex> lock(mutex_);
        data_.Resize(static_cast<usize>(size));
        return io::Status::Success();
    }
    io::Status Close() override {
        closed_.store(true);
        return io::Status::Success();
    }

    int CompletedRequests() const { return completed_.load(); }
    engine_test::Text Contents() {
        std::lock_guard<std::mutex> lock(mutex_);
        return engine_test::MakeText(StringView(data_.Data(), data_.Size()));
    }

protected:
    void StartAsync(io::AsyncRequest* r) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.Push(r);
        }
        cv_.notify_one();
    }

private:
    struct Waiter {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        io::Status status;
        usize bytes = 0;
        static void Done(void* user, const io::AsyncResult& result) {
            Waiter* w = static_cast<Waiter*>(user);
            std::lock_guard<std::mutex> lock(w->m);
            w->status = result.status;
            w->bytes = result.bytes;
            w->done = true;
            w->cv.notify_all();
        }
        io::Status Wait() {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] { return done; });
            return status;
        }
    };

    void Loop() {
        for (;;) {
            io::AsyncRequest* r = nullptr;
            bool stop = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return stopping_ || head_ < queue_.Size(); });
                if (head_ >= queue_.Size()) return;
                r = queue_[head_++];
                if (head_ == queue_.Size()) {
                    queue_.Clear();
                    head_ = 0;
                }
                stop = stopping_;
            }
            io::AsyncResult result;
            if (stop || !ClaimAsync(r)) {
                result.status = io::Status::Of(io::Error::Interrupted);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(options_.delay_us));
                result = Execute(r);
            }
            completed_.fetch_add(1);
            CompleteAsync(r, result); // may destroy the file: touch nothing of it afterwards (the loop ends with the thread)
        }
    }


    io::AsyncResult Execute(io::AsyncRequest* r) {
        io::AsyncResult result;
        const int n = ++requests_;
        if (closed_.load()) {
            result.status = io::Status::Of(io::Error::Invalid);
            return result;
        }
        if (options_.fail_nth != 0 && n == options_.fail_nth) {
            result.status = io::Status::Of(io::Error::Io);
            return result;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        switch (r->kind) {
        case io::AsyncKind::Read: {
            if (r->offset >= data_.Size()) {
                size_known_.store(true);
                break;
            }
            const usize n_bytes = Min(Min(r->length, options_.chunk), data_.Size() - static_cast<usize>(r->offset));
            std::memcpy(r->buffer, data_.Data() + r->offset, n_bytes);
            result.bytes = n_bytes;
            size_known_.store(true);
            break;
        }
        case io::AsyncKind::Write: {
            const usize end = static_cast<usize>(r->offset) + r->length;
            if (data_.Size() < end) data_.Resize(end);
            if (r->length != 0) std::memcpy(data_.Data() + r->offset, r->data, r->length);
            result.bytes = r->length;
            break;
        }
        case io::AsyncKind::Flush: break;
        }
        return result;
    }

    static Vector<char> Copy(StringView content) {
        Vector<char> out;
        out.Resize(content.Length());
        if (content.Length() != 0) std::memcpy(out.Data(), content.Data(), content.Length());
        return out;
    }

    Options options_;
    Vector<char> data_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Vector<io::AsyncRequest*> queue_; // a FIFO: `head_` is the next one to run
    usize head_ = 0;
    bool stopping_ = false;
    std::atomic<bool> closed_{false};
    std::atomic<bool> size_known_{false};
    std::atomic<int> completed_{0};
    int requests_ = 0;
    std::thread thread_;
};

} // namespace io_test
