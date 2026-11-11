#!/usr/bin/env python3
"""One-off migration (2026-11-11): doc-comment labels become list items.

`/// Fails: never.` becomes `/// - Fails: never.` (and the same for `//!`), for every known label at column 0 of the comment text.
A list item is the form clang-format's comment reflow leaves on a line of its own; see docs/DOC-CONVENTIONS.md. Plain `//` comments are not doc comments
and are left alone. Deleted again once the tree is migrated.

Usage: scripts/relabel_docs.py FILE...
"""
import re
import sys

LABEL = r"(?:Param [A-Za-z_][A-Za-z_0-9]*|Returns|Fails|Aborts|Thread-safety|Ownership|Example|See|Deprecated|Backend|Internal)"
LINE = re.compile(r"^(\s*(?:///|//!)) (" + LABEL + r":(?: |$))")


def main(files):
    changed = 0
    for name in files:
        text = open(name, encoding="utf-8", newline="").read()
        out, in_fence, touched = [], False, False
        for line in text.split("\n"):
            body = line.rstrip("\r")
            m = re.match(r"^\s*(?:///|//!)\s*```", body)
            if m:
                in_fence = not in_fence
            elif not in_fence:
                new = LINE.sub(r"\1 - \2", line, count=1)
                if new != line:
                    line, touched = new, True
            out.append(line)
        if touched:
            open(name, "w", encoding="utf-8", newline="").write("\n".join(out))
            changed += 1
    print("relabel_docs: %d files changed" % changed)


if __name__ == "__main__":
    main(sys.argv[1:])
