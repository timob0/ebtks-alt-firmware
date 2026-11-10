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

# CPREBOOT expands the mailbox opcode range without touching reset GPIO
# in an ISR; execution is deferred to the Teensy's ordinary main loop.
mailbox=(Path(__file__).resolve().parents[1] / 'include' / 'EBTKS_CPM85_Drive.h').read_text()
assert 'if(code<1 || code>12)' in mailbox
assert 'Cpm85Drive::service(&ESP_Reset);' in main
assert 'if(operation==8 && state==2)espRebootPending=true;' in mailbox
assert 'if(state!=1 || operation==8 ||' in mailbox
assert '(transferLocked && operation==4))return;' in mailbox
assert 'if(state!=1 || operation==8 || ack.isNull())return;' in mailbox
assert 'kEspRebootGraceMs=1500' in mailbox
print("PASS: CPREBOOT opcode local to Teensy, ACK-before-reset and no ISR GPIO")

# B2d: STATUS/CANCEL carry the original start ID, unlike opcode 0
# which must never be mistaken for a confirmed remote cancel.
assert 'static volatile uint16_t transferId=0;' in mailbox
assert 'static volatile bool transferLocked=false;' in mailbox
assert 'transferId=sequence;' in mailbox
assert 'transferLocked=true;' in mailbox
assert 'cmd["txn"]=(uint16_t)transferId;' in mailbox
assert 'operation>=9 && operation<=12' in mailbox
assert "response[1]='|';" in mailbox
assert "if(code==0)" in mailbox
assert "if(operation==11 || operation==12)" in mailbox
print("PASS: B2d HP transfer token, phase prefix, and local/remote abort distinction")
