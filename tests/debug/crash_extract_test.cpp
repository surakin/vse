#include <catch2/catch_test_macros.hpp>

#include "containers/vector.h"
#include "envelope.h"

#include "types.h"
#include <cstring>
#include <string>
#include "cstr/mem_ops.h"

using engine_placeholder::Vector;

using crash_extract::EnvelopeResult;
using crash_extract::FindMinidump;
using crash_extract::Minidump;

namespace {

const char kExtractHeader[] = "{\"event_id\":\"11111111-2222-3333-4444-555555555555\",\"trace\":{\"release\":\"t@1\",\"sampled\":\"false\"}}\n";

std::string ExtractItem(const std::string& header, const std::string& payload) {
    return header + "\n" + payload + "\n";
}

std::string ExtractDumpBytes() {
    std::string dump = "MDMP";
    for (int i = 1; i <= 60; ++i) {
        dump.push_back(static_cast<char>(i));
    }
    return dump;
}

// A typical envelope: an event, a session and the minidump (what the library writes after a crash).
std::string ExtractEnvelope() {
    const std::string event = "{\"event_id\":\"x\",\"level\":\"fatal\"}";
    const std::string session = "{\"sid\":\"y\"}";
    const std::string dump = ExtractDumpBytes();
    std::string text = kExtractHeader;
    text += ExtractItem("{\"type\":\"event\",\"length\":" + std::to_string(event.size()) + "}", event);
    text += ExtractItem("{\"type\":\"session\",\"length\":" + std::to_string(session.size()) + "}", session);
    text += ExtractItem("{\"type\":\"attachment\",\"length\":" + std::to_string(dump.size()) +
                            ",\"attachment_type\":\"event.minidump\",\"filename\":\"a.dmp\"}",
                        dump);
    return text;
}

EnvelopeResult Find(const std::string& text, Minidump* out) {
    return FindMinidump(text.data(), text.size(), out);
}

} // namespace

TEST_CASE("The minidump item of an envelope is found and points at its bytes", "[crash-extract][debug]") {
    const std::string text = ExtractEnvelope();
    Minidump dump;
    REQUIRE(Find(text, &dump) == EnvelopeResult::Ok);
    CHECK(dump.size == ExtractDumpBytes().size());
    CHECK(engine_placeholder::MemCompare(dump.data, "MDMP", 4) == 0);
    CHECK(std::string(dump.data, dump.size) == ExtractDumpBytes());
    // The result points inside the input.
    CHECK(dump.data >= text.data());
    CHECK(dump.data + dump.size <= text.data() + text.size());
}

TEST_CASE("An envelope without a minidump is reported as such", "[crash-extract][debug]") {
    const std::string event = "{\"level\":\"fatal\"}";
    const std::string text = std::string(kExtractHeader) + ExtractItem("{\"type\":\"event\",\"length\":" + std::to_string(event.size()) + "}", event);
    Minidump dump;
    CHECK(Find(text, &dump) == EnvelopeResult::NoMinidump);
    CHECK(dump.data == nullptr);
    CHECK(dump.size == 0);
}

TEST_CASE("With two minidump items the first is returned", "[crash-extract][debug]") {
    const std::string first = "MDMPone";
    const std::string second = "MDMPtwo!";
    std::string text = kExtractHeader;
    text += ExtractItem("{\"type\":\"attachment\",\"length\":7,\"attachment_type\":\"event.minidump\"}", first);
    text += ExtractItem("{\"type\":\"attachment\",\"length\":8,\"attachment_type\":\"event.minidump\"}", second);
    Minidump dump;
    REQUIRE(Find(text, &dump) == EnvelopeResult::Ok);
    CHECK(std::string(dump.data, dump.size) == first);
}

TEST_CASE("An item header without a length takes its payload to the end of the line", "[crash-extract][debug]") {
    std::string text = kExtractHeader;
    text += ExtractItem("{\"type\":\"attachment\",\"attachment_type\":\"event.minidump\"}", "MDMPnolength");
    Minidump dump;
    REQUIRE(Find(text, &dump) == EnvelopeResult::Ok);
    CHECK(std::string(dump.data, dump.size) == "MDMPnolength");
}

TEST_CASE("Other attachments and key order do not confuse the parser", "[crash-extract][debug]") {
    std::string text = kExtractHeader;
    text += ExtractItem("{\"attachment_type\":\"event.attachment\",\"type\":\"attachment\",\"length\":3,\"filename\":\"length\"}", "abc");
    text += ExtractItem("{ \"length\" : 5 , \"type\":\"attachment\", \"attachment_type\" : \"event.minidump\", \"nested\":{\"a\":[1,2,{\"b\":\"}\"}]} }", "MDMP!");
    Minidump dump;
    REQUIRE(Find(text, &dump) == EnvelopeResult::Ok);
    CHECK(std::string(dump.data, dump.size) == "MDMP!");
}

TEST_CASE("Damaged envelopes are rejected, never read out of range", "[crash-extract][debug]") {
    Minidump dump;
    CHECK(FindMinidump(nullptr, 0, &dump) == EnvelopeResult::Malformed);
    CHECK(FindMinidump("", 0, &dump) == EnvelopeResult::Malformed);
    CHECK(Find("no newline at all", &dump) == EnvelopeResult::Malformed);
    CHECK(Find("{}\n{\"length\":99999999999999999999999}\n", &dump) == EnvelopeResult::Malformed);       // overflows
    CHECK(Find("{}\n{\"length\":18446744073709551615}\nabc\n", &dump) == EnvelopeResult::Malformed);       // longer than the data
    CHECK(Find("{}\n{\"length\":10}\nabc\n", &dump) == EnvelopeResult::Malformed);                         // payload cut off
    CHECK(Find("{}\n{\"length\":-1}\nabc\n", &dump) == EnvelopeResult::Malformed);                         // negative
    // 2^64 + 3 wraps to 3 if overflow is not detected, which would then look like a valid short payload.
    CHECK(Find("{}\n{\"length\":18446744073709551619,\"attachment_type\":\"event.minidump\"}\nabc\n", &dump) == EnvelopeResult::Malformed);
    CHECK(Find("{}\n{\"length\":3,\"type\":\"x\"\nabc\n", &dump) == EnvelopeResult::Malformed);            // header not closed
    CHECK(Find("{}\nnot json\nabc\n", &dump) == EnvelopeResult::Malformed);
    CHECK(Find("{}\n{\"length\":3}\nabcX", &dump) == EnvelopeResult::Malformed);                           // no newline after the payload
    CHECK(Find("{}\n{\"a\":\"unterminated", &dump) == EnvelopeResult::Malformed);
}

TEST_CASE("Cutting a real envelope at every length never reads outside it and only a whole minidump is accepted", "[crash-extract][debug]") {
    const std::string full = ExtractEnvelope();
    const engine_placeholder::usize dump_end = full.size() - 1; // the payload ends before the final newline
    for (engine_placeholder::usize length = 0; length <= full.size(); ++length) {
        // A copy of exactly `length` bytes, so any read past its end is outside an allocation (ASan reports it).
        std::string cut(full.data(), length);
        Vector<char> storage; // reserved to exactly the length, so the capacity ends where the data does
        storage.Reserve(length == 0 ? 1 : length);
        storage.Resize(length);
        char* exact = storage.Data();
        if (length != 0) engine_placeholder::MemCopy(exact, cut.data(), length);
        Minidump dump;
        const EnvelopeResult result = FindMinidump(exact, length, &dump);
        if (result == EnvelopeResult::Ok) {
            // A found dump lies inside the cut buffer and is the complete payload.
            CHECK(dump.size == ExtractDumpBytes().size());
            CHECK(dump.data + dump.size <= exact + length);
            CHECK(length >= dump_end);
        } else {
            CHECK(length < dump_end + 1);
        }
    }
}

TEST_CASE("Flipping any byte of a real envelope never reads outside it", "[crash-extract][debug]") {
    const std::string full = ExtractEnvelope();
    for (engine_placeholder::usize i = 0; i < full.size(); ++i) {
        for (unsigned char flip : {static_cast<unsigned char>(0xff), static_cast<unsigned char>(0x20), static_cast<unsigned char>(0x01)}) {
            Vector<char> storage;
            storage.Reserve(full.size());
            storage.Resize(full.size());
            char* exact = storage.Data();
            engine_placeholder::MemCopy(exact, full.data(), full.size());
            exact[i] = static_cast<char>(static_cast<unsigned char>(exact[i]) ^ flip);
            Minidump dump;
            const EnvelopeResult result = FindMinidump(exact, full.size(), &dump);
            if (result == EnvelopeResult::Ok) {
                REQUIRE(dump.data >= exact);
                REQUIRE(dump.data + dump.size <= exact + full.size());
            }
        }
    }
}

TEST_CASE("Random bytes are rejected without a crash", "[crash-extract][debug]") {
    engine_placeholder::u32 state = 12345;
    for (int round = 0; round < 200; ++round) {
        Vector<char> storage;
        storage.Reserve(257);
        storage.Resize(257);
        char* bytes = storage.Data();
        for (int i = 0; i < 257; ++i) {
            state = state * 1664525u + 1013904223u;
            bytes[i] = static_cast<char>(state >> 24);
        }
        if (round % 3 == 0) {
            bytes[100] = '\n'; // make some of them have lines
            bytes[150] = '{';
        }
        Minidump dump;
        const EnvelopeResult result = FindMinidump(bytes, 257, &dump);
        if (result == EnvelopeResult::Ok) {
            REQUIRE(dump.data + dump.size <= bytes + 257);
        }
    }
}
