#include "cc_types.h"

#include <stdio.h>
#include <stdlib.h>

// test-only stand-ins for systems without a Blocks runtime
unsigned char CC_WEAK _NSConcreteStackBlock[0];
unsigned char CC_WEAK _NSConcreteGlobalBlock[0];

// runtime proof of the cc_task_t layout, a compact prototype of the future
// cc_task_new(): the task header is filled from a block literal, the captured
// variables are copied into t->ctx, and the task is then called like a block.

typedef int (^cc_probe_fn)(cc_task_t *t, int seed);

static cc_task_t *probe_new(cc_probe_fn blk)
{
    cc_blk_t *src = (cc_blk_t *)blk;
    cc_size   n   = (cc_size)(src->desc->size - sizeof(cc_blk_t));
    cc_task_t *t;

    if (n > 0xFFFFu) // ctx_size is a cc_u16
    {
        return NULL;
    }

    t = (cc_task_t *)malloc(CC_OFFSETOF(cc_task_t, ctx) + n);
    if (t == NULL)
    {
        return NULL;
    }

    t->next     = NULL;
    t->lc       = 0;
    t->ctx_size = (cc_u16)n;
    t->flags    = cc_tf_none;
    t->fn       = src->invoke;   // same slot as the block invoke
    t->ext      = NULL;

    for (cc_size i = 0; i < n; i++)
    {
        t->ctx[i] = src->ctx[i]; // verbatim, padding included
    }

    return t;
}

int main(void)
{
    int captured = 0x1234;

    cc_probe_fn blk = ^int (cc_task_t *t, int seed)
    {
        // captured is captured by value, so it lives in the literal capture area
        // and cc_task_new() copies it into t->ctx. Reading the same bytes through
        // the ctx pointer must show the captured value, which proves the offsets.
        int *c = (int *)t->ctx;

        if ((int)captured != *c || *c != 0x1234 || t->ctx_size != sizeof(int))
        {
            return -1;
        }

        *c += seed;             // write back into the task copy
        return *c;
    };

    cc_blk_t  *src = (cc_blk_t *)blk;
    cc_task_t *t   = probe_new(blk);

    if (t == NULL)
    {
        return 1;
    }

    // call the task the way cc_task_call() will do it: the task plays the block
    int rc = ((cc_probe_fn)t)(t, 1);

    printf("literal %u B (hdr %u B, capture %u B), task hdr %u B, ctx %u B, ret 0x%x\n",
           (unsigned)src->desc->size, (unsigned)sizeof(cc_blk_t),
           (unsigned)(src->desc->size - sizeof(cc_blk_t)),
           (unsigned)sizeof(cc_task_t), (unsigned)t->ctx_size, (unsigned)rc);

    free(t);

    // the task copy must have been modified, the original must stay untouched
    return (rc == 0x1235 && captured == 0x1234) ? 0 : 1;
}
