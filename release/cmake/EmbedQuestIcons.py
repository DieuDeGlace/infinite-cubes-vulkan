from pathlib import Path
import sys


source, output = map(Path, sys.argv[1:])
data = source.read_bytes()
with output.open("w", encoding="ascii") as header:
    header.write("constexpr unsigned char QuestIconPng[] = {\n")
    for offset in range(0, len(data), 16):
        header.write("    " + ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16]) + ",\n")
    header.write("};\n")
