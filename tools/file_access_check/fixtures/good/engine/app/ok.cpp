// fopen("in a comment", ...) and "fopen(in a string)" are not calls.
/* open("block comment") */
const char* kText = "opendir(x)";
void Fine(File& f, Path p) {
    f.open("member call");
    p->stat();
    io::Remove(p);   // capitalised file-layer functions are not the OS functions
    os::Open(p);
    Directory::Open(p);
    reader.remove(1);
}
// SIGNAL-SAFE-BEGIN
void InsideTheFault() { open("report", 0); }
// SIGNAL-SAFE-END
