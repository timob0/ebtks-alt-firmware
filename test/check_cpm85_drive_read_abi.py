#!/usr/bin/env python3
"""Catch EBTKS read callbacks that return bytes instead of driving readData."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "include" / "EBTKS_CPM85_Drive.h").read_text()
bus = (root / "include" / "EBTKS_Global_Data.h").read_text()
isr = (root / "src" / "EBTKS_Bus_Interface_ISR.cpp").read_text()
main = (root / "src" / "EBTKS.cpp").read_text()
assert "typedef bool (*ioReadFuncPtr_t)(void);" in bus
assert "(ioReadFuncs[addReg & 0x00FFU])()" in isr
for name, action in (
    ("onReadCpm85DriveState", "onState"),
    ("onReadCpm85DriveReply", "onReply"),
):
    expected = f"""inline bool {name}() {{
  readData = Cpm85Drive::{action}();
  return true;
}}"""
    assert expected in src, f"{name} must drive readData and return true"
assert "setIOReadFunc(0345, &onReadCpm85DriveState)" in main
assert "setIOReadFunc(0346, &onReadCpm85DriveReply)" in main
print("PASS: CPM85 read callbacks drive readData and claim the bus")

# CPDCAT keeps the 0344 HP opcode ISR allocation-free; only extend range.
mailbox=(Path(__file__).resolve().parents[1] / 'include' / 'EBTKS_CPM85_Drive.h').read_text()
assert 'if(code<1 || code>7)' in mailbox
