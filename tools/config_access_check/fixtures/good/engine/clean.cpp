// getenv("IN_A_COMMENT"), "--in-a-comment"
void F() { const char* x = settings.Name(); /* getenv("x") "--y" */ }
void G() { Init(); // was getenv("X"), now a "--bar" setting
}
const char* url = "http://example.com"; // "--quoted" in a trailing comment
