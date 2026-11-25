CC       = clang
CXX      = clang++
CFLAGS  ?= -std=gnu11 -O2 -g -fblocks -Wall -Wextra -Werror \
           -Wuninitialized -Werror=uninitialized \
           -Wconditional-uninitialized -Werror=conditional-uninitialized
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Werror \
            -Wuninitialized -Werror=uninitialized \
            -Wconditional-uninitialized -Werror=conditional-uninitialized
CPPFLAGS += -I../cc_task -DCC_CFG_TASK_CONV=0 -DCC_CFG_TASK_STACKFUL=0
LIB      := ../cc_task/cc_task.c ../cc_task/cc_port.c

.PHONY: all clean
all: echo echo_cpp

echo: echo.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ echo.c $(LIB) $(LDFLAGS)

echo_cpp: echo.cpp
	$(CXX) $(CXXFLAGS) -o $@ echo.cpp $(LDFLAGS)

clean:
	rm -f echo echo_cpp
