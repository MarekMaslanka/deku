#!/bin/bash
# Author: Marek Maślanka
# Project: KernelHotReload
# URL: https://github.com/MarekMaslanka/KernelHotReload

echo "Show diff kernel hot reload"

git --work-tree="$SOURCE_DIR" --git-dir="$workdir/.git" diff