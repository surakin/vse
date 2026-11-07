HANDLE a = CreateFileW(L"x", 0, 0, nullptr, 0, 0, nullptr);
DeleteFileA("x");
FILE* b = _wfopen(L"x", L"rb");
FILE* c = fopen(R"(C:\w\${x})", "r");
