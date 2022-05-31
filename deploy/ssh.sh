#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

SSHPARAMS=""
SCPPARAMS=""
REMOTE_OUT=""

remoteSh()
{
	REMOTE_OUT=$(ssh $SSHPARAMS "$@")
	return ${PIPESTATUS[0]}
}

getLoadedKHRModules()
{
	remoteSh 'find /sys/module -name .note.khr -type f -exec cat {} \; | grep -a khr_ 2>/dev/null'
	echo "$REMOTE_OUT"
}

getKernelRelease()
{
	remoteSh 'uname --kernel-release'
	echo $REMOTE_OUT
}

getKernelVersion()
{
	remoteSh 'uname --kernel-version'
	echo $REMOTE_OUT
}

main()
{
	local dstdir="kernelhotreload"
	local host=$DEPLOY_PARAMS
	local sshport=${host#*:}
	local scpport
	if [ "$sshport" != "" ]; then
		scpport="-P $sshport"
		sshport="-p $sshport"
		host=${host%:*}
	fi

	local options="-o ControlPath=/tmp/sshtest -o ControlMaster=auto"
	if [[ "$CHROMEOS_CHROOT" == 1 ]]; then
		if [[ ! -f "$workdir/testing_rsa" ]]; then
			local GCLIENT_ROOT=~/chromiumos
			cp -f "${GCLIENT_ROOT}/src/scripts/mod_for_test_scripts/ssh_keys/testing_rsa" "$workdir"
			chmod 0400 "$workdir/testing_rsa"
		fi
		options+=" -o IdentityFile=$workdir/testing_rsa -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes -q"
	fi
	SSHPARAMS="$options $host $sshport"
	SCPPARAMS="$options $scpport"
	unset SSH_AUTH_SOCK

	[[ "$1" == "--getids" ]] && { getLoadedKHRModules; return; }
	[[ "$1" == "--kernel-release" ]] && { getKernelRelease; return; }
	[[ "$1" == "--kernel-version" ]] && { getKernelVersion; return; }

	local files=$@
	local disablemod=
	local transwait=
	local rmmod=
	local checkmod=
	local insmod=
	# prepare script that tries in loop disable livepatch and do rmmod. Next do insmod
	local reloadscript="
	max=3\n
	for i in \`seq 1 \$max\`; do"
	for file in "$@"; do
		local skipload=
		if [[ "$file" == -* ]]; then
			skipload=1
			files=("${files[@]/$file}")
			file="${file:1}"
			echo "Unload $file"
		fi

		local module="$(filenameNoExt $file)"
		local modulename=${module/-/_}
		local modulesys="/sys/kernel/livepatch/$modulename"
		disablemod+="[ -d $modulesys ] && echo 0 > $modulesys/enabled\n"
		transwait+="if [ -d $modulesys ]; then\n"
		transwait+="for i in \`seq 1 10\`; do\n\t[[ \$(cat $modulesys/transition) == \"0\" ]] && break\n"
		transwait+="\tsleep 0.1\ndone\nfi\n"
		rmmod+="[ -d /sys/module/$modulename ] && rmmod -f $modulename\n"
		if [ -z $skipload ]; then
			checkmod+="\n[ ! -d $modulesys ] && \\\\"
			insmod+="insmod $dstdir/`basename $file`\n"
		fi
	done
	reloadscript+="\n$disablemod\n$transwait\n$rmmod$checkmod\nbreak;\nsleep 1\ndone"
	reloadscript+="\n$insmod"
	echo -e $reloadscript > $workdir/$HOTRELOAD_SCRIPT

	ssh $SSHPARAMS mkdir -p $dstdir
	scp $SCPPARAMS $files $workdir/$HOTRELOAD_SCRIPT $host:$dstdir/
	remoteSh sh "$dstdir/$HOTRELOAD_SCRIPT 2>&1"
	[ $? == "0" ] && echo -e "${GREEN}Reload done${NC}" || echo -e "${RED}Reload failed!\n$REMOTE_OUT${NC}"
}

main $@
