#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

generateCtags()
{
	local srcfile=$1
	local tagfile=$2
	# some file have syntax that deceiving ctags
	# get rid of this by apply workarounds
	local dupsrcfile=$(mktemp workdir/khr-ctags-src.XXXXXX.c)
	local tmpctagfile=$(mktemp workdir/khr-ctags.XXXXXX)
	cp "$srcfile" "$dupsrcfile"
	# remove "__releases/__acquires(lock)" from functions signature
	sed -i s/__releases\([-A-Za-z0-9_\&\>\)\(]*\)//g "$srcfile"
	sed -i s/__acquires\([-A-Za-z0-9_\&\>\)\(]*\)//g "$srcfile"
	ctags -x -u --c-kinds=+p --fields=+afmikKlnsStz --extra=+q "$srcfile" > "$tmpctagfile"
	cat "$tmpctagfile"
	mv "$dupsrcfile" "$srcfile"
	rm -f "$tmpctagfile"
}

# find out "to who" specific line belongs to
lineowner()
{
	local file="$1"
	local lineno=$2
	local out=`ctags -x -u --c-kinds=+p --fields=+afmikKlnsStz --extra=+q "$file"`
	local res=
	while read -r line; do
		arr=($line)
		local no=${arr[2]}
		(( $no <= $lineno )) && res=$line || break
	done <<< "$out"
	echo $res
}

# get list of modified functions
modifiedFunctions()
{
	git -C $workdir diff -W -- $1 | \
	sed /^diff\ --git.*/,/^+++\ .*/d | \
	sed /^@@\ -.*/d | \
	sed /^-.*/d | \
	sed s/^+//g > $workdir/diff

	ctags -x -u --c-kinds=f --language-force=c $workdir/diff | \
	sed 's/\(^\w\+\)\ .\+/\1/g'

	rm -rf "$workdir/diff"
}

functionCallers()
{
	local srcfile=$1
	local function=$2
	local out=`grep -En "\b$function\(" $srcfile | cut -f1 -d:`
	while read -r no; do
		local owner=$(lineowner $srcfile $no)
		arr=($owner)
		[[ "${arr[1]}" == "function" ]] && echo ${arr[0]}
	done <<< "$out"
}

isInlined()
{
	local srcfile=$1
	local function=$2
	local ofile=${srcfile/.c/.o}
	nm -f posix "$BUILD_DIR/$ofile" | grep -qi "\b$function\b T" 
	(( ${PIPESTATUS[1]} != 0 )) && echo "1"
}

# copy source file and makeup
prepareSourceFile()
{
	local srcfile=$1
	local tagfile=$2
	local moduledir=`dirname "$srcfile"`

	# remove #define pr_fmt(fmt) ...
	sed -i 's/^#define pr_fmt(fmt).*//' "$srcfile"
	# add semicolon at the end of module_init(...) if miss (drivers/thermal/intel/x86_pkg_temp_thermal.c)
	sed -i '/^module_init(\w\+)$/ s/$/;/' "$srcfile"
	sed -i '/^module_exit(\w\+)$/ s/$/;/' "$srcfile"
	sed -i '/^module_driver(\w\+/,/;/ s/^/\/\//' "$srcfile"
	sed -i "/^MODULE_.*[^;]$/,/;/ s/^/\/\//" "$srcfile"
	sed -i "/^MODULE_.*;.*/ s/^/\/\//" "$srcfile"
	# comment out all ACPI_EXPORT_SYMBOL
	sed -i '/^ACPI_EXPORT_SYMBOL(\w\+)$/ s/^/\/\//' "$srcfile"

	generateCtags "$srcfile" > "$tagfile"

	# change:               to:
	# static struct {		struct {
    # ...					...
	# } icmp_global = {		} icmp_global = {
	# 	...						...
	# };					};
	local lineno=`grep -m 1 -nE "\w+[ ]+variable[ ]+\w+ .+ } \w+ = {" $tagfile | cut -f1 -d:`
	if [[ $lineno != "" ]]; then
		IFS=$'\n' read -d '' -r -a lines < $tagfile
		for ((i=$lineno-2; i>=0; i--))
		do
			local arr=(${lines[$i]})
			if [[ "${arr[1]}" != "member" ]]; then
				arr=(${lines[$i+1]})
				local headerline=$((${arr[2]} - 1))
				sed -i "${headerline}s/^static struct/struct/" $file
				break
			fi
		done
	fi

	while read -r tag; do
		arr=($tag)
		local name="${arr[0]}"
		local type="${arr[1]}"
		local no="${arr[2]}"
		local pos="${arr[3]}"
		local line="${arr[4]}"
		if [[ $type == "prototype" ]]; then
			[[ "$name" == "DEFINE_MUTEX" ]] && sed -i "$no s/.*DEFINE_MUTEX(\(\w\+\)).*/extern struct mutex \1;/" "$srcfile"
			[[ "$name" == "DEFINE_SPINLOCK" ]] && sed -i "$no s/.*DEFINE_SPINLOCK(\(\w\+\)).*/extern spinlock_t \1;/" "$srcfile"
		fi
		if [[ $type == "variable" ]]; then
			[[ "$line" =~ ^[A-Za-z0-9_]+\(.+\)\; ]] && continue;
			# add extra 7 spaces before every variable to replace it with "extern" in the further proccess if needed
			sed -i "$no s/^/       /" "$srcfile"
			sed -i "$no s/\b__read_mostly\b//" "$srcfile"
		fi
	done < "$tagfile"
}

checksForInlineFun()
{
	local originfile=$1
	local srcfile=$2
	local functions=$3
	local tagfile=$4

	ctags -x -u --c-kinds=+p --fields=+afmikKlnsStz --extra=+q "$srcfile" > "$tagfile"
	# check if modified functions are inlined
	local visitedFun=()
	local inlineCallers=""
	while true; do
		local anyinlined=0
		for funname in $functions
		do
			[[ "${visitedFun[*]}" =~ "$funname" ]] && continue
			visitedFun+=$funname
			if [[ $(isInlined "$originfile" $funname) == "1" ]]; then
				local callers=$(functionCallers "$srcfile" $funname)
				if [[ "$callers" == "" ]]; then
					>&2 echo -e "${ORANGE}Modified function '$funname' in '$srcfile' is not compiled into kernel. Skip${NC}"
					continue
				fi
				functions+="\n$callers"
				functions=`echo -e "$functions" | awk '!seen[$1]++'`
				anyinlined=1
			fi
			inlineCallers+=funname
		done
		[[ $anyinlined == 0 ]] && break;
	done
	echo -e "$functions" | awk '!seen[$1]++'
}

generateMakefile()
{
	local makefile=$1
	local file=$2
	local ofile=$(filenameNoExt "$file").o
	local dir=`dirname $file`
	local inc="$SOURCE_DIR/$dir"

	echo "KBUILD_MODPOST_WARN = 1" >> $makefile
	echo "KBUILD_CFLAGS += -Wno-unused-function" >> $makefile
	echo "KBUILD_CFLAGS += -Wno-unused-variable" >> $makefile
	if [ "$MODULE_FROM_SCRATCH" != 1 ]; then
		echo "KBUILD_CFLAGS += -fdata-sections" >> $makefile
	fi
	#copy flags from source Makefile
	while true; do
		echo "EXTRA_CFLAGS += -I$inc" >> $makefile
		if [ -f "$inc/Makefile" ]; then # if grep -qs $ofile "$inc/Makefile"; then
			sed ':x; /\\$/ { N; s/\\\n//; tx }' "$inc/Makefile" |\
				grep -h "ccflags-y"  |\
				sed s#\$\(srctree\)#$SOURCE_DIR# |\
				sed s#\$\(src\)#$dir# \
				>> $makefile
		fi
		[ -f "$inc/Kconfig" ] && break
		inc+="/.."
		dir+="/.."
	done

	echo "obj-m += $module.o" >> $makefile
	echo "all:" >> $makefile
	echo "	make -C $BUILD_DIR M=\$(PWD) modules" >> $makefile
	echo "clean:" >> $makefile
	echo "	make -C $BUILD_DIR M=\$(PWD) clean" >> $makefile
}

# remove fnctions and variable initializations
removeFunAndInit()
{
	local file=$1
	local tagsfile=$2
	local laststruct=""
	# sed -i "/^module_param(.*[^;]$/,/;/ s/^/\/\//" "$file"
	# sed -i "/^module_param(.\+);.*/ s/^/\/\//" "$file"
	# sed -i "/^module_param_named(.*[^;]$/,/;/ s/^/\/\//" "$file"
	# sed -i "/^module_param_named(.\+);.*/ s/^/\/\//" "$file"
	# sed -i "s/^\(DRM_ENUM_NAME_FN(\w\+\)/\/\/\1/g" "$file" #comment out: DRM_ENUM_NAME_FN
	while read -r tag; do
		arr=($tag)
		local name="${arr[0]}"
		local type="${arr[1]}"
		local no="${arr[2]}"
		local pos="${arr[3]}"
		if [[ "$type" == "function" ]]; then
			./codeutils blanks "$file" "$pos"
		fi
		if [[ $type == "prototype" ]]; then
			[[ "$name" == "DECLARE_WORK" ]] && { ./codeutils blanks "$file" "$pos"; continue; }
			[[ "$name" == DEVICE_ATTR_* ]] && { ./codeutils blanks "$file" "$pos"; continue; }
		fi
		if [[ $type == "struct" ]]; then
			laststruct=$name
		fi
		if [[ $type == "variable" ]]; then
			local arr=(${pos//:/ })
			local varpos="${arr[0]}:${arr[2]}"
			local line=${tag#*$file }
			# # [[ "$line" == EXPORT_SYMBOL* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == module_init\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == module_exit\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == module_i2c_driver\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == module_platform_driver\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == core_initcall\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == device_initcall\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == postcore_initcall\(* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			# # [[ "$line" == DEVICE_ATTR_* ]] && { sed -i "$no s/^/\/\//" "$file"; continue; }
			[[ "$line" == DEFINE_* ]] && { continue; }
			[[ "$line" == MODULE_DESCRIPTION\(* ]] && { continue; }
			[[ "$line" == MODULE_FIRMWARE\(* ]] && { continue; }
			[[ "$line" == DEBUGFS_*\(* ]] && { continue; }
			[[ "$line" =~ ^[A-Za-z0-9_]+\(.+\)\; ]] && { ./codeutils blanks "$file" "$varpos"; continue; }
			[[ "$line" =~ ^}\ [A-Za-z0-9_]+\[\]\ =\ \{$ ]] && continue #} edid_quirk_list[] = {
			[[ "$line" == "} __packed;" ]] && { continue; }

			# skip when variable is array that is later used in ARRAY_SIZE() or sizeof()
			if [[ "$line" == *\[\]\ =\ {* ]] || [[ "$line" == *$name\[\]* ]] ; then
				if grep -q "ARRAY_SIZE($name)" "$file" || grep -q "sizeof($name)" "$file"; then
					continue
				fi
			fi
			# remove initialization
			local initpos="${arr[1]}:$((${arr[2]}-1))"
			[[ ${arr[1]} != 0 ]] && ./codeutils blanks "$file" "$initpos"

			# change "} icmp_global = {" -> "} extern icmp_global"
			# [[ "$line" =~ ^}\ $name\ =\ \{$ ]]  && { sed -i "$no s/^}/} extern/" "$file"; continue; }

			sed -i "$no s/ static /        /" "$file"
			sed -i "$no s/^      /extern/" "$file"
			# sed -i "$no s/^[extern]*/extern /" "$file"
			# sed -i "$no s/^extern static/extern/" "$file"
		fi
	done < "$tagsfile"
}

main()
{
	local files=$(modifiedFiles)
	if [ -z "$files" ]; then
		# No modification detected
		exit
	fi

	for file in $files
	do
		local ofile=${file/.c/.o}
		[ ! -f "$BUILD_DIR/$ofile" ] && { >&2 echo -e "${ORANGE}File '$file' is not used in the kernel or module. Skip${NC}"; continue; }
		local functions=$(modifiedFunctions "$file" | awk '!seen[$1]++')
		[ -z "$functions" ] && { >&2 echo -e "${ORANGE}No valid changes detected in '$file'. Skip${NC}"; continue; }
		local module=$(generateModuleName "$file")
		local moduledir="$workdir/$module/"
		local moduleid=$(generateModuleId "$file")
		# check if changed since last run
		if [ -s "$moduledir/$module.id" ]; then
			local prev=`cat "$moduledir/$module.id"`
			[ "$prev" ==  "$moduleid" ] && continue
		fi

		rm -rf $moduledir
		mkdir $moduledir

		# write diff to file for debug purpose
		git -C $workdir diff -W -- $file > "$moduledir/$module.diff"

		cp "$SOURCE_DIR/$file" "$moduledir/$module.orig.c"
		echo -n "$file" > "$moduledir/$module.src"

		generateMakefile "$moduledir/Makefile" "$file"

		# the "srcFile" variable is a livepatch module file
		local srcFile="$moduledir/$module.c"
		local symfile="$moduledir/$module.$SYMBOLS_FILE_EXT"
		local tagfile="$moduledir/$module.tag"
		local ifile=$(intermediateSrcFile $module)

		cp "$SOURCE_DIR/$file" "$ifile"
		[ "$MODULE_FROM_SCRATCH" == 1 ] && prepareSourceFile "$ifile" "$tagfile"
		functions=$(checksForInlineFun "$file" "$ifile" "$functions" "$tagfile")
		if [ -z "$functions" ]; then
			echo -n "$moduleid" > "$workdir/$module/$module.id"
			logWarn "Unable to handle changes in '$file'. Skip the file"
			continue
		fi
		cp "$ifile" "$srcFile"
		if [ "$MODULE_FROM_SCRATCH" == 1 ]; then
		generateCtags "$srcFile" > "$tagfile"
		>&2 ./codeutils ctags "$tagfile" "$srcFile"
		mv -f enhanced_ctags "$tagfile"
		removeFunAndInit "$srcFile" "$tagfile"
		rm -f $symfile
		fi

		local klpFunc=""
		local objname
		for funname in $functions
		do
			objname=$(findObjWithSymbol $funname "$file" "$BUILD_DIR")
			[ ! -z "$objname" ] && break
		done
		if [ -z "$objname" ]; then
			>&2 echo -e "${ORANGE}Modified functions in '$file' are not compiled into kernel/module. Skip the file${NC}"
			continue
		fi
		for funname in $functions
		do
			if [ "$MODULE_FROM_SCRATCH" == 1 ]; then
				>&2 restoreSymbol "$module" $funname
			[ $? -ne 0 ] && return 2
			fi
			[[ $(isInlined "$file" $funname) == "1" ]] && continue

			# add function name to symbols list
			echo $objname.$funname >> "$symfile"

			# fill list of a klp_func struct
			klpFunc="$klpFunc		{
				.old_name = \"${funname}\",
				.new_func = ${funname},
			},"
		done
		local klpobjname
		if [ $objname = "vmlinux" ]; then
			klpobjname="NULL"
		else
			klpobjname="\"$objname\""
		fi

		# add to module necessary headers
		echo >> $srcFile
		cat >> $srcFile <<- EOM
		#include <linux/kernel.h>
		#include <linux/module.h>
		#include <linux/livepatch.h>
		#include <linux/version.h>
		EOM

		# add livepatching code
		cat >> $srcFile <<- EOM
		static struct klp_func khr_funcs[] = {
		$klpFunc { }
		};

		static struct klp_object khr_objs[] = {
		    {
		        .name = $klpobjname,
		        .funcs = khr_funcs,
		    }, { }
		};

		static struct klp_patch khr_patch = {
		    .mod = THIS_MODULE,
		    .objs = khr_objs,
		};
		EOM
		cat $MODULE_SUFFIX_FILE >> $srcFile

		echo $module
	done
}

main $@
exit 1
