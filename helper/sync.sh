#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

regenerateSymbols()
{
	local files=`find "$SYMBOLS_DIR" -type f`
	[ "$files" == "" ] && return
	while read -r file; do
		file=${file#*$SYMBOLS_DIR/}
		generateSymbols "$BUILD_DIR/$file.o"
	done <<< "$files"
}

main()
{
	echo "Synchronize kernel hot reload"
	echo "Sources dir: $SOURCE_DIR"
	echo "Build dir: $BUILD_DIR"
	echo "Work dir: $workdir"
	echo "Symbols dir: $SYMBOLS_DIR"

	echo "Synchronize..."
	getCurrentKernelVersion > "$KERNEL_VERSION_FILE"
	regenerateSymbols
	generateSymbols "$BUILD_DIR/vmlinux"
	git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" add "$SOURCE_DIR/*"
}

main $@
