#include "cc_helper.h"
#include "cc_pp.h"

// counting, up to CC_NARG_MAX
CC_STATIC_ASSERT(CC_NARG(a) == 1, "narg 1");
CC_STATIC_ASSERT(CC_NARG(a, b) == 2, "narg 2");
CC_STATIC_ASSERT(CC_NARG(a, b, c) == 3, "narg 3");
CC_STATIC_ASSERT(CC_NARG(a, b, c, d, e, f, g, h) == 8, "narg 8");
CC_STATIC_ASSERT(CC_NARG(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16) == 16, "narg 16");

// picking, the trailing value is the default used when the argument is missing
CC_STATIC_ASSERT(CC_ARG_AT(1, 0x11, 0) == 0x11, "arg 1");
CC_STATIC_ASSERT(CC_ARG_AT(2, 0x11, 0x22, 0) == 0x22, "arg 2");
CC_STATIC_ASSERT(CC_ARG_AT(3, 0x11, 0x22, 0x33, 0) == 0x33, "arg 3");
CC_STATIC_ASSERT(CC_ARG_AT(1, 0) == 0, "arg default");

// paste and stringify
static int        CC_CAT(sym_, 42) = 7;
static const char s_str[]          = CC_STR(hello);

int main(void)
{
    if (sym_42 != 7)
    {
        return 1;
    }

    if (sizeof(s_str) != sizeof("hello") || s_str[0] != 'h' || s_str[4] != 'o' || s_str[5] != '\0')
    {
        return 1;
    }

    return 0;
}
