// Planted violations: the check reports the lines marked below.
int main(int argc, char** argv)
{
    const char* first = argv[1];                          // line 4: an index
    const char* last = argv[argc - 1];                    // line 5: an index with an expression
    const char* program = argv[0];                        // line 6: another index
    const char* ok[] = {"a"};                             // not argv
    return first == last && program == ok[0] ? 0 : 1;     // a comment about argv[1] is not code
}
