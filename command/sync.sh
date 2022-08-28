#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload
# URL: https://github.com/MarekMaslanka/KernelHotReload

regenerateSymbols()
{
	local files=`find "$SYMBOLS_DIR" -type f`
	[ "$files" == "" ] && return
	while read -r file; do
		file=${file#*$SYMBOLS_DIR/}
		generateSymbols "$MODULES_DIR/$file.ko"
	done <<< "$files"
}

main()
{
	logInfo "Synchronize..."
	rm -rf "$workdir"/khr_*
	getKernelVersion > "$KERNEL_VERSION_FILE"
	regenerateSymbols
	git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" add "$SOURCE_DIR/*"
}

main $@
