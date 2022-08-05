#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh
. ./header.sh

[ -e $CONFIG_FILE ] && . $CONFIG_FILE

. ./header.sh

export BUILD_DIR=$BUILD_DIR
export SOURCE_DIR=$SOURCE_DIR
export DEPLOY_TYPE=$DEPLOY_TYPE
export DEPLOY_PARAMS=$DEPLOY_PARAMS
[ -n $USE_LLVM ] && export USE_LLVM=$USE_LLVM

# detect whether we're inside a chromeos chroot
[[ -e /etc/cros_chroot_version ]] && export CHROMEOS_CHROOT=1

showHelp()
{
	echo "TODO: implement"
}

main()
{
	for opt in "$@"; do
		[[ $opt == "-h" ]] && { showHelp; exit; }
		[[ $opt == "-"* ]] && continue
		if [[ -f "$COMMANDS_DIR/$opt.sh" ]]; then
			if [[ "$opt" == "init" ]]; then
				bash "$COMMANDS_DIR/$opt.sh" "$@"
				local ret=$?
				[[ "$ret" != 0 ]] && exit $ret
				bash ${0} sync
				logInfo "Init done"
			else
				bash "$COMMANDS_DIR/$opt.sh" "$workdir"
			fi
			exit
		fi
	done
}

main "$@"
