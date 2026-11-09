#!/usr/bin/env python3
"""Mechanical port of standard-library uses to the engine's wrappers (Phase 16). Used by Days 208-218; kept so the passes are reproducible.

  scripts/std-port.py PASS [--apply] [--include-windows] [PATH ...]

PASS is one of: lang, sync, mem, cstr, math, os. Without --apply it only prints what would change. Windows-only files (see WINDOWS_RE),
third_party, the wrapper modules themselves and the oracle tests are skipped unless named.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ["engine", "game", "tools", "tests"]
EXT = (".h", ".hpp", ".cpp", ".inl")
WINDOWS_RE = re.compile(r"(/windows/|/dx12/|win32_|_windows_test|windows_|/tests/windows)")
SKIP_PREFIX = ("engine/foundation/types.h", "engine/foundation/lang/", "engine/foundation/sync/", "engine/foundation/cstr/", "tests/lang/", "tests/sync/", "tests/cstr/",
               "tests/mem/", "tests/docgen/fixtures/", "third_party/", "tools/std_container_check/fixtures", "tools/raw_new_check/fixtures", "tools/signal_safety_check/fixtures", "tools/module_deps_check/fixtures", "tools/file_access_check/fixtures", "tools/config_access_check/fixtures", "tools/platform_neutral_check/fixtures", "tools/std_usage_report", "tests/shader_mint/", "tools/shader_mint/reflect_json")
# Files whose standard-library use is the wrapper module's own implementation (the wrapper headers), per pass.
OWN = {}

def passes():
    P = {}
    # ---- lang -------------------------------------------------------------------------------------------------------------------
    tr = {"is_same_v": "IsSame", "is_convertible_v": "IsConvertible", "is_constructible_v": "IsConstructible", "is_integral_v": "IsIntegral",
          "is_floating_point_v": "IsFloatingPoint", "is_signed_v": "IsSigned", "is_unsigned_v": "IsUnsigned", "is_enum_v": "IsEnum",
          "is_pointer_v": "IsPointer", "is_const_v": "IsConst", "is_trivially_copyable_v": "IsTriviallyCopyable",
          "is_trivially_destructible_v": "IsTriviallyDestructible", "is_trivially_default_constructible_v": "IsTriviallyDefaultConstructible",
          "is_standard_layout_v": "IsStandardLayout", "is_move_constructible_v": "IsMoveConstructible", "is_copy_constructible_v": "IsCopyConstructible",
          "is_nothrow_move_constructible_v": "IsNothrowMoveConstructible", "is_base_of_v": "IsBaseOf", "is_empty_v": "IsEmpty",
          "is_invocable_v": "IsInvocable", "enable_if_t": "EnableIf", "conditional_t": "Conditional", "remove_reference_t": "RemoveReference",
          "remove_const_t": "RemoveConst", "remove_cv_t": "RemoveCV", "remove_cvref_t": "RemoveCVRef", "remove_pointer_t": "RemovePointer",
          "decay_t": "Decay", "underlying_type_t": "UnderlyingType", "make_signed_t": "MakeSigned", "make_unsigned_t": "MakeUnsigned",
          "add_lvalue_reference_t": "AddLvalueReference", "add_pointer_t": "AddPointer", "invoke_result_t": "InvokeResult",
          "true_type": "TrueType", "false_type": "FalseType", "void_t": "VoidType", "integral_constant": "IntegralConstant",
          "is_constant_evaluated": "IsConstantEvaluated"}
    lim = {"min": "Min", "max": "Max", "lowest": "Lowest", "epsilon": "Epsilon", "infinity": "Infinity", "quiet_NaN": "QuietNaN", "denorm_min": "DenormMin"}
    limc = {"digits": "kDigits", "digits10": "kDigits10", "is_signed": "kIsSigned", "is_integer": "kIsInteger", "is_iec559": "kIsIec559", "is_specialized": "kSpecialized"}
    L = []  # (regex, replacement or callable, header)
    L.append((r"\bstd::move\b(?=\s*\()", "Move", "lang/utility.h"))
    L.append((r"\bstd::forward\b(?=\s*<)", "Forward", "lang/utility.h"))
    L.append((r"\bstd::exchange\b", "Exchange", "lang/utility.h"))
    L.append((r"\bstd::swap\b", "Swap", "lang/utility.h"))
    L.append((r"\bstd::as_const\b", "AsConst", "lang/utility.h"))
    L.append((r"\bstd::declval\b", "Declval", "lang/utility.h"))
    for k, v in tr.items():
        L.append((r"\bstd::" + k + r"\b", v, "lang/traits.h"))
    L.append((r"\bstd::numeric_limits<([^<>]*)>::(\w+)\(\)", lambda m: "Limits<%s>::%s()" % (m.group(1), lim.get(m.group(2), m.group(2))), "lang/limits.h"))
    L.append((r"\bstd::numeric_limits<([^<>]*)>::(\w+)\b", lambda m: "Limits<%s>::%s" % (m.group(1), limc.get(m.group(2), m.group(2))), "lang/limits.h"))
    L.append((r"\bstd::initializer_list\b", "InitializerList", "lang/initializer_list.h"))
    L.append((r"\bstd::strong_ordering\b", "Ordering", "lang/ordering.h"))
    L.append((r"\bstd::align_val_t\b", "AlignVal", "lang/new.h"))
    L.append((r"\bstd::nothrow_t\b", "NoThrowTag", "lang/new.h"))
    L.append((r"\bstd::nothrow\b", "kNoThrow", "lang/new.h"))
    L.append((r"\bstd::bad_alloc\b", "BadAlloc", "lang/new.h"))
    L.append((r"\bstd::launder\b", "Launder", "lang/new.h"))
    for k, v in {"size_t": "usize", "ptrdiff_t": "isize", "uint8_t": "u8", "uint16_t": "u16", "uint32_t": "u32", "uint64_t": "u64",
                 "int8_t": "i8", "int16_t": "i16", "int32_t": "i32", "int64_t": "i64", "uintptr_t": "usize", "intptr_t": "isize"}.items():
        L.append((r"\bstd::" + k + r"\b", v, "types.h"))
    P["lang"] = dict(rules=L, std_includes={"utility": "lang/utility.h", "type_traits": "lang/traits.h", "limits": "lang/limits.h",
                                              "initializer_list": "lang/initializer_list.h", "compare": "lang/ordering.h", "new": "lang/new.h",
                                              "cstddef": "types.h", "cstdint": "types.h"},
                     family={"utility": r"std::(move|forward|exchange|swap|as_const|declval|pair|make_pair|index_sequence\w*|integer_sequence|get|tuple_\w+|in_place\w*)\b",
                             "type_traits": r"std::(is_\w+|remove_\w+|add_\w+|enable_if\w*|conditional\w*|decay\w*|underlying_type\w*|make_\w+|invoke_result\w*|common_type\w*|integral_constant|true_type|false_type|void_t|bool_constant|type_identity\w*|aligned_\w+|rank\w*|extent\w*)\b",
                             "limits": r"std::numeric_limits\b", "initializer_list": r"std::initializer_list\b", "compare": r"std::(strong|weak|partial)_ordering\b",
                             "new": r"std::(align_val_t|nothrow\w*|bad_alloc|launder|new_handler|set_new_handler|destroying_delete\w*|hardware_\w+)\b",
                             "cstddef": r"std::(size_t|ptrdiff_t|nullptr_t|byte|max_align_t|offsetof)\b", "cstdint": r"std::u?int\w*_t\b"})
    # ---- sync -------------------------------------------------------------------------------------------------------------------
    S = []
    for o, n in (("relaxed", "Relaxed"), ("consume", "Consume"), ("acquire", "Acquire"), ("release", "Release"), ("acq_rel", "AcqRel"), ("seq_cst", "SeqCst")):
        S.append((r"\bstd::memory_order_" + o + r"\b", "MemoryOrder::" + n, "sync/atomic.h"))
    S.append((r"\bstd::memory_order\b", "MemoryOrder", "sync/atomic.h"))
    S.append((r"\bstd::atomic_thread_fence\b", "AtomicThreadFence", "sync/atomic.h"))
    S.append((r"\bstd::atomic_signal_fence\b", "AtomicSignalFence", "sync/atomic.h"))
    S.append((r"\bstd::atomic\b(?=\s*<)", "Atomic", "sync/atomic.h"))
    S.append((r"\bstd::recursive_mutex\b", "RecursiveMutex", "sync/mutex.h"))
    S.append((r"\bstd::mutex\b", "Mutex", "sync/mutex.h"))
    S.append((r"\bstd::lock_guard\b", "LockGuard", "sync/mutex.h"))
    S.append((r"\bstd::unique_lock\b", "UniqueLock", "sync/mutex.h"))
    S.append((r"\bstd::condition_variable\b", "ConditionVariable", "sync/mutex.h"))
    S.append((r"\bstd::once_flag\b", "OnceFlag", "sync/mutex.h"))
    S.append((r"\bstd::call_once\b", "CallOnce", "sync/mutex.h"))
    S.append((r"\bstd::this_thread::sleep_for\(\s*std::chrono::milliseconds\(([^()]*)\)\s*\)", lambda m: "ThisThread::SleepForMilliseconds(%s)" % m.group(1), "sync/thread.h"))
    S.append((r"\bstd::this_thread::sleep_for\(\s*std::chrono::microseconds\(([^()]*)\)\s*\)", lambda m: "ThisThread::SleepForMicroseconds(%s)" % m.group(1), "sync/thread.h"))
    S.append((r"\bstd::this_thread::yield\b", "ThisThread::Yield", "sync/thread.h"))
    S.append((r"\bstd::this_thread::get_id\b", "ThisThread::Id", "sync/thread.h"))
    S.append((r"\bstd::thread::id\b", "ThreadId", "sync/thread.h"))
    S.append((r"\bstd::thread::hardware_concurrency\b", "Thread::HardwareConcurrency", "sync/thread.h"))
    S.append((r"\bstd::thread\b", "Thread", "sync/thread.h"))
    post = [(r"(\.|->)load\(", r"\1Load("), (r"(\.|->)store\(", r"\1Store("), (r"(\.|->)exchange\(", r"\1Exchange("),
            (r"(\.|->)fetch_add\(", r"\1FetchAdd("), (r"(\.|->)fetch_sub\(", r"\1FetchSub("), (r"(\.|->)fetch_or\(", r"\1FetchOr("),
            (r"(\.|->)fetch_and\(", r"\1FetchAnd("), (r"(\.|->)fetch_xor\(", r"\1FetchXor("),
            (r"(\.|->)compare_exchange_weak\(", r"\1CompareExchangeWeak("), (r"(\.|->)compare_exchange_strong\(", r"\1CompareExchangeStrong("),
            (r"(\.|->)notify_all\(", r"\1NotifyAll("), (r"(\.|->)notify_one\(", r"\1NotifyOne("),
            (r"(\.|->)joinable\(", r"\1Joinable("), (r"(\.|->)join\(", r"\1Join("), (r"(\.|->)detach\(", r"\1Detach("),
            (r"(\.|->)wait\(", r"\1Wait("), (r"(\.|->)get_id\(", r"\1Id("), (r"(\.|->)try_lock\(", r"\1TryLock("), (r"(\.|->)owns_lock\(", r"\1OwnsLock(")]
    P["sync"] = dict(rules=S, post=post, std_includes={"atomic": "sync/atomic.h", "mutex": "sync/mutex.h", "condition_variable": "sync/mutex.h", "thread": "sync/thread.h"},
                     family={"atomic": r"std::(atomic\w*|memory_order\w*)\b", "mutex": r"std::(mutex|recursive_mutex|timed_mutex|shared_mutex|lock_guard|unique_lock|scoped_lock|shared_lock|condition_variable\w*|once_flag|call_once|defer_lock\w*|adopt_lock\w*|try_to_lock\w*)\b",
                             "condition_variable": r"std::(condition_variable\w*|cv_status)\b", "thread": r"std::(thread|this_thread|jthread)\b"})
    # ---- cstr (memory, C strings, formatting, numbers, varargs) -------------------------------------------------------------------
    C = []
    def bare(name):  # std::name or ::name or a bare call (not a member call and not part of another word)
        return r"(?<![\w.>:])(?:std::|::)?" + name + r"\b(?=\s*\()"
    for old, new_, hdr in (("memcpy", "MemCopy", "cstr/mem_ops.h"), ("memmove", "MemMove", "cstr/mem_ops.h"), ("memset", "MemSet", "cstr/mem_ops.h"),
                           ("memcmp", "MemCompare", "cstr/mem_ops.h"), ("memchr", "MemFind", "cstr/mem_ops.h"), ("malloc", "MemAllocate", "cstr/mem_ops.h"),
                           ("calloc", "MemAllocateZeroed", "cstr/mem_ops.h"), ("realloc", "MemReallocate", "cstr/mem_ops.h"), ("free", "MemFree", "cstr/mem_ops.h"),
                           ("strlen", "CStrLength", "cstr/cstr.h"), ("strcmp", "CStrCompare", "cstr/cstr.h"), ("strncmp", "CStrCompareN", "cstr/cstr.h"),
                           ("strstr", "CStrFind", "cstr/cstr.h"), ("strchr", "CStrFindChar", "cstr/cstr.h"), ("strrchr", "CStrFindLastChar", "cstr/cstr.h"),
                           ("strpbrk", "CStrFindAny", "cstr/cstr.h"), ("strcpy", "CStrCopy", "cstr/cstr.h"), ("strncpy", "CStrCopyN", "cstr/cstr.h"),
                           ("strcat", "CStrAppend", "cstr/cstr.h"), ("wcslen", "WStrLength", "cstr/cstr.h"), ("wcscmp", "WStrCompare", "cstr/cstr.h"),
                           ("wcsncmp", "WStrCompareN", "cstr/cstr.h"), ("wcsstr", "WStrFind", "cstr/cstr.h"), ("wcsrchr", "WStrFindLastChar", "cstr/cstr.h"),
                           ("wcscat", "WStrAppend", "cstr/cstr.h"), ("wmemcpy", "WMemCopy", "cstr/cstr.h"), ("wmemmove", "WMemMove", "cstr/cstr.h"),
                           ("snprintf", "FormatTo", "cstr/format.h"), ("vsnprintf", "VFormatTo", "cstr/format.h"), ("swprintf", "WFormatTo", "cstr/format.h"),
                           ("sscanf", "ScanFrom", "cstr/format.h"), ("atoi", "CStrToInt", "cstr/number.h"), ("atol", "CStrToLong", "cstr/number.h"),
                           ("strtol", "CStrToLong", "cstr/number.h"), ("strtoll", "CStrToLongLong", "cstr/number.h"), ("strtoul", "CStrToULong", "cstr/number.h"),
                           ("strtoull", "CStrToULongLong", "cstr/number.h"), ("strtod", "CStrToDouble", "cstr/number.h"), ("atof", "CStrToDouble", "cstr/number.h"),
                           ("strtof", "CStrToFloat", "cstr/number.h")):
        C.append((bare(old), new_, hdr))
    C.append((r"(?<![\w:])(?:std::)?va_list\b", "VarArgs", "cstr/varargs.h"))
    C.append((r"(?<![\w:])va_start\s*\(", "ENGINE_VA_START(", "cstr/varargs.h"))
    C.append((r"(?<![\w:])va_end\s*\(", "ENGINE_VA_END(", "cstr/varargs.h"))
    C.append((r"(?<![\w:])va_copy\s*\(", "ENGINE_VA_COPY(", "cstr/varargs.h"))
    P["cstr"] = dict(rules=C, noprefix={"ENGINE_VA_START(", "ENGINE_VA_END(", "ENGINE_VA_COPY("},
                     std_includes={"cstring": "cstr/cstr.h", "cwchar": "cstr/cstr.h", "cstdarg": "cstr/varargs.h", "cstdio": "cstr/format.h", "cstdlib": "cstr/number.h", "string_h": ""},
                     family={"cstring": r"(?<![\w:])(?:std::|::)(mem\w+|str\w+)\b|\bstd::(strerror)\b", "cwchar": r"\bstd::(w\w+|mbstate_t|wint_t)\b",
                             "cstdarg": r"\bstd::va_\w+\b|\bva_\w+\b", "cstdio": r"\bstd::(f\w+|printf|puts|FILE|snprintf|vsnprintf|sprintf|vprintf|vfprintf|getc|putc|putchar|rename|remove|perror|tmpfile|setvbuf|sscanf|scanf)\b|\b(stdout|stderr|stdin|FILE)\b",
                             "cstdlib": r"\bstd::(abort|exit|_Exit|getenv|system|atexit|qsort|bsearch|rand|srand|malloc|calloc|realloc|free|ato\w+|strto\w+|aligned_alloc|div|ldiv|labs|llabs|abs|setenv|putenv|at_quick_exit|quick_exit)\b"})
    # ---- os (console, process) -----------------------------------------------------------------------------------------------
    O = []
    O.append((r"\bstd::fprintf\(\s*stderr\s*,\s*", "os::PrintError(", "os/console.h"))
    O.append((r"\bstd::fprintf\(\s*stdout\s*,\s*", "os::Print(", "os/console.h"))
    O.append((r"\bstd::printf\(", "os::Print(", "os/console.h"))
    O.append((r"\bstd::fputs\(([^;()]*(?:\([^()]*\))?[^;()]*?),\s*stderr\s*\)", lambda m: "os::PrintErrorText(%s)" % m.group(1), "os/console.h"))
    O.append((r"\bstd::fputs\(([^;()]*(?:\([^()]*\))?[^;()]*?),\s*stdout\s*\)", lambda m: "os::PrintText(%s)" % m.group(1), "os/console.h"))
    O.append((r"\bstd::puts\(", "os::PrintLine(", "os/console.h"))
    O.append((r"\bstd::fwrite\(([^;()]*(?:\([^()]*\))?[^;()]*?),\s*1\s*,\s*([^;(),]*(?:\([^()]*\))?[^;(),]*),\s*stderr\s*\)", lambda m: "os::PrintErrorBytes(%s, %s)" % (m.group(1), m.group(2)), "os/console.h"))
    O.append((r"\bstd::fwrite\(([^;()]*(?:\([^()]*\))?[^;()]*?),\s*1\s*,\s*([^;(),]*(?:\([^()]*\))?[^;(),]*),\s*stdout\s*\)", lambda m: "os::PrintBytes(%s, %s)" % (m.group(1), m.group(2)), "os/console.h"))
    O.append((r"\bstd::fflush\(\s*stdout\s*\)", "os::Flush()", "os/console.h"))
    O.append((r"\bstd::fflush\(\s*stderr\s*\)", "os::FlushError()", "os/console.h"))
    O.append((r"\bstd::abort\(", "os::Abort(", "os/console.h"))
    O.append((r"\bstd::exit\(", "os::Exit(", "os/console.h"))
    O.append((r"\bstd::_Exit\(", "os::ExitNow(", "os/console.h"))
    O.append((r"\bstd::getenv\(", "os::GetEnvironmentVariable(", "os/console.h"))
    P["os"] = dict(rules=O, std_includes={"cstdio": "os/console.h", "cstdlib": "os/console.h"}, family=P["cstr"]["family"])
    # ---- math ---------------------------------------------------------------------------------------------------------------------
    M = []
    for old, new_ in (("fabs", "Abs"), ("abs", "Abs"), ("fmax", "FMax"), ("fmin", "FMin"), ("sin", "Sin"), ("cos", "Cos"), ("tan", "Tan"), ("acos", "Acos"),
                      ("asin", "Asin"), ("atan2", "Atan2"), ("sqrt", "Sqrt"), ("floor", "Floor"), ("ceil", "Ceil"), ("round", "Round"), ("lround", "LRound"),
                      ("pow", "Pow"), ("exp", "Exp"), ("log", "Ln"), ("fmod", "Fmod"), ("ldexp", "LdExp"), ("isfinite", "IsFinite"), ("isnan", "IsNaN"),
                      ("isinf", "IsInf"), ("signbit", "SignBit")):
        M.append((r"(?<![\w:])(?:::)?std::" + old + r"\b(?=\s*\()", new_, "lang/scalar.h"))
    P["math"] = dict(rules=M, std_includes={"cmath": "lang/scalar.h"}, family={"cmath": r"\bstd::(sqrt|sin|cos|tan|acos|asin|atan|atan2|pow|exp|log\w*|floor|ceil|round|lround|llround|trunc|fabs|abs|fmod|fmax|fmin|isfinite|isnan|isinf|signbit|ldexp|frexp|modf|copysign|hypot|cbrt|lerp|nearbyint|rint|expm1|log1p|sinh|cosh|tanh)\b"})
    return P

def files(args):
    out = []
    for d in DIRS:
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            dn.sort()
            for f in sorted(fn):
                if f.endswith(EXT):
                    out.append(os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, "/"))
    return out

def port_file(rel, cfg, apply, include_windows):
    path = os.path.join(ROOT, rel)
    text = open(path, encoding="utf-8").read()
    orig = text
    needed = set()
    inscope = re.search(r"namespace\s+engine_placeholder|using\s+namespace\s+engine_placeholder", text) is not None
    prefix = "" if inscope else "engine_placeholder::"
    # Protect comments and string literals from replacement by splitting them off.
    parts = re.split(r'(//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\')', text, flags=re.S)
    for i in range(0, len(parts), 2):
        seg = parts[i]
        for rx, rep, hdr in cfg["rules"]:
            pre = "" if (not callable(rep) and rep in cfg.get("noprefix", ())) else prefix
            new = re.sub(rx, (lambda m, rep=rep, pre=pre: pre + rep(m)) if callable(rep) else pre + rep, seg)
            if new != seg:
                needed.add(hdr); seg = new
        parts[i] = seg
    if needed and cfg.get("post"):
        for i in range(0, len(parts), 2):
            for rx, rep in cfg["post"]:
                parts[i] = re.sub(rx, rep, parts[i])
    text = "".join(parts)
    if not needed:
        return None
    # Which std headers can go: no remaining family use in code (outside comments/strings).
    code = "".join(p for i, p in enumerate(parts) if i % 2 == 0)
    lines = text.split("\n")
    inc_re = re.compile(r"^\s*#\s*include\s*<(\w+)>\s*$")
    have = set(l.strip() for l in lines if l.strip().startswith("#include"))
    out, placed = [], set()
    for l in lines:
        m = inc_re.match(l)
        if m and m.group(1) in cfg["std_includes"]:
            h = cfg["std_includes"][m.group(1)]
            fam = cfg["family"].get(m.group(1))
            if fam and re.search(fam, code):
                out.append(l)           # still needed for names this pass does not own
                continue
            if h in needed and h not in placed and ('#include "%s"' % h) not in have:
                out.append('#include "%s"' % h); placed.add(h)
            continue                    # dropped (or merged into an existing wrapper include)
        out.append(l)
    text = "\n".join(out)
    # Wrapper headers still missing: add after the last #include line.
    missing = [h for h in sorted(needed) if h not in placed and ('#include "%s"' % h) not in text]
    if missing:
        ls = text.split("\n")
        last = max([i for i, l in enumerate(ls) if l.lstrip().startswith("#include")] or [-1])
        ins = ['#include "%s"' % h for h in missing]
        if last < 0:
            first = next((i for i, l in enumerate(ls) if l.startswith("#pragma once")), -1)
            ls[first + 1:first + 1] = [""] + ins
        else:
            ls[last + 1:last + 1] = ins
        text = "\n".join(ls)
    if text == orig:
        return None
    if apply:
        open(path, "w", encoding="utf-8").write(text)
    return sorted(needed)

def main(a):
    if not a: print(__doc__); return 2
    name = a[0]; apply = "--apply" in a; inc_win = "--include-windows" in a
    paths = [x for x in a[1:] if not x.startswith("--")]
    cfg = passes()[name]
    n = 0
    for rel in files(paths):
        if paths and not any(rel.startswith(p) for p in paths): continue
        if rel.startswith(SKIP_PREFIX): continue
        if not inc_win and WINDOWS_RE.search("/" + rel): continue
        r = port_file(rel, cfg, apply, inc_win)
        if r:
            n += 1
            print(("ported " if apply else "would port ") + rel + "  [" + ", ".join(r) + "]")
    print("%s: %d files" % (name, n))
    return 0

sys.exit(main(sys.argv[1:]))
