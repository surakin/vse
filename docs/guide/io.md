# Paths, files and directories

Status: draft (Phase 8, Day 116: paths only). The files and directories sections arrive with Days 117-120.

The file layer lives in `engine/foundation/io/` (namespace `engine_placeholder::io`). Design and reasons:
`docs/superpowers/specs/2026-11-06-file-layer-design.md`.

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
