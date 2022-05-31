#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

main()
{
	local modules=`bash generate_module.sh`
	if [ -z "$modules" ]; then
		echo "No modification detected"
		exit 1
	fi
	bash ./build_and_fix_module.sh "$modules"
	if [ $? -ne 0 ]; then
		logFatal "Abort!"
		exit 2
	fi

	while read -r module; do
		local args=""
		local relfile="$workdir/$module/$module.$REL_FILE_EXT"
		local kofile="$workdir/$module/$module.ko"
		logDebug "Processing $module"
		if [ -e "$relfile" ]; then
			while read -r sym; do
				args+="-s $sym "
			done < "$workdir/$module/$module.$SYMBOLS_FILE_EXT"

			while read -r rel; do
				args+="-r $rel,0 "
			done < "$relfile"

			[ "$MODULE_FROM_SCRATCH" != 1 ] && args+="-d "
			[[ "$LOG_LEVEL" > 0 ]] && args+="-V "
			args+="$kofile"
			logDebug "Make livepatch module"
			./mklivepatch $args
			if [ $? -ne 0 ]; then
				logFatal "Abort!"
				exit 2
			fi
		else
			logDebug "Module does not need to fix relocations"
		fi
	done <<< "$modules"
	logInfo "Generate primary module. Done"
}

main $@
exit $?
