#!/bin/bash
# Author: Marek Maślanka
# Project: DEKU
# URL: https://github.com/MarekMaslanka/deku

echo "Show diff against the kernel installed on the device"

git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" diff