// SPDX-License-Identifier: Apache-2.0

#ifndef CC_CONFIG_H
#define CC_CONFIG_H

// global build configuration, override with -D or a project provided config header
#ifdef CC_CONFIG_INCLUDE
#include CC_CONFIG_INCLUDE
#endif

#ifndef CC_CFG_USE_MALLOC
#define CC_CFG_USE_MALLOC       1  // heap backed new task APIs
#endif

#ifndef CC_CFG_TASK_BLOCK
#define CC_CFG_TASK_BLOCK       1  // closure (block) task support
#endif

#ifndef CC_CFG_TASK_CONV
#define CC_CFG_TASK_CONV        1  // conventional function pointer task support
#endif

#ifndef CC_CFG_TASK_STACKFUL
#define CC_CFG_TASK_STACKFUL    1  // stackful tasks + cc_swap.h context switch
#endif

#if CC_CFG_USE_MALLOC != 0 && CC_CFG_USE_MALLOC != 1
#error "CC_CFG_USE_MALLOC must be 0 or 1"
#endif
#if CC_CFG_TASK_BLOCK != 0 && CC_CFG_TASK_BLOCK != 1
#error "CC_CFG_TASK_BLOCK must be 0 or 1"
#endif
#if CC_CFG_TASK_CONV != 0 && CC_CFG_TASK_CONV != 1
#error "CC_CFG_TASK_CONV must be 0 or 1"
#endif
#if CC_CFG_TASK_STACKFUL != 0 && CC_CFG_TASK_STACKFUL != 1
#error "CC_CFG_TASK_STACKFUL must be 0 or 1"
#endif

#if CC_CFG_TASK_BLOCK && !defined(__BLOCKS__)
#error "CC_CFG_TASK_BLOCK=1 needs clang -fblocks, or set CC_CFG_TASK_BLOCK=0"
#endif

#endif // CC_CONFIG_H
