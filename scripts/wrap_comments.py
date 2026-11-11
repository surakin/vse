#!/usr/bin/env python3
"""Wraps comment lines that are longer than the column limit of .clang-format (the part of the style clang-format does not do safely).

clang-format's own comment reflow joins a doc-comment label line (`Fails: never.`) onto the line before it, which docgen then no longer sees as a label
(docs/DOC-CONVENTIONS.md), so .clang-format sets `ReflowComments: false` and this script breaks only the lines that are too long:

* a comment on a line of its own (`//`, `///`, `//!`) is split at spaces into lines of the same indent and marker (a list item keeps its text aligned);
* a trailing comment (`code  // text`) that makes the line too long moves above the line, at the line's indent, and is wrapped there;
* a trailing `///<` moves above as `///` (it documents the same declaration);
* left alone: `//!<`, `NOLINT`/`SIGNAL-SAFE`/`pragma` markers, lines in fenced code blocks and tables, block comments, lines with a
  raw string, and any word longer than the limit.

Usage: scripts/wrap_comments.py [--check] FILE...      (files matched by .clang-format-ignore are skipped)
Exit status 1 with --check when a file would change.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def column_limit():
    for line in (ROOT / ".clang-format").read_text().splitlines():
        m = re.match(r"ColumnLimit:\s*(\d+)", line)
        if m:
            return int(m.group(1))
    return 120


def ignore_patterns():
    patterns = []
    path = ROOT / ".clang-format-ignore"
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            regex = re.escape(line).replace(r"\*\*", ".*").replace(r"\*", "[^/]*")
            patterns.append(re.compile("^" + regex + "$"))
    return patterns


KEEP_TRAILING = re.compile(r"NOLINT|SIGNAL-SAFE|IWYU|clang-format|LCOV|pragma|coverity|NOSONAR|^//!<")
STANDALONE = re.compile(r"^(\s*)(///|//!|//)(.*)$")
LABEL = re.compile(r"^ ?(Param [A-Za-z_0-9]+|Returns|Fails|Aborts|Thread-safety|Ownership|Example|See|Deprecated|Backend|Internal):")
LIST_ITEM = re.compile(r"^( ?)((?:[-*+]|\d+[.)]) +)")


def split_words(text):
    """Splits `text` at spaces that are not inside a backtick span or a [[link]]."""
    words, current, in_code, link = [], "", False, 0
    i = 0
    while i < len(text):
        c = text[i]
        if c == "`":
            in_code = not in_code
        elif text.startswith("[[", i):
            link += 1
        elif text.startswith("]]", i) and link:
            link -= 1
        if c == " " and not in_code and not link:
            if current:
                words.append(current)
                current = ""
        else:
            current += c
        i += 1
    if current:
        words.append(current)
    return words


def wrap(indent, marker, text, limit):
    """Returns the comment lines for `text` (which may start with a space), each at most `limit` wide where a word allows it."""
    lead = " " if text.startswith(" ") else ""
    body = text[len(lead):]
    item = LIST_ITEM.match(lead + body)
    cont = " " * (len(item.group(1)) + len(item.group(2)) - len(lead)) if item else ""
    words = split_words(body)
    if not words:
        return [indent + marker + text]
    lines, line = [], indent + marker + lead + words[0]
    for w in words[1:]:
        if len(line) + 1 + len(w) <= limit:
            line += " " + w
        else:
            lines.append(line)
            line = indent + marker + lead + cont + w
    lines.append(line)
    return lines


def comment_start(code):
    """Index of a `//` that begins a trailing comment in `code`, or -1 (outside string and character literals)."""
    i, quote = 0, ""
    while i < len(code):
        c = code[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = ""
        elif c in "\"'":
            quote = c
        elif code.startswith("//", i):
            return i
        i += 1
    return -1


def process(text, limit):
    out, in_block, in_fence = [], False, False
    for line in text.split("\n"):
        stripped = line.strip()
        if in_block:
            out.append(line)
            if "*/" in line:
                in_block = False
            continue
        cut = comment_start(line)
        code_part = line if cut < 0 else line[:cut]
        if "/*" in code_part and "*/" not in code_part.split("/*", 1)[1]:
            out.append(line)
            in_block = True
            continue
        m = STANDALONE.match(line)
        if m and stripped.startswith("//"):
            indent, marker, rest = m.groups()
            if marker == "//" and rest.startswith("/"):
                marker, rest = "///", rest[1:]
            if rest.lstrip().startswith("```"):
                in_fence = not in_fence
            if (len(line) <= limit or in_fence or rest.lstrip().startswith("|") or rest.startswith("<") or marker == "//!" and rest.startswith("<")
                    or line.endswith("\\")):
                out.append(line)
            else:
                out.extend(wrap(indent, marker, rest, limit))
            continue
        if len(line) <= limit or line.rstrip().endswith("\\") or 'R"' in line or stripped.startswith("#"):
            out.append(line)
            continue
        at = comment_start(line)
        if at < 0:
            out.append(line)
            continue
        code, comment = line[:at].rstrip(), line[at:]
        if not code.strip() or len(code) > limit or KEEP_TRAILING.search(comment.replace("///<", "///", 1)):
            out.append(line)
            continue
        indent = re.match(r"\s*", line).group(0)
        # `///<` documents the declaration on its own line; a leading `///` above it documents the same declaration.
        marker = "///" if comment.startswith("///") else "//"
        rest = comment[len(marker):]
        if rest.startswith("<"):
            rest = rest[1:]
        out.extend(wrap(indent, marker, rest, limit))
        out.append(code)
    return "\n".join(out)


def main(argv):
    check = "--check" in argv
    files = [a for a in argv if not a.startswith("--")]
    limit = column_limit()
    ignored = ignore_patterns()
    changed = []
    for name in files:
        path = Path(name)
        rel = str(path.resolve().relative_to(ROOT)) if path.is_absolute() or path.exists() else name
        if any(p.match(rel) for p in ignored):
            continue
        original = path.read_text()
        wrapped = process(original, limit)
        if wrapped != original:
            changed.append(name)
            if not check:
                path.write_text(wrapped)
    if check:
        for name in changed:
            print("comment too long: " + name, file=sys.stderr)
        return 1 if changed else 0
    print("wrap_comments: %d files changed" % len(changed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
