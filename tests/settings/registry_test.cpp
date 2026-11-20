#include <catch2/catch_test_macros.hpp>

#include "lang/limits.h"

#include "sync/atomic.h"
#include "sync/thread.h"

#include "catch_engine_types.h"
#include "settings/registry.h"
#include "settings/setting.h"
#include "test_text.h"

using namespace vse;
using namespace vse::settings;

namespace
{

const char* const kRegistryModes[] = {"fifo", "mailbox", "immediate"};
enum class Mode
{
    Fifo,
    Mailbox,
    Immediate
};

RegisterError TryRegister(Registry& r, const SettingDesc& d)
{
    RegisterError e = RegisterError::None;
    const SettingId id = r.Register(d, &e);
    CHECK(id.IsValid() == (e == RegisterError::None));
    return e;
}

}  // namespace

TEST_CASE("Registry: ids are handed out in registration order and are stable", "[settings][registry]")
{
    Registry r;
    const SettingId a = r.Register(MakeInt("b.width", 1280, 0, 16384));
    const SettingId b = r.Register(MakeBool("a.fullscreen", false));
    const SettingId c = r.Register(MakeText("c.name", "x"));
    CHECK(a.value == 1);
    CHECK(b.value == 2);
    CHECK(c.value == 3);
    CHECK(r.Count() == 3);
    CHECK(r.Find("a.fullscreen") == b);
    CHECK(r.Find("nope") == SettingId{});
    CHECK(r.KeyOf(a).View() == "b.width");
    CHECK(r.Desc(c)->type == Type::Text);
    CHECK(r.Desc(SettingId{9}) == nullptr);
    CHECK(r.Desc(SettingId{}) == nullptr);
    // Sorted by key: a.fullscreen, b.width, c.name
    CHECK(r.SortedAt(0) == b);
    CHECK(r.SortedAt(1) == a);
    CHECK(r.SortedAt(2) == c);
    r.Freeze();
    r.Freeze();
    CHECK(r.IsFrozen());
    CHECK(r.Find("b.width") == a);  // ids do not change on freeze
}

TEST_CASE("Registry: every registration failure has a reason and changes nothing", "[settings][registry]")
{
    Registry r;
    REQUIRE(r.Register(MakeInt("x.n", 5, 0, 10)).IsValid());
    CHECK(TryRegister(r, MakeInt("x.n", 5, 0, 10)) == RegisterError::Duplicate);
    CHECK(TryRegister(r, MakeBool("Bad", false)) == RegisterError::BadKey);
    CHECK(TryRegister(r, MakeBool("", false)) == RegisterError::BadKey);
    SettingDesc nullkey = MakeBool("k", false);
    nullkey.key = nullptr;
    CHECK(TryRegister(r, nullkey) == RegisterError::BadKey);
    CHECK(TryRegister(r, MakeInt("y.a", 5, 10, 0)) == RegisterError::BadRange);
    CHECK(TryRegister(r, MakeInt("y.b", 11, 0, 10)) == RegisterError::BadDefault);
    CHECK(TryRegister(r, MakeInt("y.c", -1, 0, 10)) == RegisterError::BadDefault);
    CHECK(TryRegister(r, MakeFloat("y.d", 0, 2.0, 1.0)) == RegisterError::BadRange);
    CHECK(TryRegister(r, MakeFloat("y.e", 5.0, 0.0, 1.0)) == RegisterError::BadDefault);
    CHECK(TryRegister(r, MakeFloat("y.f", 0.0, 0.0, Limits<f64>::Infinity())) == RegisterError::BadRange);
    CHECK(TryRegister(r, MakeEnum("y.g", kRegistryModes, 0, 0)) == RegisterError::BadEnum);
    CHECK(TryRegister(r, MakeEnum("y.h", nullptr, 3, 0)) == RegisterError::BadEnum);
    CHECK(TryRegister(r, MakeEnum("y.i", kRegistryModes, 3, 3)) == RegisterError::BadDefault);
    const char* const bad_names[] = {"ok", "has space"};
    CHECK(TryRegister(r, MakeEnum("y.j", bad_names, 2, 0)) == RegisterError::BadEnum);
    const char* const empty_name[] = {"ok", ""};
    CHECK(TryRegister(r, MakeEnum("y.k", empty_name, 2, 0)) == RegisterError::BadEnum);
    const char* const twice[] = {"ok", "ok"};
    CHECK(TryRegister(r, MakeEnum("y.l", twice, 2, 0)) == RegisterError::BadEnum);
    const char* const hash[] = {"a#b"};
    CHECK(TryRegister(r, MakeEnum("y.m", hash, 1, 0)) == RegisterError::BadEnum);
    SettingDesc wrong_default = MakeInt("y.n", 0, 0, 1);
    wrong_default.default_value = Value::FromBool(false);
    CHECK(TryRegister(r, wrong_default) == RegisterError::BadDefault);
    SettingDesc bad_old = MakeBool("y.o", false);
    const char* const old_bad[] = {"Not A Key"};
    bad_old.deprecated_keys = old_bad;
    bad_old.deprecated_count = 1;
    CHECK(TryRegister(r, bad_old) == RegisterError::BadKey);
    const char* const old_taken[] = {"x.n"};
    bad_old.deprecated_keys = old_taken;
    CHECK(TryRegister(r, bad_old) == RegisterError::Duplicate);
    const char* const old_twice[] = {"old.a", "old.a"};
    bad_old.deprecated_keys = old_twice;
    bad_old.deprecated_count = 2;
    CHECK(TryRegister(r, bad_old) == RegisterError::Duplicate);
    const char* const name_bracket[] = {"a]"};
    CHECK(TryRegister(r, MakeEnum("y.p", name_bracket, 1, 0)) == RegisterError::BadEnum);
    const char* const name_cr[] = {"a\rb"};
    CHECK(TryRegister(r, MakeEnum("y.q", name_cr, 1, 0)) == RegisterError::BadEnum);
    bad_old.deprecated_keys = nullptr;
    bad_old.deprecated_count = 1;
    CHECK(TryRegister(r, bad_old) == RegisterError::BadDeprecated);
    CHECK(r.Count() == 1);  // nothing was added by any failure
    CHECK(StringView(RegisterErrorName(RegisterError::Full)) == "full");
}

TEST_CASE("Registry: deprecated keys find the setting", "[settings][registry]")
{
    Registry r;
    SettingDesc d = MakeInt("renderer.width", 0, 0, 100);
    const char* const old[] = {"width", "renderer.w"};
    d.deprecated_keys = old;
    d.deprecated_count = 2;
    const SettingId id = r.Register(d);
    REQUIRE(id.IsValid());
    CHECK(r.Find("width") == id);
    CHECK(r.Find("renderer.w") == id);
    CHECK(r.IsDeprecatedKey("width"));
    CHECK_FALSE(r.IsDeprecatedKey("renderer.width"));
    CHECK(TryRegister(r, MakeBool("width", false)) == RegisterError::Duplicate);  // a deprecated key stays reserved
}

TEST_CASE("Registry: five hundred settings", "[settings][registry]")
{
    Registry r;
    for (int i = 0; i < 500; ++i)
    {
        const engine_test::Text key = engine_test::Format("g%d.k%d", i % 7, i);
        REQUIRE(r.Register(MakeInt(key.CStr(), i, 0, 1000)).IsValid());
    }
    CHECK(r.Count() == 500);
    for (u32 i = 1; i < 500; ++i)
    {
        CHECK(r.KeyOf(r.SortedAt(i - 1)) < r.KeyOf(r.SortedAt(i)));
    }
    for (int i = 0; i < 500; ++i)
    {
        const engine_test::Text key = engine_test::Format("g%d.k%d", i % 7, i);
        const SettingId id = r.Find(key.View());
        REQUIRE(id.IsValid());
        CHECK(id.value == i + 1);
        CHECK(r.Current(id).AsInt() == i);
    }
}

TEST_CASE("Setting handles read the default, see published values and refuse the wrong type", "[settings][registry]")
{
    Registry r;
    const SettingId b = r.Register(MakeBool("t.b", true));
    const SettingId i = r.Register(MakeInt("t.i", -3, -10, 10));
    const SettingId f = r.Register(MakeFloat("t.f", 0.25, 0.0, 1.0));
    const SettingId e = r.Register(MakeEnum("t.e", kRegistryModes, 3, 1));
    const SettingId t = r.Register(MakeText("t.t", "hello"));
    r.Freeze();
    Setting<bool> sb(r, b);
    Setting<i64> si(r, i);
    Setting<f32> sf(r, f);
    Setting<f64> sd(r, f);
    Setting<Mode> se(r, e);
    TextSetting st(r, t);
    CHECK(sb.Get());
    CHECK(si.Get() == -3);
    CHECK(sf.Get() == 0.25f);
    CHECK(sd.Get() == 0.25);
    CHECK(se.Get() == Mode::Mailbox);
    char text[16];
    CHECK(st.Get(text, sizeof(text)) == 5);
    CHECK(StringView(text) == "hello");
    CHECK(si.Default() == -3);
    CHECK(si.Desc().max_int == 10);
    CHECK(si.Id() == i);
    r.Publish(b, Value::FromBool(false));
    r.Publish(i, Value::FromInt(9));
    r.Publish(f, Value::FromFloat(0.75));
    r.Publish(e, Value::FromEnum(2));
    r.Publish(t, Value::FromText("a longer text than before"));
    CHECK_FALSE(sb.Get());
    CHECK(si.Get() == 9);
    CHECK(sf.Get() == 0.75f);
    CHECK(se.Get() == Mode::Immediate);
    CHECK(si.Default() == -3);  // the default does not move
    CHECK(r.Current(t).AsText() == "a longer text than before");
    CHECK(st.Get(text, sizeof(text)) == 25);  // too small: nothing written, the needed length returned
    CHECK(Setting<i64>::Find(r, "t.i").Get() == 9);
}

TEST_CASE("Setting reads are safe while another thread publishes", "[settings][registry][threads]")
{
    Registry r;
    const SettingId id = r.Register(MakeInt("t.n", 0, 0, 1000000));
    const SettingId fid = r.Register(MakeFloat("t.f", 0.0, 0.0, 1e9));
    r.Freeze();
    Setting<i64> n(r, id);
    Setting<f64> f(r, fid);
    Atomic<bool> stop{false};
    Atomic<int> torn{0};
    Vector<Thread> readers;
    readers.Resize(3);
    for (usize t = 0; t < readers.Size(); ++t)
    {
        readers[t] = Thread(
            [&]
            {
                i64 last = 0;
                while (!stop.Load())
                {
                    const i64 v = n.Get();
                    // The writer publishes n, then f: when n is read, f is at most one step behind it (and may be ahead
                    // by the time f is read).
                    if (v < last || f.Get() < static_cast<f64>(v) - 1.0)
                    {
                        torn++;
                    }
                    last = v;
                }
            });
    }
    for (i64 v = 1; v <= 20000; ++v)
    {
        r.Publish(id, Value::FromInt(v));
        r.Publish(fid, Value::FromFloat(static_cast<f64>(v)));
    }
    stop = true;
    for (usize t = 0; t < readers.Size(); ++t)
    {
        readers[t].Join();
    }
    CHECK(torn.Load() == 0);
    CHECK(n.Get() == 20000);
}
