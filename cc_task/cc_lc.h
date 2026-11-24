// SPDX-License-Identifier: Apache-2.0

#ifndef CC_LC_H
#define CC_LC_H

// local continuation (lc) for stackless coroutines

#ifdef __cplusplus
#define CC_LC_STATIC_ASSERT(c, m)   static_assert((c), m)
#else
#define CC_LC_STATIC_ASSERT(c, m)   _Static_assert((c), m)
#endif

// a 16 bit lc covers 65535 save points, 255 for an 8 bit lc
// lc is not source line number but an incrementing counter, starting from 0 for every function
typedef __UINT16_TYPE__ cc_lc_t;

// label marking abort and done
#define CC_LC_ABORT                 ((cc_lc_t)0xFFFEu)
#define CC_LC_DONE                  ((cc_lc_t)0xFFFFu)

// highest usable save point
#define CC_LC_MAX                   ((cc_lc_t)0xFFFDu)

// reset before the first run, 0 is the entry case
#define CC_LC_INIT(lc)              ((lc) = 0)

// open the resumable body, close it with CC_LC_END()
#define CC_LC_RESUME(lc)            enum { CC_LC_START_ = __COUNTER__ }; switch (lc) { case 0:

// save this point, execution continues right after this macro when resumed
#define CC_LC_SET(lc)               do                                                              \
                                    {                                                               \
                                        enum { CC_LC_OFF_ = __COUNTER__ - CC_LC_START_ };           \
                                        CC_LC_STATIC_ASSERT(CC_LC_OFF_ <= CC_LC_MAX, "lc over");    \
                                        (lc) = (cc_lc_t)CC_LC_OFF_;                                 \
                                        case CC_LC_OFF_: ;                                          \
                                    } while (0)

// save this point and return ret to the caller
#define CC_LC_SET_AND_YIELD(lc, r)  do                                                              \
                                    {                                                               \
                                        enum { CC_LC_OFF_ = __COUNTER__ - CC_LC_START_ };           \
                                        CC_LC_STATIC_ASSERT(CC_LC_OFF_ <= CC_LC_MAX, "lc over");    \
                                        (lc) = (cc_lc_t)CC_LC_OFF_;                                 \
                                        return (r);                                                 \
                                        case CC_LC_OFF_: ;                                          \
                                    } while (0)

// close the resumable body
#define CC_LC_END(lc)               }

#endif // CC_LC_H
