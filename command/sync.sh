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
	regenerateSymbols

	if [ "$KERN_SRC_INSTALL_DIR" ]; then
		touch -r "$KERN_SRC_INSTALL_DIR" "$KERNEL_VERSION_FILE"
	else
		git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" add "$SOURCE_DIR/*"
	fi
}

main $@
