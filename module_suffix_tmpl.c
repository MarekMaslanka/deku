/*
* Author: Marek Maślanka
* Project: DEKU
* URL: https://github.com/MarekMaslanka/deku
*/

void __deku_inspect_register_item(const char *file, unsigned line,
				  const char *text, const char *extra,
				  int valType, int type, u32 id);

static int deku_init(void)
{
	int ret;
#ifdef __DEKU_INSPECT_
#include "inspect_map.h"
#endif /* __DEKU_INSPECT_ */

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

#ifdef __DEKU_INSPECT_
#include "module/deku_inspect.h"
#include "stacktrace.h"
#endif /* __DEKU_INSPECT_ */

module_init(deku_init);
module_exit(deku_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
