// SPDX-License-Identifier: Apache-2.0

#ifndef CC_PP_H
#define CC_PP_H

// preprocessor helpers: stringify, token paste, argument counting and picking
// no include at all, usable from C and C++ headers

//--------------------------------------------------------------------------------------------------------
// stringify / paste

#define CC_STR_(x)                  #x
#define CC_STR(x)                   CC_STR_(x)

#define CC_CAT_(a, b)               a##b
#define CC_CAT(a, b)                CC_CAT_(a, b)

//--------------------------------------------------------------------------------------------------------
// argument counting, up to CC_NARG_MAX arguments

#define CC_NARG_MAX                 16

#define CC_NARG(...)                CC_NARG_(__VA_ARGS__, CC_NARG_SEQ())
#define CC_NARG_(...)               CC_ARG_N(__VA_ARGS__)

#define CC_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...)   \
                                    N

#define CC_NARG_SEQ()               16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

//--------------------------------------------------------------------------------------------------------
// pick the n-th argument, 1 based, the caller must pass at least n arguments
// CC_ARG_AT(1, a, default) -> a, CC_ARG_AT(1, default) -> default

#define CC_ARG_AT(n, ...)           CC_ARG_AT_(n)(__VA_ARGS__)
#define CC_ARG_AT_(n)               CC_CAT(CC_ARG_AT_, n)

#define CC_ARG_AT_1(a, ...)         a
#define CC_ARG_AT_2(a, ...)         CC_ARG_AT_1(__VA_ARGS__)
#define CC_ARG_AT_3(a, ...)         CC_ARG_AT_2(__VA_ARGS__)
#define CC_ARG_AT_4(a, ...)         CC_ARG_AT_3(__VA_ARGS__)
#define CC_ARG_AT_5(a, ...)         CC_ARG_AT_4(__VA_ARGS__)
#define CC_ARG_AT_6(a, ...)         CC_ARG_AT_5(__VA_ARGS__)
#define CC_ARG_AT_7(a, ...)         CC_ARG_AT_6(__VA_ARGS__)
#define CC_ARG_AT_8(a, ...)         CC_ARG_AT_7(__VA_ARGS__)

#endif // CC_PP_H
