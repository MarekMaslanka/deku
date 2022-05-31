#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

validateKernels()
{
	local kernelrelease=`bash deploy/$DEPLOY_TYPE.sh --kernel-release`
	local kernelversion=`bash deploy/$DEPLOY_TYPE.sh --kernel-version`
	local localversion=$(getCurrentKernelVersion)
	local mismatch=""
	[[ $localversion != *"$kernelrelease"* ]] && mismatch="release:$kernelrelease"
	[[ $mismatch != "" ]] && [[ $localversion != *"$kernelrelease"* ]] && mismatch="version:$kernelversion"
	[[ $mismatch == "" ]] && return
	>&2 echo -e "${RED}Kernel image mismatch ($mismatch).${NC}"
	>&2 echo "Kernel on the device: $kernelrelease $kernelversion"
	>&2 echo "Kernel in the build directory: $localversion"
	return 1
}

main()
{
	if [ "$DEPLOY_TYPE" == "" ] || [ "$DEPLOY_PARAMS" == "" ]; then
		echo -e "${ORANGE}Please setup connection parameters to target device${NC}"
		exit
	fi
	validateKernels

	bash $HELPERS_DIR/build.sh
	local res=$?
	[ $res != 0 ] && [ $res != 1 ] && exit 1

	# find modules need to upload and unload
	local modulestoupload=()
	local modulesontarget=()
	local modulestounload=()
	while read -r line
	do
		[[ "$line" == "" ]] && break
		local module=${line% *}
		local id=${line##* }
		local moduledir="$workdir/$module/"
		[ ! -f "$moduledir/$module.id" ] && { modulestounload+=" -$module"; continue; }
		local localid=`cat "$moduledir/$module.id"`
		# to invalidate remove module from workdir if has been changed 
		[ "$id" == "$localid" ] && modulesontarget+=$module
	done <<< $(bash deploy/$DEPLOY_TYPE.sh --getids)

	while read -r moduledir
	do
		[[ "$moduledir" == "" ]] && break
		local module=`basename $moduledir`
		[[ ! "${modulesontarget[*]}" =~ "${module}" ]] && modulestoupload+="$moduledir/$module.ko "
	done <<< "`find $workdir -type d -name khr_*`"

	if ((${#modulestoupload[@]} == 0)) && ((${#modulestounload[@]} == 0)); then
		echo "No modules need to upload"
		return
	fi

	modulestoupload=${modulestoupload[@]}
	modulestounload=${modulestounload[@]}
	bash "deploy/$DEPLOY_TYPE.sh" $modulestoupload $modulestounload
	res=$?
	[ $res != 0 ] && echo -e "${RED}Failed!${NC}"
	return $res
}

main $@
exit $?