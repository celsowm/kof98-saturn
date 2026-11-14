#!/usr/bin/env sh
# ROM-free isolated object size/disassembly study, NOT a linked Saturn app.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/build-sh2-probe"}
cc=${SH2_CC:-sh2eb-elf-gcc}
size=${SH2_SIZE:-sh2eb-elf-size}
objdump=${SH2_OBJDUMP:-sh2eb-elf-objdump}
for tool in "$cc" "$size" "$objdump"; do
  command -v "$tool" >/dev/null || { echo "Missing: $tool" >&2; exit 1; }
done
mkdir -p "$out"
"$cc" --version > "$out/compiler.txt"
for opt in Os O2 O3; do
  for unit in dispatch paged_dispatch; do
    "$cc" -m2 -mb -"$opt" -std=c11 -ffreestanding -Wall -Wextra -Werror -I"$root/include" -c "$root/src/$unit.c" -o "$out/$unit-$opt.o"
    "$size" -A "$out/$unit-$opt.o" > "$out/$unit-$opt.size.txt"
    "$objdump" -dr "$out/$unit-$opt.o" > "$out/$unit-$opt.asm.txt"
  done
done
echo "Isolated SH-2 object reports: $out. Full game/link/cycle validation pending."
