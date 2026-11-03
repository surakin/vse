# API Reference

**Status:** skeleton. `modules.txt` (the module map `docgen reference` reads) is in place; the generated pages are committed once the headers carry `///` comments (Phase 2.5 Day 44). The reference is to be *generated* from `///`
comments on the public headers (by our own `docgen` tool, planned in
`plans/phase-02-5-docs-tooling.md`; no Doxygen); hand-written reference pages are not kept. This
index lists the modules and the headers each will be generated from.

| Module | Headers |
|---|---|
| foundation/memory | `engine/foundation/mem/*.h` |
| foundation/containers | `engine/foundation/containers/*.h` |
| foundation/strings | `engine/foundation/string/*.h` |
| foundation/math | `engine/foundation/math/*.h` |
| foundation/log | `engine/foundation/log/*.h` |
| foundation/types | `engine/foundation/types.h` |
| platform/os | `engine/foundation/os/{os,file_util,command_line,gpu_driver_probe}.h` |
| platform/window-input | `engine/foundation/os/{window,input}.h, os/linux/**` |
| jobs | `engine/jobs/*.h` |

## Header comment convention (to be enforced)

Every public type and function documents: purpose, parameters, return
value, ownership, thread-safety, and failure behavior. The job-system
headers (`engine/jobs/*.h`) are the current model.
