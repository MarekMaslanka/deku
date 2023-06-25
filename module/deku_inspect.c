/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/circ_buf.h>
#include <linux/module.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/workqueue.h>
#include <linux/kallsyms.h>
#include <asm/stacktrace.h>

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

typedef struct {
	u32 id;
	char *file;
	unsigned line;
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

#define NETLINK_USER 31

static struct sock *nl_sk = NULL;

void __deku_inspect_register_item(const char *file, unsigned line, const char *text, const char *extra, int type, u32 id);
static inspect_map_item *get_inspect_map_item(unsigned id);

static inspect_map_item inspect_map_head;
static inspect_map_item stacktrace_head;
int pid;

static struct delayed_work *workq;
#define WORKQUEUE_SCHEDULE_DELAY 0/*HZ/100*/

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
	switch (item_map->type) {
	case INSPECT_FUNCTION:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function: %s:%s:%d:%s:%pS\n",
					 item->time, item_map->file, item_map->name, item_map->line, item_map->extra, item->value);
		break;
	case INSPECT_VAR:
	case INSPECT_IF_COND:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: %s:%d %s = %lld\n",
					 item->time, item_map->file, item_map->line, item_map->name, item->value);
		break;
	case INSPECT_FUN_PTR:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function Pointer: %s:%d:%s:%ps\n",
					 item->time, item_map->file, item_map->line, item_map->name, item->value);
		break;
	case INSPECT_RETURN_VALUE:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function return value: %s:%d:%s %s = %lld\n",
					 item->time, item_map->file, item_map->line, item_map->name, item_map->extra, item->value);
		break;
	case INSPECT_RETURN:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function return: %s:%d %s\n",
					 item->time, item_map->file, item_map->line, item_map->name);
		break;
	case INSPECT_FUNCTION_END:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function end: %s:%d:%s\n",
					 item->time, item_map->file, item_map->line, item_map->name);
		break;
	default:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1, "UNKNOWN: %d\n", item_map->type);
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

void consume_logs(void)
{
	bool reschedule = false;
	struct pid *pid_struct;
	unsigned buf_cnt = 0;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned long head;
	unsigned long tail;
	int res;

	if (pid == 0)
		return;

	pid_struct = find_get_pid(pid);
	if (pid_task(pid_struct, PIDTYPE_PID) == NULL) {
		pid = 0;
		return;
	}

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, 0);
	if (!skb) {
		printk(KERN_ERR "Failed to allocate new skb\n");
		return;
	}

	NETLINK_CB(skb).dst_group = 0;
	nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, NLMSG_DEFAULT_SIZE, 0);

	spin_lock_irq(&buf_spinlock);
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
			if (n < 0 || (buf_cnt + n + stackTextLen >= nlmsg_len(nlh) - 1)) {
				reschedule = true;
				break;
			}

			memcpy(((u8 *)nlmsg_data(nlh)) + buf_cnt, consumer_text_buf, n + 1);
			buf_cnt += n;
			if (stackTextLen) {
				strcpy(((u8 *)nlmsg_data(nlh)) + buf_cnt, stackText);
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
	if (buf_cnt == 0) {
		spin_unlock_irq(&buf_spinlock);
		return;
	}
	spin_unlock_irq(&buf_spinlock);
	nlh->nlmsg_len = buf_cnt;
	res = nlmsg_unicast(nl_sk, skb, pid);
	if (reschedule)
		schedule_delayed_work(workq, WORKQUEUE_SCHEDULE_DELAY + HZ/100);
	if (res < 0)
	{
		pid = 0;
		printk(KERN_INFO "Error while sending inspections to daemon. Result: %s\n", res);
		return;
	}
}

static void nl_recv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int res;

	struct list_head *head;
	inspect_map_item *entry;

	nlh = (struct nlmsghdr *)skb->data;
	pid = nlh->nlmsg_pid;

	list_for_each(head, &inspect_map_head.list) {
		entry = list_entry(head, inspect_map_item, list);
		skb_out = nlmsg_new(NLMSG_DEFAULT_SIZE, 0);
		if (!skb_out) {
			printk(KERN_ERR "Failed to allocate new skb\n");
			return;
		}
		nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, NLMSG_DEFAULT_SIZE, 0);
		NETLINK_CB(skb_out).dst_group = 0;
		nlh->nlmsg_len = snprintf(nlmsg_data(nlh), nlmsg_len(nlh), "MAP: %s:%u:%s:%u:%u\n", entry->file, entry->line, entry->name, entry->type, entry->id);
		res = nlmsg_unicast(nl_sk, skb_out, pid);
		if (res < 0)
			printk(KERN_INFO "Error while sending inspect map to user\n");
	}
}

void workq_fn(struct work_struct *work)
{
	consume_logs();
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

void __deku_inspect_register_item(const char *file, unsigned line, const char *text, const char *extra, int type, u32 id)
{
	inspect_map_item *item = kmalloc(sizeof(inspect_map_item), GFP_KERNEL);
	item->file = kmalloc(strlen(file) + 1, GFP_ATOMIC);
	strcpy(item->file, file);
	item->line = line;
	item->name = kmalloc(strlen(text) + 1, GFP_ATOMIC);
	strcpy(item->name, text);
	item->extra = kmalloc(strlen(extra) + 1, GFP_ATOMIC);
	strcpy(item->extra, extra);
	item->id = id;
	item->type = type;
	list_add(&item->list, &inspect_map_head.list);
}
EXPORT_SYMBOL(__deku_inspect_register_item);

void __deku_inspect_fun_exit(void)
{
	if (pid)
		schedule_delayed_work(workq, WORKQUEUE_SCHEDULE_DELAY);
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

static int __init deku_driver_init(void)
{
	struct netlink_kernel_cfg cfg = {
			.input = nl_recv_msg,
	};
	inspect_buffer = kmalloc(BUFFER_SIZE * sizeof(inspect_item), GFP_ATOMIC);
	INIT_LIST_HEAD(&inspect_map_head.list);
	INIT_LIST_HEAD(&stacktrace_head.list);
	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
	if (!nl_sk) {
	    printk(KERN_ALERT "Error creating socket.\n");
	    goto r_netlink;
	}

	workq = kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
	INIT_DELAYED_WORK(workq, workq_fn);

	pr_info("DEKU Inspect daemon loaded\n");
	return 0;

r_netlink:
	kfree(inspect_buffer);
	return -1;
}

static void __exit deku_driver_exit(void)
{
	flush_delayed_work(workq);
	netlink_kernel_release(nl_sk);
	kfree(inspect_buffer);
	pr_info("DEKU Inspect daemon removed\n");
}

module_init(deku_driver_init);
module_exit(deku_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Maślanka <marek.maslanka@hotmail.com>");
MODULE_DESCRIPTION("Linux driver to manage inspections for DEKU");
MODULE_VERSION("1.0");
