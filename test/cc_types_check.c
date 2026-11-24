#include "cc_types.h"

// the model and the widths must agree with the real ABI, checked at compile time
CC_STATIC_ASSERT(CC_PTR_SIZE == (int)sizeof(void *), "CC_PTR_SIZE must match sizeof(void *)");
CC_STATIC_ASSERT(CC_LP64 + CC_ILP32 == 1, "exactly one data model must be selected");
CC_STATIC_ASSERT(sizeof(cc_uintptr) == sizeof(void *), "cc_uintptr must be pointer sized");
CC_STATIC_ASSERT(sizeof(cc_s64) == 8 && sizeof(cc_u64) == 8, "64 bit types must be 8 bytes");
CC_STATIC_ASSERT(sizeof(cc_size) == sizeof(__SIZE_TYPE__), "cc_size must match the libc size_t");
CC_STATIC_ASSERT((cc_bool)CC_TRUE == (cc_bool)1, "CC_TRUE");

// the exit code reports the detected pointer size to the Makefile:
// 8 = LP64, 4 = ILP32, anything else = failure
int main(void)
{
    int       probe = 0;
    void     *p     = &probe;

    if ((void *)(cc_uintptr)p != p) // a pointer must survive a cc_uintptr round trip
    {
        return 1;
    }

    if ((cc_u8)(cc_u16)0x1234 != 0x34) // truncation sanity
    {
        return 1;
    }

    return CC_PTR_SIZE;
}
