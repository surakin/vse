/* Phase 23 Day 355 (moved from host_reload.c, Day 348): the process probes on Linux, from /proc/self. */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "process_probe.h"

int ProbeThreadCount(void)
{
    int n = 0;
    DIR* dir = opendir("/proc/self/task");
    struct dirent* entry;
    if (dir == NULL)
    {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
        {
            ++n;
        }
    }
    closedir(dir);
    return n;
}

long ProbeResidentBytes(void)
{
    long total = 0;
    long resident = 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (f == NULL)
    {
        return -1;
    }
    if (fscanf(f, "%ld %ld", &total, &resident) != 2)
    {
        resident = -1;
    }
    fclose(f);
    return resident < 0 ? -1 : resident * sysconf(_SC_PAGESIZE);
}

int ProbeMapped(const char* file_name)
{
    char line[1024];
    int found = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f == NULL)
    {
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strstr(line, file_name) != NULL)
        {
            found = 1;
        }
    }
    fclose(f);
    return found;
}
