#!/usr/bin/env python3
"""Standard-library inventory (Phase 16 Day 207). Report only, not a gate (the gate is tools/std_usage_check, Day 220).

Scans engine/, game/, tools/, tests/ (not third_party/, build trees, docgen input fixtures), ignores comments and string literals, and
prints, per standard header and per `std::` name (and per unqualified C-library name), the occurrence count and the number of files,
then the files per top directory. Deterministic output.

  scripts/std-usage-report.py [--root DIR] [--files] [--self-test]
"""
import os, re, sys

DIRS = ["engine", "game", "tools", "tests"]
EXCLUDE = ("tests/docgen/fixtures/", "tests/phase_labels/", "tests/test_phase_script/")
EXT = (".h", ".hpp", ".cpp", ".inl", ".mm")
C_NAMES = ("strlen strcmp strncmp strcpy strncpy strcat strchr strrchr strstr memcpy memmove memset memcmp memchr snprintf vsnprintf "
           "sprintf printf fprintf fputs puts fflush getenv abort exit malloc calloc realloc free atoi atol atof strtol strtoul "
           "strtoull strtod wcslen wcscmp wmemcpy swprintf isdigit isalpha isspace tolower toupper qsort signal errno").split()
C_RE = re.compile(r"(?<![\w:.>])(" + "|".join(C_NAMES) + r")\s*\(")
STD_RE = re.compile(r"\bstd::(\w+)")
INC_RE = re.compile(r"^\s*#\s*include\s*<([a-z_0-9]+)>", re.M)

def strip(text):
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i); i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j)); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)

def scan(root):
    inc, std, cn, files = {}, {}, {}, {}
    for d in DIRS:
        for dp, dn, fn in os.walk(os.path.join(root, d)):
            dn.sort()
            for f in sorted(fn):
                if not f.endswith(EXT): continue
                p = os.path.join(dp, f); rel = os.path.relpath(p, root).replace(os.sep, "/")
                if rel.startswith(EXCLUDE) or "/third_party/" in rel: continue
                t = strip(open(p, encoding="utf-8", errors="replace").read())
                hit = False
                for table, rx, key in ((inc, INC_RE, None), (std, STD_RE, None), (cn, C_RE, None)):
                    for m in rx.finditer(t):
                        k = m.group(1); e = table.setdefault(k, [0, set()]); e[0] += 1; e[1].add(rel); hit = True
                if hit: files[rel] = 1
    return inc, std, cn, files

def show(title, table, limit=None):
    print("== %s ==" % title)
    rows = sorted(table.items(), key=lambda kv: (-kv[1][0], kv[0]))
    for k, (c, fs) in rows[:limit]:
        print("%7d %5d files  %s" % (c, len(fs), k))

def main(a):
    root = "."
    if "--root" in a: root = a[a.index("--root") + 1]
    if "--self-test" in a: return selftest()
    inc, std, cn, files = scan(root)
    show("standard headers (#include <x>)", inc)
    show("std:: names", std)
    show("unqualified C-library calls", cn)
    print("== files per top directory (with any use) ==")
    top = {}
    for f in files:
        k = "/".join(f.split("/")[:2]) if f.startswith(("engine/", "tests/", "tools/")) else f.split("/")[0]
        top[k] = top.get(k, 0) + 1
    for k in sorted(top): print("%5d  %s" % (top[k], k))
    print("TOTAL files with any use: %d" % len(files))
    if "--files" in a:
        for f in sorted(files): print(f)
    return 0

def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as r:
        os.makedirs(r + "/engine")
        open(r + "/engine/a.cpp", "w").write('#include <atomic>\n// std::mutex strlen(x)\nconst char* s = "std::thread";\nvoid f(){ std::atomic<int> a; std::memcpy(0,0,0); strlen(s); }\n')
        inc, std, cn, files = scan(r)
        assert list(inc) == ["atomic"] and sorted(std) == ["atomic", "memcpy"] and list(cn) == ["strlen"] and len(files) == 1, (inc, std, cn)
    print("std-usage-report self-test: ok"); return 0

sys.exit(main(sys.argv[1:]))
