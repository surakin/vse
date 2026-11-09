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

## Cursor and pointer-lock protocols (Phase 15 Day 186)

Generated the same way, from `wayland-protocols` 1.50 with `wayland-scanner` 1.26.0, no hand edits:

| Files | Protocol XML | Used for |
|---|---|---|
| `cursor-shape-v1-*` | `staging/cursor-shape/cursor-shape-v1.xml` | Standard cursor shapes (`wp_cursor_shape_manager_v1`). |
| `tablet-v2-*` | `stable/tablet/tablet-v2.xml` | Only the interface table `cursor-shape-v1` refers to (`zwp_tablet_tool_v2`); tablets are not used. |
| `pointer-constraints-unstable-v1-*` | `unstable/pointer-constraints/pointer-constraints-unstable-v1.xml` | The pointer lock (`zwp_pointer_constraints_v1`). |
| `relative-pointer-unstable-v1-*` | `unstable/relative-pointer/relative-pointer-unstable-v1.xml` | Relative motion while locked (`zwp_relative_pointer_manager_v1`). |
| `primary-selection-unstable-v1-*` | `unstable/primary-selection/primary-selection-unstable-v1.xml` | The primary (middle-click) selection (`zwp_primary_selection_device_manager_v1`), Day 187. Generated the same way, unmodified. |

```sh
P=/usr/share/wayland-protocols
for f in staging/cursor-shape/cursor-shape-v1 stable/tablet/tablet-v2 unstable/pointer-constraints/pointer-constraints-unstable-v1 unstable/relative-pointer/relative-pointer-unstable-v1; do
  n=$(basename $f)
  wayland-scanner client-header $P/$f.xml $n-client-protocol.h
  wayland-scanner private-code  $P/$f.xml $n-protocol.c
done
```
