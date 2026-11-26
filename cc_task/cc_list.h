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
#define cc_list_type(head)                  typeof(*((head)->next))

// get shadow container of list head
#define cc_list_entry(head, field)          ((cc_list_type(head) *)((char *)(head) - __builtin_offsetof(cc_list_type(head), field)))

// get list node from entry
#define cc_list_node(entry, field)          (&(entry)->field)

#define cc_list_first(head)                 ((head)->next)

#define cc_list_last(head)                  ((head)->prev)

#define cc_list_next(entry, field)          ((entry)->field.next)

#define cc_list_prev(entry, field)          ((entry)->field.prev)

//-------------------------------------------------------------------------------------------------------

// init a list head, field is the field name of node in container struct
#define cc_list_init(head, field)           ({ typeof(head) _h = (head); (_h)->next = (_h)->prev = cc_list_entry(_h, field); })

#define CC_LIST_HEAD_INIT(head, field)      { cc_list_entry(head, field), cc_list_entry(head, field) }

#define cc_list_is_empty(head, field)       ({ typeof(head) _h = (head); _h->next == cc_list_entry(_h, field); })

// insert entry b between a and c
#define cc_list_insert(a, b, c, field)      do                                                                  \
                                            {                                                                   \
                                                typeof(a) _a = (a);                                             \
                                                typeof(b) _b = (b);                                             \
                                                typeof(c) _c = (c);                                             \
                                                _b->field.next = _c;                                            \
                                                _b->field.prev = _a;                                            \
                                                _a->field.next = _b;                                            \
                                                _c->field.prev = _b;                                            \
                                            } while (0)

// delete entry from list
#define cc_list_del(entry, field)           do                                                                  \
                                            {                                                                   \
                                                typeof(entry) _e = (entry);                                     \
                                                cc_list_prev(_e, field)->field.next = _e->field.next;           \
                                                cc_list_next(_e, field)->field.prev = _e->field.prev;           \
                                            } while (0)

// add entry to the first of list
#define cc_list_add(head, entry, field)     do                                                                  \
                                            {                                                                   \
                                                typeof(head) _h  = (head);                                      \
                                                typeof(entry) _e = (entry);                                     \
                                                _e->field.next = _h->next;                                      \
                                                _e->field.prev = cc_list_entry(_h, field);                      \
                                                _h->next = _e;                                                  \
                                                cc_list_next(_e, field)->field.prev = _e;                       \
                                            } while (0)

// add entry to the last of list
#define cc_list_add_tail(h, e, f)           do                                                                  \
                                            {                                                                   \
                                                typeof(h) _h = (h);                                             \
                                                typeof(e) _e = (e);                                             \
                                                _e->f.next = cc_list_entry(_h, f);                              \
                                                _e->f.prev = _h->prev;                                          \
                                                _h->prev->f.next = _e;                                          \
                                                _h->prev = _e;                                                  \
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
