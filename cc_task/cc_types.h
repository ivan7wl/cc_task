// SPDX-License-Identifier: Apache-2.0

#ifndef CC_TYPES_H
#define CC_TYPES_H

// base types

#include "cc_config.h"
#include "cc_helper.h"
#include "cc_lc.h"

//--------------------------------------------------------------------------------------------------------
// data model

#if defined(__ILP32__) && (__ILP32__)
#define CC_ILP32                    1
#define CC_LP64                     0
#elif defined(__LP64__) && (__LP64__)
#define CC_ILP32                    0
#define CC_LP64                     1
#elif defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
#define CC_ILP32                    0
#define CC_LP64                     1
#elif defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
#define CC_ILP32                    1
#define CC_LP64                     0
#else
#error "cc_types.h: cannot detect LP64 / ILP32, define CC_ILP32 or CC_LP64"
#endif

// pointer size in bytes, 8 for LP64 and 4 for ILP32
#define CC_PTR_SIZE                 (CC_LP64 ? 8 : 4)

//--------------------------------------------------------------------------------------------------------
// fixed width integers

typedef __UINT8_TYPE__              cc_u8;
typedef __UINT16_TYPE__             cc_u16;
typedef __UINT32_TYPE__             cc_u32;
typedef __INT8_TYPE__               cc_s8;
typedef __INT16_TYPE__              cc_s16;
typedef __INT32_TYPE__              cc_s32;

// 64 bit types are spelled unsigned/signed long long so that printf("%llu" / "%llx")
// stays valid on both LP64 and ILP32: __UINT64_TYPE__ is unsigned long on LP64,
// mixing the two would silently break the format specifiers
typedef unsigned long long          cc_u64;
typedef signed long long            cc_s64;

// integer wide enough to hold a pointer: 8 bytes on LP64, 4 bytes on ILP32
typedef __UINTPTR_TYPE__            cc_uintptr;
typedef __INTPTR_TYPE__             cc_intptr;

// the compiler's `unsigned long`: 4 bytes on LLP64 (windows x64), not pointer width
typedef unsigned long               cc_ulong;

// size_t / ptrdiff_t counterpart, matches the libc malloc() and memcpy() prototypes
typedef __SIZE_TYPE__               cc_size;
typedef __PTRDIFF_TYPE__            cc_ssize;

//--------------------------------------------------------------------------------------------------------
// boolean, _Bool is a keyword so <stdbool.h> is not needed

#ifdef __cplusplus
typedef bool                        cc_bool;
#else
typedef _Bool                       cc_bool;
#endif

#define CC_TRUE                     ((cc_bool)1)
#define CC_FALSE                    ((cc_bool)0)

//--------------------------------------------------------------------------------------------------------
// self checks, a wrong model or type width breaks the build here instead of at run time

CC_STATIC_ASSERT(sizeof(cc_u8) == 1 && sizeof(cc_u16) == 2 && sizeof(cc_u32) == 4 && sizeof(cc_u64) == 8,
                 "cc_types.h: fixed width integers");
CC_STATIC_ASSERT(sizeof(cc_uintptr) == CC_PTR_SIZE, "cc_types.h: cc_uintptr must be pointer sized");

//--------------------------------------------------------------------------------------------------------
// task return codes and flags

typedef enum _cc_tr_e
{
    cc_tr_end   = 0,    // task finished, including after an abort
    cc_tr_yield = 1,    // task yielded, resumable
    cc_tr_wait  = 2,    // task wants to be resumed later
    cc_tr_trans = 3,    // used for stackful task trans
    cc_tr_error = 4,    // operation rejected, the task is unchanged
} cc_tr_e;

// task flags, 4 bytes so that they fill the block flags + reserved slot
typedef enum _cc_tf_e
{
    cc_tf_none   = 0,
    cc_tf_abort  = CC_BIT(0),   // an abort was requested
    cc_tf_retain = CC_BIT(1),   // never freed by the library, cc_task_init() storage
    cc_tf_runner = CC_BIT(2),   // this task is a runner task group
} cc_tf_e;

//--------------------------------------------------------------------------------------------------------
// closure (block) task

typedef struct _cc_blk_desc_t
{
    cc_ulong    rsv;
    cc_ulong    size;       // total block literal size, header included
    void       *copy;       // present only with BLOCK_HAS_COPY_DISPOSE
    void       *destroy;
} cc_blk_desc_t;

// read only view of a clang block literal
typedef struct _cc_blk_t
{
    void           *isa;
    int             flags;
    int             reserved;
    void          (*invoke)(void);
    cc_blk_desc_t  *desc;
    cc_u8           ctx[];
} cc_blk_t;

typedef struct _cc_task_t
{
    struct _cc_task_t *next;    // parent task, resume when ends
    cc_lc_t    lc;              // continuation label
    cc_u16     ctx_size;        // allocated ctx capacity in bytes
    cc_u32     flags;           // cc_tf_e, fixed width independent of enum ABI
    void     (*fn)(void);       // block invoke slot, never called directly
    void       *ext;            // extension context, preserves the block descriptor slot
    cc_u8      ctx[];           // user/captured ctx storage, allocated ctx_size bytes
} cc_task_t;

#if CC_LP64
CC_STATIC_ASSERT(sizeof(cc_blk_t) == 32, "cc_task_t: Block_layout must be 32 bytes on LP64");
CC_STATIC_ASSERT(sizeof(cc_task_t) == 32, "cc_task_t: header must be 32 bytes on LP64");
#else
CC_STATIC_ASSERT(sizeof(cc_blk_t) == 20, "cc_task_t: Block_layout must be 20 bytes on ILP32");
CC_STATIC_ASSERT(sizeof(cc_task_t) == 20, "cc_task_t: header must be 20 bytes on ILP32");
#endif

// per-thread task state
typedef struct _cc_port_task_slot_t
{
    cc_task_t *current;
    cc_uintptr ret;
} cc_port_task_slot_t;

#if CC_CFG_TASK_BLOCK
typedef cc_tr_e (^cc_task_func)(cc_task_t *t, void *_);
#endif

//--------------------------------------------------------------------------------------------------------
// conventional (function pointer) task

// abi is same with cc_task_func block type
// ctx alignment: 8 bytes for lp64, 4 bytes for ilp32
typedef cc_tr_e (*cc_tconv_func)(cc_task_t *t, void *_, void *ctx);

#endif // CC_TYPES_H
