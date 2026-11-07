// The registry store (Phase 13 Day 157): see registry_store.h for the layout and settings/BACKEND.md for the rules every store follows.
#include "settings/windows/registry_store.h"

#include "algorithm.h"
#include "containers/span.h"
#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include <cstdio>
#include <cwchar>

namespace engine_placeholder::settings {

namespace {

constexpr REGSAM kView = KEY_WOW64_64KEY; // the same values from a 32-bit and a 64-bit process
constexpr usize kMaxPathUnits = 512;      // a store path plus two sections, in UTF-16 units
constexpr usize kMaxNameUnits = 256;      // a value or subkey name the store reads (its own are at most 31 bytes)
constexpr usize kMaxValueUnits = kMaxTextBytes + 1;

HKEY Root(RegistryHive hive) {
    return hive == RegistryHive::LocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

const char* HiveName(RegistryHive hive) {
    return hive == RegistryHive::LocalMachine ? "HKLM" : "HKCU";
}

StoreError FromWin32(LSTATUS code) {
    switch (code) {
    case ERROR_SUCCESS: return StoreError::None;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return StoreError::NotFound;
    case ERROR_ACCESS_DENIED: return StoreError::PermissionDenied;
    default: return StoreError::Io;
    }
}

// The subkey path of a key's section(s) under `root_path`, and its value name: `renderer.sub.width` is `<root>\renderer\sub` and `width`.
bool Locate(StringView root_path, const Key& key, wchar_t* subkey, wchar_t* name) {
    const StringView text = key.View();
    const usize last_dot = text.LastIndexOf('.');
    char path[kMaxPathUnits];
    usize n = 0;
    auto put = [&](char c) {
        if (n + 1 < sizeof(path)) path[n++] = c;
    };
    for (usize i = 0; i < root_path.Length(); ++i) put(root_path[i]);
    if (last_dot != text.Length()) {
        put('\\');
        for (usize i = 0; i < last_dot; ++i) put(text[i] == '.' ? '\\' : text[i]);
    }
    if (n + 1 >= sizeof(path)) return false;
    path[n] = '\0';
    const StringView value_name = last_dot == text.Length() ? text : text.Substring(last_dot + 1);
    return os::windows::Utf8ToUtf16(StringView(path, n), subkey, kMaxPathUnits) < kMaxPathUnits &&
           os::windows::Utf8ToUtf16(value_name, name, kMaxNameUnits) < kMaxNameUnits;
}

bool ToWide(StringView text, wchar_t* out, usize capacity) {
    return os::windows::Utf8ToUtf16(text, out, capacity) < capacity;
}

// Reads one value of `key` (an open handle): the text of a REG_SZ, or NotFound / TypeMismatch / TooLong.
StoreError ReadString(HKEY key, const wchar_t* name, char* text, usize capacity, usize* length) {
    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS rc = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS) return FromWin32(rc);
    if (type != REG_SZ) return StoreError::TypeMismatch;
    wchar_t wide[kMaxValueUnits + 1];
    if (bytes > sizeof(wide) - sizeof(wchar_t)) return StoreError::TooLong;
    DWORD got = static_cast<DWORD>(sizeof(wide) - sizeof(wchar_t));
    rc = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(wide), &got);
    if (rc != ERROR_SUCCESS) return FromWin32(rc);
    if (type != REG_SZ) return StoreError::TypeMismatch;
    usize units = got / sizeof(wchar_t);
    while (units > 0 && wide[units - 1] == 0) --units; // the terminator, if the writer stored one
    const usize n = os::windows::Utf16ToUtf8(wide, units, text, capacity);
    if (n >= capacity || n > kMaxTextBytes) return StoreError::TooLong;
    *length = n;
    return StoreError::None;
}

} // namespace

void RegistrySettingsPath(StringView app, char* out, usize capacity) {
    if (capacity == 0) return;
    std::snprintf(out, capacity, "Software\\%.*s", static_cast<int>(app.Length()), app.Data());
}

RegistryConfigStore::RegistryConfigStore(const RegistryStoreOptions& options)
    : hive_(options.hive), read_only_(options.read_only), path_(HeapAllocator::Default(), StringView(options.path != nullptr ? options.path : "")) {}

RegistryConfigStore::~RegistryConfigStore() { StopWatching(); }

u32 RegistryConfigStore::Capabilities() const {
    return (read_only_ ? 0u : kCapWritable) | kCapPerKeyWrites | kCapNotifies;
}

void RegistryConfigStore::Describe(char* out, usize capacity) const {
    if (capacity == 0) return;
    std::snprintf(out, capacity, "registry %s\\%s", HiveName(hive_), path_.CStr());
}

Status RegistryConfigStore::Load() {
    wchar_t root[kMaxPathUnits];
    if (!ToWide(path_.View(), root, kMaxPathUnits)) return Status::Of(StoreError::TooLong);
    HKEY key = nullptr;
    const LSTATUS rc = RegOpenKeyExW(Root(hive_), root, 0, KEY_READ | kView, &key);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) return Status(); // not written yet: empty
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    RegCloseKey(key);
    return Status();
}

Status RegistryConfigStore::Get(StringView key, StoredValue* out) {
    Key k;
    if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
    wchar_t subkey[kMaxPathUnits];
    wchar_t name[kMaxNameUnits];
    if (!Locate(path_.View(), k, subkey, name)) return Status::Of(StoreError::TooLong);
    HKEY handle = nullptr;
    const LSTATUS rc = RegOpenKeyExW(Root(hive_), subkey, 0, KEY_QUERY_VALUE | kView, &handle);
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    char text[kMaxTextBytes + 1];
    usize length = 0;
    const StoreError error = ReadString(handle, name, text, sizeof(text), &length);
    RegCloseKey(handle);
    if (error != StoreError::None) return Status::Of(error);
    out->typed = false;
    out->value = Value::FromText(StringView(text, length));
    return Status();
}

Status RegistryConfigStore::Set(StringView key, const StoredValue& value) {
    if (read_only_) return Status::Of(StoreError::ReadOnly);
    Key k;
    if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
    if (value.typed || value.value.GetType() != Type::Text) return Status::Of(StoreError::Invalid); // a text store: ToStored(..., false, ...)
    wchar_t subkey[kMaxPathUnits];
    wchar_t name[kMaxNameUnits];
    wchar_t data[kMaxValueUnits + 1];
    const StringView text = value.value.AsText();
    if (!Locate(path_.View(), k, subkey, name)) return Status::Of(StoreError::TooLong);
    const usize units = os::windows::Utf8ToUtf16(text, data, kMaxValueUnits + 1);
    if (units > kMaxValueUnits) return Status::Of(StoreError::TooLong);
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY handle = nullptr;
    LSTATUS rc = RegCreateKeyExW(Root(hive_), subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | kView, nullptr, &handle, nullptr);
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    rc = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>((units + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    RememberOwn(k, &text);
    return Status();
}

Status RegistryConfigStore::Remove(StringView key) {
    if (read_only_) return Status::Of(StoreError::ReadOnly);
    Key k;
    if (Key::Parse(key, &k) != KeyError::None) return Status::Of(StoreError::Invalid);
    wchar_t subkey[kMaxPathUnits];
    wchar_t name[kMaxNameUnits];
    if (!Locate(path_.View(), k, subkey, name)) return Status::Of(StoreError::TooLong);
    std::lock_guard<std::mutex> lock(mutex_);
    HKEY handle = nullptr;
    LSTATUS rc = RegOpenKeyExW(Root(hive_), subkey, 0, KEY_SET_VALUE | kView, &handle);
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    rc = RegDeleteValueW(handle, name);
    RegCloseKey(handle);
    if (rc != ERROR_SUCCESS) return Status::Of(FromWin32(rc));
    RememberOwn(k, nullptr);
    // The section subkeys the value leaves empty go too (deepest first; the store's own root stays).
    wchar_t root[kMaxPathUnits];
    if (!ToWide(path_.View(), root, kMaxPathUnits)) return Status();
    const usize root_length = std::wcslen(root);
    for (usize length = std::wcslen(subkey); length > root_length;) {
        HKEY section = nullptr;
        if (RegOpenKeyExW(Root(hive_), subkey, 0, KEY_READ | kView, &section) != ERROR_SUCCESS) break;
        DWORD subkeys = 0;
        DWORD values = 0;
        const bool empty = RegQueryInfoKeyW(section, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr) ==
                               ERROR_SUCCESS &&
                           subkeys == 0 && values == 0;
        RegCloseKey(section);
        if (!empty || RegDeleteKeyExW(Root(hive_), subkey, kView, 0) != ERROR_SUCCESS) break;
        while (length > root_length && subkey[length - 1] != L'\\') --length;
        if (length <= root_length) break;
        subkey[--length] = 0; // drop the trailing separator: the parent section
    }
    return Status();
}

Status RegistryConfigStore::Enumerate(StringView prefix, Vector<Key>* out) {
    if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None) return Status::Of(StoreError::Invalid);
    Vector<Entry> all;
    if (!ReadAll(&all)) return Status::Of(StoreError::Io);
    for (usize i = 0; i < all.Size(); ++i) {
        const StringView k = all[i].key.View();
        if (prefix.Length() == 0 || k == prefix || (k.Length() > prefix.Length() && k.StartsWith(prefix) && k[prefix.Length()] == '.')) out->Push(all[i].key);
    }
    return Status();
}

Status RegistryConfigStore::Commit(const CommitOptions&) {
    return read_only_ ? Status::Of(StoreError::ReadOnly) : Status(); // every write was durable when it returned
}

void RegistryConfigStore::Origin(StringView key, char* out, usize capacity) const {
    if (capacity == 0) return;
    Key k;
    if (Key::Parse(key, &k) != KeyError::None) {
        out[0] = '\0';
        return;
    }
    const StringView section = k.Segments() > 1 ? k.View().Substring(0, k.View().LastIndexOf('.')) : StringView();
    char sections[kMaxKeyBytes + 1];
    usize n = 0;
    for (usize i = 0; i < section.Length() && n + 1 < sizeof(sections); ++i) sections[n++] = section[i] == '.' ? '\\' : section[i];
    sections[n] = '\0';
    std::snprintf(out, capacity, "%s\\%s%s%s, value %.*s", HiveName(hive_), path_.CStr(), n != 0 ? "\\" : "", sections,
                  static_cast<int>(k.Name().Length()), k.Name().Data());
}

bool RegistryConfigStore::IsPresent() const {
    wchar_t root[kMaxPathUnits];
    if (!ToWide(path_.View(), root, kMaxPathUnits)) return false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(Root(hive_), root, 0, KEY_READ | kView, &key) != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

namespace {

// Appends the values under `key` (named `prefix` so far) and, `depth` levels deeper at most, its subkeys' values. A value that is not a
// REG_SZ is recorded with a marker text naming its type, so a change of type is a change too.
void Walk(HKEY key, const char* prefix, usize prefix_length, int depth, Vector<Key>* keys, Vector<String<HeapAllocator>>* texts) {
    for (DWORD index = 0;; ++index) {
        wchar_t name[kMaxNameUnits];
        DWORD name_units = kMaxNameUnits;
        DWORD type = 0;
        const LSTATUS rc = RegEnumValueW(key, index, name, &name_units, nullptr, &type, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue; // a name too long for a key, or a value that went away meanwhile
        char full[kMaxKeyBytes + 1];
        usize n = prefix_length;
        if (n >= sizeof(full)) continue;
        for (usize i = 0; i < n; ++i) full[i] = prefix[i];
        if (n != 0) full[n++] = '.';
        const usize name_bytes = os::windows::Utf16ToUtf8(name, name_units, full + n, sizeof(full) - n);
        if (n + name_bytes >= sizeof(full)) continue;
        Key k;
        if (Key::Parse(StringView(full, n + name_bytes), &k) != KeyError::None) continue; // not one of ours (an upper-case name, a space)
        char text[kMaxTextBytes + 1];
        usize length = 0;
        const StoreError error = ReadString(key, name, text, sizeof(text), &length);
        keys->Push(k);
        if (error == StoreError::None) {
            texts->Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(text, length)));
        } else {
            char marker[32];
            const int m = std::snprintf(marker, sizeof(marker), "\x01type %lu error %d", static_cast<unsigned long>(type), static_cast<int>(error));
            texts->Push(String<HeapAllocator>(HeapAllocator::Default(), StringView(marker, static_cast<usize>(m))));
        }
    }
    if (depth == 0) return;
    for (DWORD index = 0;; ++index) {
        wchar_t name[kMaxNameUnits];
        DWORD name_units = kMaxNameUnits;
        const LSTATUS rc = RegEnumKeyExW(key, index, name, &name_units, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;
        char sub_prefix[kMaxKeyBytes + 1];
        usize n = prefix_length;
        if (n >= sizeof(sub_prefix)) continue;
        for (usize i = 0; i < n; ++i) sub_prefix[i] = prefix[i];
        if (n != 0) sub_prefix[n++] = '.';
        const usize bytes = os::windows::Utf16ToUtf8(name, name_units, sub_prefix + n, sizeof(sub_prefix) - n);
        if (n + bytes >= sizeof(sub_prefix)) continue;
        HKEY child = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ | kView, &child) != ERROR_SUCCESS) continue;
        Walk(child, sub_prefix, n + bytes, depth - 1, keys, texts);
        RegCloseKey(child);
    }
}

} // namespace

bool RegistryConfigStore::ReadAll(Vector<Entry>* out) const {
    out->Clear();
    wchar_t root[kMaxPathUnits];
    if (!ToWide(path_.View(), root, kMaxPathUnits)) return false;
    HKEY key = nullptr;
    const LSTATUS rc = RegOpenKeyExW(Root(hive_), root, 0, KEY_READ | kView, &key);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) return true; // nothing written yet
    if (rc != ERROR_SUCCESS) return false;
    Vector<Key> keys;
    Vector<String<HeapAllocator>> texts;
    Walk(key, "", 0, 2, &keys, &texts); // a key has three segments at most: values at the root and two levels of sections
    RegCloseKey(key);
    Vector<usize> order;
    for (usize i = 0; i < keys.Size(); ++i) order.Push(i);
    Sort(Span<usize>(order.Data(), order.Size()), [&](usize a, usize b) { return keys[a].View().Compare(keys[b].View()) < 0; });
    for (usize i = 0; i < order.Size(); ++i) {
        Entry e;
        e.key = keys[order[i]];
        e.text = String<HeapAllocator>(HeapAllocator::Default(), texts[order[i]].View());
        out->Push(static_cast<Entry&&>(e));
    }
    return true;
}

void RegistryConfigStore::RememberOwn(const Key& key, const StringView* text) {
    if (!watcher_.joinable()) return; // nobody compares: nothing to keep
    for (usize i = 0; i < known_.Size(); ++i) {
        if (known_[i].key == key) {
            if (text == nullptr) {
                known_.Remove(i);
            } else {
                known_[i].text = String<HeapAllocator>(HeapAllocator::Default(), *text);
            }
            return;
        }
    }
    if (text == nullptr) return;
    Entry e;
    e.key = key;
    e.text = String<HeapAllocator>(HeapAllocator::Default(), *text);
    usize at = 0;
    while (at < known_.Size() && known_[at].key.View().Compare(key.View()) < 0) ++at;
    known_.Insert(at, static_cast<Entry&&>(e));
}

void RegistryConfigStore::CheckForChange() {
    ChangeListener listener = nullptr;
    void* user = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Vector<Entry> now;
        if (!ReadAll(&now)) return;
        bool same = now.Size() == known_.Size();
        for (usize i = 0; same && i < now.Size(); ++i) same = now[i].key == known_[i].key && now[i].text.View() == known_[i].text.View();
        if (same) return; // nothing, or only this store's own writes
        known_ = static_cast<Vector<Entry>&&>(now);
        listener = listener_;
        user = listener_user_;
    }
    if (listener != nullptr && !stopping_.load(std::memory_order_acquire)) listener(user);
}

void RegistryConfigStore::WatchLoop() {
    const HANDLE stop = static_cast<HANDLE>(stop_event_);
    const HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (changed == nullptr) return;
    wchar_t root[kMaxPathUnits];
    const bool named = ToWide(path_.View(), root, kMaxPathUnits);
    while (named && !stopping_.load(std::memory_order_acquire)) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(Root(hive_), root, 0, KEY_NOTIFY | KEY_READ | kView, &key) != ERROR_SUCCESS) {
            // The key does not exist yet: look again in a moment (a write from another process creates it).
            if (WaitForSingleObject(stop, 250) == WAIT_OBJECT_0) break;
            CheckForChange(); // a key created meanwhile, with values, is a change
            continue;
        }
        // Armed first, then compared: a change between the two is seen by the comparison or fires the event.
        const LSTATUS armed = RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        CheckForChange();
        if (armed != ERROR_SUCCESS) {
            RegCloseKey(key);
            if (WaitForSingleObject(stop, 250) == WAIT_OBJECT_0) break;
            continue;
        }
        const HANDLE waits[2] = {stop, changed};
        const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        RegCloseKey(key); // also ends a pending notification
        if (woke != WAIT_OBJECT_0 + 1) break; // stopped (or the wait failed)
        CheckForChange();
    }
    CloseHandle(changed);
}

Status RegistryConfigStore::SetChangeListener(ChangeListener listener, void* user) {
    // From the listener itself (the watch thread): stopping would wait for this very call to return. Refused instead of hanging.
    if (watcher_.joinable() && std::this_thread::get_id() == watcher_.get_id()) return Status::Of(StoreError::Invalid);
    StopWatching();
    if (listener == nullptr) return Status();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listener_ = listener;
        listener_user_ = user;
        if (!ReadAll(&known_)) known_.Clear();
    }
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) return Status::Of(StoreError::Io);
    stopping_.store(false, std::memory_order_release);
    watcher_ = std::thread([this] { WatchLoop(); });
    return Status();
}

void RegistryConfigStore::StopWatching() {
    if (watcher_.joinable()) {
        stopping_.store(true, std::memory_order_release);
        SetEvent(static_cast<HANDLE>(stop_event_));
        watcher_.join();
    }
    if (stop_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = nullptr;
    listener_user_ = nullptr;
    known_.Clear();
}

} // namespace engine_placeholder::settings
