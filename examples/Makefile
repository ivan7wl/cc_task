CC       = clang
CFLAGS  ?= -std=gnu11 -O2 -g -fblocks -Wall -Wextra -Werror \
           -Wuninitialized -Werror=uninitialized \
           -Wconditional-uninitialized -Werror=conditional-uninitialized
CPPFLAGS += -I../cc_task -DCC_CFG_TASK_CONV=0 -DCC_CFG_TASK_STACKFUL=0
LIB      := ../cc_task/cc_task.c ../cc_task/cc_port.c

.PHONY: all clean
all: echo

echo: echo.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ echo.c $(LIB) $(LDFLAGS)

clean:
	rm -f echo
