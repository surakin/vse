// Post-mortem dumps on Windows (Phase 13 Day 156; os/windows/debug/BACKEND.md, EnablePostMortemDumps): Windows Error
// Reporting's LocalDumps, the dump WER writes for a process that dies of an unhandled exception. WER reads them only
// from the machine's key
//   HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps[\<program>.exe]   DumpFolder, DumpType,
//   DumpCount
// (measured on Day 156: the same values under HKCU were ignored, though WER ran and queued its report). Writing HKLM
// needs an administrator and changes every user's machine, so this function writes nothing: it reports whether a dump
// will be written and where, or, as Limited, which values to set. Nothing the process does here lasts beyond it (as on
// Linux).
#include "debug/post_mortem.h"
#include "os/windows/debug/post_mortem_windows.h"

#include "os/windows/utf16.h"
#include "os/windows/win32.h"

#include "cstr/cstr.h"
#include "cstr/format.h"

namespace vse::debug
{

namespace
{

constexpr wchar_t kWerKey[] = L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting";
constexpr wchar_t kLocalDumpsKey[] = L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps";
constexpr DWORD kMiniDump = 1;

// Reads a DWORD value; false if there is none.
bool ReadDword(HKEY root, const wchar_t* key, const wchar_t* name, DWORD* out)
{
    DWORD bytes = sizeof(*out);
    return RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, out, &bytes) == ERROR_SUCCESS;
}

bool KeyExists(HKEY root, const wchar_t* key)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &handle) != ERROR_SUCCESS)
    {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

}  // namespace

namespace detail
{

bool DescribeLocalDumps(HKEY root, const wchar_t* local_dumps_key, const wchar_t* exe_name, PostMortemReport* report,
                        u32* dump_type_out)
{
    char name_utf8[MAX_PATH * 3];
    os::windows::Utf16ToUtf8(exe_name, WStrLength(exe_name), name_utf8, sizeof(name_utf8));
    // The program's own key wins over the one for every program, as WER reads them. A name too long for the buffer has
    // no key of its own.
    wchar_t program_key[1024];
    const int key_length = WFormatTo(program_key, sizeof(program_key) / sizeof(program_key[0]), L"%ls\\%ls",
                                     local_dumps_key, exe_name);
    const bool program_key_exists = key_length > 0 && KeyExists(root, program_key);
    const wchar_t* key = program_key_exists ? program_key
                                            : (KeyExists(root, local_dumps_key) ? local_dumps_key : nullptr);
    if (key == nullptr)
    {
        FormatTo(report->destination, sizeof(report->destination),
                 "none: Windows Error Reporting keeps no local dump of %s", name_utf8);
        FormatTo(
            report->note, sizeof(report->note),
            "an administrator can create HKLM\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\%s with DumpFolder, "
            "DumpType (2: full) and DumpCount",
            name_utf8);
        return false;
    }
    // Where the dumps go: DumpFolder with its variables expanded (read unexpanded, then expanded here), or WER's
    // default.
    wchar_t raw[1024] = {};
    DWORD raw_bytes = sizeof(raw) - sizeof(wchar_t);
    wchar_t folder[1024] = {};
    char folder_utf8[1024];
    if (RegGetValueW(root, key, L"DumpFolder", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, raw,
                     &raw_bytes) == ERROR_SUCCESS)
    {
        const DWORD expanded = ExpandEnvironmentStringsW(raw, folder,
                                                         static_cast<DWORD>(sizeof(folder) / sizeof(folder[0])));
        const wchar_t* shown = expanded > 0 && expanded <= sizeof(folder) / sizeof(folder[0]) ? folder : raw;
        os::windows::Utf16ToUtf8(shown, WStrLength(shown), folder_utf8, sizeof(folder_utf8));
    }
    else
    {
        FormatTo(folder_utf8, sizeof(folder_utf8), "%%LOCALAPPDATA%%\\CrashDumps");
    }
    DWORD dump_type = kMiniDump;  // WER's default when DumpType is absent
    ReadDword(root, key, L"DumpType", &dump_type);
    FormatTo(report->destination, sizeof(report->destination), "Windows Error Reporting LocalDumps: %s (%s dump)",
             folder_utf8,
             dump_type == 2   ? "full"
             : dump_type == 0 ? "custom"
                              : "mini");
    report->previous_limit_bytes = dump_type == 2 ? ~u64{0}
                                                  : 0;  // a full dump has no limit; a minidump's size is WER's choice
    report->current_limit_bytes = report->previous_limit_bytes;
    *dump_type_out = dump_type;
    return true;
}

}  // namespace detail

PostMortemReport EnablePostMortemDumps(const PostMortemOptions& options)
{
    PostMortemReport report;
    report.result = PostMortemResult::Limited;

    wchar_t exe[MAX_PATH * 4];
    const DWORD exe_length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0])));
    if (exe_length == 0 || exe_length >= sizeof(exe) / sizeof(exe[0]))
    {
        FormatTo(report.note, sizeof(report.note), "the program's own file name could not be read");
        return report;
    }
    const wchar_t* name = exe;
    for (const wchar_t* p = exe; *p != 0; ++p)
    {
        if (*p == L'\\' || *p == L'/')
        {
            name = p + 1;
        }
    }
    u32 dump_type = 0;
    if (!detail::DescribeLocalDumps(HKEY_LOCAL_MACHINE, kLocalDumpsKey, name, &report, &dump_type))
    {
        return report;
    }

    DWORD disabled = 0;
    if (ReadDword(HKEY_LOCAL_MACHINE, kWerKey, L"Disabled", &disabled) && disabled != 0)
    {
        FormatTo(report.note, sizeof(report.note),
                 "Windows Error Reporting is turned off on this machine (HKLM ...\\Windows Error Reporting\\Disabled)");
        return report;
    }
    // A process whose error mode keeps the fault dialog away (SEM_NOGPFAULTERRORBOX, which every death-test child and
    // many launchers set) never reaches Windows Error Reporting.
    if ((GetErrorMode() & SEM_NOGPFAULTERRORBOX) != 0)
    {
        FormatTo(
            report.note, sizeof(report.note),
            "this process's error mode (SEM_NOGPFAULTERRORBOX) keeps Windows Error Reporting from running at a crash; clear it with "
            "SetErrorMode for a post-mortem dump");
        return report;
    }
    if (options.size_limit_bytes != 0 && dump_type == 2)
    {
        FormatTo(report.note, sizeof(report.note),
                 "Windows Error Reporting has no size limit: DumpType 1 (a minidump) under the key keeps dumps small");
        return report;
    }
    report.result = PostMortemResult::Enabled;
    return report;
}

}  // namespace vse::debug
