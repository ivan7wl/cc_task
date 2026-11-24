#include "cc_lc.h"

// exercise the lc switch: save points and resume points
// cc_lc.h includes nothing, so this also proves the header is standalone

CC_LC_STATIC_ASSERT(sizeof(cc_lc_t) == 2, "cc_lc_t must be 16 bit");

static cc_lc_t s_lc;
static int     s_trace[4];

static int probe(void)
{
    CC_LC_RESUME(s_lc)
    {
        s_trace[0]++;
        CC_LC_SET_AND_YIELD(s_lc, 1);
        s_trace[1]++;
        CC_LC_SET_AND_YIELD(s_lc, 2);
        s_trace[2]++;
        CC_LC_SET(s_lc);        // plain save point, falls through on the first pass
        s_trace[3]++;
    }
    CC_LC_END(s_lc);

    return 0;
}

int main(void)
{
    CC_LC_INIT(s_lc);

    int r1 = probe();           // runs to the first save point
    int r2 = probe();           // resumes right after it
    int r3 = probe();           // resumes after the second one, then falls out

    // CC_LC_END() only closes the switch, lc keeps the last save point; marking
    // the body done belongs to the task layer (cc_task_end())
    int ok = (r1 == 1 && r2 == 2 && r3 == 0 && s_lc == 3 && s_trace[0] == 1 &&
              s_trace[1] == 1 && s_trace[2] == 1 && s_trace[3] == 1);

    // re-init restarts from the entry case
    CC_LC_INIT(s_lc);
    ok = ok && probe() == 1 && s_trace[0] == 2;

    return ok ? 0 : 1;
}
