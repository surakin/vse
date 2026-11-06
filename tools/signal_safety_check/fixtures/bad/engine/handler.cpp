void Bad() {
    // SIGNAL-SAFE-BEGIN
    void* p = malloc(8);
    char text[8];
    snprintf(text, 8, "x");
    int* q = new int;
    Log(channel, "no");
    std::string s;
    std::mutex m;
    free(p);
    // SIGNAL-SAFE-END
}
void Unclosed() {
    // SIGNAL-SAFE-BEGIN
    int fine = 0;
}
