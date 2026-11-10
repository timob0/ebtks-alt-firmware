## 2026-11-11 — B2d ROM-only v0.06b hardware regression correction

Hardware v0.06a result: shorter CPM message and native HP BASIC error
reporting work, but first import after boot gave ERROR 74 STALL and
subsequent imports gave ERROR 67 FILE NAME. Static inspection identified
premature cancellation on a single ESP32 advisory 'B' busy phase:
the native CCP gate can transiently be BUSY during a normal prompt
transition. Do not treat 'B' alone as proof of an active program.
CPM85 v0.06b waits a bounded 8 poll grace on B/W/P before requesting
an atomic pre-BDOS CANCEL; active BDOS remains non-cancellable.
This is a **ROM-only correction**: ESP32 and Teensy firmware need no
change for this specific issue. CAPASM + LIF packaging and static
keyword regressions succeeded (ROM CI 42923547016).

A later ERROR 67 could alternatively reflect the newly enforced
literal SD source naming: `CPIMPORT "A_STAT.COM","F:FIXTST.COM"`
is valid, but `CPIMPORT "A:STAT.COM","F:FIXTST.COM"` is invalid
under the current ESP32 firmware. Do not claim this was the
user's exact argument; request the full statement/CPXCODE/CPXSTAT
if failures persist.

After loading ROM v0.06b, test an absent F:FIXTST.COM at CP/M A>
using `CPIMPORT "A_STAT.COM","F:FIXTST.COM"`. Expect silent
success, `CPXCODE=0`. If failed, record ERRN, CPXCODE and CPXSTAT
before testing the busy Multiplan case. No merge to main yet.

---

## 2026-11-10 — User-approved HP BASIC semantics, CPM85 v0.06a

Following successful physical v0.05b Abort-while-Multiplan test, user pointed
out a logical usability flaw: while blocking `CPIMPORT` is executing, the
same HP keyboard cannot independently bring the CP/M application back to
its native A> prompt. Therefore the C=CONTINUE/A=ABORT interactive prompt
is not useful. Also normal HP BASIC commands should not flood scripted
output with success messages, and `CPM` should display a short intro.

**Implemented in HP ROM v0.06a**, on existing B2d branch:
- `CPM` displays exactly `CONNECTED TO CP/M - K8 TO EXIT` (30 chars).
- `CPIMPORT` and `CPEXPORT` produce **no CRT output on success**.
- When ESP32 signals phase `B` (known-running program), BASIC requests
  remote opcode 12 CANCEL promptly; it raises a native HP BASIC error
  **only after the server confirms pre-start cancellation**. Thus no
  deferred write can execute after a busy failure. Phase `W` (unknown
  prompt) and `P` (armed but not yet BDOS) use a limited 10-poll grace,
  then the same atomic CANCEL. The `R` phase must continue waiting:
  BDOS that already started cannot be aborted.
- Terminal `F`/`E` failures raise BASIC error rather than printing
  diagnostics. Uses proven `JSB =ERRORP; BYT 74D` for busy/transport
  (HP-85's generic STALL error) and `BYT 67D` for file/operation
  failures (system FILE NAME error). These are *provisional mappings*
  pending physical validation; 74 and 67 are existing HP codes,
  **not new custom CP/M-specific error messages**. Scripts can use
  `ON ERROR` and `ERRN` to trap them.
- New zero-argument BASIC numeric function `CPXCODE` exposes the
  more precise last transfer outcome: `0` success, `1` busy/remote
  pre-start cancel, `2` transfer/file failure, `3` transport
  uncertainty / cancellation unconfirmed, `4` BDOS entered and transfer
  still running (normally overwritten with terminal result).
- Existing `CPXSTAT` and `CPABORT` remain explicit human diagnostic
  and recovery commands; their CRT text is intentional. Never use HP
  RESET to assume a remote operation was cancelled.
- The B2d SD filename protocol remains literal, e.g.
  `CPIMPORT "A_STAT.COM","F:STAT.COM"` with Teensy source at
  `/cpm/transfers/A_STAT.COM`. Earlier user found
  `CPIMPORT "NOFILE.COM",...` rejected for 8.3 whereas
  `A:STAT.COM` worked, and confirmed they **had not flashed** the
  ESP32 literal-source firmware. This explains the mismatch; when
  deploying v0.06a use a matched ESP32+Teensy B2d plus ROM v0.06a.

**Acceptance status:** ROM CAPASM + LIF packaging and keyword tests
pass in GitHub CI, ESP32/Teensy latest B2d firmware and native workflows
pass; **v0.06a quiet/error/busy behavior not yet tested on physical
HP-85**. Do not merge to main until user validates it.

Recommended staged physical tests:
1. Back up writable CP/M drive images; flash matched ESP32+Teensy from
   B2d branch, load new CPM85 v0.06a; `CPVER`.
2. `CPM` should show one short line; K8 exits.
3. With CP/M executing Multiplan and F:BUSY.COM confirmed absent,
   `CPIMPORT "A_STAT.COM","F:BUSY.COM"` should report a native
   BASIC error promptly (74 / STALL), and `CPXCODE` should return 1.
   After exiting Multiplan to A>, confirm F:BUSY.COM absent.
4. At A> with a new destination `F:NEWTEST.COM`, successful import
   should return silently, `CPXCODE=0`; reusing its destination should
   produce a BASIC file error (67), `CPXCODE=2`.
5. Test missing `NOFILE.COM` now accepted by the source parser but
   correctly fails as missing SD source (not syntax/8.3 error).
6. Optional `ON ERROR GOTO ...` test to verify scripted suppression.
   Validate the native HP errors before promising specific handling.

---

## 2026-11-10 — Hardware validation and prompt-on-known-busy B2d correction

User confirmed CPM85 v0.05b C/A key detection now works while Multiplan is
running: Abort cancelled the pending import correctly and a later normal
import at the native A> prompt succeeded. The remaining delay before the
prompt (user estimate ~30 seconds) is inappropriate when the Z80 is already
known to be executing a program.

Fix: ESP32 `Cpm85DriveControl::transferReply()` now distinguishes the
existing native `Gate::mode` BUSY or PROBE_RUNNING state from UNKNOWN
before claiming that CP/M is not at CCP. It sends a new **advisory only**
transfer phase `B` ("CP/M RUNNING PROGRAM - RETURN TO A>") when a
running program is known. This is not authorization to run BDOS, and
changes no native safe-point criteria.

Teensy relays that phase as `B|...`. CPM85 **v0.05c** detects the phase
and prints the familiar C=CONTINUE A=ABORT prompt immediately at the
next poll. If Continue is selected it allows an additional bounded
40-poll interval, maintaining the original transfer ID, before another
prompt. UNKNOWN and WAIT_EMPTY_LINE still use bounded polling; active
BDOS still cannot be aborted. A=Abort retains verified remote cancellation.

Hardware user additionally reported `NOFILE.COM` as an import source
being rejected for "incorrect 8.3 naming", while `A:STAT.COM` worked.
This is consistent with the earlier logical drive/FCB source parsing on
an ESP32 firmware older than the literal SD-name change. The current
B2d source code explicitly accepts literal `NOFILE.COM` under
`/cpm/transfers` (then fails **file not found**) and rejects
`A:STAT.COM` as an SD source; the user may be running a mixed build.
**Do not assume that cause is verified.** The next test must deploy
a matched **ESP32 + Teensy + CPM85 v0.05c triplet** and confirm
`CPIMPORT "NOFILE.COM","F:ABRTST.COM"` reaches the
`source_not_found` check when CCP is idle. Keep files absent and
back up any writable CP/M disk before testing writes.

CI: CAPASM/CPMLIF/keyword regression is green for v0.05c; native ESP
and Teensy regressions green. ESP32 firmware (both variants and
static DRAM guard) and latest Teensy firmware build must be confirmed
green before recommending flashing. No main merge approved.

---

## 2026-11-10 — B2d source-argument correction (user-requested)

The **first CPIMPORT argument is a literal SD filename or optional
relative subpath under `/cpm/transfers/`**, and must NOT be rewritten
using a CP/M source-drive prefix. For example:

```basic
CPIMPORT "A_STAT.COM","B:STAT.COM"
```

reads `/cpm/transfers/A_STAT.COM` on Teensy SD and creates
`B:STAT.COM` in CP/M (assuming it does not already exist).
A hypothetical `CPIMPORT "PACK/HELLO.COM","B:HELLO.COM"`
reads `/cpm/transfers/PACK/HELLO.COM`. There is no `A:` on the SD
side. A literal relative slash-separated path is accepted but always
sandboxed below `/cpm/transfers`; absolute paths, parent traversal,
dot-prefixed components, colons, backslashes and control chars are
rejected. No arbitrary SD path access.

The pre-existing B2c **web API** still accepts its historical
`A:STAT.COM` logical source and maps it to `A_STAT.COM`; this
compatibility is intentional and independent of the new BASIC syntax.
CP/M destination names remain strict A:–P: 8.3, CREATE-NEW.
**Use B: for the first writable hardware trial and back up the B:
disk image first**, not system disk A:.

The import change touches ESP32 gate/controller, its pinned CPU SD
transport, Teensy read-only source validator and native/static
regression checks. Coordinated firmware validation must complete
again before flashing these new commits.

---

# B2d — HP-85 blocking CP/M file transfers (implementation candidate)

Updated 2026-11-10. Branch in all repositories:
`feature/cpm-file-transfer-b2d`.

**This is not yet hardware accepted. Do not merge into `main`.**
The original B2c 8192-byte Z80 CPU-task stack and B2b/B2c native BDOS
engines remain unchanged.

## Implemented

- CPM85 v0.05a adds BASIC `CPEXPORT "A:STAT.COM"` and
  `CPIMPORT "A_STAT.COM","B:STAT.COM"`, using native string
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
4. Only after the above: confirm `B:STAT.COM` is ABSENT, backup A.dsk,
   check published Teensy SD `A_STAT.COM` exists, then
   `CPIMPORT "A_STAT.COM","B:STAT.COM"`. Expect 42 records,
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
