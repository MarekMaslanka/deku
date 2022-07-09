#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh
. ./header.sh

if [ -e $CONFIG_FILE ]; then
	. $CONFIG_FILE
fi
. ./header.sh
export BUILD_DIR=$BUILD_DIR
export SOURCE_DIR=$SOURCE_DIR
export DEPLOY_TYPE=$DEPLOY_TYPE
export DEPLOY_PARAMS=$DEPLOY_PARAMS
[ -n $USE_LLVM ] && export USE_LLVM=$USE_LLVM

# Detect whether we're inside a chromeos chroot
[[ -e /etc/cros_chroot_version ]] && export CHROMEOS_CHROOT=1

OPTIND=1         # Reset in case getopts has been used previously in the shell.

FORCE_FLAG=""

show_help()
{
	echo "TODO: implement"
}

main()
{
	while getopts ":s:b:d:p:w:-:fh" opt ; do
		if [ "x$opt" == "x-" ]; then
			continue
		fi
		case "$opt" in
			f)  FORCE_FLAG="-f"
				;;
			h|\?)
				show_help
				exit 0
				;;
			v)  verbose=1
				;;
			b)  BUILD_DIR="$OPTARG"
				;;
			s)  SOURCE_DIR="$OPTARG"
				;;
			d)  DEPLOY_TYPE="$OPTARG"
				;;
			p)  DEPLOY_PARAMS="$OPTARG"
				;;
			w)  workdir="$OPTARG"
				;;
		esac
	done

	shift $((OPTIND-1))

	[ "${1:-}" = "--" ] && shift
	[[ "$@" == "" ]] && { logErr "Invalid usage"; exit 1; }

	for i in "$@"; do
		case $i in
			init)
				[ -z "$workdir" ] && workdir="-"
				[ -z "$BUILD_DIR" ] && BUILD_DIR="-"
				[ -z "$SOURCE_DIR" ] && SOURCE_DIR="-"
				[ -z "$DEPLOY_TYPE" ] && DEPLOY_TYPE="-"
				[ -z "$DEPLOY_PARAMS" ] && DEPLOY_PARAMS="-"

				EXTENSION="${i#*=}"
				bash $HELPERS_DIR/$i.sh "$BUILD_DIR" "$SOURCE_DIR" "$DEPLOY_TYPE" "$DEPLOY_PARAMS" "$workdir"
				local res=$?
				if [ $res != 0 ] && [ $res != 1 ]; then
					>&1 echo -e "${RED}Abort!${NC}"
					exit 1
				fi
				[ $res == 1 ] && exit 0
				. $CONFIG_FILE
				export BUILD_DIR=$BUILD_DIR
				export SOURCE_DIR=$SOURCE_DIR
				bash $HELPERS_DIR/sync.sh "$workdir"
				echo "Init done"
				shift
				;;
			sync)
				EXTENSION="${i#*=}"
				bash $HELPERS_DIR/$i.sh "$workdir"
				shift
				;;
			diff)
				EXTENSION="${i#*=}"
				sh $HELPERS_DIR/$i.sh "$workdir"
				shift
				;;
			build)
				EXTENSION="${i#*=}"
				bash $HELPERS_DIR/$i.sh "$workdir"
				[ $? -ne 0 ] && exit 1
				shift
				;;
			deploy)
				EXTENSION="${i#*=}"
				bash $HELPERS_DIR/$i.sh "$workdir"
				shift
				;;
			*)
				echo "UNKNOWN: $i"
				;;
		esac
		if [ $? -ne 0 ]; then
			exit 1
		fi
	done
}

main "$@"
