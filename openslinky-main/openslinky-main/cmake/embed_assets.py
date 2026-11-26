#!/usr/bin/env python3
"""
Embeds binary assets (fonts, images, compiled Java .class files) into a C++ source/header pair
with clean, typed spans matching original embedded asset symbols.
"""
import os
import sys


def format_byte_array(var_name: str, data: bytes) -> str:
    lines = [f"extern const unsigned char {var_name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")
    lines.append("};")
    lines.append(f"extern const unsigned int {var_name}_size = {len(data)}u;\n")
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) < 3 or (len(sys.argv) - 3) % 2 != 0:
        print("Usage: embed_assets.py <out.h> <out.cpp> [<symbol_name> <input_file> ...]", file=sys.stderr)
        return 1

    out_h = sys.argv[1]
    out_cpp = sys.argv[2]
    pairs = [(sys.argv[i], sys.argv[i + 1]) for i in range(3, len(sys.argv), 2)]

    os.makedirs(os.path.dirname(os.path.abspath(out_h)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(out_cpp)), exist_ok=True)

    h_lines = [
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace slinky::assets {",
        "",
    ]
    cpp_lines = [
        '#include "embedded_assets.hpp"',
        "",
        "namespace slinky::assets {",
        "",
    ]

    for sym, path in pairs:
        with open(path, "rb") as f:
            raw = f.read()
        h_lines.append(f"extern const unsigned char {sym}[{len(raw)}];")
        h_lines.append(f"extern const unsigned int {sym}_size;")
        h_lines.append("")
        cpp_lines.append(format_byte_array(sym, raw))

    h_lines.append("} // namespace slinky::assets")
    cpp_lines.append("} // namespace slinky::assets")

    with open(out_h, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines) + "\n")
    with open(out_cpp, "w", encoding="utf-8") as f:
        f.write("\n".join(cpp_lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
