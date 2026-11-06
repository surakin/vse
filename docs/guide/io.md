# Paths, files and directories

Status: draft (Phase 9 complete, Days 115-124).

The file layer lives in `engine/foundation/io/` (namespace `engine_placeholder::io`). Design and reasons:
`docs/superpowers/specs/2026-11-06-file-layer-design.md`.

## Which API do I use?

| I want to... | Use |
|---|---|
| build, split, join, compare or normalize a path | `io::Path` (no disk access; `PathBuilder<N>` when you cannot allocate) |
| read a whole file | `io::ReadFile` (into a `Vector<u8>` or an arena), `io::ReadTextFile` |
| write a whole file | `io::WriteFile`; `io::WriteFileAtomic` when a half-written file would hurt (config, caches, generated output) |
| read or write part of a file, at offsets, from several threads | `io::File::Open` + `Read`/`Write` at an offset |
| read or write without blocking a job | `File::ReadAsync`/`WriteAsync` with a completion, or `co_await jobs::ReadAsync` / `ReadAllAsync` |
| ask whether something exists, its size, its time | `io::Exists`, `io::Stat` |
| create, remove, rename or copy | `io::CreateDirectories`, `CreatePrivateDirectory`, `Remove`, `RemoveAll`, `Rename`, `Copy` |
| list a directory or walk a tree | `io::Directory` (streaming), `io::ListDirectory` (sorted), `io::WalkDirectory` |
| find the user's config, cache or data folder, the temp folder, my executable | `io::UserConfigDirectory`, `UserCacheDirectory`, `UserDataDirectory`, `TempDirectory`, `ExecutablePath` |
| read from a pak or a server | an `io::File` backend registered with `RegisterFileScheme`, opened with `File::OpenLocation` |
| write a crash report from a fault handler, or print to the console | the raw descriptor primitives in `os.h` (`os::Write`, `os::CreateFileForWriting`): the documented exceptions |

The rule, and what the `file_access_check` gate allows, is in `CLAUDE.md`. Everything below is the detail.

## Paths

`io::Path` (`engine/foundation/io/path.h`) is a value type for manipulating paths. It never touches the disk: every
operation is a function of the text, so it is cheap, thread-compatible and testable for the Windows rules on Linux.

- **Canonical form.** Text is UTF-8 with `/` as the only separator, repeated and trailing separators removed. `.` and `..`
  are kept until you call `Normalize()`. A path never ends in a separator (except a root such as `/` or `C:/`).
- **Style.** `PathStyle::Posix` or `PathStyle::Windows` decides how text is read (`\` as a separator, drive letters, UNC
  roots such as `//server/share`). The default is the style of the platform. A path built with the other style is a
  different type of value: operations between mixed styles abort, and ordering puts the styles apart.
- **Lexical only.** Symbolic links are not considered by `Normalize`, `RelativeTo` or `IsInside`; the file-system helpers
  (Day 117) resolve those.
- **Limits.** A path holds at most `kMaxPathBytes` (4096). Untrusted text goes through `Path::Parse` and `TryJoin`, which
  report failure; the constructor and `Join` are for literals and abort.
- **No heap?** `PathBuilder<N>` holds a path in `N` bytes of inline storage and reports failure when it does not fit.

`View()` is not NUL-terminated; print with a length, or use `ToNative` to get a terminated copy for a system call.

```cpp compile run
auto show = [](StringView v) { std::printf("%.*s\n", static_cast<int>(v.Length()), v.Data()); };
io::Path root("/game/assets");
io::Path texture = root / "textures" / "brick.rtex";
show(texture.View());
show(texture.Stem());
show(texture.Extension());

io::Path relative(io::PathStyle::Posix);
if (texture.RelativeTo(root, &relative)) {
    show(relative.View());
}
std::printf("%d\n", io::Path("a/b/../c").Normalize() == io::Path("a/c"));
```
```output
/game/assets/textures/brick.rtex
brick
.rtex
textures/brick.rtex
1
```

Windows paths parse on any host:

```cpp compile run
io::Path p("C:\\Users\\me\\..\\you\\Cache.TXT", io::PathStyle::Windows);
io::Path normalized = p.Normalize(); // keep the Path alive while its view is used
StringView n = normalized.View();
std::printf("%.*s\n", static_cast<int>(n.Length()), n.Data());
std::printf("%d\n", p.IsAbsolute());
std::printf("%d\n", p == io::Path("c:/users/me/../you/cache.txt", io::PathStyle::Windows));
```
```output
C:/Users/you/Cache.TXT
1
1
```

## Rules in one table

| Operation | Result |
|---|---|
| `Parent` | the path without its last component; of a root, the root; of one relative name, empty |
| `FileName`, `Stem`, `Extension` | last component; without extension; from the last dot (`.gitignore` has none, `a.` has `.`) |
| `Join(b)` | `b` replaces when absolute or when it has a different root name; empty operands are identities |
| `Normalize` | drops `.`, resolves `..` where it can, never goes above a root, empty becomes `.` |
| `RelativeTo(base)` | fails for different roots or when `base` climbs with `..`; `.` for the same path |
| `==`, `<`, `Hash` | by component; Windows folds ASCII case only |

## Errors

Nothing in the file layer throws, and nothing leaks `errno`. An operation returns an `io::Status`: an `io::Error` (`NotFound`,
`PermissionDenied`, `AlreadyExists`, `NotADirectory`, `IsADirectory`, `NotEmpty`, `NoSpace`, `TooLong`, `Interrupted`,
`Unsupported`, `Invalid`, `CrossDevice`, `SymlinkLoop`, `ReadOnly`, `Busy`, `TooManyOpenFiles`, `OutOfMemory`, `Io`) plus the
operating system's own code, kept for logs only. Branch on the enum. Codes with no portable meaning (`EIO`, `ENXIO`,
`EFAULT`, ...) collapse into `Io`. `io::ErrorName` gives a stable name for logs.

```cpp compile
io::Path p("/definitely/not/here");
io::FileInfo info;
io::Status s = io::Stat(p, &info);
if (s.Is(io::Error::NotFound)) {
    std::printf("missing (%s)\n", io::ErrorName(s.error));
}
```

## Looking at the disk

`Stat` (follows links) and `Lstat` fill an `io::FileInfo`: type, size, modification time in nanoseconds since 1970, permission bits and a
`read_only` flag for the current process. `Exists`, `IsFile` and `IsDirectory` answer the common questions. `Remove` takes a file, a
link or an empty directory; `RemoveAll` removes a tree without following links and refuses the empty path, roots and anything with
fewer than two components. `Rename` replaces an existing file atomically (same file system only); `Copy` copies bytes and permission
bits and is not atomic. `CreateDirectories` treats an existing directory as success; `CreatePrivateDirectory` makes (or tightens) a
mode 0700 directory owned by you and refuses a symbolic link. `CreateTempFile` and `CreateTempDirectory` make uniquely named, exclusive
entries (the building block of atomic replace).

`CurrentDirectory`, `Absolute` (lexical), `Canonical` (resolves links; the file must exist) and `ReadLink` resolve names.
`SetCurrentDirectory` changes the whole process and is best avoided.

## Special folders

Each takes the application name, returns a path and creates nothing: `UserConfigDirectory`, `UserCacheDirectory`,
`UserDataDirectory`, `UserStateDirectory` (on Linux `$XDG_*_HOME` when it is an absolute path, otherwise under `$HOME`; a
relative value is ignored), `TempDirectory` (`$TMPDIR` when absolute, else `/tmp`), `ExecutablePath` and `ExecutableDirectory`.
Create the directory yourself, usually with `CreateDirectories` or `CreatePrivateDirectory`.

```cpp compile
io::Path cache;
if (io::UserCacheDirectory("my_game", &cache).Ok()) {
    io::CreateDirectories(cache);
}
```

## Reading and writing files

`io::File` (`engine/foundation/io/file.h`) is an abstract interface: the same calls read a local file now and a pak entry or a
network file later. Open one with `File::Open(path, mode, flags, &file)` into a `FilePtr`.

- **Modes:** `Read` (must exist), `Write` (create, truncate), `Append` (create, writes go to the end), `ReadWrite` (must exist
  unless `kOpenCreate`). `kOpenCreateNew` fails with `AlreadyExists`; `kOpenTruncate` empties an existing file.
- **Offsets, not positions:** `Read(buffer, length, offset, &bytes_read)` and `Write(data, length, offset)` take a 64-bit offset
  and share no state, so several threads can read one file at once. The offset-less `Read`/`Write` with `Seek`/`Tell` share one
  position; use them from one thread.
- **Short reads are normal.** `Read` returns fewer bytes than asked when the file ends or the backend has a reason, and zero
  means the end. Use `ReadExact` (fails with `EndOfFile`) or `ReadAll` (reads until zero, never trusting `Size`; `/proc` files
  report size 0) when you need the whole thing. `Write` is all or an error.
- **Capabilities** (`kCapSeekable`, `kCapSizeKnown`, `kCapWritable`, `kCapAppendOnly`, ...) say what a backend supports. A network
  file may not know its size until it has been read.
- **After `Close`** every call but `Close` fails with `Invalid`; `Close` is idempotent. The destructor closes and logs a failure,
  so call `Close` yourself where an error matters (a deferred write error shows up there).
- **Where files live:** `File::Open` takes a local `Path`. `File::OpenLocation` takes text that is a path or `scheme://...`;
  `file://` is local, and `RegisterFileScheme` plugs in other backends. An unregistered scheme is `Unsupported`.

```cpp compile run
io::Path dir;
io::TempDirectory(&dir);
io::Path scratch;
io::CreateTempDirectory(dir, "guide", &scratch);
io::Path path = scratch / "notes.txt";

io::FilePtr file;
io::File::Open(path, io::OpenMode::Write, io::kOpenNone, &file);
file->Write("hello, files", 12, 0);
file->Close();

Vector<u8> bytes;
io::ReadFile(path, &bytes);
std::printf("%.*s\n", static_cast<int>(bytes.Size()), reinterpret_cast<const char*>(bytes.Data()));

io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file);
char word[6];
io::ReadExact(*file, word, 5, 7); // "files"
std::printf("%.5s\n", word);
file.Reset();
io::RemoveAll(scratch);
```
```output
hello, files
files
```

Helpers that work through any file: `ReadAll` (into a `Vector<u8>` or an arena), `ReadFile`, `ReadTextFile`, `WriteFile`.

## Replacing a file atomically

`WriteFileAtomic(path, data, length)` writes a temporary file in the same directory, flushes and syncs it, and renames it over the
target, so a reader sees the old file or the new one and a crash leaves one of them. On any failure the temporary is removed and
the old file is untouched. Use it for configuration, caches and anything a crash must not half-write. A new file gets mode 0600.

```cpp compile run
io::Path dir;
io::TempDirectory(&dir);
io::Path scratch;
io::CreateTempDirectory(dir, "guide", &scratch);
io::Path config = scratch / "settings.ini";

io::WriteFileAtomic(config, "volume = 7\n", 11);
io::WriteFileAtomic(config, "volume = 9\n", 11);

String<HeapAllocator> text(HeapAllocator::Default());
io::ReadTextFile(config, &text);
std::printf("%s", text.CStr());
io::RemoveAll(scratch);
```
```output
volume = 9
```

## Asynchronous I/O

Every `io::File` can read, write and flush asynchronously. The calls take a caller-owned `io::AsyncRequest`, so starting an operation
allocates nothing, and a completion function that is called exactly once:

```cpp
file->ReadAsync(&request, buffer, length, offset, &OnDone, user);   // returns at once
// void OnDone(void* user, const io::AsyncResult& result)  - result.status, result.bytes
```

- **Lifetime.** The request and the buffer must stay valid, and the request unmoved, until the completion has run. Do not reuse a
  request before then (inside its own completion is fine).
- **Where the completion runs.** On an unspecified thread: the executor's, the backend's own, or the caller's when the operation fails
  before it starts. Keep it short; it may destroy the file and the request.
- **Executors.** A backend that is not natively asynchronous (a local file) runs its synchronous operations on an `io::IoExecutor`.
  By default that is a built-in single thread; give files the job system's I/O thread with
  `io::SetDefaultExecutor(&scheduler.Io())`. A shut-down executor completes new requests with `Interrupted`; work it had already accepted
  still runs.
- **Cancel.** `file->Cancel(&request)` succeeds only while the request has not started; it still completes, once, with `Interrupted`. Call it
  only while the request cannot have completed: the completion may free the request.
- **Destroying a file** with requests in flight waits for them (a natively asynchronous backend completes them with `Interrupted`). A
  completion may destroy the file only when no other request on it is in flight; otherwise the destructor would wait for work queued
  behind the completion that is running it.
- **In jobs.** `co_await jobs::ReadAsync(scheduler, file, buffer, length, offset)` (also `WriteAsync`, `FlushAsync`) returns the result
  and resumes on a scheduler participant; `co_await jobs::ReadAllAsync(scheduler, file)` reads the whole file in chunks and yields the bytes.

```cpp compile run standalone
#include <cstdio>
#include "io/file.h"
#include "io/file_system.h"
#include "jobs/async_io.h"
#include "jobs/file_async.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
using namespace engine_placeholder;

static jobs::Job Load(jobs::Scheduler* s, io::File* f, std::atomic<int>* done) {
    jobs::FileReadAllResult r = co_await jobs::ReadAllAsync(*s, *f);
    std::printf("%d %zu\n", r.status.Ok() ? 1 : 0, static_cast<size_t>(r.bytes.Size()));
    done->store(1);
}

int main() {
    io::Path dir;
    io::TempDirectory(&dir);
    io::Path scratch;
    io::CreateTempDirectory(dir, "guide", &scratch);
    io::Path path = scratch / "blob";
    io::WriteFile(path, "0123456789", 10);

    jobs::Scheduler::Config config;
    config.participants = 2;
    jobs::Scheduler scheduler(config);
    io::SetDefaultExecutor(&scheduler.Io());
    {
        io::FilePtr file;
        io::File::Open(path, io::OpenMode::Read, io::kOpenNone, &file);
        std::atomic<int> done{0};
        scheduler.Submit(Load(&scheduler, file.Get(), &done));
        scheduler.HelpUntil(done, 1);
    }
    io::SetDefaultExecutor(nullptr);
    io::RemoveAll(scratch);
    return 0;
}
```
```output
1 10
```

## Writing a File backend

Implement `io::File` (`engine/foundation/io/file.h`) and nothing else: the helpers, the stream position, the atomic write and the coroutine
adapters all work through the interface. The test double `tests/io/slow_file.h` is a complete example: a file whose bytes live in memory and
whose operations complete on its own thread, after a delay, in short chunks, with an unknown size until the first read.

1. **Read** returns the bytes it has (a chunk is fine, zero means the end). **Write** is all or an error.
2. **Capabilities** are honest: report `kCapSizeKnown` only once `Size` works (it can turn on later).
3. **Natively asynchronous?** Override `StartAsync`, queue the request, and when it finishes call `CompleteAsync` exactly once. Use
   `ClaimAsync` when you start working on it (it returns false for a cancelled request: complete that one with `Interrupted`).
   Complete from another thread, not from inside `StartAsync` (except to report a failure), so chains of requests cannot recurse.
   Otherwise leave `StartAsync` alone and the base class runs your synchronous operations on the default executor.
4. **Destructor:** call `WaitForAsyncRequests()` first (a backend that completes requests on its own thread stops that thread and
   completes what is queued with `Interrupted`, then waits). Do not destroy a native-async file from inside its own completion unless your
   thread can be joined from itself.
5. **Close** is idempotent, and every operation after it fails with `Invalid`.
6. Run the conformance suite (`tests/io/file_conformance.h`: `RunReadConformance`, `RunWriteConformance`) against your backend with a factory
   that makes a file holding given bytes, and register a URL scheme with `io::RegisterFileScheme` if the file is opened by location.

## Directories and walking a tree

`io::Directory` (`engine/foundation/io/directory.h`) lists one directory without loading it: `Directory::Open(path, &dir)`, then
`dir.Next(&entry, &found)` until `found` is false. An entry has a `name` (a view valid until the next call) and a `type` (a link is
`Symlink`; the call resolves `DT_UNKNOWN` for you); `entry.Info(&info)` stats it lazily. `.` and `..` are never returned. **The order is
the file system's, not sorted**; entries added or removed while you iterate may or may not be seen, none is returned twice.

- **A stable order:** `ListDirectory(path, sorted, &items)` reads one directory into a vector, bytewise by name when `sorted`. Tools whose
  output must be reproducible (the minters) use it.
- **Walking a tree:** `WalkDirectory(root, options, visitor, user)` visits everything below `root` depth first. The visitor returns `Continue`,
  `SkipSubtree` or `Stop`. Options: `max_depth` (1 = the root's children), `follow_symlinks` (off; when on, a link back to an ancestor is
  reported with `SymlinkLoop` instead of followed), `post_order` (a directory after its children), `sorted`, and `name_pattern`.
  An entry that cannot be examined or entered is reported with `entry.error` set and the walk goes on.
- **Patterns:** `MatchesWildcard(pattern, name)`: `*` any run, `?` one character (a whole UTF-8 sequence), `\` escapes the next
  pattern character; case-sensitive; a leading dot is not special.

```cpp compile run
io::Path dir;
io::TempDirectory(&dir);
io::Path scratch;
io::CreateTempDirectory(dir, "guide", &scratch);
io::CreateDirectories(scratch / "src/util");
io::WriteFile(scratch / "src/main.cpp", "", 0);
io::WriteFile(scratch / "src/util/strings.cpp", "", 0);
io::WriteFile(scratch / "src/util/strings.h", "", 0);

struct Found { io::Path root; };
Found found{scratch};
io::WalkOptions options;
options.sorted = true;
options.name_pattern = "*.cpp";
io::WalkDirectory(scratch, options, [](void* user, const io::WalkEntry& entry) {
    io::Path relative;
    entry.path.RelativeTo(static_cast<Found*>(user)->root, &relative);
    std::printf("%.*s\n", static_cast<int>(relative.View().Length()), relative.View().Data());
    return io::WalkAction::Continue;
}, &found);
io::RemoveAll(scratch);
```
```output
src/main.cpp
src/util/strings.cpp
```
