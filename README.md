## Table of Contents
[Prerequisites](#prerequisites)  
[Init KernelHotReload](#init)  
[Usage](#usage)  
[Notes](#notes)  

---

<a name="prerequisites"></a>
## Prerequisites
 - Install `exuberant-ctags`
 - Install `libelf`
 - Enable `CONFIG_LIVEPATCH` in kernel config  
 Above flag depends on the `KALLSYMS_ALL` flag that isn't enabled by default.
 - SSH Key-Based authentication to the DUT
***
_**For ChromiumOS developers**_  
 - Install ctags in cros sdk using: `sudo emerge ctags`
 - `libelf` is already installed in cros sdk
 - To enable `CONFIG_LIVEPATCH` flag the following patch can be used:
  ```
diff --git a/chromeos/config/chromeos/x86_64/common.config b/chromeos/config/chromeos/x86_64/common.config
index 4d019ef049de..fe49cf78be28 100644
--- a/chromeos/config/chromeos/x86_64/common.config
+++ b/chromeos/config/chromeos/x86_64/common.config
@@ -32,6 +32,7 @@ CONFIG_CRYPTO_AES_NI_INTEL=y
 CONFIG_CRYPTO_LRW=y
 CONFIG_CRYPTO_SHA256_SSSE3=y
 CONFIG_DEBUG_BOOT_PARAMS=y
+CONFIG_DEBUG_INFO_DWARF4=y
 CONFIG_DMI_SYSFS=y
 CONFIG_E100=m
 CONFIG_E1000=m
@@ -71,13 +72,16 @@ CONFIG_IIO_CROS_EC_LIGHT_PROX=m
 CONFIG_IIO_CROS_EC_SENSORS=m
 CONFIG_IIO_CROS_EC_SENSORS_LID_ANGLE=m
 CONFIG_IIO_CROS_EC_SENSORS_SYNC=m
+CONFIG_INIT_STACK_ALL_ZERO=y
 CONFIG_INTEL_RAPL=y
 CONFIG_INTEL_VBTN=m
 CONFIG_IO_DELAY_0XED=y
 CONFIG_JME=m
+CONFIG_KALLSYMS_ALL=y
 CONFIG_KFENCE=y
 CONFIG_KFENCE_SAMPLE_INTERVAL=500
 CONFIG_LEGACY_VSYSCALL_EMULATE=y
+CONFIG_LIVEPATCH=y
 CONFIG_LPC_ICH=y
 CONFIG_LPC_SCH=y
 CONFIG_LRU_GEN=y
 ```
***
Build and upload kernel on the DUT

<a name="init"></a>
## Init KernelHotReload
Download and go to KernelHotReload directory
```
git clone https://github.com/MarekMaslanka/KernelHotReload.git
cd KernelHotReload
make
```
***
_**For ChromiumOS developers**_  
Download KernelHotReload inside cros sdk environment
***
In the KernelHotReload directory use following command to initialize environment:
```
./kernel_hot_reload.sh -b <PATH_TO_KERNEL_BUILD_DIR> [-b <PATH_TO_KERNEL_SOURCES_DIR>] -d ssh -p <USER@DUT_ADDRESS[:PORT]> init
```
`-b` path to kernel build directory  
`-s` path to kernel sources directory. Use this parameter if initialization process can't find kernel sources dir  
`-d` method used to upload and deploy livepatch modules to the DUT. Currently only `ssh` is supported  
`-p` parameters for deploy method. For the `ssh` deploy method pass user and DUT address. Optional pass the port number.
The given user must be able to load and unload kernel modules. The SSH must be configured to use key-based authentication

***
_**For ChromiumOS developers**_  
Kernel build directory inside the cros sdk is located in `/build/${BOARD}/var/cache/portage/sys-kernel/chromeos-kernel-${KERNEL_VERSION}`

Example usage:  
`./kernel_hot_reload.sh -b /build/atlas/cache/portage/sys-kernel/chromeos-kernel-5_10/ -d ssh -p root@192.168.0.100 init`
***

<a name="usage"></a>
## Usage
Use
```
./kernel_hot_reload.sh deploy
```
to apply changes in code to the kernel on the DUT.

In case the kernel will be rebuilt manually the KernelHotReload must be synchronized with the new build.

Use
```
./kernel_hot_reload.sh sync
```
command to perform synchronization.

To generate kernel livepatch module without deploy it on the target use
```
./kernel_hot_reload.sh build
```
command. Modules can be found in `workdir/khr_XXXX/khr_XXXX.ko`

Alternative to `./kernel_hot_reload.sh deploy/sync/build` commands a `make` can used:
```
make deploy
make sync
make build
```

Changes applied in the kernel on the DUT are not persistent and live to the next reboot. After every reboot the `deploy` must be performed.

<a name="notes"></a>
## Notes
The KernelHotReload is not a perfect tool. Most of the work is done by modifying the source code files, hence source files in kernel with unconventional format may not be supported yet.