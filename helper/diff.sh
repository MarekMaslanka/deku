#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload

. ./common.sh

OPTIND=1         # Reset in case getopts has been used previously in the shell.

echo "Show diff kernel hot reload"

git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" diff