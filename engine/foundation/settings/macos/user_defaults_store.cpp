// The user defaults system as a settings store (Phase 21 Day 293; settings/macos/user_defaults_store.h,
// settings/BACKEND.md). CoreFoundation's preferences API (CFPreferences), the C layer under NSUserDefaults: the same
// domains, the same daemon (cfprefsd), no Objective-C needed. Every read and write names the domain for the current
// user on any host, so the global domain (NSGlobalDomain) and per-host values are never mixed in.
#include "settings/macos/user_defaults_store.h"

#include "algorithm.h"
#include "containers/span.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "io/file.h"
#include "io/path.h"

#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#include <unistd.h>

namespace engine_placeholder::settings
{

namespace
{

constexpr u64 kWatchIntervalMs = 250;
// The text an entry the store cannot read as a setting is compared by: a value of another type (a date, data, an
// array) changing to another such value is not noticed, which only matters for values no setting can hold.
constexpr char kUnreadable[] = "\x01unreadable";
constexpr u64 kMaxManagedPlistBytes = u64{1} << 24;

// An owned CoreFoundation reference, released when it goes out of scope.
template <typename T> class CfRef
{
public:
    explicit CfRef(T ref = nullptr) : ref_(ref) {}
    ~CfRef()
    {
        if (ref_ != nullptr)
        {
            CFRelease(ref_);
        }
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    T Get() const { return ref_; }

private:
    T ref_;
};

CFStringRef MakeString(StringView text)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.Data()),
                                   static_cast<CFIndex>(text.Length()), kCFStringEncodingUTF8, false);
}

// The string's UTF-8 bytes into `out` (not terminated); false if they do not fit.
bool CopyUtf8(CFStringRef string, char* out, usize capacity, usize* length)
{
    const CFIndex units = CFStringGetLength(string);
    CFIndex used = 0;
    const CFIndex converted = CFStringGetBytes(string, CFRangeMake(0, units), kCFStringEncodingUTF8, 0, false,
                                               reinterpret_cast<UInt8*>(out), static_cast<CFIndex>(capacity), &used);
    if (converted != units)
    {
        return false;
    }
    *length = static_cast<usize>(used);
    return true;
}

// A preference value as a setting's text: a string as it is, a boolean as `true`/`false`, a number in decimal.
StoreError ValueText(CFPropertyListRef value, char* out, usize capacity, usize* length)
{
    const CFTypeID type = CFGetTypeID(value);
    if (type == CFStringGetTypeID())
    {
        if (!CopyUtf8(static_cast<CFStringRef>(value), out, capacity, length) || *length > kMaxTextBytes)
        {
            return StoreError::TooLong;
        }
        return StoreError::None;
    }
    int n = 0;
    if (type == CFBooleanGetTypeID())
    {
        n = FormatTo(out, capacity, "%s", CFBooleanGetValue(static_cast<CFBooleanRef>(value)) ? "true" : "false");
    }
    else if (type == CFNumberGetTypeID())
    {
        const CFNumberRef number = static_cast<CFNumberRef>(value);
        if (CFNumberIsFloatType(number))
        {
            double d = 0;
            if (!CFNumberGetValue(number, kCFNumberDoubleType, &d))
            {
                return StoreError::TypeMismatch;  // not exactly a double (a 128-bit real)
            }
            n = FormatTo(out, capacity, "%.17g", d);
        }
        else
        {
            long long i = 0;
            if (!CFNumberGetValue(number, kCFNumberLongLongType, &i))
            {
                return StoreError::TypeMismatch;  // out of a setting's range (an unsigned 64-bit value above INT64_MAX)
            }
            n = FormatTo(out, capacity, "%lld", i);
        }
    }
    else
    {
        return StoreError::TypeMismatch;
    }
    if (n <= 0 || static_cast<usize>(n) >= capacity)
    {
        return StoreError::TooLong;
    }
    *length = static_cast<usize>(n);
    return StoreError::None;
}

// True if the suite can be one file name: not empty, not "." or "..", no '/' or control character. A suite that is not
// has no managed-preferences file to read (it would name a file outside /Library/Managed Preferences).
bool SuiteIsFileName(StringView suite)
{
    if (suite.Length() == 0 || suite == "." || suite == "..")
    {
        return false;
    }
    for (usize i = 0; i < suite.Length(); ++i)
    {
        const u8 c = static_cast<u8>(suite[i]);
        if (c < 0x20 || c == '/')
        {
            return false;
        }
    }
    return true;
}

// Appends the string keys of one plist file's top-level dictionary; nothing if the file is absent or not a dictionary.
void PlistKeys(const io::Path& path, Vector<String<HeapAllocator>>* out)
{
    Vector<u8> bytes;
    if (!io::ReadFile(path, &bytes, kMaxManagedPlistBytes).Ok())
    {
        return;
    }
    CfRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, bytes.Data(), static_cast<CFIndex>(bytes.Size())));
    if (data.Get() == nullptr)
    {
        return;
    }
    CfRef<CFPropertyListRef> list(
        CFPropertyListCreateWithData(kCFAllocatorDefault, data.Get(), kCFPropertyListImmutable, nullptr, nullptr));
    if (list.Get() == nullptr || CFGetTypeID(list.Get()) != CFDictionaryGetTypeID())
    {
        return;
    }
    const CFDictionaryRef dictionary = static_cast<CFDictionaryRef>(list.Get());
    const CFIndex count = CFDictionaryGetCount(dictionary);
    Vector<const void*> keys;
    keys.Resize(static_cast<usize>(count));
    CFDictionaryGetKeysAndValues(dictionary, keys.Data(), nullptr);
    for (CFIndex i = 0; i < count; ++i)
    {
        const void* key = keys[static_cast<usize>(i)];
        char name[kMaxKeyBytes + 1];
        usize length = 0;
        if (CFGetTypeID(key) == CFStringGetTypeID() &&
            CopyUtf8(static_cast<CFStringRef>(key), name, sizeof(name), &length))
        {
            out->Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(name, length)));
        }
    }
}

// The keys of the managed-preferences files a configuration profile installs for the suite (the user's, then the
// machine's); only CFPreferencesAppValueIsForced decides which of them are enforced.
void ManagedKeys(StringView suite, Vector<String<HeapAllocator>>* out)
{
    if (!SuiteIsFileName(suite))
    {
        return;
    }
    char file[512];
    const int n = FormatTo(file, sizeof(file), "%.*s.plist", static_cast<int>(suite.Length()), suite.Data());
    io::Path base;
    if (n <= 0 || static_cast<usize>(n) >= sizeof(file) ||
        !io::Path::Parse(StringView("/Library/Managed Preferences"), io::PathStyle::Posix, &base))
    {
        return;
    }
    passwd entry;
    passwd* found = nullptr;
    char buffer[4096];
    if (::getpwuid_r(::getuid(), &entry, buffer, sizeof(buffer), &found) == 0 && found != nullptr &&
        SuiteIsFileName(StringView(found->pw_name)))
    {
        PlistKeys(base.Join(StringView(found->pw_name)).Join(StringView(file, static_cast<usize>(n))), out);
    }
    PlistKeys(base.Join(StringView(file, static_cast<usize>(n))), out);
}

bool InPrefix(StringView key, StringView prefix)
{
    return prefix.Length() == 0 || key == prefix ||
           (key.Length() > prefix.Length() && key.StartsWith(prefix) && key[prefix.Length()] == '.');
}

}  // namespace

UserDefaultsConfigStore::UserDefaultsConfigStore(const UserDefaultsStoreOptions& options)
    : read_only_(options.read_only || options.forced_only), forced_only_(options.forced_only)
{
    if (options.suite != nullptr)
    {
        suite_ = String<HeapAllocator>(HeapAllocator::Default(), StringView(options.suite));
    }
}

UserDefaultsConfigStore::~UserDefaultsConfigStore()
{
    StopWatching();
}

u32 UserDefaultsConfigStore::Capabilities() const
{
    return (read_only_ ? 0u : kCapWritable) | kCapPerKeyWrites | kCapNotifies;
}

void UserDefaultsConfigStore::Describe(char* out, usize capacity) const
{
    FormatTo(out, capacity, "%s %.*s", forced_only_ ? "managed preferences" : "user defaults",
             static_cast<int>(suite_.Length()), suite_.View().Data());
}

Status UserDefaultsConfigStore::Load()
{
    if (suite_.Length() == 0)
    {
        return Status::Of(StoreError::Invalid);
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    if (suite.Get() == nullptr)
    {
        return Status::Of(StoreError::Invalid);
    }
    // Drops this process's cached copy of the domain, so the next read sees other processes' writes.
    CFPreferencesSynchronize(suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    if (forced_only_)
    {
        LockGuard<Mutex> lock(managed_mutex_);
        managed_read_ = false;  // a profile installed or removed since is read with the next call
    }
    return Status();
}

StoreError UserDefaultsConfigStore::ReadText(const Key& key, char* text, usize capacity, usize* length) const
{
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    CfRef<CFStringRef> name(MakeString(key.View()));
    if (suite.Get() == nullptr || name.Get() == nullptr || suite_.Length() == 0)
    {
        return StoreError::Invalid;
    }
    CFPropertyListRef raw = nullptr;
    if (forced_only_)
    {
        if (!IsManagedName(key.View()) || !CFPreferencesAppValueIsForced(name.Get(), suite.Get()))
        {
            return StoreError::NotFound;
        }
        raw = CFPreferencesCopyAppValue(name.Get(), suite.Get());
    }
    else
    {
        raw = CFPreferencesCopyValue(name.Get(), suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    }
    CfRef<CFPropertyListRef> value(raw);
    if (value.Get() == nullptr)
    {
        return StoreError::NotFound;
    }
    return ValueText(value.Get(), text, capacity, length);
}

Status UserDefaultsConfigStore::Get(StringView key, StoredValue* out)
{
    Key k;
    if (Key::Parse(key, &k) != KeyError::None)
    {
        return Status::Of(StoreError::Invalid);
    }
    char text[kMaxTextBytes + 1];
    usize length = 0;
    const StoreError error = ReadText(k, text, sizeof(text), &length);
    if (error != StoreError::None)
    {
        return Status::Of(error);
    }
    out->typed = false;
    out->value = Value::FromText(StringView(text, length));
    return Status();
}

Status UserDefaultsConfigStore::Set(StringView key, const StoredValue& value)
{
    if (read_only_)
    {
        return Status::Of(StoreError::ReadOnly);
    }
    Key k;
    if (Key::Parse(key, &k) != KeyError::None || suite_.Length() == 0)
    {
        return Status::Of(StoreError::Invalid);
    }
    if (value.typed || value.value.GetType() != Type::Text)
    {
        return Status::Of(StoreError::Invalid);  // a text store: ToStored(..., false, ...)
    }
    const StringView text = value.value.AsText();
    if (text.Length() > kMaxTextBytes)
    {
        return Status::Of(StoreError::TooLong);
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    CfRef<CFStringRef> name(MakeString(k.View()));
    CfRef<CFStringRef> data(MakeString(text));
    if (suite.Get() == nullptr || name.Get() == nullptr || data.Get() == nullptr)
    {
        return Status::Of(StoreError::Invalid);  // not UTF-8
    }
    LockGuard<Mutex> lock(mutex_);
    CFPreferencesSetValue(name.Get(), data.Get(), suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    RememberOwn(k, &text);
    return Status();
}

Status UserDefaultsConfigStore::Remove(StringView key)
{
    if (read_only_)
    {
        return Status::Of(StoreError::ReadOnly);
    }
    Key k;
    if (Key::Parse(key, &k) != KeyError::None || suite_.Length() == 0)
    {
        return Status::Of(StoreError::Invalid);
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    CfRef<CFStringRef> name(MakeString(k.View()));
    if (suite.Get() == nullptr || name.Get() == nullptr)
    {
        return Status::Of(StoreError::Invalid);
    }
    LockGuard<Mutex> lock(mutex_);
    CfRef<CFPropertyListRef> existing(
        CFPreferencesCopyValue(name.Get(), suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
    if (existing.Get() == nullptr)
    {
        return Status::Of(StoreError::NotFound);
    }
    CFPreferencesSetValue(name.Get(), nullptr, suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    RememberOwn(k, nullptr);
    return Status();
}

void UserDefaultsConfigStore::Names(Vector<String<HeapAllocator>>* out) const
{
    out->Clear();
    if (forced_only_)
    {
        LockGuard<Mutex> lock(managed_mutex_);
        if (!managed_read_)
        {
            managed_names_.Clear();
            ManagedKeys(suite_.View(), &managed_names_);
            managed_read_ = true;
        }
        for (usize i = 0; i < managed_names_.Size(); ++i)
        {
            out->Push(String<HeapAllocator>(HeapAllocator::Default(), managed_names_[i].View()));
        }
        return;
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    if (suite.Get() == nullptr)
    {
        return;
    }
    CfRef<CFArrayRef> keys(CFPreferencesCopyKeyList(suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost));
    const CFIndex count = keys.Get() != nullptr ? CFArrayGetCount(keys.Get()) : 0;
    for (CFIndex i = 0; i < count; ++i)
    {
        const CFTypeRef item = CFArrayGetValueAtIndex(keys.Get(), i);
        char name[kMaxKeyBytes + 1];
        usize length = 0;
        if (CFGetTypeID(item) == CFStringGetTypeID() &&
            CopyUtf8(static_cast<CFStringRef>(item), name, sizeof(name), &length))
        {
            out->Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(name, length)));
        }
    }
}

bool UserDefaultsConfigStore::IsManagedName(StringView name) const
{
    LockGuard<Mutex> lock(managed_mutex_);
    if (!managed_read_)
    {
        managed_names_.Clear();
        ManagedKeys(suite_.View(), &managed_names_);
        managed_read_ = true;
    }
    for (usize i = 0; i < managed_names_.Size(); ++i)
    {
        if (managed_names_[i].View() == name)
        {
            return true;
        }
    }
    return false;
}

bool UserDefaultsConfigStore::ReadKeys(Vector<Key>* out) const
{
    out->Clear();
    if (suite_.Length() == 0)
    {
        return false;
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    if (suite.Get() == nullptr)
    {
        return false;
    }
    Vector<String<HeapAllocator>> names;
    Names(&names);
    for (usize i = 0; i < names.Size(); ++i)
    {
        Key k;
        if (Key::Parse(names[i].View(), &k) != KeyError::None)
        {
            continue;  // written by hand under a name no setting has
        }
        if (forced_only_)
        {
            CfRef<CFStringRef> name(MakeString(k.View()));
            if (name.Get() == nullptr || !CFPreferencesAppValueIsForced(name.Get(), suite.Get()))
            {
                continue;
            }
        }
        bool duplicate = false;
        for (usize j = 0; j < out->Size(); ++j)
        {
            duplicate = duplicate || (*out)[j] == k;
        }
        if (!duplicate)
        {
            out->Push(k);  // a managed key both for the user and for the machine is listed once
        }
    }
    return true;
}

bool UserDefaultsConfigStore::ReadAll(Vector<Entry>* out) const
{
    out->Clear();
    if (suite_.Length() == 0)
    {
        return false;
    }
    Vector<String<HeapAllocator>> names;
    Names(&names);
    for (usize i = 0; i < names.Size(); ++i)
    {
        Entry e;
        if (Key::Parse(names[i].View(), &e.key) != KeyError::None)
        {
            continue;  // written by hand under a name no setting has
        }
        char text[kMaxTextBytes + 1];
        usize length = 0;
        const StoreError error = ReadText(e.key, text, sizeof(text), &length);
        if (error == StoreError::NotFound)
        {
            continue;  // removed meanwhile, or (managed) not enforced
        }
        bool duplicate = false;
        for (usize j = 0; j < out->Size(); ++j)
        {
            duplicate = duplicate || (*out)[j].key == e.key;
        }
        if (duplicate)
        {
            continue;  // a managed key both for the user and for the machine
        }
        e.text = String<HeapAllocator>(HeapAllocator::Default(),
                                       error == StoreError::None ? StringView(text, length) : StringView(kUnreadable));
        out->Push(static_cast<Entry&&>(e));
    }
    Sort(Span<Entry>(out->Data(), out->Size()),
         [](const Entry& a, const Entry& b) { return a.key.View().Compare(b.key.View()) < 0; });
    return true;
}

Status UserDefaultsConfigStore::Enumerate(StringView prefix, Vector<Key>* out)
{
    if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None)
    {
        return Status::Of(StoreError::Invalid);
    }
    Vector<Key> all;
    if (!ReadKeys(&all))
    {
        return Status::Of(suite_.Length() == 0 ? StoreError::Invalid : StoreError::Io);
    }
    for (usize i = 0; i < all.Size(); ++i)
    {
        if (InPrefix(all[i].View(), prefix))
        {
            out->Push(all[i]);
        }
    }
    return Status();
}

Status UserDefaultsConfigStore::Commit(const CommitOptions&)
{
    if (read_only_)
    {
        return Status::Of(StoreError::ReadOnly);
    }
    CfRef<CFStringRef> suite(MakeString(suite_.View()));
    if (suite.Get() == nullptr || suite_.Length() == 0)
    {
        return Status::Of(StoreError::Invalid);
    }
    return CFPreferencesSynchronize(suite.Get(), kCFPreferencesCurrentUser, kCFPreferencesAnyHost)
               ? Status()
               : Status::Of(StoreError::Io);
}

void UserDefaultsConfigStore::Origin(StringView key, char* out, usize capacity) const
{
    // A managed value is not the user's to change with `defaults`: say where it is.
    FormatTo(out, capacity, "%s %.*s %.*s", forced_only_ ? "managed preferences" : "defaults",
             static_cast<int>(suite_.Length()), suite_.View().Data(), static_cast<int>(key.Length()), key.Data());
}

bool UserDefaultsConfigStore::IsPresent() const
{
    Vector<Key> keys;
    return ReadKeys(&keys) && !keys.Empty();
}

void UserDefaultsConfigStore::RememberOwn(const Key& key, const StringView* text)
{
    // listener_ and known_ are set together under mutex_, before the watch thread starts: a write made in between is
    // recorded too (asking whether the thread runs would miss it and report the write as another writer's).
    if (listener_ == nullptr)
    {
        return;  // nobody compares: nothing to keep
    }
    for (usize i = 0; i < known_.Size(); ++i)
    {
        if (known_[i].key == key)
        {
            if (text == nullptr)
            {
                known_.Remove(i);
            }
            else
            {
                known_[i].text = String<HeapAllocator>(HeapAllocator::Default(), *text);
            }
            return;
        }
    }
    if (text == nullptr)
    {
        return;
    }
    Entry e;
    e.key = key;
    e.text = String<HeapAllocator>(HeapAllocator::Default(), *text);
    usize at = 0;
    while (at < known_.Size() && known_[at].key.View().Compare(key.View()) < 0)
    {
        ++at;
    }
    known_.Insert(at, static_cast<Entry&&>(e));
}

void UserDefaultsConfigStore::CheckForChange()
{
    ChangeListener listener = nullptr;
    void* user = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);
        if (forced_only_)
        {
            LockGuard<Mutex> managed(managed_mutex_);
            managed_read_ = false;  // a profile installed or removed is a change too
        }
        Vector<Entry> now;
        if (!ReadAll(&now))
        {
            return;
        }
        bool same = now.Size() == known_.Size();
        for (usize i = 0; same && i < now.Size(); ++i)
        {
            same = now[i].key == known_[i].key && now[i].text.View() == known_[i].text.View();
        }
        if (same)
        {
            return;  // nothing, or only this store's own writes
        }
        known_ = static_cast<Vector<Entry>&&>(now);
        listener = listener_;
        user = listener_user_;
    }
    if (listener != nullptr && !stopping_.Load(MemoryOrder::Acquire))
    {
        listener(user);
    }
}

void UserDefaultsConfigStore::WatchLoop()
{
    for (;;)
    {
        {
            UniqueLock<Mutex> lock(stop_mutex_);
            if (stop_signal_.WaitForMilliseconds(lock, kWatchIntervalMs, [this] { return stop_requested_; }))
            {
                return;  // stopped
            }
        }
        CheckForChange();
    }
}

Status UserDefaultsConfigStore::SetChangeListener(ChangeListener listener, void* user)
{
    // From the listener itself (the watch thread): stopping would wait for this very call to return. Refused instead of
    // hanging.
    if (watcher_.Joinable() && ThisThread::Id() == watcher_.Id())
    {
        return Status::Of(StoreError::Invalid);
    }
    StopWatching();
    if (listener == nullptr)
    {
        return Status();
    }
    {
        LockGuard<Mutex> lock(mutex_);
        listener_ = listener;
        listener_user_ = user;
        if (!ReadAll(&known_))
        {
            known_.Clear();
        }
    }
    {
        LockGuard<Mutex> lock(stop_mutex_);
        stop_requested_ = false;
    }
    stopping_.Store(false, MemoryOrder::Release);
    watcher_ = Thread([this] { WatchLoop(); });
    return Status();
}

void UserDefaultsConfigStore::StopWatching()
{
    if (watcher_.Joinable())
    {
        stopping_.Store(true, MemoryOrder::Release);
        {
            LockGuard<Mutex> lock(stop_mutex_);
            stop_requested_ = true;
        }
        stop_signal_.NotifyAll();
        watcher_.Join();
    }
    LockGuard<Mutex> lock(mutex_);
    listener_ = nullptr;
    listener_user_ = nullptr;
    known_.Clear();
}

}  // namespace engine_placeholder::settings
