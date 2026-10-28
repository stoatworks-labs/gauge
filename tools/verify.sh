#!/usr/bin/env bash
#
# Everything that can be checked without a host, in one go, in the order that
# fails fastest.
#
#   tools/verify.sh
#
# Each check answers a question none of the others can:
#
#   build         a FRESH universal Release build. Not the dev build: CMake
#                 latches the architecture list at the first target, so the
#                 only build worth measuring is one configured from nothing.
#   shaders       does every shader compile through a real GLSL compiler before
#                 a host finds out. The text compiled is what `gutest
#                 --dump-shaders` writes: the strings the plugin hands the driver.
#   reserved      no GLSL identifier is a word GLSL 4.10 reserves or a built-in
#                 the fleet has been bitten by (packed, noise1..4 and friends):
#                 Apple's compiler and glslc both accept some that Mesa refuses.
#   physics       every harness check, at TWO rasters: 320x180, which is what CI
#                 renders at, and 1280x720. A check that holds at one raster was
#                 fitted to it. Each is measured out of the picture:
#                   --perfs     holes where the standards put them, every format
#                   --aperture  pictures and frame lines likewise
#                   --squeeze   a round disc is 1/squeeze wide on the film
#                   --held      every frame keeps the picture it was exposed with
#                   --leader    the countdown frame by frame, on time and on the bar
#                   --flash     a sync flash is one film frame
#                   --track     the optical track is the sound at the film's speed
#                   --alpha     film opaque, only what is not film clear
#                   --resize    a moving strip keeps its frames across a resize
#                   --negative  every one of those FAILS on a perturbed model
#   browser demo  the page's GLSL is the plugin's, its WebAssembly is built
#                 from the current sources, and it draws what gutest draws.
#   pipe          the fleet's frame format, and its refusals.
#   sweep         does every control of both plugins change the picture.
#   bench         the render cost, for the record. Not pass/fail.
#   registration, lipo, plist, codesign, oxbow -- for each of the two bundles:
#                 does it contain a plugin, is it universal, does its plist name
#                 the binary on disk, does it ad-hoc sign, and does a real FFGL
#                 host load it and see the right name, id and type.
#
set -uo pipefail

cd "$(dirname "$0")/.."

BUILD="${BUILD:-build-universal}"
failures=0

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
pass() { printf '   \033[32mok\033[0m   %s\n' "$1"; }
fail() { printf '   \033[31mFAIL\033[0m %s\n' "$1"; failures=$(( failures + 1 )); }

step "build (fresh universal Release, $BUILD)"
rm -rf "$BUILD"
if cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
   && cmake --build "$BUILD" --parallel >/dev/null 2>&1; then
	pass "builds"
else
	fail "build failed -- run: cmake -B $BUILD -DCMAKE_BUILD_TYPE=Release && cmake --build $BUILD"
	exit 1
fi

GUTEST="$BUILD/gutest"

#---------------------------------------------------------------------------
# Every shader, through a real GLSL compiler. --target-env=opengl4.5 with
# -fauto-map-locations: glslc targets SPIR-V, which demands explicit locations
# GLSL does not. glslc is optional (brew install shaderc); none dumped is a
# FAILURE, because it means the dump broke.
#---------------------------------------------------------------------------
step "shaders"
dir="$( mktemp -d )"
"$GUTEST" --dump-shaders "$dir" >/dev/null
if ! command -v glslc >/dev/null 2>&1; then
	printf '   skipped: glslc not installed (brew install shaderc)\n'
else
	n=0; bad=0
	for shader in "$dir"/*.vert "$dir"/*.frag; do
		[ -e "$shader" ] || continue
		n=$(( n + 1 ))
		if ! glslc --target-env=opengl4.5 -fauto-map-locations "$shader" -o /dev/null 2>"$dir/err"; then
			printf '   %s does not compile\n' "$( basename "$shader" )"
			sed "s|$dir/||; s|^|      |" "$dir/err"
			bad=$(( bad + 1 ))
		fi
	done
	if [ "$n" -eq 0 ]; then
		fail "no shaders were dumped"
	elif [ "$bad" -eq 0 ]; then
		pass "all $n shaders compile"
	else
		fail "$bad of $n shaders do not compile"
	fi
fi

step "reserved words"
# Words GLSL 4.10 reserves that a careless identifier hits, and the noise
# built-ins (dice's trap: a user noise3 is shadowed, and only some drivers say).
words='patch|sample|input|output|filter|common|active|half|layout\s*[a-z_]|flat\s+[a-z]|packed|noise[1-4]|superp|namespace|using|cast|partition|resource'
hits="$( cat "$dir"/*.frag "$dir"/*.vert | grep -nE "\b(float|int|vec[234]|ivec[234]|bool|uint)\s+($words)\b|\b($words)\s*\(" || true )"
if [ -z "$hits" ]; then
	pass "no identifier is a reserved word or a noise built-in"
else
	fail "a reserved word is used as an identifier"
	printf '%s\n' "$hits" | sed 's/^/      /'
fi
rm -rf "$dir"

#---------------------------------------------------------------------------
# The browser demo (demo/, gauge-demo.stoatworks-labs.com). check_shaders.py:
# the page's copy of the GLSL is source/Shaders.cpp's to the character, joined
# in the plugin's order, and demo/gauge-core.wasm was built from the current
# sources. crosscheck.mjs: the page's WebAssembly and shaders in headless
# Chrome against this build's gutest --pipe on the same frames, and its
# negative control (a broken model on the page's side must be caught). The
# crosscheck needs node and Google Chrome and is skipped without them.
#---------------------------------------------------------------------------
step "browser demo"
if out=$(python3 demo/tools/check_shaders.py 2>&1); then
	pass "$( printf '%s\n' "$out" | tail -1 )"
else
	fail "demo/tools/check_shaders.py: the demo's copy has drifted"
	printf '%s\n' "$out" | grep -E '^FAIL|^ ' | sed 's/^/      /'
fi
CHROME_APP="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
if command -v node >/dev/null 2>&1 && [ -x "$CHROME_APP" ]; then
	if out=$(node demo/tools/crosscheck/crosscheck.mjs --gutest "$GUTEST" 2>&1); then
		pass "crosscheck, the page against gutest --pipe: $( printf '%s\n' "$out" | tail -1 )"
	else
		fail "crosscheck: the page does not draw what gutest draws"
		printf '%s\n' "$out" | sed 's/^/      /'
	fi
	if out=$(node demo/tools/crosscheck/crosscheck.mjs --gutest "$GUTEST" --negative 2>&1); then
		pass "crosscheck --negative: $( printf '%s\n' "$out" | tail -1 )"
	else
		fail "crosscheck --negative: a broken model on the page's side was not caught"
		printf '%s\n' "$out" | sed 's/^/      /'
	fi
else
	printf '   skipped the crosscheck: it needs node and Google Chrome\n'
fi

for size in 320x180 1280x720; do
	step "physics at $size"
	for check in perfs aperture squeeze held leader flash track alpha resize negative; do
		if out=$("$GUTEST" --$check --size $size 2>&1); then
			pass "gutest --$check: $( printf '%s\n' "$out" | grep -v '^$' | tail -1 )"
		else
			fail "gutest --$check at $size"
			printf '%s\n' "$out" | sed 's/^/      /'
		fi
	done
done

#---------------------------------------------------------------------------
# --pipe, in the fleet's frame format. Two and a half frames in must be exactly
# two out and a clean exit; a cue naming no parameter must be refused; a reader
# that hangs up must end the run with exit 1, not SIGPIPE's silent 141.
#---------------------------------------------------------------------------
step "pipe"
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp )
head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
got=$( "$GUTEST" --pipe --size 64x36 < "$raw" 2>/dev/null | wc -c | tr -d ' ' )
status=${PIPESTATUS[0]}
if [ "$status" -eq 0 ] && [ "$got" = "$(( frame * 2 ))" ]; then
	pass "2.5 frames in, exactly 2 frames out, clean exit"
else
	fail "2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
fi
printf '0 No Such Control 0.5\n' > "$cues"
"$GUTEST" --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1
status=$?
if [ "$status" -eq 2 ]; then
	pass "a cue naming no parameter is refused (exit 2)"
else
	fail "a cue naming no parameter gave exit $status, not 2"
fi
"$GUTEST" --set "Format=Nonsense" --out /dev/null --size 64x36 >/dev/null 2>&1
status=$?
if [ "$status" -eq 2 ]; then
	pass "--set refuses a value that is neither a number nor an option (exit 2)"
else
	fail "--set Format=Nonsense gave exit $status, not 2"
fi
head -c $(( frame * 20 )) /dev/zero > "$raw"
"$GUTEST" --pipe --size 64x36 < "$raw" 2>/dev/null | head -c 1 >/dev/null
status=${PIPESTATUS[0]}
if [ "$status" -eq 1 ]; then
	pass "a closed stdout ends the run with exit 1, not SIGPIPE"
else
	fail "a closed stdout gave exit $status, not 1"
fi
got=$( "$GUTEST" --plugin source --pipe --size 64x36 --frames 3 < /dev/null 2>/dev/null | wc -c | tr -d ' ' )
if [ "$got" = "$(( frame * 3 ))" ]; then
	pass "the source pipes --frames frames with no input"
else
	fail "the source piped $got bytes, not $(( frame * 3 ))"
fi
rm -f "$raw" "$cues"

step "sweep"
if out=$(python3 tools/sweep.py --binary "$GUTEST" 2>/dev/null); then
	pass "$( printf '%s\n' "$out" | tail -1 )"
else
	fail "tools/sweep.py reports a dead control"
	printf '%s\n' "$out" | grep -E '^DEAD|DEAD CONTROLS' | sed 's/^/      /'
fi

step "bench (for the record)"
"$GUTEST" --bench --frames 60 2>&1 | sed -n '3,8p' | sed 's/^/   /'

check_bundle() {
	local bundle="$1" binary="$2" ident="$3" name="$4" id="$5" kind="$6"
	step "bundle: $bundle"
	local syms archs exe got tmp probe self
	# `nm ... | grep -q X` FAILS when grep FINDS its match under pipefail:
	# capture and match instead of piping.
	syms=$(nm -gU "$BUILD/$bundle/Contents/MacOS/$binary" 2>/dev/null)
	case "$syms" in *_plugMain*) pass "exports plugMain" ;; *) fail "no plugMain -- the bundle contains no plugin" ;; esac
	archs=$(lipo -archs "$BUILD/$bundle/Contents/MacOS/$binary" 2>/dev/null)
	case "$archs" in *arm64*) pass "arm64 present" ;; *) fail "no arm64 (got: $archs)" ;; esac
	case "$archs" in *x86_64*) pass "x86_64 present" ;; *) fail "no x86_64 (got: $archs) -- a universal build was asked for" ;; esac
	exe=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$BUILD/$bundle/Contents/Info.plist" 2>/dev/null)
	got=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$BUILD/$bundle/Contents/Info.plist" 2>/dev/null)
	if [ -n "$exe" ] && [ -f "$BUILD/$bundle/Contents/MacOS/$exe" ]; then
		pass "CFBundleExecutable ($exe) is on disk"
	else
		fail "CFBundleExecutable is '$exe' but no such binary exists"
	fi
	if [ "$got" = "$ident" ]; then pass "CFBundleIdentifier is $got"; else fail "CFBundleIdentifier is '$got', not $ident"; fi
	tmp=$(mktemp -d)
	cp -R "$BUILD/$bundle" "$tmp/"
	if codesign --force --sign - --timestamp=none "$tmp/$bundle" >/dev/null 2>&1; then
		pass "ad-hoc signs (the command the release job runs)"
	else
		fail "ad-hoc signing failed"
	fi
	rm -rf "$tmp"
	OXBOW="${OXBOW:-../oxbow/build/oxbow}"
	[ -x "$OXBOW" ] || OXBOW="$HOME/Projects/resolume/oxbow/build/oxbow"
	if [ -x "$OXBOW" ]; then
		probe=$("$OXBOW" probe "$BUILD/$bundle" 2>&1)
		for want in "name:        $name" "id:          $id" "type:        $kind"; do
			case "$probe" in
				*"$want"*) pass "host sees '$want'" ;;
				*) fail "host does not see '$want' -- see: $OXBOW probe '$BUILD/$bundle'" ;;
			esac
		done
		self=$("$OXBOW" selftest "$BUILD/$bundle" 2>&1)
		case "$self" in
			*"selftest:    PASS"*) pass "instantiates through plugMain and renders 120 frames" ;;
			*) fail "oxbow selftest did not pass -- see: $OXBOW selftest '$BUILD/$bundle'" ;;
		esac
	else
		printf '   skipped: oxbow not built at %s\n' "$OXBOW"
	fi
}

if [ "$(uname)" = "Darwin" ]; then
	check_bundle "Gauge.bundle" "Gauge" "com.stoatworks.ffgl.gauge" "SW Gauge" "GU01" "effect"
	check_bundle "Gauge Leader.bundle" "Gauge Leader" "com.stoatworks.ffgl.gauge.leader" "SW Gauge Leader" "GU02" "source"
fi

printf '\n'
if [ "$failures" -eq 0 ]; then
	printf '\033[32mall checks passed\033[0m\n'
else
	printf '\033[31m%d check(s) failed\033[0m\n' "$failures"
fi
exit $(( failures > 0 ? 1 : 0 ))
