#include "net/resolver.h"

#include "cstr/cstr.h"

namespace vse::net
{

Resolver::Resolver(ResolveFn function, usize queue_capacity) : function_(function)
{
    ring_.Resize(queue_capacity == 0 ? 1 : queue_capacity);
    thread_ = Thread([this] { Loop(); });
}

Resolver::~Resolver()
{
    Shutdown();
}

Status Resolver::Submit(const SharedPtr<ResolveRequest>& request)
{
    LockGuard<Mutex> guard(lock_);
    if (stopping_)
    {
        return Status::Of(ErrorCode::Cancelled);
    }
    if (count_ >= ring_.Size())
    {
        return Status::Of(ErrorCode::LimitExceeded);
    }
    ring_[(head_ + count_) % ring_.Size()] = request;
    ++count_;
    wake_.NotifyOne();
    return Status::Success();
}

void Resolver::Loop()
{
    for (;;)
    {
        SharedPtr<ResolveRequest> request;
        {
            UniqueLock<Mutex> guard(lock_);
            wake_.Wait(guard, [this] { return stopping_ || count_ > 0; });
            if (count_ == 0)
            {
                return;  // stopping, and the queue is empty (Shutdown ended the queued requests)
            }
            request = static_cast<SharedPtr<ResolveRequest>&&>(ring_[head_]);
            head_ = (head_ + 1) % ring_.Size();
            --count_;
        }
        request->endpoints = EndpointSet();
        if (request->abandoned.Load(MemoryOrder::Acquire))
        {
            request->status = Status::Of(
                ErrorCode::Cancelled);  // nobody waits for it: a slow name server must not delay the others
        }
        else
        {
            request->status = function_(StringView(request->host, CStrLength(request->host)), request->port,
                                        request->family, &request->endpoints);
            if (request->status.Ok() && request->endpoints.Empty())
            {
                request->status = Status::Of(
                    ErrorCode::NotFound);  // a resolver that succeeds with nothing found nothing
            }
        }
        request->done
            .Fire();  // the coroutine may have gone: the request lives on in this reference until it is dropped below
    }
}

void Resolver::Shutdown()
{
    LockGuard<Mutex> shutdown_guard(shutdown_lock_);
    if (joined_)
    {
        return;
    }
    Vector<SharedPtr<ResolveRequest>> cancelled;
    {
        LockGuard<Mutex> guard(lock_);
        stopping_ = true;
        // The queued requests are not run: their coroutines are told so (after the lock is released: firing a signal
        // takes the reactor's lock).
        while (count_ > 0)
        {
            cancelled.Push(static_cast<SharedPtr<ResolveRequest>&&>(ring_[head_]));
            head_ = (head_ + 1) % ring_.Size();
            --count_;
        }
        wake_.NotifyAll();
    }
    for (usize i = 0; i < cancelled.Size(); ++i)
    {
        cancelled[i]->status = Status::Of(ErrorCode::Cancelled);
        cancelled[i]->done.Fire();
    }
    thread_.Join();
    joined_ = true;
}

}  // namespace vse::net
