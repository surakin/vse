// Addresses, endpoints and the error type (Phase 20 Day 267): tables of valid and invalid text, generated round trips, every classification, ordering and
// hash, scopes, and a fuzz of random bytes through both parsers.

#include <catch2/catch_test_macros.hpp>

#include "containers/hash_map.h"
#include "mem/heap_allocator.h"
#include "net/endpoint.h"

namespace engine_placeholder::net {
namespace {

struct ValidCase {
    const char* text;
    const char* canonical;
};

const ValidCase kValid[] = {
    {"0.0.0.0", "0.0.0.0"}, {"127.0.0.1", "127.0.0.1"}, {"255.255.255.255", "255.255.255.255"}, {"1.2.3.4", "1.2.3.4"},
    {"10.0.0.1", "10.0.0.1"}, {"192.168.100.200", "192.168.100.200"}, {"8.8.8.8", "8.8.8.8"}, {"169.254.1.1", "169.254.1.1"},
    {"0.1.2.3", "0.1.2.3"}, {"100.64.0.1", "100.64.0.1"}, {"224.0.0.251", "224.0.0.251"}, {"172.16.0.1", "172.16.0.1"},
    {"::", "::"}, {"::1", "::1"}, {"1::", "1::"}, {"1::1", "1::1"}, {"2001:db8::1", "2001:db8::1"}, {"2001:DB8::1", "2001:db8::1"},
    {"2001:0db8:0000:0000:0000:0000:0000:0001", "2001:db8::1"}, {"2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1"},
    {"2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"}, {"2001:db8::1:0:0:1", "2001:db8::1:0:0:1"},
    {"1:0:0:2:0:0:0:3", "1:0:0:2::3"}, {"1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8"}, {"fe80::1", "fe80::1"}, {"FE80::ABCD", "fe80::abcd"},
    {"ff02::1", "ff02::1"}, {"fc00::1", "fc00::1"}, {"fd12:3456:789a::1", "fd12:3456:789a::1"}, {"::ffff:1.2.3.4", "::ffff:1.2.3.4"},
    {"::ffff:0102:0304", "::ffff:1.2.3.4"}, {"::FFFF:127.0.0.1", "::ffff:127.0.0.1"}, {"::1.2.3.4", "::102:304"}, {"1:2:3:4:5:6:1.2.3.4", "1:2:3:4:5:6:102:304"},
    {"0:0:0:0:0:0:0:0", "::"}, {"0:0:0:0:0:0:0:1", "::1"}, {"1:0:0:0:0:0:0:0", "1::"}, {"0:0:1:0:0:0:0:1", "0:0:1::1"},
    {"1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0"}, {"::2:3:4:5:6:7:8", "0:2:3:4:5:6:7:8"}, {"1::8", "1::8"}, {"[::1]", "::1"}, {"[2001:db8::1]", "2001:db8::1"},
    {"fe80::1%eth0", "fe80::1%eth0"}, {"fe80::1%3", "fe80::1%3"}, {"fe80::1%0", "fe80::1"}, {"fe80::1%wlan0.5", "fe80::1%wlan0.5"},
    {"[fe80::1%en0]", "fe80::1%en0"}, {"fe80::1%Ethernet-2", "fe80::1%Ethernet-2"}, {"2001:db8::ffff", "2001:db8::ffff"}, {"abcd:ef01:2345:6789:abcd:ef01:2345:6789", "abcd:ef01:2345:6789:abcd:ef01:2345:6789"},
    {"::ffff:10.1.2.3", "::ffff:10.1.2.3"}, {"64:ff9b::1.2.3.4", "64:ff9b::102:304"}, {"2001::", "2001::"}, {"::8", "::8"},
};

const char* const kInvalid[] = {
    "", " ", "1", "1.2.3", "1.2.3.4.5", "1.2.3.", ".1.2.3", "1..2.3", "256.1.1.1", "1.2.3.256", "1.2.3.-4", "+1.2.3.4", "01.2.3.4", "1.02.3.4", "1.2.3.04", "001.2.3.4",
    "0x7f.0.0.1", "1.2.3.4 ", " 1.2.3.4", "1.2.3.4\n", "1.2.3.4/24", "1.2.3.4:80", "a.b.c.d", "1,2,3,4", "1.2.3.1000", "1.2.3.4a", "localhost", "example.com",
    ":", ":::", "::::", "1:::2", ":1", "1:", "1::2::3", "1:2:3:4:5:6:7", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7:8::", "::1:2:3:4:5:6:7:8", "12345::1", "g::1", "1::g",
    "1.2.3.4::", "::1.2.3", "::1.2.3.4.5", "::1.2.3.256", "::01.2.3.4", "1.2.3.4::1", "1::1.2.3.4:5", "::1.2.3.4:", "1:2:3:4:5:6:7:1.2.3.4", "fe80::1%", "fe80::1%%1", "fe80::1%eth0%1",
    "fe80::1%this-name-is-way-too-long", "fe80::1%a b", "fe80::1%a/b", "1.2.3.4%eth0", "[1.2.3.4]", "[]", "[::1", "::1]", "[[::1]]", "[::1]:80", "[::1] ", "::1 ",
    "2001:db8::1/64", "2001:db8:::1", "2001:db8:0:0:0:0:0:0:1", "::ffff:1.2.3.4.5", "0:0:0:0:0:0:0:0:0", "-1::1", "1:-2::", "0x1::",
};

TEST_CASE("Address: every valid text parses and formats canonically", "[net][address]") {
    for (const ValidCase& c : kValid) {
        CAPTURE(c.text);
        Address a;
        REQUIRE(Address::Parse(c.text, &a).Ok());
        CHECK(a.Format().View() == StringView(c.canonical));
        // The canonical text parses back to the same address.
        Address again;
        REQUIRE(Address::Parse(a.Format().View(), &again).Ok());
        CHECK(again == a);
        CHECK(again.Format().View() == a.Format().View());
    }
}

TEST_CASE("Address: every invalid text is refused and leaves the output alone", "[net][address]") {
    const Address sentinel = Address::V4(9, 9, 9, 9);
    for (const char* text : kInvalid) {
        CAPTURE(text);
        Address a = sentinel;
        const Status s = Address::Parse(text, &a);
        CHECK(s.Is(ErrorCode::Invalid));
        CHECK(a == sentinel);
    }
}

TEST_CASE("Address: a text with an embedded zero byte or too long is refused", "[net][address]") {
    Address a;
    CHECK(Address::Parse(StringView("1.2.3.4\0", 8), &a).Failed());
    CHECK(Address::Parse(StringView("1.2.\0.4", 7), &a).Failed());
    char long_text[200];
    for (char& c : long_text) c = '1';
    CHECK(Address::Parse(StringView(long_text, sizeof long_text), &a).Failed());
}

TEST_CASE("Address: all IPv4 octet patterns round-trip", "[net][address]") {
    for (u32 v = 0; v < 256; ++v) {
        for (u32 pattern = 0; pattern < 6; ++pattern) {
            const u8 o[4] = {static_cast<u8>(v), static_cast<u8>(255 - v), static_cast<u8>(v * 7), static_cast<u8>(pattern * 51)};
            const Address a = Address::V4(o[0], o[1], o[2], o[3]);
            Address b;
            REQUIRE(Address::Parse(a.Format().View(), &b).Ok());
            CHECK(a == b);
            CHECK(b.IsV4());
        }
    }
}

u64 NextRandom(u64& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state >> 33;
}

TEST_CASE("Address: random IPv6 addresses (with runs of zeros) round-trip and stay canonical", "[net][address]") {
    u64 rng = 12345;
    for (int i = 0; i < 4000; ++i) {
        Array<u8, 16> bytes{};
        for (usize g = 0; g < 8; ++g) {
            const u64 r = NextRandom(rng);
            const bool zero = (r % 3) != 0;  // many zero groups
            const u16 value = zero ? 0 : static_cast<u16>(NextRandom(rng) & (r % 5 == 0 ? 0xf : 0xffff));
            bytes[g * 2] = static_cast<u8>(value >> 8);
            bytes[g * 2 + 1] = static_cast<u8>(value & 0xff);
        }
        const Address a = Address::V6(bytes);
        const AddressText text = a.Format();
        Address b;
        REQUIRE(Address::Parse(text.View(), &b).Ok());
        CHECK(a == b);
        // Canonical: no uppercase, no triple colon, at most one "::".
        usize doubles = 0;
        bool clean = true;
        for (usize k = 0; k < text.length; ++k) {
            clean = clean && !(text.chars[k] >= 'A' && text.chars[k] <= 'F');
            if (k + 1 < text.length && text.chars[k] == ':' && text.chars[k + 1] == ':') ++doubles;
            clean = clean && !(k + 2 < text.length && text.chars[k] == ':' && text.chars[k + 1] == ':' && text.chars[k + 2] == ':');
        }
        CHECK(clean);
        CHECK(doubles <= 1);
    }
}

TEST_CASE("Address: RFC 5952 chooses the longest run and the first on a tie, and never compresses one group", "[net][address]") {
    auto format = [](const char* text) {
        Address a;
        REQUIRE(Address::Parse(text, &a).Ok());
        return a.Format();
    };
    CHECK(format("1:0:0:2:0:0:0:3").View() == StringView("1:0:0:2::3"));
    CHECK(format("1:0:0:2:0:0:3:4").View() == StringView("1::2:0:0:3:4"));
    CHECK(format("1:0:2:3:4:5:6:7").View() == StringView("1:0:2:3:4:5:6:7"));
    CHECK(format("0:0:0:1:0:0:0:0").View() == StringView("0:0:0:1::"));
}

TEST_CASE("Address: IPv4-mapped addresses convert both ways and classify as their IPv4 address", "[net][address]") {
    Address mapped;
    REQUIRE(Address::Parse("::ffff:127.0.0.1", &mapped).Ok());
    CHECK(mapped.IsV4Mapped());
    CHECK(mapped.IsLoopback());
    CHECK(mapped.Unmapped() == Address::V4(127, 0, 0, 1));
    CHECK(Address::V4(10, 1, 2, 3).Mapped().IsPrivate());
    CHECK(Address::V4(10, 1, 2, 3).Mapped().Unmapped() == Address::V4(10, 1, 2, 3));
    CHECK(Address::V4(1, 2, 3, 4).Unmapped() == Address::V4(1, 2, 3, 4));
    Address plain;
    REQUIRE(Address::Parse("2001:db8::1", &plain).Ok());
    CHECK_FALSE(plain.IsV4Mapped());
    CHECK(plain.Unmapped() == plain);
    CHECK(plain.Mapped() == plain);
    CHECK(Address::V4(1, 2, 3, 4).Mapped() != Address::V4(1, 2, 3, 4));
}

Address P(const char* text) {
    Address a;
    REQUIRE(Address::Parse(text, &a).Ok());
    return a;
}

TEST_CASE("Address: classification of every range and its edges", "[net][address]") {
    CHECK(P("0.0.0.0").IsAny());
    CHECK(P("::").IsAny());
    CHECK_FALSE(P("0.0.0.1").IsAny());
    CHECK_FALSE(P("::1").IsAny());
    CHECK_FALSE(Address().IsAny());
    CHECK(Address::Any(AddressFamily::V4).IsAny());
    CHECK(Address::Any(AddressFamily::V6).IsAny());

    CHECK(P("127.0.0.1").IsLoopback());
    CHECK(P("127.255.255.255").IsLoopback());
    CHECK_FALSE(P("128.0.0.1").IsLoopback());
    CHECK_FALSE(P("126.255.255.255").IsLoopback());
    CHECK(P("::1").IsLoopback());
    CHECK_FALSE(P("::2").IsLoopback());
    CHECK_FALSE(P("1::1").IsLoopback());
    CHECK(Address::Loopback(AddressFamily::V4) == Address::V4(127, 0, 0, 1));
    CHECK(Address::Loopback(AddressFamily::V6) == P("::1"));

    CHECK(P("169.254.0.0").IsLinkLocal());
    CHECK(P("169.254.255.255").IsLinkLocal());
    CHECK_FALSE(P("169.255.0.0").IsLinkLocal());
    CHECK_FALSE(P("169.253.0.0").IsLinkLocal());
    CHECK(P("fe80::").IsLinkLocal());
    CHECK(P("febf::1").IsLinkLocal());
    CHECK_FALSE(P("fec0::1").IsLinkLocal());
    CHECK_FALSE(P("fe7f::1").IsLinkLocal());

    CHECK(P("10.0.0.0").IsPrivate());
    CHECK(P("10.255.255.255").IsPrivate());
    CHECK_FALSE(P("11.0.0.0").IsPrivate());
    CHECK(P("172.16.0.0").IsPrivate());
    CHECK(P("172.31.255.255").IsPrivate());
    CHECK_FALSE(P("172.15.255.255").IsPrivate());
    CHECK_FALSE(P("172.32.0.0").IsPrivate());
    CHECK(P("192.168.0.0").IsPrivate());
    CHECK_FALSE(P("192.169.0.0").IsPrivate());
    CHECK_FALSE(P("192.167.255.255").IsPrivate());
    CHECK(P("fc00::1").IsPrivate());
    CHECK(P("fdff::1").IsPrivate());
    CHECK_FALSE(P("fe00::1").IsPrivate());
    CHECK_FALSE(P("fb00::1").IsPrivate());

    CHECK(P("224.0.0.0").IsMulticast());
    CHECK(P("239.255.255.255").IsMulticast());
    CHECK_FALSE(P("240.0.0.0").IsMulticast());
    CHECK_FALSE(P("223.255.255.255").IsMulticast());
    CHECK(P("ff00::").IsMulticast());
    CHECK(P("ff02::1").IsMulticast());
    CHECK_FALSE(P("fe02::1").IsMulticast());

    const Address none;
    CHECK_FALSE(none.IsValid());
    CHECK_FALSE(none.IsLoopback());
    CHECK_FALSE(none.IsLinkLocal());
    CHECK_FALSE(none.IsPrivate());
    CHECK_FALSE(none.IsMulticast());
    CHECK(none.Format().View().Empty());
}

TEST_CASE("Address: scopes are numeric ids or names, on IPv6 only", "[net][address]") {
    const Address named = P("fe80::1%eth0");
    CHECK(named.ScopeName() == StringView("eth0"));
    CHECK(named.ScopeId() == 0);
    const Address numbered = P("fe80::1%12");
    CHECK(numbered.ScopeId() == 12);
    CHECK(numbered.ScopeName().Empty());
    CHECK(named != numbered);
    CHECK(named != P("fe80::1"));
    CHECK(P("fe80::1%0") == P("fe80::1"));
    CHECK(P("fe80::1%999999999").ScopeId() == 999999999);
    CHECK(P("fe80::1%4294967295").ScopeId() == 4294967295u);
    CHECK(P("fe80::1%1000000000").ScopeId() == 1000000000u);
    {
        const Address big = Address::V6(P("fe80::1").Bytes(), 4000000000u);
        CHECK(P(big.Format().CStr()) == big);
    }
    CHECK(P("fe80::1%4294967296").ScopeName() == StringView("4294967296"));  // too long for an id: kept as a name
    // Rewriting a scope.
    CHECK(named.WithScopeId(5).ScopeId() == 5);
    CHECK(named.WithScopeId(5).ScopeName().Empty());
    Address renamed;
    CHECK(numbered.WithScopeName("lo", &renamed));
    CHECK(renamed.ScopeName() == StringView("lo"));
    CHECK(renamed.ScopeId() == 0);
    Address bad;
    CHECK_FALSE(numbered.WithScopeName("", &bad));
    CHECK_FALSE(numbered.WithScopeName("a b", &bad));
    CHECK_FALSE(numbered.WithScopeName("0123456789abcdef", &bad));
    CHECK(numbered.WithScopeName("0123456789abcde", &bad));
    CHECK_FALSE(Address::V4(1, 2, 3, 4).WithScopeName("eth0", &bad));
    CHECK(Address::V4(1, 2, 3, 4).WithScopeId(3) == Address::V4(1, 2, 3, 4));
}

TEST_CASE("Address: ordering, equality and hashing agree", "[net][address]") {
    const Address list[] = {Address(), P("0.0.0.0"), P("1.2.3.4"), P("1.2.3.5"), P("10.0.0.1"), P("::"), P("::1"), P("::ffff:1.2.3.4"), P("fe80::1"), P("fe80::1%eth0"),
                            P("fe80::1%eth1"), P("fe80::1%1"), P("fe80::1%2"), P("ff02::1")};
    constexpr usize n = sizeof list / sizeof list[0];
    for (usize i = 0; i < n; ++i) {
        for (usize j = 0; j < n; ++j) {
            CAPTURE(i, j);
            CHECK((list[i] == list[j]) == (i == j));
            CHECK((list[i] <=> list[j] == Ordering::less) == (i < j));
            CHECK((list[i] <=> list[j] == Ordering::greater) == (i > j));
            if (i == j) CHECK(list[i].HashValue() == list[j].HashValue());
            else CHECK(list[i].HashValue() != list[j].HashValue());
        }
    }
    HashMap<Address, int, HeapAllocator> map(HeapAllocator::Default());
    map.Insert(P("1.2.3.4"), 1);
    map.Insert(P("::1"), 2);
    REQUIRE(map.Find(P("1.2.3.4")) != nullptr);
    CHECK(*map.Find(P("1.2.3.4")) == 1);
    CHECK(*map.Find(P("::1")) == 2);
    CHECK(map.Find(P("1.2.3.5")) == nullptr);
}

TEST_CASE("Address: random bytes never crash the parser and an accepted text round-trips", "[net][address][fuzz]") {
    u64 rng = 99;
    static const char alphabet[] = "0123456789abcdefABCDEF:.[]%/ -xg\n";
    usize accepted = 0;
    for (int i = 0; i < 60000; ++i) {
        char buffer[48];
        const usize length = NextRandom(rng) % 41;
        for (usize k = 0; k < length; ++k) {
            const u64 r = NextRandom(rng);
            buffer[k] = (r % 4 == 0) ? static_cast<char>(r >> 8) : alphabet[(r >> 4) % (sizeof alphabet - 1)];
        }
        Address a;
        if (Address::Parse(StringView(buffer, length), &a).Ok()) {
            ++accepted;
            Address b;
            REQUIRE(Address::Parse(a.Format().View(), &b).Ok());
            CHECK(a == b);
        }
    }
    // Structured mutations of valid texts reach the accept path as well.
    for (const ValidCase& c : kValid) {
        const usize length = StringView(c.text).Length();
        for (usize pos = 0; pos < length; ++pos) {
            char text[80];
            for (usize k = 0; k < length; ++k) text[k] = c.text[k];
            text[pos] = alphabet[NextRandom(rng) % (sizeof alphabet - 1)];
            Address a;
            if (Address::Parse(StringView(text, length), &a).Ok()) {
                ++accepted;
                Address b;
                REQUIRE(Address::Parse(a.Format().View(), &b).Ok());
                CHECK(a == b);
            }
        }
    }
    CHECK(accepted > 0);
}

struct EndpointCase {
    const char* text;
    const char* canonical;
    u16 port;
};

TEST_CASE("Endpoint: valid forms parse and format", "[net][endpoint]") {
    const EndpointCase cases[] = {
        {"1.2.3.4:80", "1.2.3.4:80", 80}, {"0.0.0.0:0", "0.0.0.0:0", 0}, {"127.0.0.1:65535", "127.0.0.1:65535", 65535}, {"255.255.255.255:1", "255.255.255.255:1", 1},
        {"[::1]:443", "[::1]:443", 443}, {"[::]:0", "[::]:0", 0}, {"[2001:DB8::1]:8080", "[2001:db8::1]:8080", 8080}, {"[fe80::1%eth0]:5000", "[fe80::1%eth0]:5000", 5000},
        {"[fe80::1%3]:5000", "[fe80::1%3]:5000", 5000}, {"[::ffff:1.2.3.4]:9", "[::ffff:1.2.3.4]:9", 9}, {"10.0.0.1:12345", "10.0.0.1:12345", 12345},
    };
    for (const EndpointCase& c : cases) {
        CAPTURE(c.text);
        Endpoint e;
        REQUIRE(Endpoint::Parse(c.text, &e).Ok());
        CHECK(e.Port() == c.port);
        CHECK(e.Format().View() == StringView(c.canonical));
        Endpoint again;
        REQUIRE(Endpoint::Parse(e.Format().View(), &again).Ok());
        CHECK(again == e);
    }
}

TEST_CASE("Endpoint: invalid forms are refused", "[net][endpoint]") {
    const char* const invalid[] = {
        "", ":", ":80", "1.2.3.4", "1.2.3.4:", "1.2.3.4:65536", "1.2.3.4:99999", "1.2.3.4:123456", "1.2.3.4:-1", "1.2.3.4:+1", "1.2.3.4:080", "1.2.3.4:00", "1.2.3.4:8 0", "1.2.3.4: 80",
        "1.2.3.4:80 ", "1.2.3.4:0x50", "1.2.3.4:http", "1.2.3:80", "256.1.1.1:80", "01.2.3.4:80", "::1:80", "2001:db8::1:80", "[::1]", "[::1]:", "[::1]80", "[::1]:65536", "[::1]:080",
        "[::1:80", "::1]:80", "[1.2.3.4]:80", "[]:80", "[[::1]]:80", "localhost:80", "example.com:80", "1.2.3.4:80:90", "[::1]:80:90", "[::1]:80]", "1.2.3.4:80\n",
    };
    const Endpoint sentinel(Address::V4(9, 9, 9, 9), 9);
    for (const char* text : invalid) {
        CAPTURE(text);
        Endpoint e = sentinel;
        CHECK(Endpoint::Parse(text, &e).Is(ErrorCode::Invalid));
        CHECK(e == sentinel);
    }
}

TEST_CASE("Endpoint: ordering, hashing, port change and a plain value with no address", "[net][endpoint]") {
    const Endpoint a(Address::V4(1, 2, 3, 4), 80);
    const Endpoint b(Address::V4(1, 2, 3, 4), 81);
    const Endpoint c(Address::V4(1, 2, 3, 5), 1);
    CHECK(a < b);
    CHECK(b < c);
    CHECK(a == a.WithPort(80));
    CHECK(a.WithPort(81) == b);
    CHECK(a.HashValue() != b.HashValue());
    CHECK(a.HashValue() == Endpoint(Address::V4(1, 2, 3, 4), 80).HashValue());
    CHECK(Endpoint().Format().View() == StringView(":0"));
    HashMap<Endpoint, int, HeapAllocator> map(HeapAllocator::Default());
    map.Insert(a, 1);
    map.Insert(b, 2);
    CHECK(*map.Find(Endpoint(Address::V4(1, 2, 3, 4), 81)) == 2);
}

TEST_CASE("Endpoint: random bytes never crash the parser and an accepted text round-trips", "[net][endpoint][fuzz]") {
    u64 rng = 7;
    static const char alphabet[] = "0123456789abcdef:.[]%x -\n";
    for (int i = 0; i < 40000; ++i) {
        char buffer[64];
        const usize length = NextRandom(rng) % 60;
        for (usize k = 0; k < length; ++k) {
            const u64 r = NextRandom(rng);
            buffer[k] = (r % 6 == 0) ? static_cast<char>(r >> 8) : alphabet[(r >> 4) % (sizeof alphabet - 1)];
        }
        Endpoint e;
        if (Endpoint::Parse(StringView(buffer, length), &e).Ok()) {
            Endpoint b;
            REQUIRE(Endpoint::Parse(e.Format().View(), &b).Ok());
            CHECK(e == b);
        }
    }
}

TEST_CASE("Endpoint: an endpoint with no address formats as text the parser refuses", "[net][endpoint]") {
    // Documented: ":0" is for logs, not for reading back.
    Endpoint e;
    CHECK(Endpoint::Parse(Endpoint().Format().View(), &e).Failed());
}

TEST_CASE("EndpointSet: bounded, in order", "[net][endpoint]") {
    EndpointSet set;
    CHECK(set.Empty());
    for (usize i = 0; i < EndpointSet::kCapacity; ++i) {
        CHECK(set.Add(Endpoint(Address::V4(1, 1, 1, static_cast<u8>(i)), 1)));
    }
    CHECK_FALSE(set.Add(Endpoint(Address::V4(2, 2, 2, 2), 2)));
    CHECK(set.Count() == EndpointSet::kCapacity);
    CHECK(set[0].GetAddress() == Address::V4(1, 1, 1, 0));
    CHECK(set[EndpointSet::kCapacity - 1].GetAddress() == Address::V4(1, 1, 1, static_cast<u8>(EndpointSet::kCapacity - 1)));
    set.Clear();
    CHECK(set.Empty());
    CHECK(set.Add(Endpoint()));
}

TEST_CASE("Status: success, portable codes, the OS error kept as a detail", "[net][status]") {
    CHECK(Status::Success().Ok());
    CHECK_FALSE(Status::Success().Failed());
    const Status refused = Status::Make(ErrorCode::Refused, 111);
    CHECK(refused.Failed());
    CHECK(refused.Is(ErrorCode::Refused));
    CHECK_FALSE(refused.Is(ErrorCode::Reset));
    CHECK(refused.OsError() == 111);
    CHECK(Status::Of(ErrorCode::Closed).OsError() == 0);
    // Every code has a distinct non-empty name.
    for (u8 a = 0; a <= static_cast<u8>(ErrorCode::Other); ++a) {
        const StringView name(ErrorCodeName(static_cast<ErrorCode>(a)));
        CHECK_FALSE(name.Empty());
        for (u8 b = 0; b < a; ++b) CHECK(name != StringView(ErrorCodeName(static_cast<ErrorCode>(b))));
    }
    CHECK(StringView(ErrorCodeName(ErrorCode::WouldBlock)) == StringView("would_block"));
}

}  // namespace
}  // namespace engine_placeholder::net
