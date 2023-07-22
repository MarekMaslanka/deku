#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

kerndir=`find "$CROS_SDK_CHROOT/build/$CROS_BOARD/var/db/pkg/sys-kernel/" -type f -name "chromeos-kernel-*"`
kerndir=`basename $kerndir`
kerndir=${kerndir%-9999*}

afdo=`sed -nr 's/^(\w+\s)?AFDO_PROFILE_VERSION="(.*)"/\2/p' \
	  "$CROS_SDK_CHROOT/build/$CROS_BOARD/var/db/pkg/sys-kernel/$kerndir-9999/$kerndir-9999.ebuild"`
if [[ $afdo != "" ]]; then
	afdofile=$kerndir-$afdo.gcov
	afdopath="$CROS_SDK_CHROOT/var/cache/chromeos-cache/distfiles/$afdofile.xz"
	[[ ! -f $afdopath ]] && afdopath="$CROS_SDK_CHROOT/build/$CROS_BOARD/tmp/portage/sys-kernel/$kerndir-9999/distdir/$afdofile.xz"
	[[ ! -f $afdopath ]] && afdopath="$CROS_SDK_PATH/.cache/distfiles/$afdofile.xz"
	dstdir="$CROS_SDK_CHROOT/build/$CROS_BOARD/tmp/portage/sys-kernel/$kerndir-9999/work"
	mkdir -p $dstdir
	if [[ -f $afdopath ]]; then
		cp -f $afdopath $dstdir/
		xz --decompress $dstdir/$afdofile.xz
		"$CROS_SDK_CHROOT"/usr/bin/llvm-profdata merge \
			-sample \
			-extbinary \
			-output="$dstdir/$afdofile.extbinary.afdo" \
			"$dstdir/$afdofile"
		# Generate compbinary format for legacy compatibility
		"$CROS_SDK_CHROOT"/usr/bin/llvm-profdata merge \
			-sample \
			-compbinary \
			-output="$dstdir/$afdofile.compbinary.afdo" \
			"$dstdir/$afdofile"
	else
		logWarn "Can't find afdo profile file ($afdopath)"
	fi
else
	logInfo "Can't find afdo profile file"
fi
