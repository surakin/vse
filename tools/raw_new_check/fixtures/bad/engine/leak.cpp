// Planted violations: all three forms must be reported.
void Leaky() {
    int* a = new int[4];
    char* b = new (std::nothrow) char[8];
    Widget* c = new Widget[n + 1];
    delete[] a;
    delete [] b;
}
void AfterString() {
    const char* url = "http://example"; int* p = new int[2]; /* done */ delete[] p;
}
void Singles() {
    Widget* w = new Widget(1);
    Widget* n = new (std::nothrow) Widget;
    delete w;
}
void NoSpace() {
    Widget* a = new(std::nothrow) Widget;
    delete(a);
    void* operator_decl; Widget* b = new ::Widget; /* operator new is not what this line does */
}
auto* split = new
    Widget(1);
