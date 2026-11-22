import struct
import sys
from pathlib import Path


source, destination, symbol = sys.argv[1:]
words = [f"0x{word:08x}" for (word,) in struct.iter_unpack("<I", Path(source).read_bytes())]
lines = ["    " + ", ".join(words[i:i + 8]) + "," for i in range(0, len(words), 8)]
Path(destination).write_text(
    "#pragma once\n#include <cstdint>\n\n"
    + f"inline constexpr std::uint32_t {symbol}[] = {{\n"
    + "\n".join(lines) + "\n};\n",
    encoding="utf-8",
)
