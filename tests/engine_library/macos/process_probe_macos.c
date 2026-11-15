/* Phase 23 Day 355: the process probes on macOS: the task's threads and resident size from Mach, the loaded images from
 * dyld. */
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <string.h>

#include "process_probe.h"

int ProbeThreadCount(void)
{
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
    {
        return -1;
    }
    for (mach_msg_type_number_t i = 0; i < count; ++i)
    {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(threads[0]));
    return (int)count;
}

long ProbeResidentBytes(void)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
    {
        return -1;
    }
    return (long)info.resident_size;
}

int ProbeMapped(const char* file_name)
{
    const uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i)
    {
        const char* name = _dyld_get_image_name(i);
        if (name != NULL && strstr(name, file_name) != NULL)
        {
            return 1;
        }
    }
    return 0;
}
