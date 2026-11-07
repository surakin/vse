long Filter(void*) {
    // SIGNAL-SAFE-BEGIN
    // HeapAlloc in a comment and "EnterCriticalSection" in a string do not count
    const char* text = "EnterCriticalSection";
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT, PAGE_READWRITE);
    WriteFile(handle, text, 1, &written, nullptr);
    InterlockedIncrement(&count);
    // SIGNAL-SAFE-END
    return 0;
}
