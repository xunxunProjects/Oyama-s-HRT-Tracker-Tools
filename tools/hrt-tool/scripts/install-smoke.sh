#!/bin/sh
set -eu
build=${1:?build dir}
stage=${2:?stage dir}
cmake --install "$build" --prefix "$stage"
test -x "$stage/bin/hrt-tool"
"$stage/bin/hrt-tool" version
"$stage/bin/hrt-tool" f2f version
