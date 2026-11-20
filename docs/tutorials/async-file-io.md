# Tutorial: Read a File Asynchronously from a Job

**Goal:** Read files from inside a job without blocking a worker, check the result, and read several files at once.

**You need:** [Run Work in Parallel with Jobs](parallel-jobs.md). Background: [Jobs](../guide/jobs.md) and [Files](../guide/io.md).

## Steps

1. **Create a test file.** The program makes a scratch directory with the file layer (`io::CreateTempDirectory`) and writes two small files with `io::WriteFile`.
   Nothing here uses `fopen`: all file access goes through `io::`.
2. **`co_await jobs::ReadFileAsync(scheduler, path)` inside a job.** The read runs on the scheduler's dedicated I/O thread; your job is parked and resumes on a
   worker when it finishes, so no worker sits waiting for the disk.
3. **Check the result.** The awaited `FileReadResult` has `ok`, `data` and `size`. The bytes belong to the result and are freed when it goes out of scope, so copy
   what you need or use it before the end of the block.
4. **Handle a missing file.** A path that does not exist gives `ok == false` and `size == 0`; it is not a crash. Always test `ok` first.
5. **Read many files concurrently.** Spawn one job per file under one counter. All the reads queue on the I/O thread in order, while the workers stay free for other
   work. Join with `HelpUntil`.

## Complete code

```cpp compile run standalone
#include "containers/vector.h"
#include "io/directory.h"
#include "io/file_system.h"
#include "io/path.h"
#include "jobs/async_io.h"
#include "jobs/counter.h"
#include "jobs/job.h"
#include "jobs/scheduler.h"
#include "log/output.h"
#include "sync/atomic.h"

using namespace vse;

struct Report {
    bool ok = false;
    usize size = 0;
    u8 first = 0;
};

// Steps 2-4: read one file and record what happened.
static jobs::Job LoadOne(jobs::Scheduler* scheduler, StringView path, Report* report) {
    jobs::FileReadResult file = co_await jobs::ReadFileAsync(*scheduler, path);
    report->ok = file.ok;
    report->size = file.size;
    if (file.ok && file.size > 0) {
        report->first = file.data[0];
    }
}

int main() {
    // Step 1: two files in a scratch directory.
    io::Path temp;
    io::TempDirectory(&temp);
    io::Path scratch;
    io::CreateTempDirectory(temp, "tutorial", &scratch);
    const io::Path hello = scratch / "hello.txt";
    const io::Path digits = scratch / "digits.txt";
    io::WriteFile(hello, "hello", 5);
    io::WriteFile(digits, "0123456789", 10);
    const io::Path missing = scratch / "nothing-here.txt";

    jobs::Scheduler scheduler(jobs::Scheduler::Config{3, false});

    // The views point at the paths above, which stay alive until after the join.
    // Step 5: one job per file, one counter.
    Report reports[3];
    {
        jobs::Counter counter(scheduler);
        jobs::Spawn(counter, LoadOne(&scheduler, hello.View(), &reports[0]));
        jobs::Spawn(counter, LoadOne(&scheduler, digits.View(), &reports[1]));
        jobs::Spawn(counter, LoadOne(&scheduler, missing.View(), &reports[2]));
        scheduler.HelpUntil(counter);
    }

    const char* names[3] = {"hello.txt", "digits.txt", "nothing-here.txt"};
    for (int i = 0; i < 3; ++i) {
        out::Print("%s: ok=%s size=%zu first=%c\n", names[i], reports[i].ok ? "yes" : "no", reports[i].size,
                  reports[i].ok ? static_cast<char>(reports[i].first) : '-');
    }

    io::RemoveAll(scratch);
    return 0;
}
```

## Expected output

```output
hello.txt: ok=yes size=5 first=h
digits.txt: ok=yes size=10 first=0
nothing-here.txt: ok=no size=0 first=-
```

## Where next

- [Files](../guide/io.md): the `io::File` interface, atomic writes, and the awaitables for reading and writing a file you opened yourself (`ReadAllAsync`).
- [Jobs](../guide/jobs.md): the I/O thread and its one-queue limit.
