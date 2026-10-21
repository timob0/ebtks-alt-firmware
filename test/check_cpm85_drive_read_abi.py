#!/usr/bin/env python3
"""Catch EBTKS bus read callbacks that return a byte instead of driving readData."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / "include" / "EBTKS_CPM85_Drive.h").read_text()
bus = (root / "include" / "EBTKS_Global_Data.h").read_text()
isr = (root / "src" / "EBTKS_Bus_Interface_ISR.cpp").read_text()
main = (root / "src" / "EBTKS.cpp").read_text()
assert "typedef bool (*ioReadFuncPtr_t)(void);" in bus
assert re.search(r"return \\(ioReadFuncs\\[addReg & 0x00FFU\\]\\)\\(\\);", isr)
for name, action in (
    ("onReadCpm85DriveState", "onState"),
    ("onReadCpm85DriveReply", "onReply"),
):
    pattern = rf"inline bool {name}\\(\\)\\s*\\{{\\s*readData = Cpm85Drive::{action}\\(\\);\\s*return true;\\s*\\}}"
    assert re.search(pattern, src), f"{name} must publish readData and return true"
assert "setIOReadFunc(0345, &onReadCpm85DriveState)" in main
assert "setIOReadFunc(0346, &onReadCpm85DriveReply)" in main
print("PASS: CPM85 read callbacks drive readData and claim the bus")
