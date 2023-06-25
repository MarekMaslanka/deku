/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

void __deku_inspect_register_item(const char *file, unsigned line, const char *text, const char *extra, int type, unsigned id);

static int deku_init(void)
{
	int ret;
#include "inspect_map.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	ret = klp_enable_patch(&deku_patch);
		return ret;
#else
	ret = klp_register_patch(&deku_patch);
	if (ret)
		return ret;
	ret = klp_enable_patch(&deku_patch);
	if (ret) {
		WARN_ON(klp_unregister_patch(&deku_patch));
		return ret;
	}
	return 0;
#endif
}

static void deku_exit(void)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 11, 0)
	klp_disable_patch(&deku_patch);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
	WARN_ON(klp_unregister_patch(&deku_patch));
#endif
}

#include <asm/unwind.h>
#include <linux/slab.h>

#if 1
void __deku_gen_stacktrace(struct task_struct *task, struct pt_regs *regs,
			   unsigned long *stack, const char *file,
			   const char *funName)
{
	struct unwind_state state;
	struct stack_info stack_info = {0};
	unsigned long visit_mask = 0;
	int graph_idx = 0;
	bool partial = false;
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	u32 buf_idx = 0;
	const char *log_prefix = KERN_DEFAULT " DEKU Inspect: Function stacktrace:";

	unwind_start(&state, task, regs, stack);
	stack = stack ? : get_stack_pointer(task, regs);
	regs = unwind_get_entry_regs(&state, &partial);

	/*
	 * Iterate through the stacks, starting with the current stack pointer.
	 * Each stack has a pointer to the next one.
	 *
	 * x86-64 can have several stacks:
	 * - task stack
	 * - interrupt stack
	 * - HW exception stacks (double fault, nmi, debug, mce)
	 * - entry stack
	 *
	 * x86-32 can have up to four stacks:
	 * - task stack
	 * - softirq stack
	 * - hardirq stack
	 * - entry stack
	 */
	for ( ; stack; stack = PTR_ALIGN(stack_info.next_sp, sizeof(long))) {
		const char *stack_name;

		if (get_stack_info(stack, task, &stack_info, &visit_mask)) {
			/*
			 * We weren't on a valid stack.  It's possible that
			 * we overflowed a valid stack into a guard page.
			 * See if the next page up is valid so that we can
			 * generate some kind of backtrace if this happens.
			 */
			stack = (unsigned long *)PAGE_ALIGN((unsigned long)stack);
			if (get_stack_info(stack, task, &stack_info, &visit_mask))
				break;
		}

		stack_name = stack_type_name(stack_info.type);
		// if (stack_name)
		// 	printk("%s <%s>\n", log_prefix, stack_name);

		/*
		 * Scan the stack, printing any text addresses we find.  At the
		 * same time, follow proper stack frames with the unwinder.
		 *
		 * Addresses found during the scan which are not reported by
		 * the unwinder are considered to be additional clues which are
		 * sometimes useful for debugging and are prefixed with '?'.
		 * This also serves as a failsafe option in case the unwinder
		 * goes off in the weeds.
		 */
		for (; stack < stack_info.end; stack++) {
			unsigned long real_addr;
			unsigned long addr = READ_ONCE_NOCHECK(*stack);
			unsigned long *ret_addr_p =
				unwind_get_return_address_ptr(&state);

			if (!__kernel_text_address(addr))
				continue;

			/*
			 * Don't print regs->ip again if it was already printed
			 * by show_regs_if_on_stack().
			 */
			if (regs && stack == &regs->ip)
				continue;

			/*
			 * When function graph tracing is enabled for a
			 * function, its return address on the stack is
			 * replaced with the address of an ftrace handler
			 * (return_to_handler).  In that case, before printing
			 * the "real" address, we want to print the handler
			 * address as an "unreliable" hint that function graph
			 * tracing was involved.
			 */
			real_addr = ftrace_graph_ret_addr(task, &graph_idx,
							  addr, stack);
			if (real_addr != addr)
				printk("%s %s%pBb\n", log_prefix, "? ", (void *)addr);
			// printk("%s %s%pBb\n", log_prefix, stack == ret_addr_p ? "" : "? ", (void *)real_addr);
			buf_idx += sprint_backtrace_build_id(&buf[buf_idx], real_addr);
			buf[buf_idx++] = ',';
			(void)ret_addr_p;
		}

		// if (stack_name)
		// 	printk("%s </%s>\n", log_prefix, stack_name);
	}
	// printk(KERN_INFO "DEKU Inspect: Function stacktrace:%s:%s %s", file,
	//        funName, buf);
	kfree(buf);
}
#endif

module_init(deku_init);
module_exit(deku_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
