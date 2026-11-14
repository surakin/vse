// argv[1] in a comment is not a violation; neither is a declaration of an array of arguments.
int main(int argc, char** argv)
{
    const char* synthetic[] = {"prog", "--flag"};
    const char* const argv_copy[] = {"prog"};
    /* argv[2]
       argv[3] */
    return argc > 100 && argv != nullptr && synthetic != nullptr && argv_copy != nullptr ? 1 : 0;
}
