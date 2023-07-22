#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku
#
# Common functions

logDebug()
{
	[[ "$LOG_LEVEL" > 0 ]] && return
	echo "[DEBUG] $1"
}
export -f logDebug

logInfo()
{
	[[ "$LOG_LEVEL" > 1 ]] && return
	echo "$1"
}
export -f logInfo

logWarn()
{
	[[ "$LOG_LEVEL" > 2 ]] && return
	echo -e "$ORANGE$1$NC"
}
export -f logWarn

logErr()
{
	echo -e "$1" >&2
}
export -f logErr

logFatal()
{
	echo -e "$RED$1$NC" >&2
}
export -f logFatal

filenameNoExt()
{
	[[ $# = 0 ]] && set -- "$(cat -)" "${@:2}"
	local basename=`basename "$1"`
	echo ${basename%.*}
}
export -f filenameNoExt

generateSymbols()
{
	local kofile=$1
	local path=`dirname $kofile`
	if ! grep -q "\b$kofile\b" "$MODULES_DIR/$path/modules.order"; then
		logDebug "The module $kofile file is not enabled in current kernel configuration"
		return
	fi
	local outfile="$SYMBOLS_DIR/$path/"
	mkdir -p "$outfile"
	outfile+=$(filenameNoExt "$kofile")

	readelf --symbols --wide "$MODULES_DIR/$kofile" | \
	awk 'BEGIN { ORS=" " } { if($4 == "FUNC" || $4 == "OBJECT") {printf "%s\n",$8} }' \
	> "$outfile"
}
export -f generateSymbols

findObjWithSymbol()
{
	local sym=$1
	local srcfile=$2

	#TODO: Consider checking type of the symbol

	local srcpath=$SOURCE_DIR/
	local modulespath=$MODULES_DIR/
	srcpath+=`dirname $srcfile`
	modulespath+=`dirname $srcfile`
	while true; do
		local files=`find "$modulespath" -maxdepth 1 -type f -name "*.ko"`
		if [ "$files" != "" ]; then
			while read -r file; do
				file=${file#*$MODULES_DIR/}
				symfile=$(filenameNoExt "$file")
				[ -f "$SYMBOLS_DIR/$symfile" ] && continue
				generateSymbols $file
			done <<< "$files"

			out=`grep -lr "\b$sym\b" $SYMBOLS_DIR`
			[ "$out" != "" ] && { echo $(filenameNoExt "$out"); return $NO_ERROR; }
		fi
		[ -f "$srcpath/Kconfig" ] && break
		srcpath+="/.."
		modulespath+="/.."
	done

	grep -q "\b$sym\b" "$SYSTEM_MAP" && { echo vmlinux; return $NO_ERROR; }

	exit $ERROR_CANT_FIND_SYMBOL
}
export -f findObjWithSymbol

getKernelVersion()
{
	grep -r UTS_VERSION "$LINUX_HEADERS/include/generated/" | \
	sed -n "s/.*UTS_VERSION\ \"\(.\+\)\"$/\1/p"

}
export -f getKernelVersion

getKernelReleaseVersion()
{
	grep -r UTS_RELEASE "$LINUX_HEADERS/include/generated/" | \
	sed -n "s/.*UTS_RELEASE\ \"\(.\+\)\"$/\1/p"
}
export -f getKernelReleaseVersion

# find modified files
modifiedFiles()
{
	if [[ "$CASHED_MODIFIED_FILES" ]]; then
		echo "$CASHED_MODIFIED_FILES"
		return
	fi

	if [ ! "$KERN_SRC_INSTALL_DIR" ]; then
		local quickfind=1

		if [[ $quickfind ]]; then
			local ignorearrayh=(
								arch/x86/realmode/rm/pasyms.h
								arch/x86/boot/voffset.h
								arch/x86/boot/cpustr.h
								arch/x86/boot/zoffset.h
								)
			local ignorearrayc=(
								arch/x86/entry/vdso/vdso-image-32.c
								arch/x86/entry/vdso/vdso-image-64.c
								)
			local ignoreh=
			local ignorec=
			for file in ${ignorearrayh[@]}; do
				ignoreh+="! -iwholename ./$file "
			done
			for file in ${ignorearrayc[@]}; do
				ignorec+="! -iwholename ./$file "
			done
			ignorec+="! -iname \"*.mod.c\" "
			cd "$SOURCE_DIR/"
			local files=`find . -not \( -path ./include/generated -prune -o -path ./scripts -prune \) \
								-type f \( -iname "*.c" $ignorec -or -iname "*.h" $ignoreh \) \
								-newer "$BUILD_DIR/.config"`
			cd $OLDPWD

			while read -r file; do
				echo "${file:2}"
			done <<< "$files"

			return
		fi

		cd "$BUILD_DIR/"
		local ofiles=`find . -type f -name "*.o"`
		cd $OLDPWD
		cd "$SOURCE_DIR/"
		local hfiles=`find . -type f -name "*.h" -newer "$BUILD_DIR/.config"`
		cd $OLDPWD

		while read -r file; do
			echo "${file:2}"
		done <<< "$hfiles"

		while read -r ofile; do
			local cfile=${ofile/.o/.c}
			[ "$SOURCE_DIR/$cfile" -nt "$BUILD_DIR/$ofile" ] && echo "$cfile"
		done <<< "$ofiles"

		return
	fi
	local files=`rsync --archive --dry-run --verbose \
					   "$SOURCE_DIR/" "$KERN_SRC_INSTALL_DIR/" | \
					   grep -e "\.c$" -e "\.h$"`
	while read -r file; do
		if [ "$SOURCE_DIR/$file" -nt "$KERN_SRC_INSTALL_DIR/$file" ]; then
			cmp --silent "$SOURCE_DIR/$file" "$KERN_SRC_INSTALL_DIR/$file" || \
			echo "$file"
		fi
	done <<< "$files"
}
export -f modifiedFiles

generateModuleName()
{
	local file=$1
	local crc=`cksum <<< "$file" | cut -d' ' -f1`
	crc=$( printf "%08x" $crc );
	local module="$(filenameNoExt $file)"
	local modulename=${module/-/_}
	echo deku_${crc}_$modulename
}
export -f generateModuleName

generateDEKUHash()
{
	local files=`
	find command -type f -name "*";				\
	find deploy -type f -name "*";				\
	find integration -type f -name "*";			\
	find . -maxdepth 1 -type f -name "*.sh";	\
	find . -maxdepth 1 -type f -name "*.c";		\
	echo ./deku									\
	`
	local sum=
	while read -r file; do
		sum+=`md5sum $file`
	done <<< "$files"
	sum=`md5sum <<< "$sum" | cut -d" " -f1`
	echo "$sum"
}
export -f generateDEKUHash
