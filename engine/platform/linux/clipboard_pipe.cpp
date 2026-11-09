#include "platform/linux/clipboard_pipe.h"

#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

namespace engine_placeholder::os {

namespace {

void MakeNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// A write to a pipe whose reader has gone raises SIGPIPE, which kills the process by default. A library cannot change the process's signal disposition, so the signal is
// blocked on this thread for the duration of the writes and, if one was raised by them, taken off the pending set before the mask is restored.
class SigpipeGuard {
public:
    SigpipeGuard() {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &set, &old_);
        sigset_t pending;
        sigpending(&pending);
        was_pending_ = sigismember(&pending, SIGPIPE) == 1;
    }
    ~SigpipeGuard() {
        if (!was_pending_) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, SIGPIPE);
            sigset_t pending;
            sigpending(&pending);
            if (sigismember(&pending, SIGPIPE) == 1) {
                const timespec zero = {0, 0};
                sigtimedwait(&set, nullptr, &zero);
            }
        }
        pthread_sigmask(SIG_SETMASK, &old_, nullptr);
    }
    SigpipeGuard(const SigpipeGuard&) = delete;
    SigpipeGuard& operator=(const SigpipeGuard&) = delete;

private:
    sigset_t old_;
    bool was_pending_ = false;
};

} // namespace

PipeWriter::PipeWriter(PipeWriter&& other) noexcept
    : fd_(other.fd_), data_(static_cast<Vector<byte>&&>(other.data_)), offset_(other.offset_), last_progress_ns_(other.last_progress_ns_), timeout_ns_(other.timeout_ns_), state_(other.state_) {
    other.fd_ = -1;
    other.state_ = State::Idle;
}

PipeWriter& PipeWriter::operator=(PipeWriter&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        data_ = static_cast<Vector<byte>&&>(other.data_);
        offset_ = other.offset_;
        last_progress_ns_ = other.last_progress_ns_;
        timeout_ns_ = other.timeout_ns_;
        state_ = other.state_;
        other.fd_ = -1;
        other.state_ = State::Idle;
    }
    return *this;
}

void PipeWriter::Start(int fd, const byte* bytes, usize size, u64 now_ns, u64 timeout_ns) {
    Close();
    fd_ = fd;
    data_.Clear();
    data_.Reserve(size);
    for (usize i = 0; i < size; ++i) data_.Push(bytes[i]);
    offset_ = 0;
    last_progress_ns_ = now_ns;
    timeout_ns_ = timeout_ns;
    state_ = State::Writing;
    if (fd_ >= 0) MakeNonBlocking(fd_);
    else state_ = State::Failed;
}

PipeWriter::State PipeWriter::Pump(u64 now_ns) {
    if (state_ != State::Writing) return state_;
    SigpipeGuard guard;
    while (offset_ < data_.Size()) {
        const ssize_t n = ::write(fd_, data_.Data() + offset_, data_.Size() - offset_);
        if (n > 0) {
            offset_ += static_cast<usize>(n);
            last_progress_ns_ = now_ns;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (now_ns - last_progress_ns_ >= timeout_ns_) {
                state_ = State::TimedOut;
                Close();
            }
            return state_;
        } else {
            state_ = State::Failed; // EPIPE: the peer closed early
            Close();
            return state_;
        }
    }
    state_ = State::Done;
    Close();
    return state_;
}

void PipeWriter::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    data_.Reset();
}

PipeReader::PipeReader(PipeReader&& other) noexcept
    : fd_(other.fd_), data_(static_cast<Vector<byte>&&>(other.data_)), cap_(other.cap_), last_progress_ns_(other.last_progress_ns_), timeout_ns_(other.timeout_ns_), state_(other.state_) {
    other.fd_ = -1;
    other.state_ = State::Idle;
}

PipeReader& PipeReader::operator=(PipeReader&& other) noexcept {
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        data_ = static_cast<Vector<byte>&&>(other.data_);
        cap_ = other.cap_;
        last_progress_ns_ = other.last_progress_ns_;
        timeout_ns_ = other.timeout_ns_;
        state_ = other.state_;
        other.fd_ = -1;
        other.state_ = State::Idle;
    }
    return *this;
}

void PipeReader::Start(int fd, usize cap, u64 now_ns, u64 timeout_ns) {
    Close();
    data_.Clear();
    fd_ = fd;
    cap_ = cap;
    last_progress_ns_ = now_ns;
    timeout_ns_ = timeout_ns;
    state_ = State::Reading;
    if (fd_ >= 0) MakeNonBlocking(fd_);
    else state_ = State::Failed;
}

PipeReader::State PipeReader::Pump(u64 now_ns) {
    if (state_ != State::Reading) return state_;
    byte buffer[16384];
    for (;;) {
        const ssize_t n = ::read(fd_, buffer, sizeof(buffer));
        if (n > 0) {
            if (data_.Size() + static_cast<usize>(n) > cap_) {
                state_ = State::TooLarge;
                data_.Reset();
                Close();
                return state_;
            }
            for (ssize_t i = 0; i < n; ++i) data_.Push(buffer[i]);
            last_progress_ns_ = now_ns;
        } else if (n == 0) {
            state_ = State::Done;
            Close();
            return state_;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (now_ns - last_progress_ns_ >= timeout_ns_) {
                state_ = State::TimedOut;
                Close();
            }
            return state_;
        } else {
            state_ = State::Failed;
            Close();
            return state_;
        }
    }
}

void PipeReader::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace engine_placeholder::os
