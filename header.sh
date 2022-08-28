# Author: Marek Maślanka
# Project: KernelHotReload
# URL: https://github.com/MarekMaslanka/KernelHotReload

# default name for workdir
export DEFAULT_WORKDIR=workdir

# file with modiefied symbols
export MOD_SYMBOLS_FILE=modsym

# file with module name where file was built-in
export FILE_OBJECT=obj

# file with source file path
export FILE_SRC_PATH=path

# file for note in module
export NOTE_FILE=note

# dir with kernel's object symbols
export SYMBOLS_DIR="$workdir/symbols"

# configuration file
export CONFIG_FILE="$workdir/config"

# template for hotreload module suffix
export MODULE_SUFFIX_FILE=module_suffix_tmpl.c

# hot reload script
export HOTRELOAD_SCRIPT=khr_reload.sh

# prefix for functions that manages hotreload
export KHR_FUN_PREFIX="__khr_fun_"

# file where kernel version is stored
export KERNEL_VERSION_FILE="$workdir/version"

# commands script dir
export COMMANDS_DIR=command

# is inside chromeos sdk
export CHROMEOS_CHROOT=0

# log level filter
export LOG_LEVEL=0 # 0 - debug, 1 - info, 2 - warning, 3 - error

#colors
export RED='\033[0;31m'
export GREEN='\033[0;32m'
export ORANGE='\033[0;33m'
export NC='\033[0m' # No Color