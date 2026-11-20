// The registry store and the group-policy layers on Windows (Phase 13 Day 157; settings/BACKEND.md,
// docs/guide/settings.md "Settings on Windows").
//
// Every test works under a key of its own, HKCU\Software\engine-settings-test-<process id>-<n>, removed at the end of
// the test; the first test of a process also removes the keys of earlier runs whose process is gone (a run that
// crashed). Nothing touches the developer's real settings (HKCU\Software\<app> of a real program) or any HKLM key.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/number.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#include "catch_engine_types.h"
#include "config_store_conformance.h"
#include "io/file.h"
#include "settings/env_source.h"
#include "settings/settings.h"
#include "settings/setting.h"
#include "settings/windows/registry_store.h"
#include "temp_dir.h"
#include "test_text.h"

#include "os/windows/win32.h"

using namespace vse;
using namespace vse::settings;
using namespace settings_test;

namespace
{

constexpr char kTestPrefix[] = "engine-settings-test-";

// Deletes HKCU\Software\engine-settings-test-<pid>-* of processes that no longer run.
void SweepStaleTestKeys()
{
    HKEY software = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ | KEY_WOW64_64KEY, &software) != ERROR_SUCCESS)
    {
        return;
    }
    Vector<engine_test::Text> stale;
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[256];
        DWORD units = 256;
        if (RegEnumKeyExW(software, index, name, &units, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
        {
            break;
        }
        char utf8[256];
        const int n = WideCharToMultiByte(CP_UTF8, 0, name, static_cast<int>(units), utf8, sizeof(utf8) - 1, nullptr,
                                          nullptr);
        if (n <= 0)
        {
            continue;
        }
        utf8[n] = '\0';
        if (CStrCompareN(utf8, kTestPrefix, sizeof(kTestPrefix) - 1) != 0)
        {
            continue;
        }
        const unsigned long pid = CStrToULong(utf8 + sizeof(kTestPrefix) - 1, nullptr, 10);
        if (pid == GetCurrentProcessId())
        {
            continue;
        }
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (process != nullptr)
        {
            const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
            if (running)
            {
                continue;
            }
        }
        stale.Push(engine_test::MakeText(StringView(utf8)));
    }
    RegCloseKey(software);
    for (usize i = 0; i < stale.Size(); ++i)
    {
        wchar_t wide[300];
        WFormatTo(wide, 300, L"Software\\%hs", stale[i].CStr());
        RegDeleteTreeW(HKEY_CURRENT_USER, wide);
    }
}

// A key of this test's own under HKCU, deleted (with everything below it) at the end of the scope.
class TestKey
{
public:
    TestKey()
    {
        static Atomic<int> next{0};
        static Atomic<bool> swept{false};
        if (!swept.Exchange(true))
        {
            SweepStaleTestKeys();
        }
        FormatTo(path_, sizeof(path_), "Software\\%s%lu-%d", kTestPrefix,
                 static_cast<unsigned long>(GetCurrentProcessId()), next.FetchAdd(1));
    }
    ~TestKey()
    {
        wchar_t wide[300];
        MultiByteToWideChar(CP_UTF8, 0, path_, -1, wide, 300);
        RegDeleteTreeW(HKEY_CURRENT_USER, wide);
        RegDeleteKeyExW(HKEY_CURRENT_USER, wide, KEY_WOW64_64KEY, 0);
    }
    TestKey(const TestKey&) = delete;
    TestKey& operator=(const TestKey&) = delete;
    const char* Path() const { return path_; }
    RegistryStoreOptions Options(bool read_only = false) const
    {
        RegistryStoreOptions o;
        o.path = path_;
        o.read_only = read_only;
        return o;
    }
    // Writes a raw value with the Win32 API (as a person with regedit would), under `section` (null: the key itself).
    void WriteRaw(const wchar_t* section, const wchar_t* name, DWORD type, const void* data, DWORD bytes) const
    {
        wchar_t wide[400];
        if (section != nullptr)
        {
            WFormatTo(wide, 400, L"%hs\\%ls", path_, section);
        }
        else
        {
            WFormatTo(wide, 400, L"%hs", path_);
        }
        HKEY key = nullptr;
        REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, wide, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key,
                                nullptr) == ERROR_SUCCESS);
        REQUIRE(RegSetValueExW(key, name, 0, type, static_cast<const BYTE*>(data), bytes) == ERROR_SUCCESS);
        RegCloseKey(key);
    }
    bool SubkeyExists(const wchar_t* section) const
    {
        wchar_t wide[400];
        WFormatTo(wide, 400, L"%hs\\%ls", path_, section);
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, wide, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        RegCloseKey(key);
        return true;
    }

private:
    char path_[128];
};

StoredValue AsText(const SettingDesc& desc, const Value& value)
{
    StoredValue out;
    REQUIRE(ToStored(desc, value, false, &out) == StoreError::None);
    return out;
}

}  // namespace

TEST_CASE("Windows: the registry store passes the store conformance suite", "[settings][store][windows]")
{
    TestKey key;
    auto make = [&] { return ConfigStorePtr(MakeUnique<RegistryConfigStore>(key.Options())); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
}

TEST_CASE("Windows: a read-only registry store (a policy key) reads and refuses every write",
          "[settings][store][windows]")
{
    TestKey key;
    const SettingDesc d = MakeInt("renderer.width", 1280, 0, 16384);
    {
        RegistryConfigStore writer(key.Options());
        REQUIRE(writer.Set("renderer.width", AsText(d, Value::FromInt(1920))).Ok());
    }
    RunReadOnlyConformance([&] { return ConfigStorePtr(MakeUnique<RegistryConfigStore>(key.Options(true))); },
                           "renderer.width", d, Value::FromInt(1920));
}

TEST_CASE("Windows: sections are subkeys, values are REG_SZ text, and anything else is reported, never converted",
          "[settings][store][windows]")
{
    TestKey key;
    RegistryConfigStore store(key.Options());
    REQUIRE(store.Load().Ok());
    CHECK_FALSE(store.IsPresent());
    const SettingDesc width = MakeInt("renderer.width", 1280, 0, 16384);
    REQUIRE(store.Set("renderer.width", AsText(width, Value::FromInt(1920))).Ok());
    CHECK(store.IsPresent());
    CHECK(key.SubkeyExists(L"renderer"));
    // What regedit shows: the value width under the subkey renderer, a REG_SZ with the text of the value.
    wchar_t sub[400];
    WFormatTo(sub, 400, L"%hs\\renderer", key.Path());
    wchar_t text[64] = {};
    DWORD bytes = sizeof(text);
    DWORD type = 0;
    REQUIRE(RegGetValueW(HKEY_CURRENT_USER, sub, L"width", RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY, &type, text, &bytes) ==
            ERROR_SUCCESS);
    CHECK(type == REG_SZ);
    CHECK(WStrCompare(text, L"1920") == 0);
    // A value written by hand as a REG_DWORD is a type mismatch for its key, not a number read some other way.
    const DWORD dword = 7;
    key.WriteRaw(L"renderer", L"height", REG_DWORD, &dword, sizeof(dword));
    StoredValue sv;
    CHECK(store.Get("renderer.height", &sv).Is(StoreError::TypeMismatch));
    // Plain text typed by hand reads like a file's unquoted text; a name that is not a key (upper case) is not listed.
    const wchar_t adapter[] = L"my gpu";
    key.WriteRaw(L"renderer", L"adapter", REG_SZ, adapter, sizeof(adapter));
    const wchar_t upper[] = L"x";
    key.WriteRaw(L"renderer", L"Upper Case", REG_SZ, upper, sizeof(upper));
    REQUIRE(store.Get("renderer.adapter", &sv).Ok());
    Value parsed;
    CHECK(FromStored(MakeText("renderer.adapter", ""), sv, &parsed) == ValueError::None);
    CHECK(parsed.AsText() == "my gpu");
    Vector<Key> keys;
    REQUIRE(store.Enumerate("renderer", &keys).Ok());
    CHECK(keys.Size() == 3);  // width, height, adapter
    // A typed value is not what a text store takes.
    StoredValue typed;
    typed.typed = true;
    typed.value = Value::FromInt(3);
    CHECK(store.Set("renderer.width", typed).Is(StoreError::Invalid));
    // Removing the last value of a section removes the section.
    const SettingDesc depth = MakeInt("a.b.c", 0, 0, 10);
    REQUIRE(store.Set("a.b.c", AsText(depth, Value::FromInt(4))).Ok());
    CHECK(key.SubkeyExists(L"a\\b"));
    REQUIRE(store.Remove("a.b.c").Ok());
    CHECK_FALSE(key.SubkeyExists(L"a\\b"));
    CHECK_FALSE(key.SubkeyExists(L"a"));
    CHECK(store.Remove("a.b.c").Is(StoreError::NotFound));
    char where[256];
    store.Origin("renderer.width", where, sizeof(where));
    CHECK(StringView(where).Contains("\\renderer, value width"));
    store.Describe(where, sizeof(where));
    CHECK(StringView(where).StartsWith("registry HKCU\\Software\\engine-settings-test-"));
}

namespace
{

struct Counter
{
    Atomic<int> calls{0};
};

void Count(void* user)
{
    static_cast<Counter*>(user)->calls.FetchAdd(1);
}

bool WaitFor(const Counter& c, int at_least, int ms)
{
    for (int waited = 0; waited < ms; waited += 10)
    {
        if (c.calls.Load() >= at_least)
        {
            return true;
        }
        ThisThread::SleepForMilliseconds(10);
    }
    return c.calls.Load() >= at_least;
}

}  // namespace

TEST_CASE("Windows: another writer's change is announced, the store's own is not", "[settings][store][windows]")
{
    TestKey key;
    const SettingDesc d = MakeInt("renderer.width", 1280, 0, 16384);
    RegistryConfigStore watched(key.Options());
    RegistryConfigStore other(key.Options());
    Counter counter;
    REQUIRE(watched.SetChangeListener(&Count, &counter).Ok());
    // The key does not exist yet: the first write of another store creates it, and that is a change too.
    REQUIRE(other.Set("renderer.width", AsText(d, Value::FromInt(800))).Ok());
    CHECK(WaitFor(counter, 1, 5000));
    const int after_other = counter.calls.Load();
    // The store's own write: nothing.
    REQUIRE(watched.Set("renderer.width", AsText(d, Value::FromInt(1024))).Ok());
    ThisThread::SleepForMilliseconds(400);
    CHECK(counter.calls.Load() == after_other);
    // A change by hand (regedit, another process) is announced.
    const wchar_t height[] = L"600";
    key.WriteRaw(L"renderer", L"height", REG_SZ, height, sizeof(height));
    CHECK(WaitFor(counter, after_other + 1, 5000));
    // Stopped: nothing more.
    REQUIRE(watched.SetChangeListener(nullptr, nullptr).Ok());
    const int stopped = counter.calls.Load();
    REQUIRE(other.Remove("renderer.width").Ok());
    ThisThread::SleepForMilliseconds(300);
    CHECK(counter.calls.Load() == stopped);
}

namespace
{
struct SelfRemoval
{
    RegistryConfigStore* store = nullptr;
    Atomic<int> result{-1};
};
void RemoveSelf(void* user)
{
    SelfRemoval* s = static_cast<SelfRemoval*>(user);
    s->result.Store(static_cast<int>(s->store->SetChangeListener(nullptr, nullptr).error));
}
}  // namespace

// A listener that tries to remove itself would make the watch thread wait for itself: the call is refused, and nothing
// hangs.
TEST_CASE("Windows: a listener cannot change its own store's listener, and nothing hangs", "[settings][store][windows]")
{
    TestKey key;
    const SettingDesc d = MakeInt("s.n", 0, 0, 10);
    RegistryConfigStore watched(key.Options());
    SelfRemoval self;
    self.store = &watched;
    REQUIRE(watched.SetChangeListener(&RemoveSelf, &self).Ok());
    RegistryConfigStore other(key.Options());
    REQUIRE(other.Set("s.n", AsText(d, Value::FromInt(1))).Ok());
    for (int waited = 0; waited < 5000 && self.result.Load() < 0; waited += 10)
    {
        ThisThread::SleepForMilliseconds(10);
    }
    CHECK(self.result.Load() == static_cast<int>(StoreError::Invalid));
    CHECK(watched.SetChangeListener(nullptr, nullptr).Ok());  // from another thread it works
}

TEST_CASE("Windows: a store destroyed while a change is being announced stops cleanly, a thousand times",
          "[settings][store][windows]")
{
    TestKey key;
    const SettingDesc d = MakeInt("s.n", 0, 0, 1000000);
    RegistryConfigStore writer(key.Options());
    REQUIRE(writer.Set("s.n", AsText(d, Value::FromInt(0))).Ok());
    Atomic<bool> stop{false};
    Thread churn(
        [&]
        {
            RegistryConfigStore w(key.Options());
            for (int n = 1; !stop.Load(); ++n)
            {
                (void)w.Set("s.n", AsText(d, Value::FromInt(n % 1000000)));
            }
        });
    Counter counter;
    for (int i = 0; i < 1000; ++i)
    {
        RegistryConfigStore watched(key.Options());
        REQUIRE(watched.SetChangeListener(&Count, &counter).Ok());
        if (i % 50 == 0)
        {
            ThisThread::SleepForMilliseconds(2);  // now and then, long enough to be mid-announcement
        }
    }
    stop = true;
    churn.Join();
    CHECK(counter.calls.Load() >= 0);  // the point is that no listener ran on a destroyed store (ASan) and nothing hung
}

TEST_CASE("Windows: a group-policy value wins over the user's file and the command line, and is never written",
          "[settings][policy][windows]")
{
    TestKey key;
    io_test::TempDir dir;
    Registry registry;
    const SettingId width = registry.Register(MakeInt("renderer.width", 1280, 0, 16384));
    const SettingId vsync = registry.Register(MakeBool("renderer.vsync", true));
    // The administrator's value, as regedit writes it (the test's own key stands in for Software\Policies\<app>).
    const wchar_t enforced[] = L"1024";
    key.WriteRaw(L"renderer", L"width", REG_SZ, enforced, sizeof(enforced));
    const io::Path user = dir / "user.cfg";
    const StringView user_text("[renderer]\nwidth = 1920\nvsync = false\n");
    REQUIRE(io::WriteFile(user, user_text.Data(), user_text.Length()).Ok());
    const char* argv[] = {"prog", "--set", "renderer.width=2560"};
    LoadOptions o;
    o.app_name = "engine-settings-test";
    o.user_file = &user;
    o.use_environment = false;
    o.policy_path = key.Path();
    o.argv = argv;
    o.argc = 3;
    Settings settings(registry);
    settings.Load(o);
    CHECK(Setting<i64>(registry, width).Get() == 1024);
    const Resolution r = settings.Provenance(width);
    CHECK(StringView(r.layer_name) == "user policy");
    CHECK(StringView(r.origin).Contains("value width"));
    CHECK_FALSE(Setting<bool>(registry, vsync).Get());  // not enforced: the user's own value
    // The program cannot change it (a change of a setting the policy leaves alone is fine).
    CHECK(settings.Set(width, Value::FromInt(1600)).error == SetError::Enforced);
    CHECK(Setting<i64>(registry, width).Get() == 1024);
    CHECK(settings.Set(vsync, Value::FromBool(true)).error == SetError::None);
    CHECK(Setting<bool>(registry, vsync).Get());
    RegistryConfigStore policy(key.Options(true));
    StoredValue sv;
    REQUIRE(policy.Get("renderer.width", &sv).Ok());
    CHECK(sv.value.AsText() == "1024");
    REQUIRE(settings.Save().Ok());
    Vector<u8> saved;
    REQUIRE(io::ReadFile(user, &saved).Ok());
    CHECK(StringView(reinterpret_cast<const char*>(saved.Data()), saved.Size())
              .Contains("width = 1920"));  // the user's own value, untouched
}

TEST_CASE("Windows: environment names ignore case, so two spellings are one variable", "[settings][env][windows]")
{
    EnvironmentSnapshot env(true);
    env.Add("ENGINE_SETTING_RENDERER__WIDTH", "800");
    env.Add("engine_setting_renderer__width", "900");  // the same variable on Windows: it replaces the first
    CHECK(env.Size() == 1);
    REQUIRE(env.Find("Engine_Setting_Renderer__Width") != nullptr);
    CHECK(env.Find("Engine_Setting_Renderer__Width")->View() == "900");
    EnvironmentSnapshot posix(false);
    posix.Add("A", "1");
    posix.Add("a", "2");
    CHECK(posix.Size() == 2);
    // The process's own snapshot follows the platform: SetEnvironmentVariable("Path") and "PATH" name one variable
    // here.
    const EnvironmentSnapshot captured = EnvironmentSnapshot::Capture();
    CHECK(captured.IgnoresCase());
    CHECK(captured.Find("path") != nullptr);
}
