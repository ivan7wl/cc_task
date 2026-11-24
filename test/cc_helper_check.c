#include "cc_helper.h"

typedef struct
{
    int  a;
    char b;
} probe_t;

typedef struct
{
    char             c;
    CC_ALIGN(16) int v;
} aligned_t;

static int s_arr[3];

CC_STATIC_ASSERT(CC_ARRAY_SIZE(s_arr) == 3, "CC_ARRAY_SIZE");
CC_STATIC_ASSERT(CC_BIT(3) == 8, "CC_BIT");
CC_STATIC_ASSERT(CC_OFFSETOF(probe_t, b) == sizeof(int), "CC_OFFSETOF");
CC_STATIC_ASSERT(CC_OFFSETOF(aligned_t, v) == 16, "CC_ALIGN");

CC_INLINE int twice(int v)
{
    return v * 2;
}

CC_NORETURN static void fail(void)
{
    __builtin_trap();
}

int main(void)
{
    int  v = 0;
    int *p = CC_NULL;

    CC_UNUSED(v, p); // variadic, any number of arguments

    s_arr[0] = 1; // s_arr is also used at compile time by CC_ARRAY_SIZE

    if (CC_UNLIKELY(twice(2) != 4))
    {
        fail();
    }

    if (CC_LIKELY(1) != 1 || CC_NULL != (void *)0 || s_arr[0] != 1)
    {
        fail();
    }

    return 0;
}
