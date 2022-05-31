#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

SKIP_ON_FAILURE=""

processModule()
{
	local module=$1
	local moduledir="$workdir/$(filenameNoExt $1)/"

	rm -f $MISS_FUN_FILE

	# check in loop if module have a missing symbols and resolve them
	# simple condition to avoid unexpected infinity loop
	local count=0
	while [ $count -lt 1000 ]; do
		local sumbefore=""
		[ -e $MISS_FUN_FILE ] && sumbefore=`md5sum "$MISS_FUN_FILE"`
		bash find_missing_sym.sh $module
		if [ $? -ne 0 ] && [[ "$SKIP_ON_FAILURE" != "1" ]]; then
			return 1
		fi

		if [ -e $MISS_FUN_FILE ]; then
			local sum=`md5sum "$MISS_FUN_FILE"`
			if [[ "$sum" == "$sumbefore" ]]; then
				echo -e "${ORANGE}Can't resolve missing symbols for $module${NC}"
				if [[ "$SKIP_ON_FAILURE" != "1" ]]; then
					local err=$(<$moduledir/err.log)
					err=${err//error:/${RED}error:${NC}}
					echo -e "$err"
					return 2
				fi
				break
			fi
			while read -r line; do
				# fetch missing function from temp source file
				local srcfile=`echo $line | cut -d'|' -f1`
				local miss=`echo $line | cut -d'|' -f2`

				echo "Adding \"$miss\" to `basename $srcfile`"
				restoreSymbol "$module" $miss
				[ $? -ne 0 ] && break
			done < $MISS_FUN_FILE
		else
			rm -f "$moduledir/err.log"
			echo "All functions are resolved in $module"
			break
		fi
		((count++))
	done
	if [ "$MODULE_FROM_SCRATCH" != 1 ]; then
		objcopy -R __ksymtab "$moduledir/$module.ko"
		objcopy -R __ksymtab_gpl "$moduledir/$module.ko"
	fi
	[ ! -f "$moduledir/$module.ko" ] && { >&2 echo "Module '$moduledir/$module.ko' does not exists!"; return 3; }
	local srcfile=`cat "$moduledir/$module.src"`
	generateAndSaveModuleId "$SOURCE_DIR/$srcfile" "$module"
	return 0
}

main()
{
	for i in "$@"; do
		processModule $i
		[ $? -ne 0 ] && return 3
	done
	return 0
}

main $@
exit $?
