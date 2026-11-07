DWORD a = GetEnvironmentVariableW(L"ENGINE_X", buf, 4);
const wchar_t* b = _wgetenv(L"ENGINE_Y");
wchar_t** c = CommandLineToArgvW(GetCommandLineW(), &n);
