#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./header.sh
. ./common.sh

OPTIND=1         # Reset in case getopts has been used previously in the shell.

isKernelSroucesDir()
{
	dir=$1
	if [ ! -f "$dir/Kbuild" ]; then
		return 1
	fi
	if [ ! -f "$dir/Kconfig" ]; then
		return 1
	fi
	if [ ! -f "$dir/Makefile" ]; then
		return 1
	fi
	return 0
}

isKernelBuildDir()
{
	local dir=$1

	local usellvm=
	isLLVMUsed "$dir" && usellvm=1

	local tmpdir=$(mktemp -d init-XXX --tmpdir=$workdir)
	cat > "$tmpdir/test.c" <<- EOF
	#include <linux/kernel.h>
	#include <linux/module.h>
	#include <linux/livepatch.h>
	static int hotreload_init(void)
	{
		return klp_enable_patch(NULL);
	}
	static void hotreload_exit(void)
	{
	}
	module_init(hotreload_init);
	module_exit(hotreload_exit);
	MODULE_INFO(livepatch, "Y");
	MODULE_LICENSE("GPL");
	EOF

	echo "obj-m += test.o" > "$tmpdir/Makefile"
	echo "all:" >> "$tmpdir/Makefile"
	echo "	make -C $1 M=\$(PWD)/$tmpdir modules" >> "$tmpdir/Makefile"
	res=`make -C $tmpdir LLVM=$usellvm 2>&1`
	rc=$?
	rm -rf $tmpdir
	if [ $rc -ne 0 ]; then
		local kplerr=`echo "$res" | grep "klp_enable_patch"`
		if [ -n "$kplerr" ]; then
			return 2
		else
			logErr "$res"
			return 1
		fi
	fi
	return 0
}

isLLVMUsed()
{
	local builddir=$1
	grep -q "clang" "$builddir/vmlinux"
}

enableKLP()
{
	local sourcesdir=$1
	local configfile="$sourcesdir/chromeos/config/x86_64/common.config"
	[ ! -f "$configfile" ] && configfile="$sourcesdir/chromeos/config/chromeos/x86_64/common.config"
	[ ! -f "$configfile" ] && configfile="$builddir/.config"
	[ ! -f "$configfile" ] && return 1
	local flags=("CONFIG_KALLSYMS_ALL" "CONFIG_LIVEPATCH")
	for flag in "${flags[@]}"
	do
		bash $sourcesdir/scripts/config --file $configfile --enable $flag
	done
	grep -q "CONFIG_LIVEPATCH" "$configfile" && return 0
	return 1
}

main()
{
	local builddir="."
	local sourcesdir="."
	local deploytype=""
	local deployparams=""
	local workdir='workdir'

	[[ "$1" != "-" ]] && builddir=$1
	[[ "$2" != "-" ]] && sourcesdir=$2
	[[ "$3" != "-" ]] && deploytype=$3
	[[ "$4" != "-" ]] && deployparams=$4
	[[ "$5" != "-" ]] && workdir=$5

	builddir=${builddir%/}
	workdir=${workdir%/}

	[[ "$deploytype" == "" ]] && { logErr "Please specify deploy type -d [ssh]"; exit 2; }
	[[ "$deployparams" == "" ]] && { logErr "Please specify parameters for deploy \"$deploytype\". Use -p paramer"; exit 1; }

	[ -L "$builddir/source" ] && sourcesdir="$builddir/source"
	echo "Check for kernel sources in: $sourcesdir"
	isKernelSroucesDir $sourcesdir || sourcesdir="$builddir"

	sourcesdir=${sourcesdir%/}
	
	[ "$(git --version)" ] || { logErr "\"git\" could not be found. Please install \"git\""; exit 2; }
	[ "$(ctags --version)" ] || { logErr "\"ctags\" could not be found. Please install \"exuberant-ctags\""; exit 1; }

	echo "Initialize kernel hot reload"
	echo "Sources dir: $sourcesdir"
	echo "Build dir: $builddir"
	echo "Work dir: $workdir"

	if [ -d "$workdir" ]
	then
		[ "$(ls -A $workdir)" ] && { logErr "Director \"$workdir\" is not empty"; exit 2; }
	else
		mkdir -p "$workdir"
	fi

	isKernelBuildDir $builddir
	local res=$?
	if [[ $res != 0 ]]; then
		if [[ $res == 2 ]]; then
			logErr "Kernel livepatching is not enabled. Please enable CONFIG_LIVEPATCH flag and rebuild the kernel"
			echo "Would you like to try enable this flag now? [y/n]"
			while true; do
				read -p "" yn
				case $yn in
					[Yy]* )
						enableKLP "$sourcesdir" && { logInfo "Flag was enabled. Pleas rebuild the kernel and try again."; exit 1; } || "Failed do enable the flag. Please enable it manually."
						break;;
					[Nn]* ) exit 2;;
					* ) echo "Please answer [y]es or [n]o.";;
				esac
			done
		elif [ "$builddir" = "." ]; then
			logErr "Current directory is not a kernel build directory"
		else
			logErr "Given directory is not a kernel build directory: \"$builddir\""
		fi
		exit 2
	fi

	if ! isKernelSroucesDir $sourcesdir; then
		if [ "$sourcesdir" = "." ]; then
			logErr "Current directory is not a kernel srouces directory"
		else
			logErr "Given directory does not contains valid kernel sources: \"$sourcesdir\""
		fi
		exit 2
	fi

	if [ ! -f "deploy/$deploytype.sh" ]; then
		logErr "Unknown deploy type '$deploytype'"
		exit 2
	fi

	echo "BUILD_DIR=\"$builddir\"" > $CONFIG_FILE
	echo "SOURCE_DIR=\"$sourcesdir\"" >> $CONFIG_FILE
	echo "DEPLOY_TYPE=\"$deploytype\"" >> $CONFIG_FILE
	echo "DEPLOY_PARAMS=\"$deployparams\"" >> $CONFIG_FILE
	isLLVMUsed $builddir && echo "USE_LLVM=\"LLVM=1\"" >> $CONFIG_FILE
	echo "" >> $CONFIG_FILE
	git --work-tree="$sourcesdir" --git-dir="$workdir/.git" init

	mkdir -p "$SYMBOLS_DIR"
}

main "$@"
