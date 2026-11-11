#include "net/endpoint.h"

namespace engine_placeholder::net {

Status Endpoint::Parse(StringView text, Endpoint* out) {
    if (text.Empty() || text.Length() > 80) return Status::Of(ErrorCode::Invalid);
    StringView host;
    StringView port_text;
    if (text[0] == '[') {
        const usize close = text.IndexOf(']');
        if (close == text.Length() || close + 1 >= text.Length() || text[close + 1] != ':') return Status::Of(ErrorCode::Invalid);
        host = text.Substring(0, close + 1);  // Address::Parse takes the brackets, and refuses an IPv4 inside them
        port_text = text.Substring(close + 2);
    } else {
        const usize colon = text.LastIndexOf(':');
        if (colon == text.Length()) return Status::Of(ErrorCode::Invalid);
        host = text.Substring(0, colon);
        port_text = text.Substring(colon + 1);
        if (host.IndexOf(':') != host.Length()) return Status::Of(ErrorCode::Invalid);  // a bare IPv6 address
    }
    if (port_text.Empty() || port_text.Length() > 5) return Status::Of(ErrorCode::Invalid);
    if (port_text.Length() > 1 && port_text[0] == '0') return Status::Of(ErrorCode::Invalid);
    u32 port = 0;
    for (usize i = 0; i < port_text.Length(); ++i) {
        if (port_text[i] < '0' || port_text[i] > '9') return Status::Of(ErrorCode::Invalid);
        port = port * 10 + static_cast<u32>(port_text[i] - '0');
    }
    if (port > 65535) return Status::Of(ErrorCode::Invalid);
    Address address;
    if (Address::Parse(host, &address).Failed()) return Status::Of(ErrorCode::Invalid);
    *out = Endpoint(address, static_cast<u16>(port));
    return Status::Success();
}

EndpointText Endpoint::Format() const {
    EndpointText t;
    auto put = [&t](char c) {
        if (t.length < EndpointText::kCapacity) t.chars[t.length++] = c;
    };
    const AddressText a = address_.Format();
    const bool bracket = address_.IsV6();
    if (bracket) put('[');
    for (usize i = 0; i < a.length; ++i) put(a.chars[i]);
    if (bracket) put(']');
    put(':');
    char digits[5];
    int n = 0;
    u32 p = port_;
    do {
        digits[n++] = static_cast<char>('0' + p % 10);
        p /= 10;
    } while (p != 0);
    while (n > 0) put(digits[--n]);
    t.chars[t.length] = 0;
    return t;
}

usize Endpoint::HashValue() const {
    return detail::MixHash64(static_cast<u64>(address_.HashValue()) * 0x9e3779b97f4a7c15ULL + port_);
}

Ordering Endpoint::operator<=>(const Endpoint& other) const {
    const Ordering a = address_ <=> other.address_;
    if (a != Ordering::equal) return a;
    return port_ <=> other.port_;
}

}  // namespace engine_placeholder::net
