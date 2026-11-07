#include "settings/file_store.h"

#include <cstdio>
#include <cstring>

#include "io/file.h"
#include "io/file_system.h"

namespace engine_placeholder::settings {

namespace {

StoreError FromIo(io::Error e) {
    switch (e) {
    case io::Error::None: return StoreError::None;
    case io::Error::PermissionDenied: return StoreError::PermissionDenied;
    case io::Error::ReadOnly: return StoreError::ReadOnly;
    case io::Error::TooLong: return StoreError::TooLong;
    case io::Error::Interrupted: return StoreError::Interrupted;
    default: return StoreError::Io;
    }
}

} // namespace

FileConfigStore::FileConfigStore(const io::Path& path, const FileStoreOptions& options) : path_(path), options_(options) {}

u32 FileConfigStore::Capabilities() const { return options_.read_only ? kCapPreservesOrder : (kCapWritable | kCapPreservesOrder | kCapAtomicCommit); }

void FileConfigStore::Origin(StringView key, char* out, usize capacity) const {
    if (capacity == 0) return;
    const u32 line = doc_.LineOf(key);
    if (line != 0) std::snprintf(out, capacity, "line %u", static_cast<unsigned>(line));
    else out[0] = '\0';
}

void FileConfigStore::Describe(char* out, usize capacity) const {
    if (capacity == 0) return;
    std::snprintf(out, capacity, "%.*s", static_cast<int>(path_.View().Length()), path_.View().Data());
}

Status FileConfigStore::Load() {
    Vector<u8> bytes;
    const io::Status read = io::ReadFile(path_, &bytes, options_.max_bytes);
    if (read.Is(io::Error::NotFound) || read.Is(io::Error::NotADirectory)) { // no file yet is an empty store
        load_error_ = StoreError::None;
        staged_ = 0;
        doc_.Parse(StringView(), &report_);
        on_disk_.Clear();
        exists_ = false;
        return Status();
    }
    if (read.Failed()) {
        // The store is as it was (staged changes stay staged), but what is on disk is unknown: Commit refuses until a Load succeeds, so a
        // file that could not be read is not overwritten.
        load_error_ = FromIo(read.error);
        return Status::Of(load_error_);
    }
    load_error_ = StoreError::None;
    staged_ = 0;
    on_disk_.Resize(bytes.Size());
    if (bytes.Size() != 0) std::memcpy(on_disk_.Data(), bytes.Data(), bytes.Size());
    exists_ = true;
    doc_.Parse(StringView(on_disk_.Data(), on_disk_.Size()), &report_);
    return report_.error_count != 0 ? Status::Of(StoreError::Damaged) : Status();
}

Status FileConfigStore::Get(StringView key_text, StoredValue* out) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    StringView value;
    if (!doc_.Find(key_text, &value)) return Status::Of(StoreError::NotFound);
    out->typed = false;
    if (value.Length() > kMaxTextBytes) return Status::Of(StoreError::TooLong);
    out->value = Value::FromText(value);
    return Status();
}

Status FileConfigStore::Set(StringView key_text, const StoredValue& value) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    if (options_.read_only) return Status::Of(StoreError::ReadOnly);
    if (value.typed || value.value.GetType() != Type::Text) return Status::Of(StoreError::Invalid); // a text store takes text (use ToStored)
    if (!doc_.Set(key_text, value.value.AsText())) return Status::Of(StoreError::Invalid);
    ++staged_;
    return Status();
}

Status FileConfigStore::Remove(StringView key_text) {
    Key key;
    if (Key::Parse(key_text, &key) != KeyError::None) return Status::Of(StoreError::Invalid);
    if (options_.read_only) return Status::Of(StoreError::ReadOnly);
    if (!doc_.Remove(key_text)) return Status::Of(StoreError::NotFound);
    ++staged_;
    return Status();
}

Status FileConfigStore::Enumerate(StringView prefix, Vector<Key>* out) {
    if (prefix.Length() != 0 && Key::Parse(prefix, nullptr) != KeyError::None) return Status::Of(StoreError::Invalid);
    doc_.Keys(prefix, out);
    return Status();
}

Status FileConfigStore::Commit(const CommitOptions& options) {
    Status result;
    result.total = staged_;
    if (options_.read_only) {
        result.error = StoreError::ReadOnly;
        return result;
    }
    if (staged_ == 0) return result;
    if (load_error_ != StoreError::None) {
        result.error = load_error_;
        return result;
    }
    if (Damaged() && !options.replace_damaged) {
        result.error = StoreError::Damaged;
        return result;
    }
    // Replacing a damaged file drops the lines that could not be read (the .bak keeps them), but only once the write has succeeded.
    const bool dropping = Damaged();
    Vector<char> text;
    doc_.Serialize(&text, dropping);
    if (options_.create_directories && !path_.Parent().IsEmpty()) {
        const io::Status made = io::CreateDirectories(path_.Parent());
        if (made.Failed()) {
            result.error = FromIo(made.error);
            return result;
        }
    }
    if (options_.keep_backup && exists_) {
        String<HeapAllocator> name(HeapAllocator::Default(), path_.View());
        name.Append(".bak");
        io::Path backup_path;
        if (!io::Path::Parse(name.View(), path_.Style(), &backup_path)) {
            result.error = StoreError::TooLong; // no backup, no overwrite
            return result;
        }
        const io::Status kept = io::WriteFileAtomic(backup_path, on_disk_.Data(), on_disk_.Size(), options_.permissions);
        if (kept.Failed()) {
            result.error = FromIo(kept.error);
            return result;
        }
    }
    const io::Status written = io::WriteFileAtomic(path_, text.Data(), text.Size(), options_.permissions);
    if (written.Failed()) {
        result.error = FromIo(written.error);
        return result; // the old file is intact and the changes stay staged
    }
    if (dropping) doc_.RemoveUnreadable();
    on_disk_.Resize(text.Size());
    if (text.Size() != 0) std::memcpy(on_disk_.Data(), text.Data(), text.Size());
    exists_ = true;
    report_ = ConfigReport(); // what was damaged has been replaced
    staged_ = 0;
    result.done = result.total;
    return result;
}

} // namespace engine_placeholder::settings
