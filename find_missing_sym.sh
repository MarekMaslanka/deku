#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

main()
{
	local module=$1
	local moduledir="$workdir/$(filenameNoExt $1)/"

	rm -f $MISS_FUN_FILE
	rm -f $moduledir/$module.ko
	# go to workdir instead of use "-C" because the "$(PWD)" is used in Makefile
	cd $moduledir
	out=`make $USE_LLVM 2>&1`
	rc=$?
	cd $OLDPWD
	local filelog="${moduledir}err.log"
	rm -f "$filelog"
	if [ $rc != 0 ]; then
		# regex for:
		# <PATH>:<LINE>:<COL>: error: implicit declaration of function ‘XYZ’ [-Werror=implicit-function-declaration]
		# <PATH>:<LINE>:<COL>: error: implicit declaration of function 'XYZ' [-Werror,-Wimplicit-function-declaration]
		# <PATH>:<LINE>:<COL>: error: implicit declaration of function ‘XYZ’; did you mean ‘XYX’? [-Werror=implicit-function-declaration]
		# <PATH>:<LINE>:<COL>: error: implicit declaration of function XYZ; did you mean XYX? [-Werror=implicit-function-declaration]
		local regex='^\(.\+\):[0-9]\+:[0-9]\+: error: implicit declaration of function \W\?\(\w\+\)\W\?.*\[.*implicit-function-declaration\]$'
		# <PATH>:<LINE>:<COL>: error: 'XYZ' undeclared (first use in this function); did you mean XYX?
		# <PATH>:<LINE>:<COL>: error: 'XYZ' undeclared here (not in a function); did you mean XYX?
		# <PATH>:<LINE>:<COL>: error: 'XYZ' undeclared (first use in this function)
		local regex2='^\(.\+\):[0-9]\+:[0-9]\+: error: \W\(\w\+\)\W undeclared .\+'
		# <PATH>:<LINE>:<COL>: error: use of undeclared identifier 'XYZ'
		# <PATH>:<LINE>:<COL>: error: use of undeclared identifier 'XYZ'; did you mean 'XYX'?
		local regex3='^\(.\+\):[0-9]\+:[0-9]\+: error: use of undeclared identifier \W\(\w\+\)\W.*'
		# <PATH>:<LINE>:<COL>: error: ‘XYZ’ used but never defined [-Werror]
		local regex4='^\(.\+\):[0-9]\+:[0-9]\+: error: \W\(\w\+\)\W used but never defined.*'
		# <PATH>:<LINE>:<COL>: error: function 'XYZ' has internal linkage but is not defined [-Werror,-Wundefined-internal]
		local regex5='^\(.\+\):[0-9]\+:[0-9]\+: error: function \W\(\w\+\)\W has internal linkage but is not defined.*'
		# <PATH>:<LINE>:<COL>: fatal error: 'XYZ' file not found
		regexAnyErr='^.\+\(\/\w\+\.\w\+\):\([0-9]\+\):[0-9]\+:.* error: \(.\+\)'
		errorcatched=0
		fatalerr=0
		if [ "$MODULE_FROM_SCRATCH" == 1 ]; then
			while read -r line;
			do
				f=`echo $line | grep -e "$regex" | sed "s/$regex/\1\|\2/"`
				[[ "$f" == "" ]] && f=`grep -e "$regex2" <<<$line | sed "s/$regex2/\1\|\2/"`
				[[ "$f" == "" ]] && f=`grep -e "$regex3" <<<$line | sed "s/$regex3/\1\|\2/"`
				[[ "$f" == "" ]] && f=`sed -n "s/$regex4/\1\|\2/p" <<<$line`
				[[ "$f" == "" ]] && f=`sed -n "s/$regex5/\1\|\2/p" <<<$line`
				if [ -n "$f" ]; then
					echo $f >> $MISS_FUN_FILE
					echo $f | awk -F  "|" '{ print "Need to add \"" $2 "\" function" }'
					errorcatched=1
				else
					err=`echo $line | grep -e "$regexAnyErr" | sed "s/$regexAnyErr/\1:\2 \3/"`
					if [ -n "$err" ] && [ ! -e $MISS_FUN_FILE ]; then
						local file=`echo $line | sed "s/$regexAnyErr/\1/" | tr -d "//"`
						local no=`echo $line | sed "s/$regexAnyErr/\2/"`
						local err=`echo $line | sed "s/$regexAnyErr/\3/"`
						echo -e "$file:$no ${RED}error:${NC} $err. See more: $filelog"
						fatalerr=1
						errorcatched=1
					fi
				fi
			done <<< "$out"
		fi

		if [ $errorcatched == 0 ]; then
			fatalerr=1
			echo -e "Unhandled ${RED}error:${NC}"
			while read -r line; do
				echo $line
			done <<< "$out"
		fi
		echo -e "$out" > "$filelog"

		exit $fatalerr
	fi
	# regex for:
	# WARNING: "XYZ" [...] undefined!
	# WARNING: modpost: "XYZ" [...] undefined!
	regex='^WARNING:.* \"\(\w\+\)\" \[\(.\+\)\.ko\] undefined!$'
	echo "$out" |
	while IFS= read -r line; do
		line=`echo $line | grep -e "$regex" | sed "s/$regex/\1 \2/"`
		if [ -n "$line" ]; then
			local sym=`echo $line | cut -d' ' -f1`
			local file=`echo $line | cut -d' ' -f2`
			logDebug "Need to relocate \"$sym\" function in `basename $file`"
			local srcfile=`cat $moduledir/$module.src`
			local objname=$(findObjWithSymbol "$sym" "$srcfile" "$BUILD_DIR")
			if [[ "$objname" == "" ]]; then
				echo "$srcfile|$sym" >> $MISS_FUN_FILE
				continue
			fi
			local relfile="$moduledir/$(filenameNoExt $file).$REL_FILE_EXT"
			echo "$objname.$sym" >> "$relfile"
		fi
	done
}

main $@
