##=================================================================================================
## cc_task_test: build and run the cc_task checks
##
## the library lives in ../cc_task, the checks and output stay in this directory
## no external dependency: plain clang + libc, builds and runs on the host
##
## make            build and run the host checks (LP64 or ILP32, whatever the host is)
## make check      same as above
## make cross      compile the other data models, including real asm codegen for all supported ABIs
## make clean
##=================================================================================================

SRC         :=  ../cc_task
TEST        :=  .

CC          :=  clang
CFLAGS      ?=  -std=gnu11 -O2 -g -Wall -Wextra -Werror -fblocks
CFLAGS_NB   ?=  -std=gnu11 -O2 -g -Wall -Wextra -Werror        # no -fblocks
LDFLAGS     ?=
INC         :=  -I$(SRC)
OUT         :=  out_dir

# some clang builds (Apple clang on macOS) enable Blocks by default, so __BLOCKS__
# stays defined even without -fblocks; force them off where the flag exists so the
# "no -fblocks" negative check still means something
BLOCKS_OFF  :=  $(shell printf 'int main(void){return 0;}\n' | $(CC) -x c -fno-blocks -fsyntax-only - >/dev/null 2>&1 && echo -fno-blocks)

CC_CFG_USE_MALLOC  ?= 1
CC_CFG_TASK_BLOCK    ?= 1
CC_CFG_TASK_CONV     ?= 1
CC_CFG_TASK_STACKFUL ?= 1   # on by default, the checks cover the swap asm
CFG_FLAGS   :=  -DCC_CFG_USE_MALLOC=$(CC_CFG_USE_MALLOC) \
                -DCC_CFG_TASK_BLOCK=$(CC_CFG_TASK_BLOCK) \
                -DCC_CFG_TASK_CONV=$(CC_CFG_TASK_CONV) \
                -DCC_CFG_TASK_STACKFUL=$(CC_CFG_TASK_STACKFUL)
# same config with block off, for the "no -fblocks" check
CFG_FLAGS_NB := -DCC_CFG_USE_MALLOC=$(CC_CFG_USE_MALLOC) \
                -DCC_CFG_TASK_BLOCK=0 \
                -DCC_CFG_TASK_CONV=$(CC_CFG_TASK_CONV) \
                -DCC_CFG_TASK_STACKFUL=$(CC_CFG_TASK_STACKFUL)
# same config with stackful off, for the cc_swap check without the swap asm
CFG_FLAGS_NS := -DCC_CFG_USE_MALLOC=$(CC_CFG_USE_MALLOC) \
                -DCC_CFG_TASK_BLOCK=$(CC_CFG_TASK_BLOCK) \
                -DCC_CFG_TASK_CONV=$(CC_CFG_TASK_CONV) \
                -DCC_CFG_TASK_STACKFUL=0
# init-only build, with no allocator and no default port object
CFG_FLAGS_NM := -DCC_CFG_USE_MALLOC=0 \
                -DCC_CFG_TASK_BLOCK=$(CC_CFG_TASK_BLOCK) \
                -DCC_CFG_TASK_CONV=$(CC_CFG_TASK_CONV) \
                -DCC_CFG_TASK_STACKFUL=$(CC_CFG_TASK_STACKFUL)

# layout / data model checks for the other ABIs, no sysroot or libc needed
CROSS       :=  x86_64-unknown-linux-gnu i386-unknown-linux-gnu x86_64-w64-windows-gnu \
                i686-w64-windows-gnu aarch64-unknown-linux-gnu armv7-unknown-linux-gnueabihf \
                thumbv7-unknown-linux-gnueabihf thumbv6m-none-eabi thumbv8m.base-none-eabi
CROSS_RISCV :=  riscv64-unknown-linux-gnu riscv32-unknown-linux-gnu
ifneq ($(findstring riscv,$(shell $(CC) --print-targets 2>/dev/null)),)
CROSS       +=  $(CROSS_RISCV)
endif

# checks that need no libc, so they can also be compiled for the other models
# (cc_task_check.c and cc_task_layout_check.c need stdio and run on the host only)
CHECKS      :=  $(TEST)/cc_types_check.c $(TEST)/cc_lc_check.c $(TEST)/cc_helper_check.c $(TEST)/cc_pp_check.c \
                $(TEST)/cc_primitive_check.c $(TEST)/cc_swap_compile_check.c
ifeq ($(CC_CFG_TASK_CONV),1)
ifeq ($(CC_CFG_USE_MALLOC),1)
CHECKS      +=  $(TEST)/cc_task_conv_check.c
endif
endif

# library source
LIB         :=  $(SRC)/cc_task.c $(SRC)/cc_task_conv.c $(SRC)/cc_task_stackful.c $(SRC)/cc_runner.c $(SRC)/cc_port.c
LIB_NM      :=  $(SRC)/cc_task.c $(SRC)/cc_task_conv.c $(SRC)/cc_task_stackful.c $(SRC)/cc_runner.c

CHECK_BINS  :=  $(OUT)/cc_swap_check $(OUT)/cc_swap_nostk $(OUT)/cc_port_check \
                $(OUT)/cc_helper_check $(OUT)/cc_pp_check $(OUT)/cc_primitive_check $(OUT)/cc_types_check \
                $(OUT)/cc_lc_check $(OUT)/cc_tstack_guard_check $(OUT)/cc_nmalloc_check
ifeq ($(CC_CFG_USE_MALLOC),1)
CHECK_BINS  +=  $(OUT)/cc_tstack_check $(OUT)/cc_tstack_noblk $(OUT)/cc_tstack_nostk
ifeq ($(CC_CFG_TASK_CONV),1)
CHECK_BINS  +=  $(OUT)/cc_task_conv_check $(OUT)/cc_task_conv_noblk $(OUT)/cc_runner_check
endif
ifeq ($(CC_CFG_TASK_BLOCK),1)
CHECK_BINS  +=  $(OUT)/cc_port_override_check $(OUT)/cc_task_layout_check $(OUT)/cc_task_check
endif
endif
$(CHECK_BINS): $(SRC)/cc_config.h

.DEFAULT_GOAL   :=  all
.PHONY: all check cross clean

all: check cross

check: $(CHECK_BINS)
	@$(OUT)/cc_swap_check || { echo "[check] cc_swap FAILED"; exit 1; }
	@echo "[check] cc_swap: start / resume / args / nested stack ok"
	@$(OUT)/cc_swap_nostk || { echo "[check] cc_swap (no stackful) FAILED"; exit 1; }
	@echo "[check] cc_swap: CC_CFG_TASK_STACKFUL=0 leaves no swap asm"
	@$(OUT)/cc_port_check || { echo "[check] cc_port FAILED"; exit 1; }
	@echo "[check] cc_port: defaults + panic hook ok"
	@$(OUT)/cc_nmalloc_check || { echo "[check] init-only / no allocator FAILED"; exit 1; }
	@echo "[check] init-only: block / conv / stackful work without malloc/free or cc_port.c"
	@$(OUT)/cc_helper_check || { echo "[check] cc_helper FAILED"; exit 1; }
	@echo "[check] cc_helper: static_assert / align / likely / unused ok"
	@$(OUT)/cc_pp_check || { echo "[check] cc_pp FAILED"; exit 1; }
	@echo "[check] cc_pp: narg / arg_at / cat / str ok"
	@$(OUT)/cc_primitive_check || { echo "[check] cc_primitive FAILED"; exit 1; }
	@echo "[check] cc_primitive: conventional body, yield/end ok"
ifeq ($(CC_CFG_TASK_CONV),1)
ifeq ($(CC_CFG_USE_MALLOC),1)
	@$(OUT)/cc_task_conv_check || { echo "[check] cc_task_conv FAILED"; exit 1; }
	@echo "[check] cc_task_conv: function body, ctx / await ok"
	@$(OUT)/cc_task_conv_noblk || { echo "[check] cc_task_conv (no -fblocks) FAILED"; exit 1; }
	@echo "[check] cc_task_conv: no -fblocks, function body ok"
	@$(OUT)/cc_runner_check || { echo "[check] cc_runner FAILED"; exit 1; }
	@echo "[check] cc_runner: ready/wait/current/add/nested ok"
endif
endif
	@$(OUT)/cc_types_check; rc=$$?; \
	if [ $$rc -eq 8 ]; then echo "[check] native: LP64, pointer 8 bytes"; \
	elif [ $$rc -eq 4 ]; then echo "[check] native: ILP32, pointer 4 bytes"; \
	else echo "[check] native: FAILED (rc=$$rc)"; exit 1; fi
	@$(OUT)/cc_lc_check || { echo "[check] cc_lc FAILED"; exit 1; }
	@echo "[check] cc_lc: resume / save ok"
ifeq ($(CC_CFG_TASK_BLOCK),1)
ifeq ($(CC_CFG_USE_MALLOC),1)
	@$(OUT)/cc_port_override_check || { echo "[check] cc_port override FAILED"; exit 1; }
	@echo "[check] cc_port: CC_PORT_INCLUDE replaced current task + panic"
	@$(OUT)/cc_task_layout_check || { echo "[check] cc_task_t layout FAILED"; exit 1; }
	@echo "[check] cc_task_t layout: block call + ctx copy ok"
	@$(OUT)/cc_task_check || { echo "[check] cc_task FAILED"; exit 1; }
endif
	@if $(CC) $(CFG_FLAGS) $(CFLAGS_NB) $(INC) $(BLOCKS_OFF) -fsyntax-only $(TEST)/cc_types_check.c 2>$(OUT)/cfg_block.log; then \
		echo "[check] block config FAILED: accepted CC_CFG_TASK_BLOCK=1 without -fblocks"; exit 1; \
	fi
	@grep -q 'clang -fblocks' $(OUT)/cfg_block.log || { cat $(OUT)/cfg_block.log; \
		echo "[check] block config FAILED: error does not mention clang -fblocks"; exit 1; }
	@echo "[check] block config: CC_CFG_TASK_BLOCK=1 without -fblocks errors"
endif
ifeq ($(CC_CFG_USE_MALLOC),1)
	@$(OUT)/cc_tstack_check || { echo "[check] cc_tstack FAILED"; exit 1; }
	@echo "[check] cc_tstack: suspend at depth / wait / abort / trans / exit / exec / mark / block ok"
	@$(OUT)/cc_tstack_noblk || { echo "[check] cc_tstack (no -fblocks) FAILED"; exit 1; }
	@echo "[check] cc_tstack: no -fblocks, conventional stackful body ok"
	@$(OUT)/cc_tstack_nostk || { echo "[check] cc_tstack (no stackful) FAILED"; exit 1; }
	@echo "[check] cc_tstack: CC_CFG_TASK_STACKFUL=0 declares and compiles nothing"
endif
ifeq ($(strip $(CC_CFG_TASK_STACKFUL)),1)
	@$(OUT)/cc_tstack_guard_check; rc=$$?; if [ $$rc -ne 86 ]; then \
		echo "[check] cc_tstack guard FAILED: outside yield returned $$rc instead of panic"; exit 1; \
	fi
	@echo "[check] cc_tstack guard: a plain task calling cc_tstack_* panics"
else
	@$(OUT)/cc_tstack_guard_check || { echo "[check] cc_tstack guard (no stackful) FAILED"; exit 1; }
	@echo "[check] cc_tstack guard: CC_CFG_TASK_STACKFUL=0, nothing to check"
endif
	@if $(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -Wconditional-uninitialized -Werror=conditional-uninitialized \
		-fsyntax-only $(TEST)/cc_suspend_warn_check.c 2>$(OUT)/suspend_warn.log; then \
		echo "[check] suspend warn FAILED: stack local across a suspend point compiled"; exit 1; \
	fi
	@for name in n_yield n_wait n_until n_while n_trans n_await n_struct; do \
		grep -q "$$name" $(OUT)/suspend_warn.log || { \
			echo "[check] suspend warn FAILED: no diagnostic for $$name"; \
			cat $(OUT)/suspend_warn.log; exit 1; }; \
	done
ifeq ($(CC_CFG_TASK_BLOCK),1)
	@grep -q 'n_block' $(OUT)/suspend_warn.log || { \
		echo "[check] suspend warn FAILED: no diagnostic for n_block"; \
		cat $(OUT)/suspend_warn.log; exit 1; }
endif
	@echo "[check] suspend warn: stack local across yield/wait/until/while/trans/await errors"

cross:
	@for t in $(CROSS); do \
		printf "[check] %-30s " $$t; \
		$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) --target=$$t -fsyntax-only $(CHECKS) $(LIB) || exit 1; \
		$(CC) $(CFG_FLAGS_NM) $(CFLAGS) $(INC) -I$(TEST) -DCC_PORT_INCLUDE='"cc_nmalloc_port.h"' \
			--target=$$t -fsyntax-only $(TEST)/cc_nmalloc_check.c $(LIB_NM) || exit 1; \
		$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) --target=$$t -c $(TEST)/cc_swap_compile_check.c -o /dev/null || exit 1; \
		echo "ok ($$($(CC) --target=$$t -E -dM -x c /dev/null | sed -n 's/.*__SIZEOF_POINTER__ /ptr=/p'))"; \
	done

$(OUT)/cc_types_check: $(TEST)/cc_types_check.c $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_types_check.c $(LDFLAGS)

$(OUT)/cc_swap_check: $(TEST)/cc_swap_check.c $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_config.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_swap_check.c $(LDFLAGS)

$(OUT)/cc_swap_nostk: $(TEST)/cc_swap_check.c $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_config.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_swap_check.c $(LDFLAGS)

$(OUT)/cc_port_check: $(TEST)/cc_port_check.c $(SRC)/cc_port.c $(SRC)/cc_port.h $(SRC)/cc_types.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_port_check.c $(SRC)/cc_port.c $(LDFLAGS)

# a port header replaces the accessors and the panic, so cc_port.c is not linked
$(OUT)/cc_port_override_check: $(TEST)/cc_port_override_check.c $(TEST)/cc_port_override.h $(SRC)/cc_task.c $(SRC)/cc_runner.c \
                               $(SRC)/cc_task.h $(SRC)/cc_runner.h $(SRC)/cc_list.h $(SRC)/cc_task_conv.h $(SRC)/cc_task_stackful.h $(SRC)/cc_primitive.h \
                               $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NS) $(CFLAGS) $(INC) -I$(TEST) -DCC_PORT_INCLUDE='"cc_port_override.h"' \
		-o $@ $(TEST)/cc_port_override_check.c $(SRC)/cc_task.c $(SRC)/cc_runner.c $(LDFLAGS)

$(OUT)/cc_primitive_check: $(TEST)/cc_primitive_check.c $(LIB) $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_task.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_primitive_check.c $(LIB) $(LDFLAGS)

# no allocator macros and no cc_port.c: proves init-only builds are self-contained
$(OUT)/cc_nmalloc_check: $(TEST)/cc_nmalloc_check.c $(TEST)/cc_nmalloc_port.h $(LIB_NM) \
                         $(SRC)/cc_config.h $(SRC)/cc_task.h $(SRC)/cc_task_conv.h $(SRC)/cc_task_stackful.h \
                         $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NM) $(CFLAGS) $(INC) -I$(TEST) -DCC_PORT_INCLUDE='"cc_nmalloc_port.h"' \
		-o $@ $(TEST)/cc_nmalloc_check.c $(LIB_NM) $(LDFLAGS)

$(OUT)/cc_task_conv_check: $(TEST)/cc_task_conv_check.c $(LIB) $(SRC)/cc_task_conv.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.h $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_task_conv_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_task_conv_noblk: $(TEST)/cc_task_conv_check.c $(LIB) $(SRC)/cc_task_conv.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.h $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NB) $(CFLAGS_NB) $(INC) -o $@ $(TEST)/cc_task_conv_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_runner_check: $(TEST)/cc_runner_check.c $(LIB) $(SRC)/cc_runner.h $(SRC)/cc_list.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.h $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_runner_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_pp_check: $(TEST)/cc_pp_check.c $(SRC)/cc_pp.h $(SRC)/cc_helper.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_pp_check.c $(LDFLAGS)

$(OUT)/cc_helper_check: $(TEST)/cc_helper_check.c $(SRC)/cc_helper.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_helper_check.c $(LDFLAGS)

$(OUT)/cc_lc_check: $(TEST)/cc_lc_check.c $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_lc_check.c $(LDFLAGS)

$(OUT)/cc_task_layout_check: $(TEST)/cc_task_layout_check.c $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_task_layout_check.c $(LDFLAGS)

$(OUT)/cc_task_check: $(TEST)/cc_task_check.c $(LIB) $(SRC)/cc_task.h $(SRC)/cc_task_stackful.h $(SRC)/cc_primitive.h $(SRC)/cc_pp.h $(SRC)/cc_port.h $(SRC)/cc_swap.h $(SRC)/cc_types.h $(SRC)/cc_lc.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_task_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_tstack_check: $(TEST)/cc_tstack_check.c $(LIB) $(SRC)/cc_task_stackful.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.c \
                        $(SRC)/cc_swap.h $(SRC)/cc_primitive.h $(SRC)/cc_task_conv.h $(SRC)/cc_port.h $(SRC)/cc_types.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_tstack_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_tstack_noblk: $(TEST)/cc_tstack_check.c $(LIB) $(SRC)/cc_task_stackful.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.c \
                        $(SRC)/cc_swap.h $(SRC)/cc_primitive.h $(SRC)/cc_task_conv.h $(SRC)/cc_port.h $(SRC)/cc_types.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NB) $(CFLAGS_NB) $(INC) -o $@ $(TEST)/cc_tstack_check.c $(LIB) $(LDFLAGS)

$(OUT)/cc_tstack_nostk: $(TEST)/cc_tstack_check.c $(LIB) $(SRC)/cc_task_stackful.h $(SRC)/cc_task.h $(SRC)/cc_task_stackful.c \
                        $(SRC)/cc_swap.h $(SRC)/cc_primitive.h $(SRC)/cc_task_conv.h $(SRC)/cc_port.h $(SRC)/cc_types.h
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS_NS) $(CFLAGS) $(INC) -o $@ $(TEST)/cc_tstack_check.c $(LIB) $(LDFLAGS)

# misuse checks: the test panic hook exits with status 86, distinguishing guards from crashes
$(OUT)/cc_tstack_guard_check: $(TEST)/cc_tstack_guard_check.c $(TEST)/cc_tstack_guard_port.h $(LIB) $(SRC)/cc_task_stackful.h $(SRC)/cc_task_stackful.c
	@mkdir -p $(OUT)
	$(CC) $(CFG_FLAGS) $(CFLAGS) $(INC) -I$(TEST) -DCC_PORT_INCLUDE='"cc_tstack_guard_port.h"' \
		-o $@ $(TEST)/cc_tstack_guard_check.c $(LIB) $(LDFLAGS)

clean:
	rm -rf $(OUT)
