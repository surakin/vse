#include <catch2/catch_test_macros.hpp>

#include "catch_engine_types.h"
#include "settings/config_text.h"
#include "test_text.h"

using namespace engine_placeholder;
using namespace engine_placeholder::settings;

namespace {

struct ValueRow {
    const char* text;
    const char* key;
    const char* value; // the value as written (null: the key must be absent)
};

struct ErrorRow {
    const char* text;
    u32 line;
    u32 column;
    const char* message;
};

engine_test::Text Roundtrip(StringView text) {
    ConfigDocument doc;
    doc.Parse(text, nullptr);
    Vector<char> out;
    doc.Serialize(&out);
    return engine_test::MakeText(StringView(out.Data(), out.Size()));
}

struct Lcg {
    u64 state;
    u32 Next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<u32>(state >> 33);
    }
};

// A random document built from fragments that exercise every construct, including broken ones.
engine_test::Text RandomDocument(Lcg& rng) {
    static const char* const pieces[] = {
        "# comment\n", "; other comment\n", "\n", "   \n", "[renderer]\n", "[a.b]\n", "[audio]   # trailing\n", "width = 1280\n", "width=1\n",
        "  indented = 5   # note\n", "name = \"quoted \\\"x\\\" text\"  ; c\n", "mode = mailbox\n", "flag = true\r\n", "ratio = 0.25\n",
        "bad line without equals\n", "x = \"unterminated\n", "[broken\n", "key with space = 1\n", "Upper = 1\n", "a.b = 3\n", "path = C:\\dir\\file\n",
        "empty =\n", "list = [1, 2, 3]\n", "hash = a#b\n", "tab\t=\t7\t# t\n", "caf\xc3\xa9 = 1\n", "last = 1", "\r\n",
    };
    engine_test::Text doc = engine_test::MakeText();
    const u32 n = rng.Next() % 25;
    for (u32 i = 0; i < n; ++i) doc.Append(StringView(pieces[rng.Next() % (sizeof(pieces) / sizeof(pieces[0]))]));
    return doc;
}

} // namespace

TEST_CASE("Config text: values, keys and sections", "[settings][text]") {
    const ValueRow rows[] = {
        {"a = 1", "a", "1"},
        {"a=1", "a", "1"},
        {"  a  =  1  ", "a", "1"},
        {"\ta\t=\t1\t", "a", "1"},
        {"a = 1 # comment", "a", "1"},
        {"a = 1 ; comment", "a", "1"},
        {"a = 1\t# comment", "a", "1"},
        {"a = x#y", "a", "x#y"},
        {"a = x;y", "a", "x;y"},
        {"a = x # y", "a", "x"},
        {"a = two words", "a", "two words"},
        {"a = two  words  # c", "a", "two  words"},
        {"a = \"quoted\"", "a", "\"quoted\""},
        {"a = \"quoted\" # c", "a", "\"quoted\""},
        {"a = \"has # inside\"", "a", "\"has # inside\""},
        {"a = \"esc \\\" \\\\ \\n \\t \\x41\"", "a", "\"esc \\\" \\\\ \\n \\t \\x41\""},
        {"a =", "a", ""},
        {"a = # only a comment", "a", ""},
        {"a = [1, 2]", "a", "[1, 2]"},
        {"a = C:\\dir", "a", "C:\\dir"},
        {"a.b = 1", "a.b", "1"},
        {"a.b.c = 1", "a.b.c", "1"},
        {"[s]\na = 1", "s.a", "1"},
        {"[s]\na = 1", "a", nullptr},
        {"[s.t]\na = 1", "s.t.a", "1"},
        {"[s] # header comment\na = 1", "s.a", "1"},
        {"[ s ]\na = 1", "s.a", nullptr}, // blanks inside the brackets are not allowed
        {"[s]\n[t]\na = 1", "t.a", "1"},
        {"[s]\na = 1\n[t]\na = 2", "s.a", "1"},
        {"[s]\na = 1\n[t]\na = 2", "t.a", "2"},
        {"a = 1\na = 2", "a", "2"}, // the last one wins
        {"[s]\na = 1\n[s]\na = 2", "s.a", "2"},
        {"# a = 1", "a", nullptr},
        {"; a = 1", "a", nullptr},
        {"  # a = 1", "a", nullptr},
        {"a = 1\r\nb = 2\r\n", "b", "2"},
        {"\xEF\xBB\xBF" "a = 1", "a", "1"},
        {"a = caf\xc3\xa9", "a", "caf\xc3\xa9"},
        {"a = 1\n\n\n", "a", "1"},
        {"version = 1\n[s]\nx = 2", "version", "1"},
    };
    int checked = 0;
    for (const ValueRow& r : rows) {
        INFO(r.text);
        ConfigDocument doc;
        ConfigReport report;
        doc.Parse(r.text, &report);
        StringView v;
        const bool found = doc.Find(r.key, &v);
        if (r.value == nullptr) {
            CHECK_FALSE(found);
        } else {
            CHECK(found);
            if (found) CHECK(v == StringView(r.value));
        }
        ++checked;
    }
    CHECK(checked >= 35);
}

TEST_CASE("Config text: every error has a line, a column and a message, and the rest is still read", "[settings][text]") {
    const ErrorRow rows[] = {
        {"no equals sign", 1, 1, "expected key = value"},
        {"  no equals", 1, 3, "expected key = value"},
        {"ok = 1\nbroken line\nok2 = 2", 2, 1, "expected key = value"},
        {"= 5", 1, 1, "invalid key"},
        {"Upper = 1", 1, 1, "invalid key"},
        {"two words = 1", 1, 1, "invalid key"},
        {"a..b = 1", 1, 1, "invalid key"},
        {"a.b.c.d = 1", 1, 1, "invalid key"},
        {"[s]\na.b = 1", 2, 1, "invalid key"},
        {"1abc = 1", 1, 1, "invalid key"},
        {"a = \"unterminated", 1, 5, "unterminated quoted text"},
        {"a = \"bad \\q escape\"", 1, 10, "unknown escape in quoted text"},
        {"a = \"bad \\x4 escape\"", 1, 10, "bad \\x escape in quoted text"},
        {"a = \"x\" junk", 1, 9, "unexpected text after the value"},
        {"a = \"x\"y", 1, 8, "unexpected text after the value"},
        {"[unterminated", 1, 1, "unterminated section header"},
        {"[Bad]", 1, 2, "invalid section name"},
        {"[a.b.c]", 1, 2, "invalid section name"},
        {"[]", 1, 2, "invalid section name"},
        {"[a] junk", 1, 5, "unexpected text after the section header"},
        {"a = 1\nb = \"x", 2, 5, "unterminated quoted text"},
        {"x\0y = 1", 1, 1, "a NUL byte in the line"},
        {"a = \xff\xfe", 1, 1, "the line is not valid UTF-8"},
        {"a = \xc3", 1, 1, "the line is not valid UTF-8"},
    };
    for (const ErrorRow& r : rows) {
        INFO(r.text);
        ConfigDocument doc;
        ConfigReport report;
        const StringView text = (r.text[0] == 'x' && r.text[1] == '\0') ? StringView("x\0y = 1", 7) : StringView(r.text);
        doc.Parse(text, &report);
        REQUIRE(report.error_count >= 1);
        CHECK(report.errors[0].line == r.line);
        CHECK(report.errors[0].column == r.column);
        CHECK(StringView(report.errors[0].message) == StringView(r.message));
    }
    // Parsing goes on after an error, up to 20 are recorded, the count keeps going.
    engine_test::Text many = engine_test::MakeText();
    for (int i = 0; i < 50; ++i) many.Append("bad\n");
    many.Append("good = 1\n");
    ConfigDocument doc;
    ConfigReport report;
    doc.Parse(many.View(), &report);
    CHECK(report.error_count == 50);
    CHECK(report.errors[19].line == 20);
    StringView v;
    CHECK(doc.Find("good", &v));
    // A line over 4096 bytes is an error and is kept.
    engine_test::Text longline = engine_test::Concat("a = ", engine_test::Repeated(5000, 'x').View(), "\nb = 2\n");
    doc.Parse(longline.View(), &report);
    CHECK(report.error_count == 1);
    CHECK(StringView(report.errors[0].message) == "line is longer than 4096 bytes");
    CHECK(doc.Find("b", &v));
}

TEST_CASE("Config text: a repeated key is a warning with the first line, not an error", "[settings][text]") {
    ConfigDocument doc;
    ConfigReport report;
    doc.Parse("a = 1\nb = 2\na = 3\n", &report);
    CHECK(report.error_count == 0);
    REQUIRE(report.warning_count == 1);
    CHECK(report.warnings[0].line == 3);
    CHECK(StringView(report.warnings[0].message).Contains("first on line 1"));
}

TEST_CASE("Config text: an unchanged document serializes to the bytes it was parsed from", "[settings][text]") {
    const char* const docs[] = {"", "\n", "a = 1", "a = 1\n", "a = 1\r\nb = 2\r\n", "# only comments\n; and more\n", "\xEF\xBB\xBF[s]\nx = 1\n",
                                "mixed = 1\r\nline = 2\nendings = 3\r\n", "bad line\nstill = ok\n", "a = \"unterminated\nb = 1\n", "   \n\t\n", "a = 1\r"};
    for (const char* d : docs) {
        INFO(d);
        CHECK(Roundtrip(d).View() == StringView(d));
    }
    // A NUL inside a line survives.
    CHECK(Roundtrip(StringView("a\0b = 1\n", 8)).View() == StringView("a\0b = 1\n", 8));
}

TEST_CASE("Config text: 1000 random documents round-trip, and one change rewrites only its own line", "[settings][text][property]") {
    Lcg rng{20261107};
    int changed_checks = 0;
    for (int n = 0; n < 1000; ++n) {
        const engine_test::Text text = RandomDocument(rng);
        ConfigDocument doc;
        ConfigReport report;
        doc.Parse(text.View(), &report);
        Vector<char> out;
        doc.Serialize(&out);
        REQUIRE(StringView(out.Data(), out.Size()) == text.View());
        // Change the value of the first key that exists: only that line may differ.
        Vector<Key> keys;
        doc.Keys(StringView(), &keys);
        if (keys.Empty()) continue;
        const Key key = keys[0];
        StringView old_value;
        REQUIRE(doc.Find(key.View(), &old_value));
        REQUIRE(doc.Set(key.View(), "changed_value"));
        Vector<char> after;
        doc.Serialize(&after);
        // Compare line by line: count the lines that differ.
        const StringView a = text.View(), b(after.Data(), after.Size());
        usize pa = 0, pb = 0, differing = 0, lines_a = 0, lines_b = 0;
        while (pa < a.Length() || pb < b.Length()) {
            usize ea = pa, eb = pb;
            while (ea < a.Length() && a[ea] != '\n') ++ea;
            while (eb < b.Length() && b[eb] != '\n') ++eb;
            if (a.Substring(pa, ea - pa) != b.Substring(pb, eb - pb)) ++differing;
            ++lines_a;
            ++lines_b;
            pa = ea < a.Length() ? ea + 1 : a.Length();
            pb = eb < b.Length() ? eb + 1 : b.Length();
            if (ea >= a.Length() && eb >= b.Length()) break;
        }
        CHECK(differing == 1);
        CHECK(lines_a == lines_b);
        StringView now;
        REQUIRE(doc.Find(key.View(), &now));
        CHECK(now == "changed_value");
        ++changed_checks;
    }
    CHECK(changed_checks > 500);
}

TEST_CASE("Config text: Set edits in place, adds at the end of a section, and creates sections", "[settings][text]") {
    ConfigDocument doc;
    doc.Parse("# head\nversion = 1\n\n[renderer]\n# size\nwidth = 1280  # px\nheight = 720\n\n[audio]\nvolume = 0.5\n", nullptr);
    auto text = [&] {
        Vector<char> out;
        doc.Serialize(&out);
        return engine_test::MakeText(StringView(out.Data(), out.Size()));
    };
    REQUIRE(doc.Set("renderer.width", "1920"));
    CHECK(text().View() == "# head\nversion = 1\n\n[renderer]\n# size\nwidth = 1920  # px\nheight = 720\n\n[audio]\nvolume = 0.5\n");
    REQUIRE(doc.Set("renderer.hdr", "\"off\""));
    CHECK(text().View() == "# head\nversion = 1\n\n[renderer]\n# size\nwidth = 1920  # px\nheight = 720\nhdr = \"off\"\n\n[audio]\nvolume = 0.5\n");
    REQUIRE(doc.Set("input.sensitivity", "2.0"));
    CHECK(text().View() == "# head\nversion = 1\n\n[renderer]\n# size\nwidth = 1920  # px\nheight = 720\nhdr = \"off\"\n\n[audio]\nvolume = 0.5\n\n[input]\nsensitivity = 2.0\n");
    REQUIRE(doc.Set("version", "2"));
    REQUIRE(doc.Set("language", "en"));
    CHECK(text().View().StartsWith("# head\nversion = 2\nlanguage = en\n\n[renderer]"));
    // Bad values change nothing.
    CHECK_FALSE(doc.Set("renderer.width", "two\nlines"));
    CHECK_FALSE(doc.Set("renderer.width", " padded"));
    CHECK_FALSE(doc.Set("renderer.width", "\"unterminated"));
    CHECK_FALSE(doc.Set("renderer.width", "x # y"));
    CHECK_FALSE(doc.Set("Bad Key", "1"));
    StringView v;
    REQUIRE(doc.Find("renderer.width", &v));
    CHECK(v == "1920");
    // Remove deletes entry lines and leaves comments.
    CHECK(doc.Remove("renderer.width"));
    CHECK_FALSE(doc.Remove("renderer.width"));
    CHECK(text().View().Contains("# size\nheight = 720"));
}

TEST_CASE("Config text: new lines use the file's line ending, a missing final newline is added only when appending", "[settings][text]") {
    ConfigDocument crlf;
    crlf.Parse("a = 1\r\n", nullptr);
    REQUIRE(crlf.Set("b", "2"));
    Vector<char> out;
    crlf.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "a = 1\r\nb = 2\r\n");
    ConfigDocument bare;
    bare.Parse("a = 1", nullptr);
    REQUIRE(bare.Set("a", "5"));
    bare.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "a = 5"); // an edit in place keeps the missing newline
    REQUIRE(bare.Set("b", "2"));
    bare.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "a = 5\nb = 2\n");
    ConfigDocument bom;
    bom.Parse("\xEF\xBB\xBF" "a = 1\n", nullptr);
    REQUIRE(bom.Set("a", "2"));
    bom.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == StringView("\xEF\xBB\xBF" "a = 2\n"));
}

TEST_CASE("Config text: Keys lists each key once, in file order, under a prefix", "[settings][text]") {
    ConfigDocument doc;
    doc.Parse("z = 1\n[b]\nx = 1\ny = 2\n[a]\nx = 1\n[b]\nx = 9\n[bb]\nx = 1\n", nullptr);
    Vector<Key> keys;
    doc.Keys(StringView(), &keys);
    REQUIRE(keys.Size() == 5);
    CHECK(keys[0].View() == "z");
    CHECK(keys[1].View() == "b.x");
    CHECK(keys[2].View() == "b.y");
    CHECK(keys[3].View() == "a.x");
    CHECK(keys[4].View() == "bb.x");
    keys.Clear();
    doc.Keys("b", &keys);
    CHECK(keys.Size() == 2);
}

TEST_CASE("Config text: flipping every byte of a sample never crashes and always round-trips", "[settings][text][mutation]") {
    const engine_test::Text sample = engine_test::MakeText(
        "# Engine settings\nversion = 1\n\n[renderer]\npresent_mode = \"mailbox\"   # fifo | mailbox\nwidth = 1920\nheight = 1080\n"
        "adapter = \"NVIDIA \\\"RTX\\\" 4090\"\n\n[audio]\nvolume = 0.8\r\nlegacy = 3 ; kept\n");
    for (usize i = 0; i < sample.Length(); ++i) {
        for (unsigned flip : {0x01u, 0x80u, 0xFFu}) {
            Vector<char> bytes = engine_test::Chars(sample.Length());
            for (usize k = 0; k < sample.Length(); ++k) bytes[k] = sample.View()[k];
            bytes[i] = static_cast<char>(static_cast<byte>(bytes[i]) ^ flip);
            const StringView mutated(bytes.Data(), bytes.Size());
            ConfigDocument doc;
            ConfigReport report;
            doc.Parse(mutated, &report);
            Vector<char> out;
            doc.Serialize(&out);
            REQUIRE(StringView(out.Data(), out.Size()) == mutated);
            // Editing the mutated document must not crash either.
            doc.Set("renderer.width", "1");
            doc.Set("new.key", "2");
            doc.Remove("audio.volume");
            doc.Serialize(&out);
        }
    }
}

TEST_CASE("Config text: a new key never lands in the wrong section", "[settings][text][regression]") {
    auto reread = [](ConfigDocument& doc, StringView key, StringView expected) {
        Vector<char> out;
        doc.Serialize(&out);
        ConfigDocument again;
        again.Parse(StringView(out.Data(), out.Size()), nullptr);
        StringView v;
        return again.Find(key, &v) && v == expected;
    };
    // A top-level dotted key is not a place to put a sibling: the new key gets a real section.
    ConfigDocument a;
    a.Parse("renderer.width = 1\n[audio]\nvolume = 1\n", nullptr);
    REQUIRE(a.Set("renderer.height", "2"));
    CHECK(reread(a, "renderer.height", "2"));
    CHECK(reread(a, "renderer.width", "1"));
    // A comment block above the first header stays with it; one that opens the file stays at the top.
    ConfigDocument b;
    b.Parse("version = 1\n\n# renderer options\n[renderer]\nw = 1\n", nullptr);
    REQUIRE(b.Set("mode", "x"));
    Vector<char> out;
    b.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "version = 1\nmode = x\n\n# renderer options\n[renderer]\nw = 1\n");
    ConfigDocument c;
    c.Parse("# my file\n[renderer]\nw = 1\n", nullptr);
    REQUIRE(c.Set("mode", "x"));
    c.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "# my file\nmode = x\n\n[renderer]\nw = 1\n");
}

TEST_CASE("Config text: setting an empty value that is followed by a comment keeps the comment a comment", "[settings][text][regression]") {
    for (const char* line : {"a = # note\n", "a =#c\n", "a = ; note\n"}) {
        INFO(line);
        ConfigDocument doc;
        doc.Parse(line, nullptr);
        REQUIRE(doc.Set("a", "x"));
        Vector<char> out;
        doc.Serialize(&out);
        ConfigDocument again;
        again.Parse(StringView(out.Data(), out.Size()), nullptr);
        StringView v;
        REQUIRE(again.Find("a", &v));
        CHECK(v == "x");
    }
    ConfigDocument doc;
    doc.Parse("a = # note\n", nullptr);
    CHECK(doc.Set("a", "x"));
    Vector<char> out;
    doc.Serialize(&out);
    CHECK(StringView(out.Data(), out.Size()) == "a = x # note\n");
}

TEST_CASE("Config text: entries below a section header that could not be read are errors, not another section's keys", "[settings][text][regression]") {
    ConfigDocument doc;
    ConfigReport report;
    doc.Parse("[audio]\nvolume=1\n[renderer\nwidth=1920\n[audio]\nmute=0\n", &report);
    CHECK(report.error_count == 2);
    CHECK(StringView(report.errors[1].message) == "entry below a section header that could not be read");
    StringView v;
    CHECK_FALSE(doc.Find("audio.width", &v));
    CHECK_FALSE(doc.Find("renderer.width", &v));
    CHECK(doc.Find("audio.volume", &v));
    CHECK(doc.Find("audio.mute", &v)); // a good header ends the damage
}

TEST_CASE("Config text: an edit that would make a line too long is refused", "[settings][text][regression]") {
    const engine_test::Text line = engine_test::Concat("a = 1 # ", engine_test::Repeated(1000, 'c').View(), "\n");
    ConfigDocument doc;
    doc.Parse(line.View(), nullptr);
    const engine_test::Text big = engine_test::Repeated(3500, 'v');
    CHECK_FALSE(doc.Set("a", big.View()));
    StringView v;
    REQUIRE(doc.Find("a", &v));
    CHECK(v == "1");
    CHECK(doc.Set("a", engine_test::Repeated(100, 'v').View()));
}

TEST_CASE("Config text: a file of 60000 distinct keys parses and lists in linear time", "[settings][text][performance]") {
    engine_test::Text text = engine_test::MakeText();
    for (int i = 0; i < 60000; ++i) text.Append(engine_test::Format("s%d.k = %d\n", i, i).View());
    ConfigDocument doc;
    ConfigReport report;
    doc.Parse(text.View(), &report);
    CHECK(report.error_count == 0);
    CHECK(report.warning_count == 0);
    Vector<Key> keys;
    doc.Keys(StringView(), &keys);
    CHECK(keys.Size() == 60000);
}
