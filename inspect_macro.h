/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include "module/deku_inspect.h"

#define __DEKU_INSPECT_TRIAL_NUM_VAR __deku_trial_num

void __deku_inspect(unsigned id, unsigned trial_num, unsigned long long value);
unsigned __deku_inspect_fun_exit(void);
unsigned __deku_inspect_trial_num(void);

#define __DEKU_inspect(id, file, value) \
	({ \
	__deku_inspect(id, __DEKU_INSPECT_TRIAL_NUM_VAR, (unsigned long long )value); \
	value; \
	})

#define __DEKU_inspect_fun(id, file, start, end) \
	unsigned __DEKU_INSPECT_TRIAL_NUM_VAR = __deku_inspect_trial_num(); \
	__deku_inspect(id, ++__DEKU_INSPECT_TRIAL_NUM_VAR, _RET_IP_);

#define __DEKU_inspect_fun_pointer(id, file, funPtr, ...) \
	({\
	__deku_inspect(id, __DEKU_INSPECT_TRIAL_NUM_VAR, (unsigned long long )funPtr); \
	(funPtr)(__VA_ARGS__); \
	})

#define __DEKU_inspect_fun_end(id, file) \
	__deku_inspect(id, __DEKU_INSPECT_TRIAL_NUM_VAR, 0); \
	__deku_inspect_fun_exit()

#define __DEKU_inspect_return(id, file) \
	__deku_inspect(id, __DEKU_INSPECT_TRIAL_NUM_VAR, 0); \
	__deku_inspect_fun_exit();

#define __DEKU_inspect_return_value(id, file, value) \
	({ \
	__deku_inspect(id, __DEKU_INSPECT_TRIAL_NUM_VAR, (unsigned long long )value); \
	__deku_inspect_fun_exit(); \
	value; \
	})

struct task_struct;
struct pt_regs;
struct inspect_stacktrace __deku_gen_stacktrace(struct task_struct *task,
						 void *ret_addr);
void __deku_inspect_add_stacktrace(unsigned id, unsigned trial_num,
				   struct inspect_stacktrace *stack);

#define __DEKU_gen_stacktrace(id) \
{ \
	struct inspect_stacktrace stack = __deku_gen_stacktrace(current, \
							 (void *)_RET_IP_); \
	__deku_inspect_add_stacktrace(id, __DEKU_INSPECT_TRIAL_NUM_VAR, \
				      &stack); \
}
