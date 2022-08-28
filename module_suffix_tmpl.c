/*
* Author: Marek Maślanka
* Project: KernelHotReload
* URL: https://github.com/MarekMaslanka/KernelHotReload
*/

static int hotreload_init(void)
{
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	ret = klp_enable_patch(&khr_patch);
		return ret;
#else
	ret = klp_register_patch(&khr_patch);
	if (ret)
		return ret;
	ret = klp_enable_patch(&khr_patch);
	if (ret) {
		WARN_ON(klp_unregister_patch(&khr_patch));
		return ret;
	}
	return 0;
#endif
}

static void hotreload_exit(void)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 11, 0)
	klp_disable_patch(&khr_patch);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
	WARN_ON(klp_unregister_patch(&khr_patch));
#endif
}

module_init(hotreload_init);
module_exit(hotreload_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");