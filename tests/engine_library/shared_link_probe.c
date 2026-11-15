/* Phase 23 Day 343: a C program linked at build time against the shared engine library: runs jobs inside it. */
#include <stdio.h>

int engine_shared_probe(int count);

int main(void)
{
    const int ran = engine_shared_probe(1000) + engine_shared_probe(1000);
    printf("shared_link_probe: %d jobs ran\n", ran);
    return ran == 2000 ? 0 : 1;
}
