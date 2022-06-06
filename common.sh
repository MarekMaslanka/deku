#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

logDebug()
{
	[[ "$LOG_LEVEL" > 0 ]] && return
	echo "[DEBUG] $1"
}

logInfo()
{
	[[ "$LOG_LEVEL" > 1 ]] && return
	echo "$1"
}

logWarn()
{
	[[ "$LOG_LEVEL" > 2 ]] && return
	echo -e "$ORANGE$1$NC"
}

logErr()
{
	echo -e "$1" >&2
}

logFatal()
{
	echo -e "$RED$1$NC" >&2
}

filenameNoExt()
{
	[[ $# = 0 ]] && set -- "$(cat -)" "${@:2}"
	local basename=`basename "$1"`
	echo ${basename%.*}
}

generateSymbols()
{
	local objfile=$1
	local path=`dirname $objfile`
	path=${path#*$BUILD_DIR}
	local outfile="$SYMBOLS_DIR/$path/"
	mkdir -p "$outfile"
	outfile+=$(filenameNoExt "$objfile")
	nm -f posix "$objfile" | cut -d ' ' -f 1,2 > "$outfile"
}

findObjWithSymbol()
{
	local sym=$1
	local srcfile=$2
	local builddir=$3

	local out=`grep -lr "\b$sym\b" $SYMBOLS_DIR`
	[ "$out" != "" ] && { echo $(filenameNoExt "$out"); return; }

	local ofile=$(filenameNoExt "$srcfile").o
	local srcpath=$SOURCE_DIR/
	local buildpath=$builddir/
	srcpath+=`dirname $srcfile`
	buildpath+=`dirname $srcfile`
	while true; do
		local files=`find "$buildpath" -maxdepth 1 -type f -name "*.ko"`
		if [ "$files" != "" ]; then
			while read -r file; do
				symfile=$(filenameNoExt "$file")
				[ -f "$SYMBOLS_DIR/$symfile" ] && continue
				generateSymbols $file
			done <<< "$files"

			out=`grep -lr "\b$sym\b" $SYMBOLS_DIR`
			[ "$out" != "" ] && { echo $(filenameNoExt "$out"); return; }
		fi
		[ -f "$srcpath/Kconfig" ] && break
		srcpath+="/.."
		buildpath+="/.."
	done

	# not found
}

getTagLine()
{
	local tagname=$1
	local tagtype=$2
	local tagsfile=$3
	sed -n "s/^$tagname\s\+$tagtype\s\+\(\b[0-9]\+\b\).\+/\1/p" "$tagsfile"
}

getSymbolPos()
{
	local tagname=$1
	local tagtype=$2
	local tagsfile=$3
	sed -n "s/^$tagname\s\+$tagtype\s\+[0-9]\+\s\+\([0-9:]\+\).\+/\1/p" "$tagsfile"
}

restoreSymbol()
{
	local module=$1
	local tagname=$2

	local infile=$(intermediateSrcFile $module)
	local outfile="$workdir/$module/$module.c"
	local tagsfile="$workdir/$module/$module.tag"
	local isvar=0

	local poses=$(getSymbolPos "$tagname" "function" "$tagsfile")
	[[ "$poses" == "" ]] && { poses=$(getSymbolPos "$tagname" "variable" "$tagsfile"); isvar=1; }
	[[ "$poses" == "" ]] && { echo "Can not restore symbol $tagname in $infile"; return 1; }
	while read -r pos; do
		local arr=(${pos//:/ })
		local offset=${arr[0]}
		local end=${arr[1]}
		[[ $isvar == 1 ]] && end=${arr[2]}
		local cnt=$((end-offset))
		dd if=$infile of=$outfile skip=$offset seek=$offset count=$cnt bs=1 status=none conv=notrunc
	done <<< "$poses"
}

generateModuleId()
{
	local file=$1
	local khr_module_id=`git -C $workdir diff -W -- $file | cksum | cut -d' ' -f1`
	printf "0x%08x" $khr_module_id
}

generateAndSaveModuleId()
{
	local file=$1
	local module=$2
	local khr_module_id=$(generateModuleId "$file")
	echo -n "$khr_module_id" > "$workdir/$module/$module.id"
}

getCurrentKernelVersion()
{
	grep -a -m 1 "Linux version" "$BUILD_DIR/vmlinux" | tr -d '\0'
}

# find modified files
modifiedFiles()
{
	git -C $workdir diff --name-only | grep -E "*.c$"
}

generateModuleName()
{
	local file=$1
	local crc=`cksum <<< "$file" | cut -d' ' -f1`
	crc=$( printf "%08x" $crc );
	echo khr_${crc}_$(filenameNoExt "$file")
}

intermediateSrcFile()
{
	local module=$1
	echo "$workdir/$module/$module$ISRC_FILE_EXT"
}
