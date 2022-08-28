#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload
# URL: https://github.com/MarekMaslanka/KernelHotReload

main()
{
	local syncversion=$(<"$KERNEL_VERSION_FILE")
	local localversion=$(getKernelVersion)
	if [[ "$syncversion" != "$localversion" ]]; then
		logWarn "Kernel image in build directory has changed from last run. You must undo any changes made after kernel was rebuild and run 'make sync'."
		exit 2
	fi

	# remove old modules from workdir
	local validmodules=()
	for file in $(modifiedFiles)
	do
		validmodules+=$(generateModuleName "$file")
	done
	while read moduledir
	do
		[[ $moduledir == "" ]] && break
		local module=`basename $moduledir`
		[[ ! " ${validmodules[*]} " =~ "$module" ]] && rm -rf  "$moduledir"
	done <<< "`find $workdir -type d -name khr_*`"

	echo "Build hot reload module"

	bash generate_hotreload.sh
	local res=$?
	if [ $res == 1 ]; then
		return 1
	fi
	if [ $res -ne 0 ]; then
		exit 2
	fi
	return 0
}

main $@
exit $?
