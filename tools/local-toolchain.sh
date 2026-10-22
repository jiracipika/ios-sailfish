#!/bin/bash
# Local build environment for the SWEET XNU kernel — no homebrew, no GitHub
# Actions (this Mac's brew is broken; see docs/LOCAL-BUILD.md for the story).
# Idempotent; stages in the repo's research/ (gitignored):
#   1. tools: cmake/ninja/gum + bash 5 (built from source) + xcodes stub -> research/bin
#   2. Xcode 15.4: research/incoming/Xcode_15.4*.xip  -> research/xcode15/Xcode.app
#   3. KDK 13.5:   research/incoming/KDK_13.5*.dmg    -> research/kdk/KDK_13.5_22G74.kdk
#   4. verify the toolchain exactly as tools/xnu-build-local.sh uses it
# Both Apple downloads need your Apple ID (developer.apple.com/download/all);
# drop the files into research/incoming/ (or /Volumes/HARD/ios-sailfish-incoming
# — ADATA is nearly full, the xip is ~7.7 GB).
# Usage: tools/local-toolchain.sh [--build]    (--build: then start a kernel build)
set -eu
REPO=$(cd "$(dirname "$0")/.." && pwd)
R=$REPO/research
BIN=$R/bin
XCODE_APP=$R/xcode15/Xcode.app
KDK_DIR=$R/kdk/KDK_13.5_22G74.kdk
mkdir -p "$R/incoming" /Volumes/HARD/ios-sailfish-incoming 2>/dev/null || mkdir -p "$R/incoming"

find_incoming() { # $1 = glob -> first match in either incoming dir
	for d in "$R/incoming" /Volumes/HARD/ios-sailfish-incoming; do
		f=$(ls "$d"/$1 2>/dev/null | head -1)
		[ -n "$f" ] && { echo "$f"; return 0; }
	done
	return 1
}

stage_tools() {
	mkdir -p "$BIN"
	# xcodes stub (same trick as the CI workflow: Xcode comes from research/xcode15)
	if [ ! -x "$BIN/xcodes" ]; then
		printf '#!/bin/bash\n# stub: Xcode is provided via research/xcode15 + DEVELOPER_DIR\nexit 0\n' > "$BIN/xcodes"
		chmod +x "$BIN/xcodes"
	fi
	# official release binaries (curl does not set quarantine attributes)
	if [ ! -x "$BIN/ninja" ]; then
		curl -sL -o /tmp/ninja.zip https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip
		unzip -oq /tmp/ninja.zip -d "$BIN" && rm /tmp/ninja.zip && chmod +x "$BIN/ninja"
	fi
	if [ ! -x "$BIN/gum" ]; then
		curl -sL -o /tmp/gum.tgz https://github.com/charmbracelet/gum/releases/download/v2.0.2/gum_2.0.2_Darwin_x86_64.tar.gz
		tar xzf /tmp/gum.tgz -C /tmp gum_2.0.2_Darwin_x86_64/gum
		mv /tmp/gum_2.0.2_Darwin_x86_64/gum "$BIN/gum" && rm -rf /tmp/gum.tgz /tmp/gum_2.0.2_Darwin_x86_64 && chmod +x "$BIN/gum"
	fi
	if [ ! -x "$BIN/cmake" ]; then
		curl -sL -o /tmp/cmake.tgz https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-macos-universal.tar.gz
		mkdir -p "$R/cmake"
		tar xzf /tmp/cmake.tgz -C "$R/cmake" --strip-components=1 && rm /tmp/cmake.tgz
		printf '#!/bin/bash\n# wrapper: cmake needs its Modules/share tree, kept in research/cmake/\nDIR="$(cd "$(dirname "$0")" && pwd)"\nexec "$DIR/../cmake/CMake.app/Contents/bin/cmake" "$@"\n' > "$BIN/cmake"
		chmod +x "$BIN/cmake"
	fi
	# bash 5: build.sh needs pipefail/set -u arrays; build from GNU source
	if [ ! -x "$BIN/bash" ] && [ ! -x /opt/homebrew/bin/bash ]; then
		echo "building bash 5 from source (needed by darwin-xnu-build)..."
		rm -rf /tmp/bash53build
		curl -sL -o /tmp/bash53.tar.gz https://ftp.gnu.org/gnu/bash/bash-5.3.tar.gz
		tar xzf /tmp/bash53.tar.gz -C /tmp && cd /tmp/bash-5.3
		./configure --prefix=/tmp/bash53 --without-bash-malloc >/dev/null
		make -j8 >/dev/null && make install >/dev/null
		cp /tmp/bash53/bin/bash "$BIN/bash"
		cd "$REPO" && rm -rf /tmp/bash-5.3 /tmp/bash53 /tmp/bash53.tar.gz /tmp/bash53build
	fi
	echo "tools: $(ls "$BIN" | tr '\n' ' ')"
}

stage_xcode() {
	if [ -d "$XCODE_APP" ]; then echo "xcode: already at $XCODE_APP"; return 0; fi
	XIP=$(find_incoming 'Xcode_15.4*.xip') || {
		echo "XCODE MISSING: download Xcode_15.4.xip (~7.7 GB) from"
		echo "  https://developer.apple.com/download/all/  (search \"Xcode 15.4\", needs Apple ID)"
		echo "  drop it in research/incoming/ or /Volumes/HARD/ios-sailfish-incoming/"; return 1; }
	echo "expanding $(basename "$XIP") -> $XCODE_APP (this takes a few minutes)..."
	mkdir -p "$R/xcode15"
	(cd "$R/xcode15" && xip -x "$XIP" | tail -2)
	VER=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$XCODE_APP/Contents/Info.plist")
	[ "$VER" = "15.4" ] || { echo "unexpected Xcode version $VER (want 15.4)"; return 1; }
	echo "xcode: $XCODE_APP (15.4) ready — the xip can be deleted once you have checked this"
}

stage_kdk() {
	if [ -d "$KDK_DIR" ]; then echo "kdk: already at $KDK_DIR"; return 0; fi
	DMG=$(find_incoming 'KDK_13.5*.dmg') || {
		echo "KDK MISSING: download Kernel Debug Kit 13.5 build 22G74 from"
		echo "  https://developer.apple.com/download/all/  (search \"KDK 13.5\", needs Apple ID)"
		echo "  drop it in research/incoming/ or /Volumes/HARD/ios-sailfish-incoming/"; return 1; }
	MNT=$(mktemp -d /tmp/kdkmnt.XXXXXX)
	hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$DMG" >/dev/null
	PKG=$(find "$MNT" -maxdepth 2 -name '*.pkg' | head -1)
	[ -n "$PKG" ] || { echo "no .pkg inside $DMG"; hdiutil detach "$MNT" >/dev/null; return 1; }
	rm -rf "$R/kdk/.expand"; mkdir -p "$R/kdk"
	pkgutil --expand-full "$PKG" "$R/kdk/.expand"
	K=$(find "$R/kdk/.expand" -maxdepth 3 -type d -name 'KDK_13.5_22G74.kdk' | head -1)
	[ -n "$K" ] || { echo "KDK_13.5_22G74.kdk not found in expanded payload"; return 1; }
	mv "$K" "$KDK_DIR" && rm -rf "$R/kdk/.expand"
	hdiutil detach "$MNT" >/dev/null
	echo "kdk: $KDK_DIR ready"
}

verify() {
	local DEV="$XCODE_APP/Contents/Developer"
	"$DEV/usr/bin/xcodebuild" -version
	"$DEV/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang" --version | head -1
	ls "$KDK_DIR"/System/Library/KernelSupport/lib*.a >/dev/null && echo "kdk: KernelSupport libs present"
	echo
	echo "toolchain ready. Build the kernel:"
	echo "  tools/xnu-build-local.sh xnu/out-local-0001            (Pixel 1 config)"
	echo "  PIXEL7=1 tools/xnu-build-local.sh xnu/out-local-p7-0001  (gs201 CPU target)"
	[ "${1:-}" = "--build" ] || return 0
	OUT=${OUT:-$REPO/xnu/out-local-0001}
	mkdir -p "$OUT"
	PATH="$BIN:$PATH" "$BIN/bash" "$REPO/tools/xnu-build-local.sh" "$OUT"
}

stage_tools
stage_xcode || MISSING=1
stage_kdk || MISSING=1
if [ "${MISSING:-0}" = 1 ]; then
	echo; echo "toolchain incomplete — add the missing Apple downloads above and re-run."; exit 1
fi
verify "${1:-}"
