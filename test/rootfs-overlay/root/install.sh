#!/bin/bash
export DEBIAN_FRONTEND=noninteractive

ln -fs /usr/share/zoneinfo/Europe/Warsaw /etc/localtime
apt update
apt install -y --no-install-recommends rsyslog kmod ssh \
			ifupdown iputils-ping network-manager wget
passwd -d root
ssh-keygen -b 4096 -f ~/.ssh/testing_rsa -N ""
cat ~/.ssh/testing_rsa.pub >> ~/.ssh/authorized_keys
mkdir deku