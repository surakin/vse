// A Windows-only test file (rule 1 exempts *_windows*): platform names such as HANDLE are allowed here.
using HANDLE = void*;
HANDLE g_handle = nullptr;
