/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/circ_buf.h>
#include <linux/kallsyms.h>
#include <asm/stacktrace.h>
#include <linux/debugfs.h>

#include "deku_inspect.h"

typedef enum
{
	INSPECT_VAR,
	INSPECT_IF_COND,
	INSPECT_FUN_PTR,
	INSPECT_RETURN,
	INSPECT_RETURN_VALUE,
	INSPECT_FUNCTION,
	INSPECT_FUNCTION_END,
} InspectType;

enum InspectValType
{
	INSPECT_VAL_TYPE_UNKNOWN,
	INSPECT_VAL_TYPE_INT,
	INSPECT_VAL_TYPE_UINT,
	INSPECT_VAL_TYPE_BOOL,
	INSPECT_VAL_TYPE_STR,
	INSPECT_VAL_TYPE_PTR,
};

typedef struct {
	u32 id;
	char *file;
	unsigned line;
	enum InspectValType valType;
	InspectType type;
	char *name;
	char *extra;
	struct list_head list;
} inspect_map_item;

typedef struct {
	u32 id;
	u32 trial_num;
	u64 time;
	u64 value;
} inspect_item;

struct inspect_stacktrace_item {
	u32 id;
	unsigned trial_num;
	struct inspect_stacktrace stack;
	struct list_head list;
};

static inspect_map_item *get_inspect_map_item(unsigned id);

static inspect_map_item inspect_map_head;
static inspect_map_item stacktrace_head;

static struct dentry *root_dentry;
static struct dentry *inspect_dentry;

char consumer_text_buf[PAGE_SIZE];

#define BUFFER_SIZE 4096
#define BUFFER_CNT(head, tail, size) (((head) - (tail)) & ((size)-1))
DEFINE_SPINLOCK(buf_spinlock);

inspect_item *inspect_buffer = NULL;
u8 *tail_buf = NULL, *head_buf = NULL;
struct circ_buf buf_crc;

#define circ_count(circ) \
	(CIRC_CNT((circ)->head, (circ)->tail, BUFFER_SIZE))
#define circ_count_to_end(circ) \
	(CIRC_CNT_TO_END((circ)->head, (circ)->tail, BUFFER_SIZE))
#define circ_space(circ) \
	(CIRC_SPACE((circ)->head, READ_ONCE((circ)->tail), BUFFER_SIZE))
#define circ_space_to_end(circ) \
	(CIRC_SPACE_TO_END((circ)->head, (circ)->tail, BUFFER_SIZE))
DEFINE_SPINLOCK(producer_lock);

void __deku_inspect(u32 id, u32 trial_num, u64 value)
{
	if (circ_space(&buf_crc) > 0) {
		inspect_item item;
		item.id = id;
		item.trial_num = trial_num;
		item.time = ktime_get_boottime_ns();
		item.value = value;
		inspect_buffer[buf_crc.head] = item;

		smp_store_release(&buf_crc.head,
				  (buf_crc.head + 1) & (BUFFER_SIZE - 1));
	}
}
EXPORT_SYMBOL(__deku_inspect);

static int formatInspect(inspect_map_item *item_map, inspect_item *item)
{
	int n = 0;
	char *buf = consumer_text_buf;
	size_t buf_size = sizeof(consumer_text_buf) - 1;
	switch (item_map->type) {
	case INSPECT_FUNCTION:
		n = snprintf(buf, buf_size,
			     "[%llu] DEKU Inspect: Function: %s:%s:%d:%s:%pS\n",
			     item->time, item_map->file, item_map->name,
			     item_map->line, item_map->extra,
			     (void *)item->value);
		break;
	case INSPECT_VAR:
	case INSPECT_IF_COND: {
		if (item_map->valType == INSPECT_VAL_TYPE_BOOL)
		{
			n = snprintf(buf, buf_size,
				     "[%llu] DEKU Inspect: %s:%d %s = %s\n",
				     item->time, item_map->file,
				     item_map->line, item_map->name,
				     item->value ? "true" : "false");
		} else {
			char *fmt;
			switch (item_map->valType) {
			case INSPECT_VAL_TYPE_PTR:
				fmt = "[%llu] DEKU Inspect: %s:%d %s = 0x%p\n";
				break;
			case INSPECT_VAL_TYPE_INT:
				fmt = "[%llu] DEKU Inspect: %s:%d %s = %lld\n";
				break;
			case INSPECT_VAL_TYPE_UINT:
			default:
				fmt = "[%llu] DEKU Inspect: %s:%d %s = %llu\n";
				break;
			}
			n = snprintf(buf, buf_size, fmt, item->time,
				     item_map->file, item_map->line,
				     item_map->name, item->value);
		}
		break;
	}
	case INSPECT_FUN_PTR:
		n = snprintf(buf, buf_size,
			     "[%llu] DEKU Inspect: Function Pointer: %s:%d:%s:%ps\n",
			     item->time, item_map->file, item_map->line,
			     item_map->name, (void *)item->value);
		break;
	case INSPECT_RETURN_VALUE:
		if (item_map->valType == INSPECT_VAL_TYPE_BOOL)
		{
			n = snprintf(buf, buf_size,
				     "[%llu] DEKU Inspect: Function return value: %s:%d:%s %s = %s\n",
				     item->time, item_map->file,
				     item_map->line, item_map->name,
				     item_map->extra,
				     item->value ? "true" : "false");
		} else {
			char *fmt;
			switch (item_map->valType) {
			case INSPECT_VAL_TYPE_PTR:
				fmt = "[%llu] DEKU Inspect: Function return value: %s:%d:%s %s = 0x%p\n";
				break;
			case INSPECT_VAL_TYPE_INT:
				fmt = "[%llu] DEKU Inspect: Function return value: %s:%d:%s %s = %lld\n";
				break;
			case INSPECT_VAL_TYPE_UINT:
			default:
				fmt = "[%llu] DEKU Inspect: Function return value: %s:%d:%s %s = %llu\n";
				break;
			}
			n = snprintf(buf, buf_size, fmt, item->time,
				     item_map->file, item_map->line,
				     item_map->name, item_map->extra,
				     item->value);
		}
		break;
	case INSPECT_RETURN:
		n = snprintf(buf, buf_size,
			     "[%llu] DEKU Inspect: Function return: %s:%d %s\n",
			     item->time, item_map->file, item_map->line,
			     item_map->name);
		break;
	case INSPECT_FUNCTION_END:
		n = snprintf(buf, buf_size,
			     "[%llu] DEKU Inspect: Function end: %s:%d:%s\n",
			     item->time, item_map->file, item_map->line,
			     item_map->name);
		break;
	default:
		n = snprintf(buf, buf_size,
			     "UNKNOWN: %d\n", item_map->type);
	}
	return n;
}

static const char * const exception_stack_names[] = {
	[ ESTACK_DF	]	= "#DF",
	[ ESTACK_NMI	]	= "NMI",
	[ ESTACK_DB	]	= "#DB",
	[ ESTACK_MCE	]	= "#MC",
	[ ESTACK_VC	]	= "#VC",
	[ ESTACK_VC2	]	= "#VC2",
};

const char *stack_type_name(enum stack_type type)
{
	if (type == STACK_TYPE_TASK)
		return "TASK";

	if (type == STACK_TYPE_IRQ)
		return "IRQ";

	if (type == STACK_TYPE_SOFTIRQ)
		return "SOFTIRQ";

	if (type == STACK_TYPE_ENTRY) {
		/*
		 * On 64-bit, we have a generic entry stack that we
		 * use for all the kernel entry points, including
		 * SYSENTER.
		 */
		return "ENTRY_TRAMPOLINE";
	}

	if (type >= STACK_TYPE_EXCEPTION && type <= STACK_TYPE_EXCEPTION_LAST)
		return exception_stack_names[type - STACK_TYPE_EXCEPTION];

	return NULL;
}

static char *formatStacktrace(inspect_map_item *item_map, struct inspect_stacktrace *stack)
{
	static char buffer[(KSYM_SYMBOL_LEN + 32*4) * STACKTRACE_MAX_SIZE];

	char *buf = buffer;
	int i = 0;
	int stack_name_counter = 0;
	buf += snprintf(buf, sizeof(buffer) - 1, "[%llu] DEKU Inspect: Function stacktrace:%s:%s ",
					ktime_get_boottime_ns(), item_map->file, item_map->name);
	for (; i < STACKTRACE_MAX_SIZE && stack->address[i] != 0; i++) {
		void *addr = stack->address[i];
		int remain_space = sizeof(buffer) - (buf - buffer) - 1;
		if (addr <= (void *)STACK_TYPE_EXCEPTION_LAST) {
			const char *stack_name = stack_type_name((enum stack_type)addr);
			char *open_close_stack_fmt = stack_name_counter++ & 0x1 ? "<%s>," : "</%s>,";
			buf += snprintf(buf, remain_space, open_close_stack_fmt, stack_name);
		} else {
			bool unreliable = test_bit(i, (unsigned long *)&stack->unreliable);
			buf += snprintf(buf, remain_space, "%s%pBb,", unreliable ? "? " : "", (void *)addr);
		}
	}

	buf[0] = '\n';
	buf[1] = '\0';
	return buffer;
}

static bool popStacktrace(unsigned id, unsigned trial_num, struct inspect_stacktrace *result)
{
	struct list_head *head;
	struct inspect_stacktrace_item *entry;

	// TODO: delete entry
	list_for_each(head, &stacktrace_head.list) {
		entry = list_entry(head, struct inspect_stacktrace_item, list);
		if (entry->id == id && entry->trial_num == trial_num) {
			*result = entry->stack;
			return true;
		}
	}
	return false;
}

static char *getStacktraceText(inspect_item *item, inspect_map_item *item_map, unsigned *len)
{
	struct inspect_stacktrace stack;
	if (popStacktrace(item_map->id, item->trial_num, &stack)) {
		char *text = formatStacktrace(item_map, &stack);
		*len = strlen(text);
		return text;
	}
	return NULL;
}

static void notify_new_inspect_map_item(inspect_map_item *item_map)
{
	(void) item_map;
}

static inspect_map_item *get_inspect_map_item(unsigned id)
{
	struct list_head *head;
	inspect_map_item *entry;

	list_for_each(head, &inspect_map_head.list) {
		entry = list_entry(head, inspect_map_item, list);
		if (entry->id == id)
			return entry;
	}
	return NULL;
}

void __deku_inspect_register_item(const char *file, unsigned line,
				  const char *text, const char *extra,
				  int valType, int type, u32 id)
{
	inspect_map_item *item = kmalloc(sizeof(inspect_map_item), GFP_KERNEL);
	if (!item) {
		pr_err("Failed to allocate new inspect_map_item\n");
		return;
	}
	item->file = kmalloc(strlen(file) + 1, GFP_KERNEL);
	if (!item->file) {
		pr_err("Failed to allocate memory for file path\n");
		return;
	}
	strcpy(item->file, file);
	item->line = line;
	item->name = kmalloc(strlen(text) + 1, GFP_KERNEL);
	if (!item->name) {
		pr_err("Failed to allocate memory for item name\n");
		return;
	}
	strcpy(item->name, text);
	item->extra = kmalloc(strlen(extra) + 1, GFP_KERNEL);
	if (!item->extra) {
		pr_err("Failed to allocate memory for item extra\n");
		return;
	}
	strcpy(item->extra, extra);
	item->id = id;
	item->valType = valType;
	item->type = type;
	list_add(&item->list, &inspect_map_head.list);
	notify_new_inspect_map_item(item);
}
EXPORT_SYMBOL(__deku_inspect_register_item);

void __deku_inspect_fun_exit(void)
{
}
EXPORT_SYMBOL(__deku_inspect_fun_exit);

unsigned __deku_inspect_trial_num(void)
{
	static unsigned trial_num;
	return trial_num++;
}
EXPORT_SYMBOL(__deku_inspect_trial_num);

void __deku_inspect_add_stacktrace(unsigned id, unsigned trial_num,
				   struct inspect_stacktrace *stack)
{
	struct inspect_stacktrace_item *stacktace;
	stacktace = kmalloc(sizeof(struct inspect_stacktrace_item), GFP_ATOMIC);
	stacktace->id = id;
	stacktace->trial_num = trial_num;
	stacktace->stack = *stack;
	list_add(&stacktace->list, &stacktrace_head.list);
}
EXPORT_SYMBOL(__deku_inspect_add_stacktrace);

static int open_inspect(struct inode *inode, struct file *filp)
{
	return 0;
}

static int release_inspect(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t read_inspect(struct file *filp, char __user *ubuf, size_t cnt,
			    loff_t *ppos)
{
	unsigned buf_cnt = 0;
	unsigned long head;
	unsigned long tail;

	head = smp_load_acquire(&buf_crc.head);
	tail = buf_crc.tail;

	while (CIRC_CNT(head, tail, BUFFER_SIZE) == 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/5);
		if (signal_pending(current))
			return -EINTR;

		head = smp_load_acquire(&buf_crc.head);
		tail = buf_crc.tail;
	}

	head = smp_load_acquire(&buf_crc.head);
	tail = buf_crc.tail;

	while (CIRC_CNT(head, tail, BUFFER_SIZE) > 0) {
		inspect_item item = inspect_buffer[tail];
		inspect_map_item *item_map = get_inspect_map_item(item.id);
		if (item_map) {
			char *stackText = "";
			unsigned stackTextLen = 0;
			unsigned n = formatInspect(item_map, &item);
			if (item_map->type == INSPECT_FUNCTION)
				stackText = getStacktraceText(&item, item_map,
							      &stackTextLen);
			if (n < 0 || (buf_cnt + n + stackTextLen >= cnt - 1))
				break;

			n = copy_to_user(ubuf + buf_cnt, consumer_text_buf,
					 n + 1);
			buf_cnt += n;
			if (stackTextLen) {
				n = copy_to_user(ubuf + buf_cnt, stackText,
						 stackTextLen);
				buf_cnt += stackTextLen;
			}
		} else if (item.id != 0) {
			pr_warn("Invalid inspect map id: %d\n", item.id);
		}
		smp_store_release(&buf_crc.tail,
				  (tail + 1) & (BUFFER_SIZE - 1));

		head = smp_load_acquire(&buf_crc.head);
		tail = buf_crc.tail;
	}

	return buf_cnt;
}

static struct file_operations inspect_fops = {
	.open = open_inspect,
	.read = read_inspect,
	.release = release_inspect,
};

static int __init deku_driver_init(void)
{
	inspect_buffer = kmalloc(BUFFER_SIZE * sizeof(inspect_item), GFP_ATOMIC);
	if (!inspect_buffer) {
		pr_err("Can't alloc inspect buffer\n");
		return -1;
	}
	root_dentry = debugfs_create_dir("deku", NULL);
	inspect_dentry = debugfs_create_file("inspect",
					     S_IRUGO, root_dentry, NULL,
					     &inspect_fops);

	INIT_LIST_HEAD(&inspect_map_head.list);
	INIT_LIST_HEAD(&stacktrace_head.list);

	pr_info("DEKU Inspect daemon loaded\n");
	return 0;
}

static void __exit deku_driver_exit(void)
{
	debugfs_remove(inspect_dentry);
	debugfs_remove(root_dentry);
	kfree(inspect_buffer);
	pr_info("DEKU Inspect daemon removed\n");
}

module_init(deku_driver_init);
module_exit(deku_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Maślanka <marek.maslanka@hotmail.com>");
MODULE_DESCRIPTION("Linux driver to manage inspections for DEKU");
MODULE_VERSION("1.0");
