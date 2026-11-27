// SPDX-License-Identifier: Apache-2.0

// cc_list.h is an intrusive doubly linked list, faster than Linux kernel's list_head and BSD TAILQ.
// Use it just the way you use list_head.
//
// cc_list.h is part of cc_task, the async/await C coroutine library.
// https://github.com/ivan7wl/cc_task

#ifndef CC_LIST_H
#define CC_LIST_H

// list head and node type
// type: the type of the entry (container) struct, which has this struct as a field
// next: points to next entry (container)
// prev: points to previous entry (container)
#define cc_list_head_t(type)                                \
    struct _ ## type ## _list_node_t                        \
    {                                                       \
        struct type *next;                                  \
        struct type *prev;                                  \
    }

//-------------------------------------------------------------------------------------------------------

// get container (entry) type
#define cc_list_type(head)                  __typeof__(*((head)->next))

#define cc_list_entry_raw(head, field)      ((cc_list_type(head) *)((unsigned char *)(head) - __builtin_offsetof(cc_list_type(head), field)))

// Keep the shadow-container address opaque to the optimizer.
// Input-only asm plus the memory clobber avoids the extra output-register move;
// the "memory" clobber is still required for GCC, otherwise it may reuse a stale head.next.
#if defined(__GNUC__) || defined(__clang__)
#define cc_list_entry(head, field)          ({                                                                  \
                                                __UINTPTR_TYPE__ _addr = (__UINTPTR_TYPE__)(head) -             \
                                                    __builtin_offsetof(cc_list_type(head), field);              \
                                                cc_list_type(head) *_container = (cc_list_type(head) *)_addr;   \
                                                __asm__ __volatile__("" : : "r"(_container) : "memory");        \
                                                _container;                                                     \
                                            })
#else
#define cc_list_entry(head, field)          cc_list_entry_raw(head, field)
#endif

// get shadow container of list head
#define cc_list_node(entry, field)          (&(entry)->field)

#define cc_list_first(head)                 ((head)->next)

#define cc_list_last(head)                  ((head)->prev)

#define cc_list_next(entry, field)          ((entry)->field.next)

#define cc_list_prev(entry, field)          ((entry)->field.prev)

//-------------------------------------------------------------------------------------------------------

// init a list head, field is the field name of node in container struct
#define cc_list_init(head, field)           ({                                                                  \
                                                cc_list_type(head) *_e = cc_list_entry((head), field);          \
                                                _e->field.next = _e->field.prev = _e;                           \
                                            })

#define CC_LIST_HEAD_INIT(head, field)      { cc_list_entry_raw(head, field), cc_list_entry_raw(head, field) }

#define cc_list_is_empty(head, field)       ({                                                                  \
                                                cc_list_type(head) *_e = cc_list_entry((head), field);          \
                                                _e->field.next == _e;                                           \
                                            })

// insert entry b between a and c
#define cc_list_insert(a, b, c, field)      do                                                                  \
                                            {                                                                   \
                                                __typeof__(a) _a = (a);                                         \
                                                __typeof__(b) _b = (b);                                         \
                                                __typeof__(c) _c = (c);                                         \
                                                _c->field.prev = _b;                                            \
                                                _b->field.next = _c;                                            \
                                                _b->field.prev = _a;                                            \
                                                _a->field.next = _b;                                            \
                                            } while (0)

// delete entry from list
#define cc_list_del(entry, field)           do                                                                  \
                                            {                                                                   \
                                                __typeof__(entry) _e = (entry);                                 \
                                                __typeof__(_e)    _p = cc_list_prev(_e, field);                 \
                                                __typeof__(_e)    _n = cc_list_next(_e, field);                 \
                                                _n->field.prev = _p;                                            \
                                                _p->field.next = _n;                                            \
                                            } while (0)

// add entry to the first of list
#define cc_list_add(head, entry, field)     do                                                                  \
                                            {                                                                   \
                                                cc_list_type(head) *_e = cc_list_entry((head), field);          \
                                                cc_list_insert(_e, (entry), _e->field.next, field);             \
                                            } while (0)

// add entry to the last of list
#define cc_list_add_tail(head, entry, field) do                                                                 \
                                            {                                                                   \
                                                cc_list_type(head) *_e = cc_list_entry((head), field);          \
                                                cc_list_insert(_e->field.prev, (entry), _e, field);             \
                                            } while (0)

//-------------------------------------------------------------------------------------------------------
// list iterators:
// h: list head
// i: iterator variable, the list entry type
// t: temp variable, the list entry type
// f: field name

#define cc_list_foreach(h, i, f)                    for ((i) = cc_list_first(h);                                \
                                                         (i) != cc_list_entry(h, f);                            \
                                                         (i) = cc_list_next(i, f))

#define cc_list_foreach_safe(h, i, t, f)            for ((i) = cc_list_first(h), (t) = cc_list_next(i, f);      \
                                                         (i) != cc_list_entry(h, f);                            \
                                                         (i) = (t), (t) = cc_list_next(i, f))

#define cc_list_foreach_reverse(h, i, f)            for ((i) = cc_list_last(h);                                 \
                                                         (i) != cc_list_entry(h, f);                            \
                                                         (i) = cc_list_prev(i, f))

#define cc_list_foreach_reverse_safe(h, i, t, f)    for ((i) = cc_list_last(h), (t) = cc_list_prev(i, f);       \
                                                         (i) != cc_list_entry(h, f);                            \
                                                         (i) = (t), (t) = cc_list_prev(i, f))

#endif // CC_LIST_H
