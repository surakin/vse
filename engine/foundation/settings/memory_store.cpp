#include "settings/memory_store.h"

#include "cstr/format.h"
#include "sync/mutex.h"

namespace engine_placeholder::settings {

namespace {

bool UnderPrefix(const Key& key, StringView prefix) {
    if (prefix.Length() == 0) return true;
    const StringView k = key.View();
    return k == prefix || (k.Length() > prefix.Length() && k.StartsWith(prefix) && k[prefix.Length()] == '.');
}

void CopyDescription(const char* text, char* out, usize capacity) {
    if (capacity == 0) return;
    FormatTo(out, capacity, "%s", text);
}

} // namespace

usize MemoryBacking::Size() const {
    LockGuard<Mutex> lock(mutex_);
    return entries_.Size();
}

MemoryBackingPtr MakeMemoryBacking() { return MakeShared<MemoryBacking>(); }

MemoryConfigStore::MemoryConfigStore() : backing_(MakeMemoryBacking()) {
    LockGuard<RecursiveMutex> lock(backing_->notify_mutex_);
    backing_->stores_.Push(this);
}

MemoryConfigStore::MemoryConfigStore(MemoryBackingPtr backing) : backing_(static_cast<MemoryBackingPtr&&>(backing)) {
    LockGuard<RecursiveMutex> lock(backing_->notify_mutex_);
    backing_->stores_.Push(this);
    // The working copy starts from the durable state, as after a Load.
    LockGuard<Mutex> data(backing_->mutex_);
    for (usize i = 0; i < backing_->entries_.Size(); ++i) working_.Push(backing_->entries_[i]);
}

MemoryConfigStore::~MemoryConfigStore() {
    LockGuard<RecursiveMutex> lock(backing_->notify_mutex_); // no listener call is running on this store after this returns
    for (usize i = 0; i < backing_->stores_.Size(); ++i) {
        if (backing_->stores_[i] == this) {
            backing_->stores_.Remove(i);
            break;
        }
    }
}

u32 MemoryConfigStore::Capabilities() const {
    return kCapWritable | kCapPreservesOrder | kCapAtomicCommit | kCapNotifies | kCapTypedValues;
}

void MemoryConfigStore::Describe(char* out, usize capacity) const { CopyDescription("memory", out, capacity); }

usize MemoryConfigStore::FindWorking(const Key& key) const {
    for (usize i = 0; i < working_.Size(); ++i) {
        if (working_[i].key == key) return i;
    }
    return working_.Size();
}

void MemoryConfigStore::Stage(const Op& op) {
    // Last write wins per key, so the log is bounded by the number of distinct keys however often a setting is changed.
    for (usize i = 0; i < staged_.Size(); ++i) {
        if (staged_[i].entry.key == op.entry.key) {
            staged_.Remove(i);
            break;
        }
    }
    staged_.Push(op);
}

Status MemoryConfigStore::Load() {
    working_.Clear();
    LockGuard<Mutex> lock(backing_->mutex_);
    for (usize i = 0; i < backing_->entries_.Size(); ++i) working_.Push(backing_->entries_[i]);
    staged_.Clear();
    return Status();
}

Status MemoryConfigStore::Get(StringView key_text, StoredValue* out) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    const usize at = FindWorking(key);
    if (at == working_.Size()) return Status::Of(StoreError::NotFound);
    *out = working_[at].value;
    return Status();
}

Status MemoryConfigStore::Set(StringView key_text, const StoredValue& value) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    const usize at = FindWorking(key);
    if (at == working_.Size()) {
        MemoryBacking::Entry e;
        e.key = key;
        e.value = value;
        working_.Push(e);
    } else {
        working_[at].value = value;
    }
    Op op;
    op.entry.key = key;
    op.entry.value = value;
    Stage(op);
    return Status();
}

Status MemoryConfigStore::Remove(StringView key_text) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    const usize at = FindWorking(key);
    if (at == working_.Size()) return Status::Of(StoreError::NotFound);
    working_.Remove(at);
    Op op;
    op.remove = true;
    op.entry.key = key;
    Stage(op);
    return Status();
}

Status MemoryConfigStore::Enumerate(StringView prefix, Vector<Key>* out) {
    if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None) return Status::Of(StoreError::Invalid);
    for (usize i = 0; i < working_.Size(); ++i) {
        if (UnderPrefix(working_[i].key, prefix)) out->Push(working_[i].key);
    }
    return Status();
}

Status MemoryConfigStore::Commit(const CommitOptions&) {
    Status s;
    s.total = static_cast<u32>(staged_.Size());
    s.done = s.total;
    if (staged_.Empty()) return s;
    {
        LockGuard<Mutex> lock(backing_->mutex_);
        for (usize i = 0; i < staged_.Size(); ++i) {
            const Op& op = staged_[i];
            usize at = 0;
            while (at < backing_->entries_.Size() && !(backing_->entries_[at].key == op.entry.key)) ++at;
            if (op.remove) {
                if (at < backing_->entries_.Size()) backing_->entries_.Remove(at);
            } else if (at < backing_->entries_.Size()) {
                backing_->entries_[at].value = op.entry.value;
            } else {
                backing_->entries_.Push(op.entry);
            }
        }
    }
    staged_.Clear();
    // Tell the other stores on this backing (not this one) that the durable state changed.
    LockGuard<RecursiveMutex> lock(backing_->notify_mutex_);
    for (usize i = 0; i < backing_->stores_.Size(); ++i) {
        MemoryConfigStore* other = backing_->stores_[i];
        if (other != this && other->listener_ != nullptr) other->listener_(other->listener_user_);
    }
    return s;
}

Status MemoryConfigStore::SetChangeListener(ChangeListener listener, void* user) {
    LockGuard<RecursiveMutex> lock(backing_->notify_mutex_);
    listener_ = listener;
    listener_user_ = user;
    return Status();
}

ReadOnlyConfigStore::ReadOnlyConfigStore(const DefaultEntry* entries, usize count) : entries_(entries), count_(count) {}

u32 ReadOnlyConfigStore::Capabilities() const { return kCapPreservesOrder; }

void ReadOnlyConfigStore::Describe(char* out, usize capacity) const { CopyDescription("read-only defaults", out, capacity); }

Status ReadOnlyConfigStore::Load() { return Status(); }

Status ReadOnlyConfigStore::Get(StringView key_text, StoredValue* out) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    for (usize i = 0; i < count_; ++i) {
        if (entries_[i].key != nullptr && entries_[i].text != nullptr && StringView(entries_[i].key) == key_text) { // the first entry for a key wins
            out->typed = false;
            out->value = Value::FromText(StringView(entries_[i].text));
            return Status();
        }
    }
    return Status::Of(StoreError::NotFound);
}

Status ReadOnlyConfigStore::Set(StringView, const StoredValue&) { return Status::Of(StoreError::ReadOnly); }
Status ReadOnlyConfigStore::Remove(StringView) { return Status::Of(StoreError::ReadOnly); }
Status ReadOnlyConfigStore::Commit(const CommitOptions&) { return Status::Of(StoreError::ReadOnly); }

Status ReadOnlyConfigStore::Enumerate(StringView prefix, Vector<Key>* out) {
    if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None) return Status::Of(StoreError::Invalid);
    for (usize i = 0; i < count_; ++i) {
        Key key;
        if (entries_[i].key == nullptr || entries_[i].text == nullptr || Key::Parse(StringView(entries_[i].key), &key) != KeyError::None || !UnderPrefix(key, prefix)) continue;
        bool repeated = false;
        for (usize j = 0; j < i && !repeated; ++j) repeated = entries_[j].key != nullptr && entries_[j].text != nullptr && StringView(entries_[j].key) == key.View();
        if (!repeated) out->Push(key);
    }
    return Status();
}

} // namespace engine_placeholder::settings
