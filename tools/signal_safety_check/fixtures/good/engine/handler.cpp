void Unmarked() {
    void* p = malloc(8); // outside a region: not checked
    free(p);
}
void Fine() {
    // SIGNAL-SAFE-BEGIN
    // mentions of malloc in comments and "printf" in strings do not count
    const char* text = "printf and malloc";
    std::atomic<int> a;
    a.store(1, std::memory_order_release);
    std::memset(&a, 0, 1);
    write(2, text, 1);
    // SIGNAL-SAFE-END
}
