#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

isKernelSroucesDir()
{
	dir=$1
	[ ! -f "$dir/Kbuild" ] && return 1
	[ ! -f "$dir/Kconfig" ] && return 1
	[ ! -f "$dir/Makefile" ] && return 1
	return 0
}

isKernelBuildDir()
{
	local dir=$1

	[ -f "$dir/vmlinux" ] && \
	[ -f "$dir/System.map" ] && \
	[ -f "$dir/Makefile" ] && \
	[ -f "$dir/.config" ] && \
	[ -f "$dir/include/generated/uapi/linux/version.h" ] && \
	return $NO_ERROR

	return $ERROR_INVALID_BUILDDIR
}

isKlpEnabled()
{
	local dir=$1

	grep -q "CONFIG_LIVEPATCH" "$dir/.config" || return $ERROR_KLP_IS_NOT_ENABLED
	grep -q "klp_enable_patch" "$dir/System.map" || return $ERROR_KLP_IS_NOT_ENABLED
	return $NO_ERROR
}

isLLVMUsed()
{
	local linuxheaders=$1
	grep -qs "CONFIG_CC_IS_CLANG=y" "$linuxheaders/.config"
}

enableKLP()
{
	local sourcesdir=$1
	local configfile="$sourcesdir/chromeos/config/x86_64/common.config"
	[ ! -f "$configfile" ] && configfile="$sourcesdir/chromeos/config/chromeos/x86_64/common.config"
	[ ! -f "$configfile" ] && configfile="$builddir/.config"
	[ ! -f "$configfile" ] && return 1
	local flags=("CONFIG_KALLSYMS_ALL" "CONFIG_LIVEPATCH")
	for flag in "${flags[@]}"
	do
		bash $sourcesdir/scripts/config --file $configfile --enable $flag
	done
	grep -q "CONFIG_LIVEPATCH" "$configfile" && return 0
	return 1
}

main()
{
	local builddir=""
	local sourcesdir=""
	local deploytype=""
	local deployparams=""
	local sshoptions=""
	local board=""
	local prebuild=""
	local postbuild=""
	local kernsrcinstall=""
	local target=""
	local ignorecros=""

	if ! options=$(getopt -u -o b:s:d:p:w:c: -l builddir:,sourcesdir:,\
				   deploytype:,deployparams:,ssh_options:,src_inst_dir:,prebuild:,\
				   postbuild:,board:,workdir:,cros_sdk:,target:,ignore_cros: \
				   -- "$@")
	then
		exit $ERROR_INVALID_PARAMETERS
	fi

	while [ $# -gt 0 ]
	do
		local opt="$1"
		local value="$2"
		if [[ "$opt" =~ ^\-\-.+=.+ ]]; then
			value=${opt#*=}
			opt=${opt%%=*}
		else
			shift
		fi

		case $opt in
		-b|--builddir) builddir="$value" ;;
		-s|--sourcesdir) sourcesdir="$value" ;;
		-d|--deploytype) deploytype="$value" ;;
		-p|--deployparams) deployparams="$value" ;;
		-w|--workdir) workdir="$value" ;;
		-c|--cros_sdk) crossdk="$value" ;;
		--ssh_options) sshoptions="$value" ;;
		--board) board="$value" ;;
		--src_inst_dir) kernsrcinstall="$value" ;;
		--prebuild) prebuild="$value" ;;
		--postbuild) postbuild="$value" ;;
		--target) target="$value" ;;
		--ignore_cros) ignorecros="$value" ;;
		(--) shift; break;;
		(-*) logInfo "$0: Error - Unrecognized option $opt" 1>&2; exit 1;;
		(*) break;;
		esac
		shift
	done

	[[ "$builddir" && "$builddir" != /* ]] && builddir="$CURRENT_DIR/$builddir"
	[[ "$sourcesdir" && "$sourcesdir" != /* ]] && sourcesdir="$CURRENT_DIR/$sourcesdir"
	[[ "$crossdk" && "$crossdk" != /* ]] && crossdk="$CURRENT_DIR/$crossdk"
	[[ "$prebuild" && "$prebuild" != /* ]] && prebuild="$CURRENT_DIR/$prebuild"
	[[ "$postbuild" && "$postbuild" != /* ]] && postbuild="$CURRENT_DIR/$postbuild"

	# detect whether we're working on a kernel for Chromebook
	if [[ "$ignorecros" == "" && -e /etc/cros_chroot_version ]] || [[ "$board" != "" && "$crossdk" != "" ]]; then

		if [[ ! -e /etc/cros_chroot_version ]]; then
			logErr "Build kernel for Chromebook outside CrOS SDK is not supported yet"
			exit $ERROR_INVALID_PARAMETERS
		fi

		if [[ "$board" == "" ]]; then
			logErr "Please specify the Chromebook board name using: $0 --board=<BOARD_NAME> ... syntax"
			exit $ERROR_NO_BOARD_PARAM
		fi

		if [[ ! -d "/build/$board" ]]; then
			logErr "Please setup the board using \"setup_board\" command"
			exit $ERROR_BOARD_NOT_EXISTS
		fi

		if [[ "$builddir" ]]; then
			logErr "-b|--builddir parameter can not be used for Chromebook kernel"
			exit $ERROR_INVALID_PARAMETERS
		fi

		if [[ "$crossdk" != "" && ! -e "$crossdk/chroot/etc/cros_chroot_version" ]]; then
			logErr "Given cros_sdk path is invalid"
			exit $ERROR_INVALID_PARAMETERS
		fi

		local basedir=
		if [[ "$crossdk" != "" ]]; then
			source integration/cros_init_outside.sh
		fi

		local kerndir=`find "$basedir/build/$board/var/db/pkg/sys-kernel/" -type f -name "chromeos-kernel-*"`
		kerndir=`basename $kerndir`
		kerndir=${kerndir%-9999*}
		builddir="$basedir/build/$board/var/cache/portage/sys-kernel/$kerndir"
		prebuild="bash integration/cros_prebuild.sh"
		postbuild="bash integration/cros_postbuild.sh"
		if [[ "$kernsrcinstall" == "" ]]; then
			kernsrcinstall=`find "$basedir/build/$board/usr/src/" -maxdepth 1 -type d \
								-name "chromeos-kernel-*"`
		fi
		if [[ "$kernsrcinstall" == "" ]]; then
			logErr "Kernel must be build with: USE=\"livepatch kernel_sources\" emerge-$board chromeos-kernel-..."
			exit $ERROR_INSUFFICIENT_BUILD_PARAMS
		fi
		if [[ "$sourcesdir" == "" && ! -e /etc/cros_chroot_version ]]; then
			sourcesdir=`readlink "$builddir/source"`
			sourcesdir=`sed -E -e "s:/mnt/host/source/:${crossdk}/:g" <<< "$sourcesdir"`
		fi
		[[ "$workdir" == "" ]] && workdir="workdir_$board"

		mkdir -p "$workdir"
		if [[ ! -f "$workdir/testing_rsa" ]]; then
			local GCLIENT_ROOT=~/chromiumos
			cp -f "${GCLIENT_ROOT}/src/third_party/chromiumos-overlay/chromeos-base/chromeos-ssh-testkeys/files/testing_rsa" "$workdir"
			chmod 0400 "$workdir/testing_rsa"
		fi
		if [[ "$sshoptions" == "" ]]; then
			sshoptions=" -o IdentityFile=$workdir/testing_rsa -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -q"
		fi
		[[ "$target" != "" && "$target" != *@* ]] && target="root@$target"
		deploytype="ssh"
	fi

	if [[ -z "$builddir" ]]; then
		logErr "Please specify the kernel build directory using -b or --builddir parameter"
		exit $ERROR_NO_BUILDDIR
	fi

	if [[ -z "$workdir" ]]; then
		local absbuilddir=`readlink -f "$builddir"`
		local crc=`cksum <<< "$absbuilddir" | cut -d' ' -f1`
		crc=$( printf "%08x" $crc );
		workdir="workdir_$crc"
	fi

	builddir=${builddir%/}
	workdir=${workdir%/}
	local linuxheaders="$builddir"

	[[ "$sourcesdir" == "" && -L "$builddir/source" ]] && sourcesdir="$builddir/source"
	logDebug "Check for kernel sources in: $sourcesdir"
	isKernelSroucesDir $sourcesdir || sourcesdir="$builddir"

	sourcesdir=${sourcesdir%/}

	[ "$(git --version)" ] || { logErr "\"git\" could not be found. Please install \"git\""; exit 2; }

	logDebug "Sources dir: $sourcesdir"
	logDebug "Build dir: $builddir"
	logDebug "Work dir: $workdir"

	if [ ! -d "$workdir" ]; then
		mkdir -p "$workdir" || { logErr "Failed to create directory \"$workdir\""; exit $?; }
	fi

	isKernelBuildDir "$builddir"
	rc=$?
	if [[ $rc != $NO_ERROR ]]; then
		logErr "Given directory is not a valid kernel build directory: \"$builddir\""
		exit $rc
	fi

	isKlpEnabled "$builddir"
	local rc=$?
	if [[ $rc != $NO_ERROR ]]; then
		if [[ $rc == $ERROR_KLP_IS_NOT_ENABLED ]]; then
			if [[ "$board" ]]; then
				logErr "Your kernel must be build with: USE=\"livepatch kernel_sources\" emerge-$board chromeos-kernel-..."
				exit $ERROR_INSUFFICIENT_BUILD_PARAMS
			fi
			logErr "Kernel livepatching is not enabled. Please enable CONFIG_LIVEPATCH flag and rebuild the kernel"
			echo "Would you like to try enable this flag now? [y/n]"
			while true; do
				read -p "" yn
				case $yn in
					[Yy]* )
						enableKLP "$sourcesdir" && { logInfo "Flag is enabled. Please rebuild the kernel and try again."; exit $rc; } || logInfo "Failed do enable the flag. Please enable it manually."
						break;;
					[Nn]* ) exit $rc;;
					* ) echo "Please answer [y]es or [n]o.";;
				esac
			done
		fi
		exit $rc
	fi

	if ! isKernelSroucesDir $sourcesdir; then
		if [[ "$sourcesdir" == "" ]]; then
			logErr "Current directory is not a kernel srouces directory"
		else
			logErr "Given directory does not contains valid kernel sources: \"$sourcesdir\""
		fi
		exit $ERROR_INVALID_KERN_SRC_DIR
	fi

	deployparams="$target"
	[[ "$sshoptions" != "" ]] && deployparams+=" $sshoptions"
	[[ "$deploytype" == "" ]] && deploytype="ssh"

	. ./header.sh

	local hash=
	[[ -f "$CONFIG_FILE" ]] && hash=`sed -rn "s/^WORKDIR_HASH=([a-f0-9]+)/\1/p" "$CONFIG_FILE"`
	[[ $hash == "" ]] && hash=$(generateDEKUHash)

	echo "BUILD_DIR=\"$builddir\"" > $CONFIG_FILE
	echo "SOURCE_DIR=\"$sourcesdir\"" >> $CONFIG_FILE
	echo "DEPLOY_TYPE=\"$deploytype\"" >> $CONFIG_FILE
	echo "DEPLOY_PARAMS=\"$deployparams\"" >> $CONFIG_FILE
	echo "SSH_OPTIONS=\"$sshoptions\"" >> $CONFIG_FILE
	echo "MODULES_DIR=\"$builddir\"" >> $CONFIG_FILE
	echo "LINUX_HEADERS=\"$linuxheaders\"" >> $CONFIG_FILE
	echo "SYSTEM_MAP=\"$builddir/System.map\"" >> $CONFIG_FILE
	[[ "$prebuild" != "" ]] && echo "PRE_BUILD=\"$prebuild\"" >> $CONFIG_FILE
	[[ "$postbuild" != "" ]] && echo "POST_BUILD=\"$postbuild\"" >> $CONFIG_FILE
	[[ "$board" != "" ]] && echo "CROS_BOARD=\"$board\"" >> $CONFIG_FILE
	[[ "$kernsrcinstall" != "" ]] && echo "KERN_SRC_INSTALL_DIR=\"$kernsrcinstall\"" >> $CONFIG_FILE
	isLLVMUsed "$linuxheaders" && echo "USE_LLVM=\"LLVM=1\"" >> $CONFIG_FILE
	echo "WORKDIR_HASH=$hash" >> $CONFIG_FILE

	if [[ "$kernsrcinstall" == "" ]]; then
		git --work-tree="$sourcesdir" --git-dir="$workdir/.git" \
			-c init.defaultBranch=deku init > /dev/null
	fi

	mkdir -p "$SYMBOLS_DIR"
}

main "$@"
