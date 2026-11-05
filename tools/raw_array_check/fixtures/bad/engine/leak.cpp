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
