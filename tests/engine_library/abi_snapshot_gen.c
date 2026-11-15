/* Phase 23 Day 347: writes the ABI snapshot of engine_c.h to stdout: the version, the value of every status, and for
   every struct its size and the offset and size of every field, in declaration order. Compiled against the header, so
   the compiler computes the layout. abi_snapshot_check.cmake compares the output with abi/engine_api.snapshot. A new
   field of engine_api or of any struct is added to the lists below in the same change as the header: the struct size
   line catches one that is missed. The layout is that of a 64-bit target (pointers and size_t are 8 bytes). */
#include <stddef.h>
#include <stdio.h>

#include "engine_c/engine_c.h"

#define STATUS(name) printf("status " #name " %d\n", (int)name)
#define STRUCT(type) printf("struct " #type " size=%zu\n", sizeof(type))
#define FIELD(type, member)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        const type* probe_ = NULL;                                                                                     \
        printf("field " #type "." #member " offset=%zu size=%zu\n", offsetof(type, member), sizeof(probe_->member));   \
    } while (0)

int main(void)
{
    printf("version %d.%d\n", ENGINE_ABI_VERSION_MAJOR, ENGINE_ABI_VERSION_MINOR);
    STATUS(ENGINE_OK);
    STATUS(ENGINE_ERROR_INVALID_ARGUMENT);
    STATUS(ENGINE_ERROR_STRUCT_SIZE);
    STATUS(ENGINE_ERROR_UNSUPPORTED_VERSION);
    STATUS(ENGINE_ERROR_STALE_HANDLE);
    STATUS(ENGINE_ERROR_NOT_FOUND);
    STATUS(ENGINE_ERROR_ALREADY_EXISTS);
    STATUS(ENGINE_ERROR_CORRUPT);
    STATUS(ENGINE_ERROR_UNSUPPORTED);
    STATUS(ENGINE_ERROR_IO);
    STATUS(ENGINE_ERROR_CANCELLED);
    STATUS(ENGINE_ERROR_LIMIT_EXCEEDED);
    STATUS(ENGINE_ERROR_NOT_READY);
    STATUS(ENGINE_ERROR_IN_USE);
    STATUS(ENGINE_ERROR_TYPE_MISMATCH);
    STATUS(ENGINE_ERROR_MISSING_DEPENDENCY);
    STATUS(ENGINE_ERROR_DEPENDENCY_CYCLE);
    STATUS(ENGINE_ERROR_OFFLINE);
    STATUS(ENGINE_ERROR_TOO_LARGE);
    STATUS(ENGINE_ERROR_DENIED);
    STATUS(ENGINE_ERROR_INTERNAL);

    STRUCT(engine_string);
    FIELD(engine_string, data);
    FIELD(engine_string, length);
    STRUCT(engine_buffer);
    FIELD(engine_buffer, data);
    FIELD(engine_buffer, size);
    FIELD(engine_buffer, owner);
    FIELD(engine_buffer, release);
    STRUCT(engine_handle);
    FIELD(engine_handle, value);
    STRUCT(engine_setting_entry);
    FIELD(engine_setting_entry, key);
    FIELD(engine_setting_entry, value);
    STRUCT(engine_create_info);
    FIELD(engine_create_info, struct_size);
    FIELD(engine_create_info, flags);
    FIELD(engine_create_info, app_name);
    FIELD(engine_create_info, settings);
    FIELD(engine_create_info, setting_count);
    FIELD(engine_create_info, args);
    FIELD(engine_create_info, arg_count);
    FIELD(engine_create_info, log);
    FIELD(engine_create_info, log_user);
    STRUCT(engine_info);
    FIELD(engine_info, struct_size);
    FIELD(engine_info, setting_count);
    STRUCT(engine_api);
    FIELD(engine_api, struct_size);
    FIELD(engine_api, abi_major);
    FIELD(engine_api, abi_minor);
    FIELD(engine_api, sanitizer);
    FIELD(engine_api, status_name);
    FIELD(engine_api, create);
    FIELD(engine_api, destroy);
    FIELD(engine_api, get_info);
    FIELD(engine_api, setting_set);
    FIELD(engine_api, setting_get);
    FIELD(engine_api, last_error);
    FIELD(engine_api, get_extension);
    FIELD(engine_api, thread_detach);
    return 0;
}
