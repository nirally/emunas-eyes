#!/bin/sh
# Disassembles a code dump written by the mod's CODEDUMP recon tool.
# Usage: tools/dis.sh <dump.bin> <load-rva-hex>
L="/e/Microsoft Visual Studio/Community/VC/Tools/Llvm/x64/bin"
tmp="$(mktemp -d)"
"$L/llvm-objcopy.exe" -I binary -O elf64-x86-64 --rename-section=.data=.text,code,alloc,load,readonly "$1" "$tmp/f.o" &&
"$L/llvm-objdump.exe" -d -M intel --no-show-raw-insn --adjust-vma=0x$2 "$tmp/f.o" | tail -n +7
rm -rf "$tmp"
