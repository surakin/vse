// The foundation allocators are exempt.
void Allocator() {
    char* block = new char[16];
    delete[] block;
}
