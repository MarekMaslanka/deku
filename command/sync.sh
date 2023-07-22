#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

regenerateSymbols()
{
	local files=`find "$SYMBOLS_DIR" -type f`
	[ "$files" == "" ] && return $NO_ERROR
	rm -rf "$SYMBOLS_DIR"
	while read -r file; do
		file=${file#*$SYMBOLS_DIR/}
		generateSymbols "$file.ko"
	done <<< "$files"
}

main()
{
	local run=$1

	if [[ "$KERN_SRC_INSTALL_DIR" ]]; then
		if [[ $run != "auto"  ]]; then
			logInfo "For this configuration, manual synchronization is not required."
			return
		fi
	else
		local modfiles=$(modifiedFiles)
		if [[ $run != "force" && "$modfiles" != "" ]]; then
			# TODO: if files doesn't contains valid changes then allow make the sync
			logErr "Some changes have been made to the source code since the kernel was built. You will need to undo any changes made after the kernel was built, and run 'deku sync' again."
			exit $ERROR_NOT_SYNCED
		fi
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
