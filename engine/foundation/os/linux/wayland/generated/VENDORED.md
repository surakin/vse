# Generated xdg-shell protocol bindings

Upstream protocol spec: `xdg-shell.xml` from the `wayland-protocols`
package (stable protocol, not unstable/xdg-shell-unstable-v5/v6).
Pinned source version: **wayland-protocols 1.49** (Arch Linux package),
generated with **wayland-scanner 1.26.0**.

License (per `xdg-shell.xml`'s embedded `<copyright>` block, carried
into the generated output): MIT/X11-style, permissive, allows
redistribution of generated bindings.

Phase 1 Day 18's design decision (see
`plans/phase-01-linux-platform/day-18-wayland-connection-registry.md`):
generate once, commit the output — the same pattern as the Unicode data
tables under `engine/foundation/string/generated/` (Days 11-12). This
keeps `wayland-scanner` and the `wayland-protocols` package as dev-time
tools only, never a build-time or runtime dependency of this repo.
`libwayland-client` (the reference client library used to actually talk
to a compositor) remains the real, already-approved dependency —
xdg-shell isn't part of the core Wayland protocol it ships bindings for.

## Regenerating (if the protocol version needs to change)

```sh
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
```

Unmodified wayland-scanner output — no hand edits to either generated
file.

## xdg-decoration-unstable-v1 (added Day 19 follow-up)

`xdg-decoration-client-protocol.h` / `xdg-decoration-protocol.c`,
generated the same way from
`/usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml`
(wayland-protocols 1.49, wayland-scanner 1.26.0), unmodified. Needed
because KWin (Plasma) only draws a titlebar/close button/resize edges
for clients that request server-side decorations through this
protocol; without it the window is completely undecorated.

```sh
wayland-scanner client-header /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h
wayland-scanner private-code  /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c
```
