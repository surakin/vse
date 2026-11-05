// Mentions in comments are fine: new T[n] and delete[] p.
/* a block comment with delete[] inside
   and new int[3] too */
void Fine() {
    int* single = new int(3); // a single object is not an array
    delete single;
    int table[4] = {};        // plain C arrays are not allocations
    (void)table;
}
void Quoted() {
    const char* text = "new int[3] and delete[] p // not code";
    /* new T[n] */ int x = 0;
    (void)text; (void)x;
}
