#!/bin/zsh
set -eu
repository_directory=${0:A:h}
exec "${repository_directory}/skill/codex-performance-macos/scripts/uninstall" "$@"
