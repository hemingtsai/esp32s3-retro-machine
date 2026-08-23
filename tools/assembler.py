#!/usr/bin/env python3
"""
16-bit Retro CPU Assembler

Usage:
    python assembler.py input.asm -o output.bin
    python assembler.py input.asm -o output.txt --format text
    python assembler.py input.asm -o output.txt --format addrtext

Text output:
    - one 16-bit word per line
    - each word is displayed as 8-bit groups separated by a space
    - e.g. 00000100 10001000

AddrText output:
    - includes word address (hex) before each word
    - e.g. 0000: 00000100 10001000
"""

import argparse
import re
import struct
from pathlib import Path

OPCODES = {
    "MOV":  0b00000,
    "LDI":  0b00001,
    "RED":  0b00010,
    "WRT":  0b00011,
    "PUSH": 0b00100,
    "POP":  0b00101,
    "MNXT": 0b00110,
    "MPRV": 0b00111,
    "ADD":  0b01000,
    "SUB":  0b01001,
    "DIV":  0b01010,
    "INC": 0b01011,
    "DEC": 0b01100,
    "AND": 0b01101,
    "OR":  0b01110,
    "XOR": 0b01111,
    "NOT": 0b10000,
    "SHL": 0b10001,
    "SHR": 0b10010,
    "CMP": 0b10011,
    "JMP": 0b10100,
    "JZ":  0b10101,
    "JNZ": 0b10110,
    "JN":  0b10111,
    "JP":  0b11000,
    "JC":  0b11001,
    "JNC": 0b11010,
    "JV":  0b11011,
    "CALL":0b11100,
    "RET": 0b11101,
    "NOP": 0b11110,
    "HLT": 0b11111,
}

REGISTERS = {
    "ORD0": 0x0,
    "ORD1": 0x1,
    "PC": 0x2,
    "IR": 0x3,
    "MAR": 0x4,
    "SP": 0x5,
    "RS": 0x6,
    "DISPLAY": 0x7,
    "R0": 0x8,
    "R1": 0x9,
    "R2": 0xA,
    "R3": 0xB,
    "R4": 0xC,
    "R5": 0xD,
    "R6": 0xE,
    "R7": 0xF,
}

# Registers that are safe as general MOV destinations.
MOV_DEST_ALLOWED = set(REGISTERS) - {"IR"}

# Instructions with exactly one register operand.
ONE_REG = {"RED", "WRT", "PUSH", "POP"}

# Instructions whose operand is a register containing the target address.
JUMP_REG = {"JMP", "JZ", "JNZ", "JN", "JP", "JC", "JNC", "JV", "CALL"}

NO_OPERAND = {
    "MNXT", "MPRV", "ADD", "SUB", "DIV", "INC", "DEC",
    "AND", "OR", "XOR", "NOT", "SHL", "SHR", "CMP", "RET", "NOP", "HLT"
}

DIRECTIVE_RE = re.compile(r"^\s*(?:\.word|WORD)\b", re.I)
ENTRY_RE = re.compile(r"^\s*\.entry\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", re.I)
SECTION_RE = re.compile(r"^\s*\.(?:text|data)(?:\s+.*)?$", re.I)


class AssemblerError(Exception):
    pass


def strip_comment(line: str) -> str:
    # ';' and '#' are accepted as comments.
    # '#' inside a token is intentionally not used by this assembler.
    line = line.split(";", 1)[0]
    line = line.split("#", 1)[0]
    return line.strip()


def split_operands(s: str):
    if not s.strip():
        return []
    return [x.strip() for x in s.split(",")]


def parse_number(token: str) -> int:
    token = token.strip()
    neg = token.startswith("-")
    if neg:
        token = token[1:].strip()

    if token.lower().startswith("0x"):
        value = int(token, 16)
    elif token.lower().endswith("h"):
        value = int(token[:-1], 16)
    elif token.lower().startswith("0b"):
        value = int(token, 2)
    elif token.lower().startswith("0o"):
        value = int(token, 8)
    elif token.isdigit():
        value = int(token, 10)
    else:
        raise ValueError(token)

    return -value if neg else value


def parse_register(token: str) -> int:
    key = token.strip().upper()
    if key not in REGISTERS:
        raise AssemblerError(f"invalid register: {token}")
    return REGISTERS[key]


def parse_value(token: str, labels: dict, line_no: int) -> int:
    token = token.strip()
    try:
        return parse_number(token)
    except ValueError:
        key = token.upper()
        if key in labels:
            return labels[key]
        raise AssemblerError(f"line {line_no}: invalid value or unknown label: {token}")


def encode(line: str, line_no: int, labels: dict, pc: int = 0):
    # Returns list of 16-bit words. `pc` is the word address of this
    # instruction; it is needed to expand pseudo-instructions that
    # reference the following address (e.g. CALL with a label).
    parts = line.split(None, 1)
    mnemonic = parts[0].upper()
    operands = split_operands(parts[1] if len(parts) > 1 else "")

    if mnemonic not in OPCODES:
        raise AssemblerError(f"line {line_no}: unknown instruction: {mnemonic}")

    op = OPCODES[mnemonic]

    def u16(value, what="value"):
        if not 0 <= value <= 0xFFFF:
            raise AssemblerError(
                f"line {line_no}: {what} out of 16-bit range: {value}"
            )
        return value

    def reg_operand(index=0):
        if len(operands) <= index:
            raise AssemblerError(f"line {line_no}: missing operand")
        return parse_register(operands[index])

    if mnemonic == "MOV":
        if len(operands) != 2:
            raise AssemblerError(f"line {line_no}: MOV requires source,destination")

        src, dst = operands
        # MOV source, destination
        # R-Type: OPCODE | SOURCE | DEST | RSV
        # Source/destination are registers only.
        s = parse_register(src)
        d = parse_register(dst)

        if dst.upper() == "IR":
            raise AssemblerError(f"line {line_no}: IR cannot be a MOV destination")

        word = (op << 11) | (s << 7) | (d << 3)
        return [word]

    if mnemonic == "LDI":
        if len(operands) != 2:
            raise AssemblerError(f"line {line_no}: LDI immediate,destination")

        # LDI immediate, destination
        value = u16(
            parse_value(operands[0], labels, line_no),
            "immediate"
        )
        d = parse_register(operands[1])

        if d < REGISTERS["R0"] or d > REGISTERS["R7"]:
            raise AssemblerError(
                f"line {line_no}: LDI destination must be R0-R7"
            )

        return [(op << 11) | (d << 7), value]

    if mnemonic in ONE_REG:
        if len(operands) != 1:
            raise AssemblerError(f"line {line_no}: {mnemonic} requires one register")
        r = reg_operand()
        return [(op << 11) | (r << 7)]

    if mnemonic in JUMP_REG:
        if len(operands) != 1:
            raise AssemblerError(
                f"line {line_no}: {mnemonic} requires target register or label"
            )

        target = operands[0]

        if target.upper() not in REGISTERS:
            key = target.upper()
            if key not in labels:
                raise AssemblerError(
                    f"line {line_no}: unknown label: {target}"
                )

            value = u16(labels[key], "address")

            if mnemonic == "CALL":
                # Pseudo-instruction:
                #   CALL LABEL
                # ->
                #   LDI LABEL, R7
                #   LDI ret, R6
                #   CALL R7
                #
                # RET pops the address of the word right after the CALL
                # opcode itself (inside this expansion), so the return
                # address must be loaded explicitly. `ret` points to the
                # first word after the whole 5-word expansion.
                ldi_target = (
                    OPCODES["LDI"] << 11
                ) | (REGISTERS["R7"] << 7)
                ldi_ret = (
                    OPCODES["LDI"] << 11
                ) | (REGISTERS["R6"] << 7)
                ret = u16(pc + 5, "return address")
                call = (op << 11) | (REGISTERS["R7"] << 7)
                return [ldi_target, value, ldi_ret, ret, call]

            # Pseudo-instruction:
            #   Jxx LABEL
            # ->
            #   LDI LABEL, R7
            #   Jxx R7
            ldi = (
                OPCODES["LDI"] << 11
            ) | (REGISTERS["R7"] << 7)
            jump = (op << 11) | (REGISTERS["R7"] << 7)
            return [ldi, value, jump]

        r = parse_register(target)
        return [(op << 11) | (r << 7)]

    if mnemonic in NO_OPERAND:
        if operands:
            raise AssemblerError(f"line {line_no}: {mnemonic} takes no operands")
        return [op << 11]

    raise AssemblerError(f"line {line_no}: assembler has no encoding rule for {mnemonic}")


def preprocess(source: str):
    """
    Returns:
        items: (line_no, labels_defined, instruction_text)
        entry_label: optional program entry label
    """
    result = []
    entry_label = None

    for line_no, raw in enumerate(source.splitlines(), 1):
        line = strip_comment(raw)

        if not line:
            continue

        entry_match = ENTRY_RE.match(line)
        if entry_match:
            if entry_label is not None:
                raise AssemblerError(
                    f"line {line_no}: duplicate .entry directive"
                )
            entry_label = entry_match.group(1).upper()
            result.append((line_no, [], ""))
            continue

        # .text and .data are assembler directives, not CPU instructions.
        # They currently share one linear address space.
        if SECTION_RE.match(line):
            result.append((line_no, [], ""))
            continue

        labels = []

        while True:
            m = re.match(
                r"^\s*([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$",
                line
            )
            if not m:
                break

            labels.append(m.group(1).upper())
            line = m.group(2).strip()

            if not line:
                break

        result.append((line_no, labels, line))

    return result, entry_label


def first_pass(items):
    labels = {}
    pc = 0  # word address

    for line_no, defined_labels, text in items:
        for label in defined_labels:
            if label in labels:
                raise AssemblerError(
                    f"line {line_no}: duplicate label: {label}"
                )
            labels[label] = pc

        if not text:
            continue

        if DIRECTIVE_RE.match(text):
            parts = text.split(None, 1)
            values = split_operands(
                parts[1] if len(parts) > 1 else ""
            )
            if not values:
                raise AssemblerError(
                    f"line {line_no}: .word requires a value"
                )
            pc += len(values)
            continue

        mnemonic = text.split(None, 1)[0].upper()

        if mnemonic not in OPCODES:
            raise AssemblerError(
                f"line {line_no}: unknown instruction: {mnemonic}"
            )

        # LDI occupies two 16-bit words.
        pc += 2 if mnemonic == "LDI" else 1

        # A label used directly as a jump/CALL target expands to:
        #   Jxx LABEL -> LDI LABEL, R7; Jxx R7          (2 extra words)
        #   CALL LABEL -> LDI LABEL, R7; LDI ret, R6;
        #                 CALL R7                        (4 extra words)
        if mnemonic in JUMP_REG:
            parts = text.split(None, 1)
            operands = split_operands(
                parts[1] if len(parts) > 1 else ""
            )
            if (
                len(operands) == 1
                and operands[0].upper() not in REGISTERS
            ):
                pc += 4 if mnemonic == "CALL" else 2

    if pc > 0x10000:
        raise AssemblerError("program exceeds 64 Ki words")

    return labels


def second_pass(items, labels):
    words = []
    source_map = []

    for line_no, _, text in items:
        if not text:
            continue

        if DIRECTIVE_RE.match(text):
            parts = text.split(None, 1)
            values = split_operands(parts[1] if len(parts) > 1 else "")
            for value in values:
                word = parse_value(value, labels, line_no)
                if not 0 <= word <= 0xFFFF:
                    raise AssemblerError(
                        f"line {line_no}: .word value out of range: {word}"
                    )
                words.append(word)
                source_map.append((line_no, text))
            continue

        encoded = encode(text, line_no, labels, len(words))
        for word in encoded:
            words.append(word)
            source_map.append((line_no, text))

    return words, source_map


def write_binary(path: Path, words):
    with path.open("wb") as f:
        for word in words:
            # Little-endian 16-bit words.
            f.write(struct.pack("<H", word))


def write_text(path: Path, words):
    with path.open("w", encoding="ascii") as f:
        for word in words:
            bits = f"{word:016b}"
            f.write(bits[:8] + " " + bits[8:] + "\n")


def write_addr_text(path: Path, words):
    """Write text output with address prefix, e.g. '0000: 00000100 10001000'."""
    with path.open("w", encoding="ascii") as f:
        for addr, word in enumerate(words):
            bits = f"{word:016b}"
            f.write(f"{addr:04X}: {bits[:8]} {bits[8:]}\n")


def write_hex(path: Path, words):
    with path.open("w", encoding="ascii") as f:
        for word in words:
            f.write(f"{word:04X}\n")


def assemble(source: str):
    items, entry_label = preprocess(source)
    labels = first_pass(items)

    if entry_label is None:
        entry_address = 0x0000
    else:
        if entry_label not in labels:
            raise AssemblerError(
                f"unknown .entry label: {entry_label}"
            )
        entry_address = labels[entry_label]

    words, source_map = second_pass(items, labels)
    return words, source_map, labels, entry_address


def main():
    parser = argparse.ArgumentParser(description="Assembler for the 16-bit retro CPU")
    parser.add_argument("input", type=Path, help="assembly source file")
    parser.add_argument("-o", "--output", type=Path, required=True,
                        help="output file")
    parser.add_argument(
        "--format",
        choices=("bin", "text", "hex", "addrtext"),
        default="bin",
        help="output format (default: bin)",
    )
    parser.add_argument(
        "--symbols",
        action="store_true",
        help="show symbol table and entry address",
    )
    args = parser.parse_args()

    try:
        source = args.input.read_text(encoding="utf-8")
        words, _, labels, entry_address = assemble(source)

        if args.format == "bin":
            write_binary(args.output, words)
        elif args.format == "text":
            write_text(args.output, words)
        elif args.format == "addrtext":
            write_addr_text(args.output, words)
        else:
            write_hex(args.output, words)

        print(f"Assembled {len(words)} word(s) -> {args.output}")

        if args.symbols:
            print(f"Entry: {entry_address:04X}h")
            print("Symbols:")
            for name, address in sorted(
                labels.items(),
                key=lambda item: item[1]
            ):
                print(f"  {name:<20} {address:04X}h")

    except (OSError, AssemblerError) as e:
        print(f"Error: {e}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()