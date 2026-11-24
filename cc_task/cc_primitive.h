// SPDX-License-Identifier: Apache-2.0

#ifndef CC_PRIMITIVE_H
#define CC_PRIMITIVE_H

// task body control primitives

#include "cc_pp.h"
#include "cc_port.h"

//--------------------------------------------------------------------------------------------------------
// body primitives

// begin of the task body, jump to yield or wait location
#define CC_BEGIN(t)                 CC_UNUSED(_); { CC_LC_RESUME((t)->lc)

// end of the task body, the optional end value goes to cc_task_ret()
// usage: CC_END(t) or CC_END(t, end_value)
#define CC_END(t, ...)              if (CC_NARG(t, ##__VA_ARGS__) > 1)                              \
                                    {                                                               \
                                        CC_PORT_SET_TASK_RET((cc_uintptr)(                          \
                                            CC_ARG_AT(1, ##__VA_ARGS__, 0)));                       \
                                    }                                                               \
                                    CC_LC_END((t)->lc);                                             \
                                    return cc_task_end((t)); }

// exit the task right here, the optional end value goes to cc_task_ret()
// usage: CC_EXIT(t) or CC_EXIT(t, end_value)
// CC_END() still has to close the body at the end
#define CC_EXIT(t, ...)             do                                                              \
                                    {                                                               \
                                        if (CC_NARG(t, ##__VA_ARGS__) > 1)                          \
                                        {                                                           \
                                            CC_PORT_SET_TASK_RET((cc_uintptr)(                      \
                                                CC_ARG_AT(1, ##__VA_ARGS__, 0)));                   \
                                        }                                                           \
                                        return cc_task_end((t));                                    \
                                    } while (0)

//--------------------------------------------------------------------------------------------------------

// throw an abort from inside a stackless task when condition is true, jump to CC_CATCH():
#define CC_THROW_ON(t, condition)   do if (CC_UNLIKELY(condition))                                  \
                                    {                                                               \
                                        cc_task_abort(t);                                           \
                                        return cc_task_call((t), (t), (t)->ctx);                    \
                                    } while (0)

// catch an abort from cc_task_abort() and CC_THROW_ON()
// usage: CC_CATCH(t): or CC_CATCH(t, end_value):
// cc_task_abort(target) marks a suspended target; resume it to jump to CC_CATCH()
// for self abort use CC_THROW_ON()
#define CC_CATCH(t, ...)            CC_EXIT(t, ##__VA_ARGS__);                                      \
                                    case CC_LC_ABORT

//--------------------------------------------------------------------------------------------------------

// yield to the owner, the task stays resumable
#define CC_YIELD(t)                 CC_LC_SET_AND_YIELD((t)->lc, cc_tr_yield)

// park until the owner resumes the task again
#define CC_WAIT(t)                  CC_LC_SET_AND_YIELD((t)->lc, cc_tr_wait)

// keep yielding to the owner until cond is true
#define CC_YIELD_UNTIL(t, cond)     do                                                              \
                                    {                                                               \
                                        CC_LC_SET((t)->lc);                                         \
                                        if (!(cond)) { return cc_tr_yield; }                        \
                                    } while (0)

#define CC_YIELD_WHILE(t, cond)     CC_YIELD_UNTIL((t), !(cond))

//--------------------------------------------------------------------------------------------------------

// symmetric transfer to nxt task with best effort tail call
#define CC_TRANS(t, nxt)            do                                                              \
                                    {                                                               \
                                        cc_task_t *cc_nxt_ = (nxt);                                 \
                                        CC_LC_SET_AND_YIELD((t)->lc,                                \
                                            cc_task_call(cc_nxt_, cc_nxt_, cc_nxt_->ctx));          \
                                    } while (0)

// await child completion and continue here, optionally acquire the child's end value
// usage: CC_AWAIT(t, child) or CC_AWAIT(t, child, var) to assign child's end value to var
#define CC_AWAIT(t, child, ...)     do                                                              \
                                    {                                                               \
                                        cc_task_t *cc_child_ = (child);                             \
                                        cc_child_->next = (t);                                      \
                                        CC_TRANS((t), cc_child_);                                   \
                                        if (CC_NARG(t, ##__VA_ARGS__) > 1)                          \
                                        {                                                           \
                                            cc_uintptr cc_ret_;                                     \
                                            CC_UNUSED(cc_ret_);                                     \
                                            (CC_ARG_AT(1, ##__VA_ARGS__, cc_ret_)) = cc_task_ret(); \
                                        }                                                           \
                                    } while (0)

#endif // CC_PRIMITIVE_H
