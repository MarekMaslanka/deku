/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <asm/unwind.h>
#include <linux/slab.h>

struct inspect_stacktrace __deku_gen_stacktrace(struct task_struct *task,
						 void *ret_addr)
{
	struct unwind_state state;
	struct stack_info stack_info = {0};
	struct pt_regs *regs = NULL;
	unsigned long *stack = NULL;
	unsigned long visit_mask = 0;
	int graph_idx = 0;
	bool partial = false;
	int i = 0;
	struct inspect_stacktrace stacktrace = {0};
	void *address[STACKTRACE_MAX_SIZE];
	bool found_ret_addr = false;

	unwind_start(&state, task, regs, stack);
	stack = get_stack_pointer(task, regs);
	regs = unwind_get_entry_regs(&state, &partial);

	for (; stack && i < STACKTRACE_MAX_SIZE;
		stack = PTR_ALIGN(stack_info.next_sp, sizeof(long))) {
		if (get_stack_info(stack, task, &stack_info, &visit_mask)) {
			stack = (unsigned long *)PAGE_ALIGN((unsigned long)stack);
			if (get_stack_info(stack, task, &stack_info, &visit_mask))
				break;
		}

		if (stack_type_name(stack_info.type) && i < STACKTRACE_MAX_SIZE) {
			address[i++] = (void *)stack_info.type;
			stacktrace.id += stack_info.type;
		}

		for (; stack < stack_info.end; stack++) {
			unsigned long real_addr;
			unsigned long addr = READ_ONCE_NOCHECK(*stack);
			unsigned long *ret_addr_p =
				unwind_get_return_address_ptr(&state);

			if (!__kernel_text_address(addr))
				continue;
			if (regs && stack == &regs->ip) {
				unwind_next_frame(&state);
				continue;
			}

			real_addr = ftrace_graph_ret_addr(task, &graph_idx,
							  addr, stack);
			if ((void *)real_addr == ret_addr)
				found_ret_addr = true;
			if (found_ret_addr) {
				if (real_addr != addr && i < STACKTRACE_MAX_SIZE) {
					set_bit(i, (unsigned long *)&stacktrace.unreliable);
					address[i++] = (void*)addr;
					stacktrace.id += addr;
				}
				if (i < STACKTRACE_MAX_SIZE) {
					address[i++] = (void*)real_addr;
					stacktrace.id += real_addr;
				}
			}
			if(stack == ret_addr_p)
				unwind_next_frame(&state);
		}

		if (stack_type_name(stack_info.type) && i < STACKTRACE_MAX_SIZE) {
			address[i++] = (void *)stack_info.type;
			stacktrace.id += stack_info.type;
		}
	}
	memcpy(stacktrace.address, address, sizeof(address));

	return stacktrace;
}
