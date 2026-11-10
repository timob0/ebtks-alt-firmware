# B2d — HP-85 blocking CP/M file transfers (implementation candidate)

Updated 2026-11-10. Branch in all repositories:
`feature/cpm-file-transfer-b2d`.

**This is not yet hardware accepted. Do not merge into `main`.**
The original B2c 8192-byte Z80 CPU-task stack and B2b/B2c native BDOS
engines remain unchanged.

## Implemented

- CPM85 v0.05a adds BASIC `CPEXPORT "A:STAT.COM"` and
  `CPIMPORT "A:STAT.COM","A:COPY.COM"`, using native string
  parser ABI and the existing four-port Teensy mailbox.
- Separate START (9 export/10 import), STATUS (11) and remotely
  confirmed pre-start CANCEL (12) opcodes. All opcode constants
  in Capricorn source are **octal** (`=11`, `=12`, `=13`, `=14`).
- Teensy fixes transaction ID when the START opcode arrives, BEFORE
  any ACK, and retains it across local timeout (opcode 0), preventing
  lost ACKs from stranding a potentially running operation.
- ESP32 owns one serialized HP transaction. Its gate reservation
  excludes competing HTTP services; CCP start uses an atomic
  `WAIT_EMPTY_LINE` -> `RUNNING` compare-and-swap against
  pre-start cancellation. Repeated status queries cannot reissue MAKE.
- Transfer replies begin with a machine phase (`W` waiting CCP,
  `P` armed, `R` running, `S` succeeded, `F` failed,
  `X` cancelled, `E` rejected); a local UART timeout is a
  separate `U`, never proof of remote cancellation.
- HP BASIC blocks and polls status; for repeated pre-start waits it
  prompts `CP/M NOT IDLE - RETURN TO A>` and
  `C=CONTINUE A=ABORT`. The C/A keyboard path reads HP native
  `KEYHIT`; **verify on physical hardware before writable tests**.
  Initial prompt delay uses an uncalibrated bounded software counter;
  the previously discussed ~10 seconds is the target, not yet measured.
- An unacknowledged pre-start request is automatically cancelled after
  60 seconds without HP status contact. Active BDOS is never aborted
  by this lease or by the C/A request. A stopped/interrupted BASIC
  statement is not remote cancellation.
- Teensy refuses `CPRESET` and `CPREBOOT` while an unresolved
  HP transfer still owns the transaction. Existing K8 keyboard
  detach/reattach and disk/terminal paths are unchanged.
- Final report includes actual 128-byte records sent, byte count,
  cumulative CRC16, source/BDOS failure reason, and possible
  partial CP/M target warning. CREATE-NEW, 63/64-record limits and
  independent BDOS readback remain in force. No ZIP/batch yet.

## Build gate on Mac (no flash before all succeed)

ESP32:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
bash tests/run_z80_core_native.sh
python3 tests/check_cpm85_bridge.py
pio run -e esp32dev
pio run -e esp32dev-tft
python3 tests/check_esp32_dram_budget.py
```

Teensy:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
python3 test/check_cpm85_drive_read_abi.py
pio run -e teensy41
```
Option ROM:
```sh
git switch feature/cpm-file-transfer-b2d && git pull
python3 tests/check_cpm85_keywords.py
mkdir -p build
./py311/bin/capasm src/cpm85.asm -b build/CPM85.bin -l build/CPM85.lst -r 2 -c
./py311/bin/caplif build/CPM85.bin -l build/CPM85.dat -f CPM85
```
When the Mac build gates are green, retain tested B2c binaries and
backup mounted CP/M disk images plus the SD transfer folder.
Deployment order: matched Teensy41 firmware, ESP32
`esp32dev-tft` via the working **serial->Teensy->ESP Prog**
recovery route (browser OTA is unreliable), then CPM85.dat to SD
and `LOADBIN "CPM85"` on HP-85. Do not replace SIMON85 or PRINTER85.

## Controlled first hardware tests (use this order)

1. Verify `CPVER` reports **CPM85 0.05a**; check CPSTATUS, native
   CP/M DIR, K8 back to HP BASIC, and `CPM` re-entry.
2. First exercise the waiting/C/A path with CP/M deliberately BUSY,
   not parked at `A>` (e.g. a nonwriting program). Issue
   `CPEXPORT "A:NOFILE.COM"` from HP BASIC. Test **A** cancellation
   before returning to CCP. Observe message `TRANSFER CANCELLED
   BEFORE BDOS START`; check no surprise operation occurs when
   CP/M later reaches `A>`.
3. Repeat waiting with **C** and allow CP/M to return to A>.
   `NOFILE.COM` does not exist, so the expected result is a
   non-destructive `file_not_found` error. If C/A keyboard input
   does not work, STOP testing and do not attempt a writable import.
4. Only after the above: confirm `A:COPY.COM` is ABSENT, backup A.dsk,
   check published Teensy SD `A_STAT.COM` exists, then
   `CPIMPORT "A:STAT.COM","A:COPY.COM"`. Expect 42 records,
   5376 bytes, CRC16 54346 and clean BDOS readback. Check CP/M
   DIR/STAT and SD file integrity. Never execute copied COM until
   independently verified.
5. Test export of an existing small CP/M file whose destination
   `/cpm/transfers/A_<NAME>` is ABSENT. Do not reuse
   `A:STAT.COM` export if `A_STAT.COM` already exists.
6. After successful transfer, check ESP /api/system/memory and
   /api/system/health, especially Z80 8192-byte stack watermark,
   free8, largest8, watchdog/reset and SPI failure counters.

A failed import after MAKE may leave a partial new CP/M file; do
not auto-delete or silently retry. No extra diagnostic-only flashes.
No `main` merge or branch deletion without explicit user approval.
