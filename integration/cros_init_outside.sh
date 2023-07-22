#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

. ./header.sh

interceptBuildCmd()
{
	local cmd="$1"

	# Make relative include paths absolute.
	cmd=`sed -e "s:-I\./:-I${BUILD_DIR}/:g" <<< "$cmd"`

	local inchrootpath="$(readlink -f "${SOURCE_DIR}")"
	local extsrcpath="${CROS_SDK_PATH}/${inchrootpath#/mnt/host/source/}"

	# Generate non-chroot version of the DB with the following
	# changes:
	#
	# 1. translate file and directory paths
	# 2. call clang directly instead of using CrOS wrappers
	# 3. use standard clang target triples
	#
	sed -E -e "s:/mnt/host/source/:${CROS_SDK_PATH}/:g" \
		-e "s:\"${SOURCE_DIR}:\"${extsrcpath}:g" \
		-e "s:-I/build/:-I${CROS_SDK_CHROOT}/build/:g" \
		-e "s:\"/build/:\"${CROS_SDK_CHROOT}/build/:g" \
		-e "s:-isystem /:-isystem ${CROS_SDK_CHROOT}/:g" \
		-e "s:=/build/:=${CROS_SDK_CHROOT}/build/:g" \
		\
		-e "s:[a-z0-9_]+-(cros|pc)-linux-gnu([a-z]*)?-clang:${CROS_SDK_CHROOT}/usr/bin/clang:g" \
		\
		-e "s:([a-z0-9_]+)-cros-linux-gnu:\1-linux-gnu:g" \
		\
		<<< "$cmd"
}
export -f interceptBuildCmd

basedir="$crossdk/chroot"

export CROS_SDK_PATH="$crossdk"
export CROS_SDK_CHROOT="$basedir"
export INTERCEPT_BUILD_CMD_LINE=interceptBuildCmd

