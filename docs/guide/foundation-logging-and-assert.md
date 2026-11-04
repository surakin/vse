# Foundation: Logging and Assertions

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 0 (+ job system channel)

## Source of truth

- `engine/foundation/log/log.h`
- `engine/foundation/log/assert.h`

## API reference

Full reference: [Foundation: Logging and Assertions](../reference/foundation-log.md).

- [[LogChannel]] — a named, filterable log channel
- [[LogVerbosity]] — verbosity levels
- [[Log]] — write a message to a channel
- [[LoadConfig]] — configure channels from a file

## Already known — seed material (verify against the code when writing)

- Named channels (LogCore, LogRender, LogPlatform, LogJobs) with per-channel verbosity, conservative default Warn
- Config file override via LoadConfig; `--log-level` flag on the hello-window binary
- `ENGINE_ASSERT` is always enabled in every build and aborts with file/line/message
- No allocator is needed for logging

## Outline

### Channels and verbosity

_TODO_

### Configuring from a file and the command line

_TODO_

### ENGINE_ASSERT semantics

_TODO_

### Writing good messages

_TODO_

### Adding a channel

_TODO_

