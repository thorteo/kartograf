#!/usr/bin/env python3
"""Embeds a text file into a generated C++ translation unit.

Keeps the overlay a single .exe with no data files alongside it: maps.json is
pinned in the repository and compiled in, so a release cannot half-update.

    embed_file.py <input> <output.cpp> <namespace> <symbol>
"""
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    source, target, namespace, symbol = sys.argv[1:5]

    with open(source, "r", encoding="utf-8") as handle:
        text = handle.read()

    # A raw string keeps the payload readable in the generated file. Pick a
    # delimiter the content cannot contain.
    delimiter = "embed"
    while f'){delimiter}"' in text:
        delimiter += "_"

    parts = namespace.split("::")
    opening = "".join(f"namespace {p} {{\n" for p in parts)
    closing = "".join(f"}}  // namespace {p}\n" for p in reversed(parts))

    with open(target, "w", encoding="utf-8") as handle:
        handle.write(f"// Generated from {source}. Do not edit.\n\n")
        handle.write('#include <string_view>\n\n')
        handle.write(opening)
        handle.write(f'\nextern const std::string_view {symbol};\n')
        handle.write(f'const std::string_view {symbol} = R"{delimiter}(')
        handle.write(text)
        handle.write(f'){delimiter}";\n\n')
        handle.write(closing)
    return 0


if __name__ == "__main__":
    sys.exit(main())
