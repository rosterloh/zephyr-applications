#!/usr/bin/env bash
# Read changed paths on stdin, print the apps they can affect, one per line.
# A path outside applications/ that can still change firmware (boards/, west.yml,
# mise.*, zephyr/ patches, scripts/, lockfiles, the workflow) affects every app.
set -euo pipefail
cd "$(dirname "$0")/../.."

every_app() {
	for dir in applications/*/; do
		app=$(basename "$dir")
		[ "$app" = common ] || echo "$app"
	done
}

while IFS= read -r path; do
	case "$path" in
	# The workflow's paths-ignore: never firmware.
	docs/* | .agents/* | .claude/* | LICENSE | *.md) ;;
	# Host-side pytest scripts; no CI job runs them.
	tests/*) ;;
	applications/common/*)
		module=${path#applications/common/}
		module=${module%%/*}
		grep -l "common/$module/" applications/*/CMakeLists.txt applications/*/Kconfig 2>/dev/null |
			cut -d/ -f2 || true
		;;
	applications/*/*)
		app=${path#applications/}
		echo "${app%%/*}"
		;;
	*)
		every_app
		break
		;;
	esac
done | sort -u
