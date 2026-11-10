#include <catch2/catch_test_macros.hpp>

#include "containers/array.h"
#include "containers/vector.h"
#include "envelope.h"

#include "types.h"
#include "cstr/mem_ops.h"
#include "string/string.h"
#include "catch_engine_types.h"
#include "test_text.h"

using engine_placeholder::StringView;
using engine_placeholder::usize;
using engine_placeholder::Vector;
using engine_test::Text;

using crash_extract::EnvelopeResult;
using crash_extract::FindMinidump;
using crash_extract::Minidump;

namespace {

const char kExtractHeader[] = "{\"event_id\":\"11111111-2222-3333-4444-555555555555\",\"trace\":{\"release\":\"t@1\",\"sampled\":\"false\"}}\n";

Text ExtractItem(StringView header, StringView payload) {
    Text item = engine_test::Concat(header, "\n", payload);
    item.Append('\n');
    return item;
}

Text ExtractDumpBytes() {
    Text dump = engine_test::MakeText("MDMP");
    for (int i = 1; i <= 60; ++i) {
        dump.Append(static_cast<char>(i));
    }
    return dump;
}

// An item header with a "length" of `length`, then `more` (the rest of the members, with their leading comma, or nothing).
Text LengthHeader(const char* type, usize length, const char* more = "") {
    return engine_test::Format("{\"type\":\"%s\",\"length\":%zu%s}", type, length, more);
}

// A typical envelope: an event, a session and the minidump (what the library writes after a crash).
Text ExtractEnvelope() {
    const StringView event("{\"event_id\":\"x\",\"level\":\"fatal\"}");
    const StringView session("{\"sid\":\"y\"}");
    const Text dump = ExtractDumpBytes();
    Text text = engine_test::MakeText(kExtractHeader);
    text.Append(ExtractItem(LengthHeader("event", event.Length()).View(), event).View());
    text.Append(ExtractItem(LengthHeader("session", session.Length()).View(), session).View());
    text.Append(ExtractItem(LengthHeader("attachment", dump.Length(), ",\"attachment_type\":\"event.minidump\",\"filename\":\"a.dmp\"").View(),
                            dump.View()).View());
    return text;
}

EnvelopeResult Find(StringView text, Minidump* out) {
    return FindMinidump(text.Data(), text.Length(), out);
}

} // namespace

TEST_CASE("The minidump item of an envelope is found and points at its bytes", "[crash-extract][debug]") {
    const Text text = ExtractEnvelope();
    Minidump dump;
    REQUIRE(Find(text.View(), &dump) == EnvelopeResult::Ok);
    CHECK(dump.size == ExtractDumpBytes().Length());
    CHECK(engine_placeholder::MemCompare(dump.data, "MDMP", 4) == 0);
    CHECK(StringView(dump.data, dump.size) == ExtractDumpBytes().View());
    // The result points inside the input.
    CHECK(dump.data >= text.CStr());
    CHECK(dump.data + dump.size <= text.CStr() + text.Length());
}

TEST_CASE("An envelope without a minidump is reported as such", "[crash-extract][debug]") {
    const StringView event("{\"level\":\"fatal\"}");
    Text text = engine_test::MakeText(kExtractHeader);
    text.Append(ExtractItem(LengthHeader("event", event.Length()).View(), event).View());
    Minidump dump;
    CHECK(Find(text.View(), &dump) == EnvelopeResult::NoMinidump);
    CHECK(dump.data == nullptr);
    CHECK(dump.size == 0);
}

TEST_CASE("With two minidump items the first is returned", "[crash-extract][debug]") {
    const StringView first("MDMPone");
    const StringView second("MDMPtwo!");
    Text text = engine_test::MakeText(kExtractHeader);
    text.Append(ExtractItem("{\"type\":\"attachment\",\"length\":7,\"attachment_type\":\"event.minidump\"}", first).View());
    text.Append(ExtractItem("{\"type\":\"attachment\",\"length\":8,\"attachment_type\":\"event.minidump\"}", second).View());
    Minidump dump;
    REQUIRE(Find(text.View(), &dump) == EnvelopeResult::Ok);
    CHECK(StringView(dump.data, dump.size) == first);
}

TEST_CASE("An item header without a length takes its payload to the end of the line", "[crash-extract][debug]") {
    Text text = engine_test::MakeText(kExtractHeader);
    text.Append(ExtractItem("{\"type\":\"attachment\",\"attachment_type\":\"event.minidump\"}", "MDMPnolength").View());
    Minidump dump;
    REQUIRE(Find(text.View(), &dump) == EnvelopeResult::Ok);
    CHECK(StringView(dump.data, dump.size) == "MDMPnolength");
}

TEST_CASE("Other attachments and key order do not confuse the parser", "[crash-extract][debug]") {
    Text text = engine_test::MakeText(kExtractHeader);
    text.Append(ExtractItem("{\"attachment_type\":\"event.attachment\",\"type\":\"attachment\",\"length\":3,\"filename\":\"length\"}", "abc").View());
    text.Append(ExtractItem("{ \"length\" : 5 , \"type\":\"attachment\", \"attachment_type\" : \"event.minidump\", \"nested\":{\"a\":[1,2,{\"b\":\"}\"}]} }", "MDMP!").View());
    Minidump dump;
    REQUIRE(Find(text.View(), &dump) == EnvelopeResult::Ok);
    CHECK(StringView(dump.data, dump.size) == "MDMP!");
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
    const Text full = ExtractEnvelope();
    const engine_placeholder::usize dump_end = full.Length() - 1; // the payload ends before the final newline
    for (engine_placeholder::usize length = 0; length <= full.Length(); ++length) {
        // A copy of exactly `length` bytes, so any read past its end is outside an allocation (ASan reports it).
        Vector<char> storage; // reserved to exactly the length, so the capacity ends where the data does
        storage.Reserve(length == 0 ? 1 : length);
        storage.Resize(length);
        char* exact = storage.Data();
        if (length != 0) engine_placeholder::MemCopy(exact, full.CStr(), length);
        Minidump dump;
        const EnvelopeResult result = FindMinidump(exact, length, &dump);
        if (result == EnvelopeResult::Ok) {
            // A found dump lies inside the cut buffer and is the complete payload.
            CHECK(dump.size == ExtractDumpBytes().Length());
            CHECK(dump.data + dump.size <= exact + length);
            CHECK(length >= dump_end);
        } else {
            CHECK(length < dump_end + 1);
        }
    }
}

TEST_CASE("Flipping any byte of a real envelope never reads outside it", "[crash-extract][debug]") {
    const Text full = ExtractEnvelope();
    for (engine_placeholder::usize i = 0; i < full.Length(); ++i) {
        const engine_placeholder::Array<engine_placeholder::u8, 3> flips = {{0xff, 0x20, 0x01}};
        for (const engine_placeholder::u8 flip : flips) {
            Vector<char> storage;
            storage.Reserve(full.Length());
            storage.Resize(full.Length());
            char* exact = storage.Data();
            engine_placeholder::MemCopy(exact, full.CStr(), full.Length());
            exact[i] = static_cast<char>(static_cast<engine_placeholder::u8>(exact[i]) ^ flip);
            Minidump dump;
            const EnvelopeResult result = FindMinidump(exact, full.Length(), &dump);
            if (result == EnvelopeResult::Ok) {
                REQUIRE(dump.data >= exact);
                REQUIRE(dump.data + dump.size <= exact + full.Length());
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
