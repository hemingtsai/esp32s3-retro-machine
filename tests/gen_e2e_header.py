#!/usr/bin/env python3
"""Assemble tests/programs/*.asm and emit a C header with the words.

Usage: python3 tests/gen_e2e_header.py path/to/assembler.py path/to/outdir
"""
import struct
import subprocess
import sys
from pathlib import Path

assembler = Path(sys.argv[1])
out_dir = Path(sys.argv[2])
programs_dir = Path(__file__).resolve().parent / "programs"


def assemble(asm_path: Path) -> list[int]:
    name = asm_path.stem
    out = out_dir / f"{name}.bin"
    subprocess.run(
        ["python3", str(assembler), str(asm), "-o", str(out)],
        check=True,
        capture_output=True,
    )
    data = out.read_bytes()
    return [struct.unpack("<H", data[i:i + 2])[0] for i in range(0, len(data), 2)]


lines = ["#pragma once", ""]
for asm in sorted(programs_dir.glob("*.asm")):
    name = asm.stem
    words = assemble(asm)
    symbol = name.upper()
    lines.append(f"static const uint16_t e2e_{name}[] = {{")
    for word in words:
        lines.append(f"  0x{word:04X},")
    lines.append("};")
    lines.append(f"#define E2E_{symbol}_SIZE {len(words)}")
    lines.append("")

print("\n".join(lines))
