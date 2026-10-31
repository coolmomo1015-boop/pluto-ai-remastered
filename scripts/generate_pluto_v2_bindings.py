"""Generate verified legacy-memory relocations for the pinned Pluto v2.0 DLL.

This is a compatibility probe, not proof that v2.0 is functionally compatible.
The script only reads the upstream DLL and emits src/pluto_bindings.h.
"""
import argparse
import hashlib
import pathlib
import struct
import sys

import capstone
import pefile

EXPECTED_SHA256 = "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc"

SINGLE = {
    0x485A40, 0x512688, 0x59688C, 0x596904,
    0x6283EC, 0x6283F4, 0x628430, 0x64DEC4,
    0x654880, 0x654AA0, 0x6D1260,
}
REQUIRED = {
    0x485A40, 0x512688, 0x59688C, 0x596904,
    0x6283EC, 0x6283F4, 0x628430, 0x64DEC4,
    0x654880, 0x654AA0, 0x6D1260,
}

def known(value: int) -> bool:
    return (
        value in SINGLE
        or 0x5124D8 <= value < 0x512510
        or 0x57EEE0 <= value < 0x5967F0
        or 0x59CCA8 <= value < 0x59CDF8
    )

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    raw = args.dll.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != EXPECTED_SHA256:
        raise SystemExit(f"Unsupported Pluto DLL: SHA-256 {actual}")

    pe = pefile.PE(data=raw)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise SystemExit("Pluto v2.0 DLL must be PE32/x86")

    base = pe.OPTIONAL_HEADER.ImageBase
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    cs.detail = True
    cs.skipdata = True
    rows = []
    seen = set()

    for section in pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        for ins in cs.disasm(section.get_data(), base + section.VirtualAddress):
            if not ins.id:
                continue
            for operand in ins.operands:
                if operand.type == capstone.x86.X86_OP_MEM:
                    value, offset, size = operand.mem.disp, ins.disp_offset, ins.disp_size
                elif operand.type == capstone.x86.X86_OP_IMM and ins.mnemonic not in ("call", "jmp"):
                    value, offset, size = operand.imm, ins.imm_offset, ins.imm_size
                else:
                    continue

                signed = (value + 0x80000000) % 0x100000000 - 0x80000000
                normalized = abs(signed)
                if not known(normalized):
                    continue
                if size != 4:
                    raise SystemExit(
                        f"Unexpected relocation operand width {size} at RVA "
                        f"0x{ins.address - base:x}; manual review required"
                    )
                expected_word = value & 0xFFFFFFFF
                if struct.unpack_from("<I", ins.bytes, offset)[0] != expected_word:
                    raise SystemExit("Instruction operand verification failed")
                seen.add(normalized)
                rows.append(
                    f"  {{0x{ins.address-base+offset:x},0x{expected_word:08x}}}, "
                    f"// {ins.address-base:x}: {ins.mnemonic} {ins.op_str}"
                )

    missing = sorted(REQUIRED - seen)
    if missing:
        formatted = ", ".join(f"0x{x:08x}" for x in missing)
        raise SystemExit(f"v2.0 lacks required legacy ABI references: {formatted}")
    if len(rows) < 200:
        raise SystemExit(f"Only {len(rows)} relocation operands found; expected at least 200")

    header = (
        "#pragma once\n"
        "#include <cstdint>\n"
        "// Generated for Pluto v2.0 md09b_1331000. Do not apply to another release.\n"
        "struct PlutoBinding {uint32_t rva;uint32_t expected;};\n"
        "inline constexpr PlutoBinding pluto_bindings[]={\n"
        + "\n".join(rows)
        + "\n};\n"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="utf-8", newline="\n")
    print(f"Verified {len(rows)} address operands; all required ABI sentinels found.")
    print(f"Output: {args.output}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
