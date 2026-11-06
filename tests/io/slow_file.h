#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "io/file.h"

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

    SlowFile(const std::string& content, Options options) : options_(options), data_(content), thread_([this] { Loop(); }) {}

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
        *out = data_.size();
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
        data_.resize(static_cast<size_t>(size), '\0');
        return io::Status::Success();
    }
    io::Status Close() override {
        closed_.store(true);
        return io::Status::Success();
    }

    int CompletedRequests() const { return completed_.load(); }
    std::string Contents() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_;
    }

protected:
    void StartAsync(io::AsyncRequest* r) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(r);
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
                cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;
                r = queue_.front();
                queue_.pop_front();
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
            if (r->offset >= data_.size()) {
                size_known_.store(true);
                break;
            }
            const usize n_bytes = std::min({r->length, options_.chunk, data_.size() - static_cast<usize>(r->offset)});
            std::memcpy(r->buffer, data_.data() + r->offset, n_bytes);
            result.bytes = n_bytes;
            size_known_.store(true);
            break;
        }
        case io::AsyncKind::Write: {
            const usize end = static_cast<usize>(r->offset) + r->length;
            if (data_.size() < end) data_.resize(end, '\0');
            if (r->length != 0) std::memcpy(data_.data() + r->offset, r->data, r->length);
            result.bytes = r->length;
            break;
        }
        case io::AsyncKind::Flush: break;
        }
        return result;
    }

    Options options_;
    std::string data_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<io::AsyncRequest*> queue_;
    bool stopping_ = false;
    std::atomic<bool> closed_{false};
    std::atomic<bool> size_known_{false};
    std::atomic<int> completed_{0};
    int requests_ = 0;
    std::thread thread_;
};

} // namespace io_test
