#!/bin/bash
# package.sh [machine...]
# Collect the built programs into build/dist/, one flat set of files:
#   booter_<machine>        every machine
#   loadlinux_<machine>     the real machines (configs that set LINUX_LINK_ADDR)
#   placements.txt          where each machine's kernel and guest are placed
# and the same files in build/h2-builds.tar.gz.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
machines=("$@")
[ ${#machines[@]} -gt 0 ] || mapfile -t machines < <(cd "$here/configs" && ls *.mk | sed 's/\.mk$//')

dist=$here/build/dist
rm -rf "$dist" "$here/build/h2-builds.tar.gz"
mkdir -p "$dist"
printf '%-16s %-6s %-12s %-12s %s\n' machine archv kernel guest loadlinux > "$dist/placements.txt"
for m in "${machines[@]}"; do
    ( . "$here/configs/$m.mk"
      install -m 0755 "$here/build/$m/booter" "$dist/booter_$m"
      if [ -n "${LINUX_LINK_ADDR:-}" ]; then
          install -m 0755 "$here/build/$m/loadlinux" "$dist/loadlinux_$m"
      fi
      printf '%-16s %-6s %-12s %-12s %s\n' "$m" "$ARCHV" "$H2K_LOAD_ADDR" \
          "$H2K_GUEST_START" "${LINUX_LINK_ADDR:--}" >> "$dist/placements.txt" )
done

tar -C "$here/build" --transform 's,^dist,h2-builds,' -czf "$here/build/h2-builds.tar.gz" dist
echo "dist: $(ls "$dist" | wc -l) files in $dist, tarball $here/build/h2-builds.tar.gz"
