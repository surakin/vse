#!/usr/bin/env bash
# Files the debug information of one build in a symbol store, under its build id.
#
#   scripts/store-symbols.sh BINARY [STORE]        BINARY is the unstripped program, or its .debug file
#
# Linux only (a shell script). STORE defaults to ./symbols. The layout, both of which debuggers look for:
#   STORE/<name>/<build-id>/<name>.debug         one directory per build, easy to read and to archive
#   STORE/.build-id/<xx>/<rest>.debug            the layout gdb and lldb search by build id (a link to the file above)
# Keep the store for as long as you keep the releases it belongs to; scripts/crash-report.sh takes the store as its second
# argument. Prints the build id and the path of the stored file.
set -eu
bin="${1:-}"
store="${2:-symbols}"
if [ -z "$bin" ] || [ ! -f "$bin" ]; then
  echo "usage: store-symbols.sh BINARY [STORE]" >&2
  exit 64
fi
id="$(readelf -n "$bin" 2>/dev/null | sed -n 's/.*Build ID: *//p' | head -n 1)"
if [ -z "$id" ]; then
  echo "store-symbols.sh: $bin has no GNU build id (link with -Wl,--build-id)" >&2
  exit 2
fi
# Without debug sections there is nothing worth storing.
if ! readelf -S "$bin" 2>/dev/null | grep -q '\.debug_info'; then
  echo "store-symbols.sh: $bin has no debug information (build with -g, and give the file from before it was stripped)" >&2
  exit 2
fi
name="$(basename "$bin")"
name="${name%.debug}"
dir="$store/$name/$id"
mkdir -p "$dir" "$store/.build-id/${id:0:2}"
if [ "${bin%.debug}" = "$bin" ]; then
  objcopy --only-keep-debug "$bin" "$dir/$name.debug"
else
  cp "$bin" "$dir/$name.debug"
fi
chmod 644 "$dir/$name.debug"
# A relative link, so the store can be moved, archived or copied to another host without breaking.
ln -sf "../../$name/$id/$name.debug" "$store/.build-id/${id:0:2}/${id:2}.debug"
echo "$id"
echo "$dir/$name.debug"
