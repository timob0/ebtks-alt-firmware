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

imp=(root/"include"/"EBTKS_CPM85_SD_Import.h").read_text()
esp=(root/"include"/"EBTKS_ESP.h").read_text()
assert "safeLiteralSource(const char *source)" in imp
assert "if(part==0 && ch=='.')return false;" in imp
assert '"/cpm/transfers/%s"' in imp
assert 'strcmp(op,"begin_literal")==0' in esp
print("PASS: B2d Teensy literal SD path sandbox and transport dispatch")

# B2d: known-busy phase is an HP UI hint, not a transaction cancellation.
assert "response[0]=(c=='B'||c=='W'" in mailbox
print("PASS: Teensy relays early known-CP/M-busy phase")

# B2d optional export leaf/path: source is still CP/M drive-qualified;
# SD rename uses the same sandbox and no-overwrite staging.
exp=(root/"include"/"EBTKS_CPM85_SD_Export.h").read_text()
assert "safeLiteralDestination(const char *name)" in exp
assert "if(!part && c=='.')return false;" in exp
assert "const char *requested=leaf;" in exp
assert "requested=sdDestination;" in exp
assert "strrchr(requested,'/')" in exp
assert "SD.exists(dest)" in exp and "SD.exists(part)" in exp
assert 'const char *sdName=req["sd_name"] | "";' in esp
assert "Cpm85SdExport::begin(name,reason,sdName)" in esp
print("PASS: Teensy optional CPEXPORT literal name validation and exclusive staging")

# Orphan recovery must only unlock a retained START if the ESP32
# explicitly confirms the original txn is not present.
assert 'strcmp(ack["text"] | (const char *)"", "UNKNOWN TRANSFER TRANSACTION")==0' in mailbox
assert "if((operation==11 || operation==12) && !ok && phase[0]=='E'" in mailbox
print("PASS: only authenticated unknown-transaction status clears stale Teensy ownership")
