#include "net/rate_limit.h"

namespace engine_placeholder::net
{
namespace
{

constexpr usize kBucketEntries = 8;

f64 Burst(f64 burst)
{
    return burst < 1.0 ? 1.0 : burst;
}

}  // namespace

RateLimiter::RateLimiter(const RateLimiterConfig& config) : config_(config)
{
    usize n = config_.max_addresses < kBucketEntries ? kBucketEntries : config_.max_addresses;
    n = (n + kBucketEntries - 1) / kBucketEntries * kBucketEntries;
    entries_.Resize(n);
}

void RateLimiter::Refill(Entry* e, u64 now) const
{
    if (now > e->refilled)
    {
        e->tokens += static_cast<f64>(now - e->refilled) / 1e9 * config_.address_per_second;
        e->refilled = now;
        const f64 cap = Burst(config_.address_burst);
        if (e->tokens > cap)
        {
            e->tokens = cap;
        }
    }
}

bool RateLimiter::Idle(const Entry& e, u64 now) const
{
    if (e.banned_until > now)
    {
        return false;
    }
    if (e.strikes != 0 && now - e.first_strike < config_.strike_window_nanos)
    {
        return false;
    }
    if (config_.address_per_second <= 0)
    {
        return true;
    }
    const f64 cap = Burst(config_.address_burst);
    const f64 tokens = e.tokens +
                       (now > e.refilled ? static_cast<f64>(now - e.refilled) / 1e9 * config_.address_per_second : 0.0);
    return tokens >= cap;  // full: the same as a stranger
}

RateLimiter::Entry* RateLimiter::Lookup(const Address& address)
{
    const usize base = address.HashValue() % (entries_.Size() / kBucketEntries) * kBucketEntries;
    for (usize i = 0; i < kBucketEntries; ++i)
    {
        Entry& e = entries_[base + i];
        if (e.used && e.address == address)
        {
            return &e;
        }
    }
    return nullptr;
}

const RateLimiter::Entry* RateLimiter::Lookup(const Address& address) const
{
    const usize base = address.HashValue() % (entries_.Size() / kBucketEntries) * kBucketEntries;
    for (usize i = 0; i < kBucketEntries; ++i)
    {
        const Entry& e = entries_[base + i];
        if (e.used && e.address == address)
        {
            return &e;
        }
    }
    return nullptr;
}

RateLimiter::Entry* RateLimiter::Claim(const Address& address, u64 now)
{
    const usize base = address.HashValue() % (entries_.Size() / kBucketEntries) * kBucketEntries;
    Entry* victim = nullptr;
    for (usize i = 0; i < kBucketEntries; ++i)
    {
        Entry& e = entries_[base + i];
        if (!e.used || Idle(e, now))
        {
            victim = &e;
            break;
        }
        // Otherwise the entry seen longest ago that is not banned (a banned one only if nothing else is left).
        const bool banned = e.banned_until > now;
        const bool victim_banned = victim != nullptr && victim->banned_until > now;
        if (victim == nullptr || (victim_banned && !banned) ||
            (banned == victim_banned && e.last_seen < victim->last_seen))
        {
            victim = &e;
        }
    }
    Entry fresh;
    fresh.address = address;
    fresh.used = true;
    fresh.tokens = Burst(config_.address_burst);
    fresh.refilled = now;
    fresh.last_seen = now;
    *victim = fresh;
    return victim;
}

bool RateLimiter::Allow(const Address& address, u64 now)
{
    Entry* e = Lookup(address);
    if (e != nullptr && e->banned_until > now)
    {
        ++counters_.refused_banned;
        return false;
    }
    if (config_.total_per_second > 0)
    {
        const f64 cap = Burst(config_.total_burst);
        if (!total_started_)
        {
            total_started_ = true;
            total_tokens_ = cap;
            total_refilled_ = now;
        }
        if (now > total_refilled_)
        {
            total_tokens_ += static_cast<f64>(now - total_refilled_) / 1e9 * config_.total_per_second;
            total_refilled_ = now;
            if (total_tokens_ > cap)
            {
                total_tokens_ = cap;
            }
        }
        if (total_tokens_ < 1.0)
        {
            ++counters_.refused_total;
            return false;
        }
    }
    if (config_.address_per_second > 0)
    {
        if (e == nullptr)
        {
            e = Claim(address, now);
        }
        Refill(e, now);
        e->last_seen = now;
        if (e->tokens < 1.0)
        {
            ++counters_.refused_address;
            return false;
        }
        e->tokens -= 1.0;
    }
    if (config_.total_per_second > 0)
    {
        total_tokens_ -= 1.0;
    }
    ++counters_.allowed;
    return true;
}

void RateLimiter::Strike(const Address& address, u64 now)
{
    if (config_.strikes_to_ban == 0)
    {
        return;
    }
    Entry* e = Lookup(address);
    if (e == nullptr)
    {
        e = Claim(address, now);
    }
    e->last_seen = now;
    if (e->banned_until > now)
    {
        return;
    }
    if (e->strikes == 0 || now - e->first_strike >= config_.strike_window_nanos)
    {
        e->strikes = 0;
        e->first_strike = now;
    }
    ++e->strikes;
    if (e->strikes >= config_.strikes_to_ban)
    {
        e->banned_until = now + config_.ban_nanos;
        e->strikes = 0;
        ++counters_.bans;
    }
}

bool RateLimiter::IsBanned(const Address& address, u64 now) const
{
    const Entry* e = Lookup(address);
    return e != nullptr && e->banned_until > now;
}

usize RateLimiter::ActiveAddresses(u64 now) const
{
    usize n = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        n += entries_[i].used && !Idle(entries_[i], now) ? 1 : 0;
    }
    return n;
}

usize RateLimiter::BannedAddresses(u64 now) const
{
    usize n = 0;
    for (usize i = 0; i < entries_.Size(); ++i)
    {
        n += entries_[i].used && entries_[i].banned_until > now ? 1 : 0;
    }
    return n;
}

}  // namespace engine_placeholder::net
