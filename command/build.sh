#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

main()
{
	local syncversion=$(<"$KERNEL_VERSION_FILE")
	local localversion=$(getKernelVersion)
	if [[ "$syncversion" != "$localversion" ]]; then
		logWarn "Kernel image in the build directory has changed from last run. You must undo any changes made after the kernel was built and run 'make sync' again."
		exit $ERROR_NOT_SYNCED
	fi

	# remove old modules from workdir
	local validmodules=()
	for file in $(modifiedFiles)
	do
		validmodules+=$(generateModuleName "$file")
	done
	local modules=`find "$workdir" -type d -name "deku_*"`
	while read moduledir
	do
		[[ $moduledir == "" ]] && break
		local module=`basename $moduledir`
		[[ ! " ${validmodules[*]} " =~ "$module" ]] && rm -rf "$moduledir"
	done <<< "$modules"

	logDebug "Build DEKU module"

	bash generate_hotreload.sh
	return $?
}

main $@
