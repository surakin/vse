long Filter(void*) {
    // SIGNAL-SAFE-BEGIN
    void* p = HeapAlloc(heap, 0, 8);
    EnterCriticalSection(&section);
    wsprintfW(text, L"%d", 1);
    AcquireSRWLockExclusive(&lock);
    // SIGNAL-SAFE-END
    return 0;
}
