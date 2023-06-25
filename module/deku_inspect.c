/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/list.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include<linux/sysfs.h>
#include<linux/kobject.h>
#include <linux/err.h>

#include <linux/circ_buf.h>

#include <linux/module.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#include <linux/sched.h>
#include <linux/pid.h>

#include <linux/workqueue.h>

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

#define SYSFS_KERNEL_DIR "deku_inspect"
#define SYSFS_MESSAGES_FILE messages

#define NETLINK_USER 31

static struct sock *nl_sk = NULL;

static struct kobject *kobj_ref;

static ssize_t  sysfs_show(struct kobject *kobj,
	                    struct kobj_attribute *attr, char *buf);
static ssize_t  sysfs_store(struct kobject *kobj,
	                    struct kobj_attribute *attr,const char *buf, size_t count);
void __deku_inspect_register_item(const char *file, unsigned line, const char *text, const char *extra, int type, u32 id);
static inspect_map_item *get_inspect_map_item(unsigned id);

struct kobj_attribute messages_attr = __ATTR(SYSFS_MESSAGES_FILE, 0660, sysfs_show, sysfs_store);

static inspect_map_item inspect_map_head;
int pid;

static struct delayed_work *workq;
#define WORKQUEUE_SCHEDULE_DELAY 0/*HZ/100*/

u8 consumer_text_buf[PAGE_SIZE];

#define BUFFER_SIZE 4096
#define BUFFER_CNT(head, tail, size) (((head) - (tail)) & ((size)-1))
u8 *BufferX = NULL;
u8 *tail_bufX = NULL, *head_bufX = NULL;
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
	if (circ_space(&buf_crc) >= 1) {
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

u8 ConsumeBuf[PAGE_SIZE];

static int formatInspect(inspect_map_item *item_map, inspect_item *item)
{
	int n = 0;
	switch (item_map->type) {
	case INSPECT_FUNCTION:
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] DEKU Inspect: Function: %s:%s:%d:%s\n",
					 item->time, item_map->file, item_map->name, item_map->line, item_map->extra);
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

void consume_logs(void)
{
	bool reschedule = false;
	struct pid *pid_struct;
	unsigned buf_cnt = 0;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned long head;
	unsigned long tail;
	unsigned n;
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
			n = formatInspect(item_map, &item);
			if (n < 0 || buf_cnt + n >= nlmsg_len(nlh) - 1) {
				reschedule = true;
				break;
			}

			memcpy(((u8 *)nlmsg_data(nlh)) + buf_cnt, consumer_text_buf, n + 1);
			buf_cnt += n;
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

#if 0
void *__deku_inspect_get_memoryX(u32 id, unsigned size)
{
	unsigned data_size;
	u8 *result;
	spin_lock_irq(&buf_spinlock);
	result = head_buf;
	if (size < 4) size = 4;
	pr_info("GET memory for id: %d size: %d\n", id, size);
	data_size = sizeof(u32) + sizeof(u32) + sizeof(u64) + size;
	if ((BUFFER_SIZE - BUFFER_CNT(head_buf, tail_buf, BUFFER_SIZE)) < data_size){
			pr_info("============================= DISCARD REMAINING DATA 0: %p = %p ============\n",tail_buf, head_buf);
			/*
			The consumer does not consume data. Let's discard the data left
			behind
			*/
			tail_buf = head_buf;
	}
	if ((head_buf - Buffer) + data_size > BUFFER_SIZE) {
		pr_info("============================= ROLL OUT: ID: %d %d (%d < %d) ============\n", id, (head_buf - Buffer), tail_buf - Buffer, data_size);
		if ((head_buf - Buffer) < BUFFER_SIZE)
			/*
			We can safe assume that there is at least 4 bytes free in the
			buffer to write a 0 as a size beacuse minimal "size" argument is 4
			*/
			*((u32 *)head_buf) = 0;
		result = Buffer;
		if ((tail_buf - Buffer) < data_size) {
			pr_info("============================= DISCARD REMAINING DATA 1: %p = %p ============\n",tail_buf, Buffer);
			/*
			The consumer does not consume data. Let's discard the data left
			behind
			*/
			tail_buf = Buffer;
		}
	}
	head_buf = result + data_size;
	*((u32 *)result) = size;
	result += sizeof(u32);
	*((u32 *)result) = id;
	result += sizeof(u32);
	*((u64 *)result) = ktime_get_boottime_ns();
	result += sizeof(u64);
	spin_unlock_irq(&buf_spinlock);
	return (void *)result;
}
EXPORT_SYMBOL(__deku_inspect_get_memoryX);

u8 ConsumeBuf[PAGE_SIZE];
void consume_logsX(void)
{
	unsigned buf_size = 0;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned size;
	unsigned id;
	unsigned n;
	u64 value;
	u64 time;
	u8 *buf;
	int res;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, 0);
	if (!skb) {
		printk(KERN_ERR "Failed to allocate new skb\n");
		return;
	}
	nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, NLMSG_DEFAULT_SIZE, 0);
	spin_lock_irq(&buf_spinlock);
	buf = tail_buf;
	if (buf == head_buf)
		printk(KERN_INFO "No data to consume\n");

	while(buf != head_buf) {
		u8 *tmp_buf = buf;
		if (buf == (Buffer + BUFFER_SIZE)) {
			buf = Buffer;
			pr_info("============================= CONSUMER ROLL OUT 0 ============ %p (%d)\n", buf, (int)*buf);
			continue;
		}
		size = *((u32 *)tmp_buf);
		if (size == 0) {
			buf = Buffer;
			pr_info("============================= CONSUMER ROLL OUT 1 ============ %p (%d)\n", buf, (int)*buf);
			continue;
		}
		tmp_buf += sizeof(u32);
		id = *((u32 *)tmp_buf);
		tmp_buf += sizeof(u32);
		time = *((u64 *)tmp_buf);
		tmp_buf += sizeof(u64);
		value = 0;
		pr_info("%p / %p (%d/%d) ID: %d SIZE: %d\n", buf, head_buf, BUFFER_CNT(head_buf, tail_buf, BUFFER_SIZE), BUFFER_CNT(head_buf, buf, BUFFER_SIZE), id, size);
		memcpy(&value, tmp_buf, size <= 8 ? size : 8);
		n = snprintf(consumer_text_buf, sizeof(consumer_text_buf) - 1,
					 "[%llu] Deku Inspect: id:%d size:%d value:%lld\n",time, id, size, value);
		if (n < 50)
			pr_info("============================= Invalid snprintf size: %d\n", n);
		if (buf_size + n >= NLMSG_DEFAULT_SIZE) {
			pr_info("No more space in out buffer\n");
			break;
		}
		memcpy(((u8 *)nlmsg_data(nlh)) + buf_size, consumer_text_buf, n);
		buf_size += n;
		buf = tmp_buf + size;
	}
	if (buf_size == 0) {
		spin_unlock_irq(&buf_spinlock);
		printk(KERN_INFO "buf_size == 0\n");
		return;
	}

	pr_info("Send: buf_size:%d / %d (%d) %p\n", buf_size, BUFFER_CNT(head_buf, buf, BUFFER_SIZE), head_buf == buf, buf);
	NETLINK_CB(skb).dst_group = 0; /* not in mcast group */

tail_buf = buf;
spin_unlock_irq(&buf_spinlock);
	if (pid) {
		res = nlmsg_unicast(nl_sk, skb, pid);
		if (res < 0)
		{
			// spin_unlock_irq(&buf_spinlock);
			printk(KERN_INFO "Error while sending inspections to daemon\n");
			return;
		}
	}
}
#endif

static void nl_recv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int msg_size;
	char *msg = "Hello from kernel";
	int res;

	struct list_head *head;
	inspect_map_item *entry;

	msg_size = strlen(msg);

	nlh = (struct nlmsghdr *)skb->data;
	printk(KERN_INFO "Netlink received msg payload:%s\n", (char *)nlmsg_data(nlh));
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
			printk(KERN_INFO "Error while sending bak to user\n");
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

static ssize_t sysfs_show(struct kobject *kobj,
	            struct kobj_attribute *attr, char *buf)
{
	    struct list_head *listptr;
	    inspect_map_item *entry;
	    ssize_t result = 0;
	    list_for_each(listptr, &inspect_map_head.list) {
	            entry = list_entry(listptr, inspect_map_item, list);
	            result += sprintf(buf + result, "name = %s\n", entry->name);
	    }
	    return result;
}

static ssize_t sysfs_store(struct kobject *kobj,
	            struct kobj_attribute *attr,const char *buf, size_t count)
{
	return 0;
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

static int __init deku_driver_init(void)
{
	struct netlink_kernel_cfg cfg = {
			.input = nl_recv_msg,
	};
	inspect_buffer = kmalloc(BUFFER_SIZE * sizeof(inspect_item), GFP_ATOMIC);
	// Buffer = kmalloc(BUFFER_SIZE, GFP_ATOMIC);
	// head_buf = tail_buf = Buffer;
	INIT_LIST_HEAD(&inspect_map_head.list);

	/*Creating a directory in /sys/kernel/ */
	kobj_ref = kobject_create_and_add(SYSFS_KERNEL_DIR, kernel_kobj);

	/*Creating sysfs file for deku_value*/
	if(sysfs_create_file(kobj_ref, &messages_attr.attr)){
			pr_err("Cannot create sysfs file......\n");
			goto r_sysfs;
	}

	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
	if (!nl_sk) {
	    printk(KERN_ALERT "Error creating socket.\n");
	    return -10;
	}

	workq = kmalloc(sizeof(struct delayed_work), GFP_KERNEL);
	INIT_DELAYED_WORK(workq, workq_fn);

	pr_info("DEKU Inspect daemon loaded\n");
	return 0;

r_sysfs:
	kfree(inspect_buffer);
	kobject_put(kobj_ref);
	sysfs_remove_file(kernel_kobj, &messages_attr.attr);
	return -1;
}

static void __exit deku_driver_exit(void)
{
	flush_delayed_work(workq);
	netlink_kernel_release(nl_sk);
	kobject_put(kobj_ref);
	sysfs_remove_file(kernel_kobj, &messages_attr.attr);
	// kfree(Buffer);
	kfree(inspect_buffer);
	pr_info("DEKU Inspect daemon removed\n");
}

module_init(deku_driver_init);
module_exit(deku_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Maślanka <marek.maslanka@hotmail.com>");
MODULE_DESCRIPTION("Linux device driver to manage inspections for DEKU");
MODULE_VERSION("1.0");
