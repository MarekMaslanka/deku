#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

regenerateSymbols()
{
	local files=`find "$SYMBOLS_DIR" -type f`
	[ "$files" == "" ] && return $NO_ERROR
	while read -r file; do
		file=${file#*$SYMBOLS_DIR/}
		generateSymbols "$MODULES_DIR/$file.ko"
	done <<< "$files"
}

generateInspectModule()
{
	logInfo "Prepare inspect module..."
	local makefile="$workdir/inspect/Makefile"

	rm -rf "$workdir/inspect"
	mkdir -p "$workdir/inspect"

	echo "obj-m := deku_inspect.o" >> $makefile
	echo "all:" >> $makefile
	echo "	make -C $LINUX_HEADERS M=\$(PWD) modules" >> $makefile
	echo "clean:" >> $makefile
	echo "	make -C $LINUX_HEADERS M=\$(PWD) clean" >> $makefile

	cp module/* "$workdir/inspect"
	cd "$workdir/inspect"
	make $USE_LLVM 2>&1 > /dev/null
	cd $OLDPWD
}

main()
{
	local run=$1

	if [[ $run != "auto" && "$KERN_SRC_INSTALL_DIR"  ]]; then
		logInfo "For this configuration, manual synchronization is not required."
		return
	fi

	logInfo "Synchronize..."
	rm -rf "$workdir"/deku_*
	getKernelVersion > "$KERNEL_VERSION_FILE"
	generateInspectModule
	regenerateSymbols

	if [ "$KERN_SRC_INSTALL_DIR" ]; then
		touch -r "$KERN_SRC_INSTALL_DIR" "$KERNEL_VERSION_FILE"
	else
		git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" add "$SOURCE_DIR/*"
	fi
}

main $@
