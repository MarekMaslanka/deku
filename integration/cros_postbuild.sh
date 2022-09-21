#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

kerndir=`find /build/$CROS_BOARD/var/db/pkg/sys-kernel/ -type f -name "chromeos-kernel-*"`
kerndir=`basename $kerndir`
kerndir=${kerndir%-9999*}

afdo=`grep AFDO_PROFILE_VERSION= /build/$CROS_BOARD/var/db/pkg/sys-kernel/$kerndir-9999/$kerndir-9999.ebuild`
afdo=${afdo#*=\"}
afdo=${afdo%\"}

afdofile=$kerndir-$afdo.gcov
dstdir=/build/$CROS_BOARD/tmp/portage/sys-kernel/$kerndir-9999/work

if [[ $afdo != "" ]]; then
    rm -rf "$dstdir/$afdofile"
    rm -rf "$dstdir/$afdofile.compbinary.afdo"
else
    echo "Can't find afdo profile version"
fi
