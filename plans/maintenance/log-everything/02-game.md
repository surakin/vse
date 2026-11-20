# Log everything / Day 2: demos and benches (`game/`)

**Series:** [log everything](../log-everything.md)
**Depends on:** Day 1.

## Tasks

- [ ] Rename `os::Print`, `PrintError`, `PrintText`, `PrintErrorText`, `PrintLine`, `PrintBytes`, `PrintErrorBytes`, `Flush`, `FlushError` to
  `out::...` in the 20 files of `game/` (196 calls) and add `#include "log/output.h"` (script-assisted; every file reviewed in the diff).
- [ ] Diagnostics that are really log lines (a failed load, a reload that failed, a warning) become `Log(...)` on the program's channel where a channel
  exists; results, frame statistics and help text stay output. Record in the day file which calls moved.
- [ ] The programs' `--help` texts and the settings listings are unchanged byte for byte (the golden tests of `game/*` compare them on Day 5).
- [ ] Every target in `game/` builds clean (Clang unity).

**Done:** no `os::Print*` call is left in `game/`; the tree builds.
