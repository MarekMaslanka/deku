# Author: Marek Maślanka
# Project: KernelHotReload

export workdir='workdir'

# extension for file with list of functions to replace
export SYMBOLS_FILE_EXT=sym

# file with functions that needs to be added from source file
export MISS_FUN_FILE=$workdir/miss

# extension for file with functions that need relocations
export REL_FILE_EXT=rel

# extension for intermediate source code file
export ISRC_FILE_EXT=_.c

# file for note in module
export NOTE_FILE=note

# dir with kernel's object symbols
export SYMBOLS_DIR=$workdir/symbols

# configuration file
export CONFIG_FILE=$workdir/config

# template for hotreload module suffix
export MODULE_SUFFIX_FILE=module_suffix_tmpl.c

# hot reload script
export HOTRELOAD_SCRIPT=khr_reload.sh

# file where kernel version is stored
export KERNEL_VERSION_FILE="$workdir/version"

# commands script dir
export COMMANDS_DIR=command

# generate new module instead manipulate on .relah
export MODULE_FROM_SCRATCH=1

# is inside chromeos sdk
export CHROMEOS_CHROOT=0

# log level filter
export LOG_LEVEL=0 # 0 - debug, 1 - info, 2 - warning, 3 - error

#colors
export RED='\033[0;31m'
export GREEN='\033[0;32m'
export ORANGE='\033[0;33m'
export NC='\033[0m' # No Color