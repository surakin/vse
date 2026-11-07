// Mentions in comments are fine: new T[n] and delete[] p.
/* a block comment with delete[] inside
   and new int[3] too */
void Fine() {
    // single objects are owned by UniquePtr now: new Widget / delete p in a comment is fine
    int table[4] = {};        // plain C arrays are not allocations
    (void)table;
}
void Quoted() {
    const char* text = "new int[3] and delete[] p // not code";
    /* new T[n] */ int x = 0;
    (void)text; (void)x;
}
struct NoCopy {
    NoCopy(const NoCopy&) = delete;
    NoCopy& operator=(const NoCopy&) = delete;
};
void* operator new(unsigned long n);
void operator delete(void* p) noexcept;
void Placement(void* storage) {
    new (storage) NoCopy2();   // placement new into container-owned storage is allowed
    Widget* p = new (storage) Widget(2);
}
// a brand new type, and: return new_value;
