// The user defaults store and the managed-preferences policy layer on macOS (Phase 21 Day 293; settings/BACKEND.md,
// docs/guide/settings.md "Settings on macOS").
//
// Every test works in a defaults suite of its own, engine-settings-test-<process id>-<n>, whose values are removed at
// the end of the test: nothing touches the developer's real preferences (a real program's suite) or the global domain.
// A run that crashed leaves a small ~/Library/Preferences/engine-settings-test-*.plist behind; `defaults delete`
// removes it.
#include <catch2/catch_test_macros.hpp>

#include "cstr/cstr.h"
#include "cstr/format.h"
#include "os/last_error.h"
#include "sync/atomic.h"
#include "sync/thread.h"

#include "catch_engine_types.h"
#include "config_store_conformance.h"
#include "settings/env_source.h"
#include "settings/layers.h"
#include "settings/macos/user_defaults_store.h"
#include "settings/policy_layers.h"
#include "settings/settings.h"
#include "test_env.h"
#include "test_text.h"

#include <CoreFoundation/CoreFoundation.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char** environ;

using namespace vse;
using namespace vse::settings;
using namespace settings_test;

namespace
{

CFStringRef MakeCf(const char* text)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
}

// A defaults suite of this test's own, emptied at the end of the scope.
class TestSuite
{
public:
    TestSuite()
    {
        static Atomic<int> next{0};
        FormatTo(name_, sizeof(name_), "engine-settings-test-%ld-%d", static_cast<long>(::getpid()), next.FetchAdd(1));
    }
    ~TestSuite()
    {
        const CFStringRef suite = MakeCf(name_);
        const CFArrayRef keys = CFPreferencesCopyKeyList(suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        if (keys != nullptr)
        {
            CFPreferencesSetMultiple(nullptr, keys, suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
            CFRelease(keys);
        }
        CFPreferencesSynchronize(suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        CFRelease(suite);
    }
    TestSuite(const TestSuite&) = delete;
    TestSuite& operator=(const TestSuite&) = delete;
    const char* Name() const { return name_; }
    UserDefaultsStoreOptions Options(bool read_only = false) const
    {
        UserDefaultsStoreOptions o;
        o.suite = name_;
        o.read_only = read_only;
        return o;
    }
    // Writes a raw value with CoreFoundation, as `defaults write` or another program would.
    void WriteRaw(const char* key, CFPropertyListRef value) const
    {
        const CFStringRef suite = MakeCf(name_);
        const CFStringRef name = MakeCf(key);
        CFPreferencesSetValue(name, value, suite, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
        CFRelease(name);
        CFRelease(suite);
    }
    // Whether the raw value is a string equal to `text`.
    bool RawIsString(const char* key, const char* text) const
    {
        const CFStringRef suite = MakeCf(name_);
        const CFStringRef name = MakeCf(key);
        const CFPropertyListRef value = CFPreferencesCopyValue(name, suite, kCFPreferencesCurrentUser,
                                                               kCFPreferencesAnyHost);
        bool same = false;
        if (value != nullptr)
        {
            if (CFGetTypeID(value) == CFStringGetTypeID())
            {
                const CFStringRef expected = MakeCf(text);
                same = CFStringCompare(static_cast<CFStringRef>(value), expected, 0) == kCFCompareEqualTo;
                CFRelease(expected);
            }
            CFRelease(value);
        }
        CFRelease(name);
        CFRelease(suite);
        return same;
    }

private:
    char name_[96];
};

StoredValue AsText(const SettingDesc& desc, const Value& value)
{
    StoredValue out;
    REQUIRE(ToStored(desc, value, false, &out) == StoreError::None);
    return out;
}

struct Counter
{
    Atomic<int> calls{0};
};

void Count(void* user)
{
    static_cast<Counter*>(user)->calls.FetchAdd(1);
}

// Runs `defaults write <suite> <key> -string <value>` as another process would; true if it exited with 0.
bool DefaultsWrite(const char* suite, const char* key, const char* value)
{
    char program[] = "defaults";
    char write[] = "write";
    char kind[] = "-string";
    char* argv[] = {program, write, const_cast<char*>(suite), const_cast<char*>(key), kind, const_cast<char*>(value),
                    nullptr};
    pid_t pid = 0;
    if (::posix_spawnp(&pid, "defaults", nullptr, nullptr, argv, environ) != 0)
    {
        return false;
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
    {
        if (os::LastError() != EINTR)
        {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
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

TEST_CASE("macOS: the user defaults store passes the store conformance suite", "[settings][store][macos]")
{
    TestSuite suite;
    auto make = [&] { return ConfigStorePtr(MakeUnique<UserDefaultsConfigStore>(suite.Options())); };
    RunWritableConformance(make);
    RunDurabilityConformance(make);
    RunCapabilityConformance(make);
}

TEST_CASE("macOS: a read-only user defaults store reads and refuses every write", "[settings][store][macos]")
{
    TestSuite suite;
    const SettingDesc d = MakeInt("renderer.width", 1280, 0, 16384);
    {
        UserDefaultsConfigStore writer(suite.Options());
        REQUIRE(writer.Set("renderer.width", AsText(d, Value::FromInt(1920))).Ok());
    }
    RunReadOnlyConformance([&] { return ConfigStorePtr(MakeUnique<UserDefaultsConfigStore>(suite.Options(true))); },
                           "renderer.width", d, Value::FromInt(1920));
}

TEST_CASE("macOS: values are strings of their text, and hand-written typed values read as text",
          "[settings][store][macos]")
{
    TestSuite suite;
    UserDefaultsConfigStore store(suite.Options());
    REQUIRE(store.Load().Ok());
    CHECK_FALSE(store.IsPresent());
    const SettingDesc width = MakeInt("renderer.width", 1280, 0, 16384);
    REQUIRE(store.Set("renderer.width", AsText(width, Value::FromInt(1920))).Ok());
    CHECK(store.IsPresent());
    // What `defaults read` shows: the dotted key as it is, a string with the value's text.
    CHECK(suite.RawIsString("renderer.width", "1920"));

    // Written by hand with a type (`defaults write <suite> renderer.height -int 600`, `-bool`, `-float`): read as text.
    int raw_int = 600;
    const CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &raw_int);
    suite.WriteRaw("renderer.height", number);
    CFRelease(number);
    suite.WriteRaw("renderer.vsync", kCFBooleanFalse);
    const double raw_scale = 1.5;
    const CFNumberRef real = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &raw_scale);
    suite.WriteRaw("ui.scale", real);
    CFRelease(real);
    Value parsed;
    StoredValue sv;
    REQUIRE(store.Get("renderer.height", &sv).Ok());
    CHECK(FromStored(MakeInt("renderer.height", 0, 0, 10000), sv, &parsed) == ValueError::None);
    CHECK(parsed.AsInt() == 600);
    REQUIRE(store.Get("renderer.vsync", &sv).Ok());
    CHECK(FromStored(MakeBool("renderer.vsync", true), sv, &parsed) == ValueError::None);
    CHECK_FALSE(parsed.AsBool());
    REQUIRE(store.Get("ui.scale", &sv).Ok());
    CHECK(FromStored(MakeFloat("ui.scale", 1.0, 0.5, 4.0), sv, &parsed) == ValueError::None);
    CHECK(parsed.AsFloat() == 1.5);
    // A date is no setting's value: a type mismatch for its key, never converted.
    const CFDateRef date = CFDateCreate(kCFAllocatorDefault, 0);
    suite.WriteRaw("renderer.when", date);
    CFRelease(date);
    CHECK(store.Get("renderer.when", &sv).Is(StoreError::TypeMismatch));
    // A name that is not a key (upper case) is not listed.
    const CFStringRef x = MakeCf("x");
    suite.WriteRaw("Upper Case", x);
    CFRelease(x);
    Vector<Key> keys;
    REQUIRE(store.Enumerate("renderer", &keys).Ok());
    CHECK(keys.Size() == 4);  // width, height, vsync, when
    keys.Clear();
    REQUIRE(store.Enumerate("", &keys).Ok());
    CHECK(keys.Size() == 5);  // and ui.scale

    // A typed value is not what a text store takes.
    StoredValue typed;
    typed.typed = true;
    typed.value = Value::FromInt(3);
    CHECK(store.Set("renderer.width", typed).Is(StoreError::Invalid));
    CHECK(store.Set("Not A Key", AsText(width, Value::FromInt(3))).Is(StoreError::Invalid));
    REQUIRE(store.Remove("renderer.width").Ok());
    CHECK(store.Remove("renderer.width").Is(StoreError::NotFound));
    CHECK(store.Commit(CommitOptions()).Ok());

    char where[256];
    store.Origin("renderer.width", where, sizeof(where));
    CHECK(StringView(where).StartsWith("defaults engine-settings-test-"));
    CHECK(StringView(where).Contains(" renderer.width"));
    store.Describe(where, sizeof(where));
    CHECK(StringView(where).StartsWith("user defaults engine-settings-test-"));
}

TEST_CASE("macOS: a key of the global domain is not read through a suite", "[settings][store][macos]")
{
    // CFPreferencesCopyAppValue would fall back to the global domain (NSGlobalDomain); the store asks the suite alone.
    TestSuite suite;
    // The fallback finds the key through this suite (the global domain holds it on a stock Mac); without it the check
    // proves nothing.
    const CFStringRef name = MakeCf("com.apple.swipescrolldirection");
    const CFStringRef domain = MakeCf(suite.Name());
    const CFPropertyListRef fallback = CFPreferencesCopyAppValue(name, domain);
    CFRelease(domain);
    CFRelease(name);
    if (fallback == nullptr)
    {
        SKIP("this Mac's global domain has no com.apple.swipescrolldirection to fall back to");
    }
    CFRelease(fallback);
    UserDefaultsConfigStore store(suite.Options());
    StoredValue sv;
    CHECK(store.Get("com.apple.swipescrolldirection", &sv).Is(StoreError::NotFound));
}

TEST_CASE("macOS: another writer's change is announced, the store's own is not", "[settings][store][macos]")
{
    TestSuite suite;
    const SettingDesc d = MakeInt("renderer.width", 1280, 0, 16384);
    UserDefaultsConfigStore watched(suite.Options());
    UserDefaultsConfigStore other(suite.Options());
    Counter counter;
    REQUIRE(watched.SetChangeListener(&Count, &counter).Ok());
    // Another store's write (this process, but not this store).
    REQUIRE(other.Set("renderer.width", AsText(d, Value::FromInt(800))).Ok());
    CHECK(WaitFor(counter, 1, 5000));
    const int after_other = counter.calls.Load();
    // The store's own writes: nothing, even across several polls.
    REQUIRE(watched.Set("renderer.width", AsText(d, Value::FromInt(1024))).Ok());
    REQUIRE(watched.Set("renderer.height", AsText(d, Value::FromInt(768))).Ok());
    REQUIRE(watched.Remove("renderer.height").Ok());
    ThisThread::SleepForMilliseconds(700);
    CHECK(counter.calls.Load() == after_other);
    // Another process's write (`defaults write`, which macOS's own notification does not report).
    REQUIRE(DefaultsWrite(suite.Name(), "renderer.height", "600"));
    CHECK(WaitFor(counter, after_other + 1, 5000));
    // Stopped: nothing more.
    REQUIRE(watched.SetChangeListener(nullptr, nullptr).Ok());
    const int stopped = counter.calls.Load();
    REQUIRE(other.Remove("renderer.width").Ok());
    ThisThread::SleepForMilliseconds(600);
    CHECK(counter.calls.Load() == stopped);
}

namespace
{

struct SelfRemoval
{
    UserDefaultsConfigStore* store = nullptr;
    Atomic<int> result{-1};
};

void RemoveSelf(void* user)
{
    SelfRemoval* s = static_cast<SelfRemoval*>(user);
    s->result.Store(static_cast<int>(s->store->SetChangeListener(nullptr, nullptr).error));
}

}  // namespace

TEST_CASE("macOS: a listener cannot change its own store's listener, and destroying a watched store stops its thread",
          "[settings][store][macos]")
{
    TestSuite suite;
    const SettingDesc d = MakeInt("s.n", 0, 0, 10);
    {
        UserDefaultsConfigStore watched(suite.Options());
        SelfRemoval self;
        self.store = &watched;
        REQUIRE(watched.SetChangeListener(&RemoveSelf, &self).Ok());
        UserDefaultsConfigStore other(suite.Options());
        REQUIRE(other.Set("s.n", AsText(d, Value::FromInt(1))).Ok());
        for (int waited = 0; waited < 5000 && self.result.Load() < 0; waited += 10)
        {
            ThisThread::SleepForMilliseconds(10);
        }
        CHECK(self.result.Load() == static_cast<int>(StoreError::Invalid));
    }  // destroyed with its watch thread running: the destructor stops it
    Counter counter;
    for (int i = 0; i < 20; ++i)
    {
        UserDefaultsConfigStore watched(suite.Options());
        REQUIRE(watched.SetChangeListener(&Count, &counter).Ok());
    }
    SUCCEED();
}

TEST_CASE("macOS: managed preferences are an empty policy layer when no profile enforces anything",
          "[settings][store][macos]")
{
    TestSuite suite;
    // A value in the user's own domain is not enforced: the forced-only view does not show it.
    {
        UserDefaultsConfigStore writer(suite.Options());
        REQUIRE(writer.Set("renderer.width", AsText(MakeInt("renderer.width", 0, 0, 9000), Value::FromInt(5))).Ok());
    }
    UserDefaultsStoreOptions options = suite.Options();
    options.forced_only = true;
    UserDefaultsConfigStore managed(options);
    CHECK((managed.Capabilities() & kCapWritable) == 0);
    REQUIRE(managed.Load().Ok());
    CHECK_FALSE(managed.IsPresent());
    StoredValue sv;
    CHECK(managed.Get("renderer.width", &sv).Is(StoreError::NotFound));
    Vector<Key> keys;
    REQUIRE(managed.Enumerate("", &keys).Ok());
    CHECK(keys.Empty());
    CHECK(managed.Set("renderer.width", AsText(MakeInt("renderer.width", 0, 0, 9000), Value::FromInt(1)))
              .Is(StoreError::ReadOnly));
    char where[256];
    managed.Describe(where, sizeof(where));
    CHECK(StringView(where).StartsWith("managed preferences engine-settings-test-"));
    // Where an enforced value lives is not `defaults` (review finding, Day 293).
    managed.Origin("renderer.width", where, sizeof(where));
    CHECK(StringView(where).StartsWith("managed preferences engine-settings-test-"));

    // A suite that is not one file name reads no managed file at all (review finding, Day 293: it could name a file
    // outside /Library/Managed Preferences; its keys were still filtered by CFPreferencesAppValueIsForced, so this case
    // passed before the fix too, and pins the behavior).
    for (const char* bad : {"../../../tmp/x", "..", "a/b", ""})
    {
        UserDefaultsStoreOptions escape;
        escape.suite = bad;
        escape.forced_only = true;
        UserDefaultsConfigStore odd(escape);
        Vector<Key> none;
        const Status listed = odd.Enumerate("", &none);
        CHECK((listed.Failed() || none.Empty()));
        CHECK_FALSE(odd.IsPresent());
    }

    // Settings::Load adds it as the one Policy layer, over the policy suite it is given.
    LoadOptions load;
    load.policy_path = suite.Name();
    LayerStack stack;
    settings::detail::AddPolicyLayers(load, &stack);
    REQUIRE(stack.Size() == 1);
    CHECK(stack.Role(0) == LayerRole::Policy);
}

TEST_CASE("macOS: the environment snapshot keeps names that differ only in case apart", "[settings][layers][macos]")
{
    // macOS's environment is case-sensitive, as Linux's (os::EnvironmentNamesIgnoreCase is false): two spellings are
    // two variables in the captured snapshot (Windows folds them into one).
    test_support::SetEnv("ENGINE_SETTINGS_MACOS_CASE", "upper");
    test_support::SetEnv("engine_settings_macos_case", "lower");
    const EnvironmentSnapshot captured = EnvironmentSnapshot::Capture();
    test_support::UnsetEnv("ENGINE_SETTINGS_MACOS_CASE");
    test_support::UnsetEnv("engine_settings_macos_case");
    CHECK_FALSE(captured.IgnoresCase());
    REQUIRE(captured.Find("ENGINE_SETTINGS_MACOS_CASE") != nullptr);
    REQUIRE(captured.Find("engine_settings_macos_case") != nullptr);
    CHECK(captured.Find("ENGINE_SETTINGS_MACOS_CASE")->View() == "upper");
    CHECK(captured.Find("engine_settings_macos_case")->View() == "lower");
    CHECK(captured.Find("Engine_Settings_Macos_Case") == nullptr);
}
