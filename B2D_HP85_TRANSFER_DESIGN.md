## 2026-11-11 — User-confirmed v0.06d physical HP-85 export/import success

After rebuilding and loading CPM85 **v0.06d** with the previously installed
matched B2d ESP32 and Teensy firmware, user reports **both transfer directions
work as designed**:

1. `CPEXPORT "A:PIP.COM","PIPB2D2.COM"` created
   `/cpm/transfers/PIPB2D2.COM` on the Teensy SD card; user verified the
   exported file exists on SD.
2. `CPIMPORT "PIPB2D2.COM","F:PIPNEW.COM"` created the imported file
   on CP/M's F: drive; user verified the file exists there.

This validates on real hardware the optional CPEXPORT SD destination,
literal import source naming, and the v0.06d **fully drain every Teensy
mailbox reply through NUL** correction. The prior universal Error 74 /
CPXCODE 3 regression was removed in this test.

Prior hardware regressions include a MODEM.COM export/import round trip
followed by successful execution (earlier B2d build), import rejection on
read-only B:, and no overwrite of existing F: files.

**Not explicitly reverified in this v0.06d user report:** byte-for-byte CRC
of PIPB2D2.COM, actual execution of its reimported copy, CPXCODE numeric
values, busy Multiplan handling, ON ERROR trapping, and an overwrite attempt
with the new optional CPEXPORT SD name. Preserve these as follow-up
acceptance checks rather than falsely marking them passed. Do not merge
main without authorization.

---

## 2026-11-11 — HARDWARE BREAKAGE ROOT CAUSE: v0.06d mailbox reply drain

After matched v0.06c firmware deploy, user reported BOTH CPIMPORT and
CPEXPORT always failing with **ERROR 74: STALL** and **CPXCODE=3**
(transport/uncertain). This implicated the shared transfer control path,
not filename validation or CP/M disk semantics.

**Root cause confirmed by source inspection:** The v0.06a "quiet BASIC"
rewrite dropped a critical operation present in the earlier v0.05b
blocking implementation. Every Teensy mailbox response has two prefix
bytes (`W|`, `P|`, `S|`, etc.) followed by text and terminal NUL. The
Teensy `onReply()` only returns mailbox state from 2/3 to IDLE when
the HP reads **through NUL**. The quiet ROM `CXQRY` read only the
two-byte prefix and then immediately issued a new STATUS or CANCEL.
Teensy `onOpcode` deliberately ignores ANY new opcode while
`state!=0`. The HP then sampled stale text bytes as a phase header,
obtained `U`, and ultimately raised ERROR 74 / CPXCODE 3. This also
explains why repeated attempts could leave confusing transfer ownership.

**ROM-only correction CPM85 v0.06d:**
- New `CXQST` subroutine calls `CXQRY` then `CXDRN` to consume the
  *entire* Teensy reply including the final NUL and release the
  mailbox before another opcode.
- All three silent transfer calls (START, STATUS, CANCEL) use `CXQST`.
- `CPXSTAT` and explicit `CPABORT` continue using `CXQRY` and
  `CPREAD`, which consume the remaining text for diagnostics.
- The phase byte remains saved at `CPXPH` independently of draining.
- Native Teensy regression reproduces the two-byte-prefix-only reply
  lock and verifies consuming the final NUL releases the mailbox.
- No ESP32 or Teensy firmware changes are required for this **fix**.
  Both are already at matched B2d v0.06c protocol; new optional
  two-argument CPEXPORT remains unverified on hardware.

**CI**: HP ROM v0.06d assembled, keyword tests passed and CAPLIF packaged
successfully (run 42928499659). Teensy native regression was added at
8cb880cca5f8915c826f0bf8f5d39d7e585d86b1 and CI verification pending
at the time of note.

**Next physical trial**:
1. Keep installed B2d ESP32/Teensy firmware; pull and rebuild *only*
   HP85-Option-Roms feature branch; verify `CPVER` displays 0.06d.
2. Confirm CP/M is really idle at A>, and no previous transfer is active
   (`CPXSTAT` should show terminal/none; `CPABORT` if pending).
3. Use new non-existing SD target:
   `CPEXPORT "A:PIP.COM","PIPB2D2.COM"`.
   Expect silent success, `CPXCODE=0`, file present on SD.
4. Import `CPIMPORT "PIPB2D2.COM","F:PIPNEW.COM"` only when F:
   destination absent. Expect silent success, `CPXCODE=0`.
5. If failure persists, capture exact error, `CPXCODE` and
   full `CPXSTAT` output *before retry*. The root-cause logic is
   proven by inspection/CI but still requires physical verification.

No merge to main before user hardware acceptance.

---

## 2026-11-11 — Additional exact-ID recovery guard (after v0.06c implementation)

Further protocol review showed that a Teensy transaction retained across a
lost HP/ESP acknowledgement can outlive the ESP32's transaction table after
an ESP reboot. Subsequent START requests are intentionally blocked by
`transferLocked`; v0.06b would misreport that local block as FILE NAME
(Error 67). v0.06c maps E to STALL (Error 74) and CPXCODE 3.
The Teensy now also clears ownership on STATUS/CANCEL E **only if**
ESP32 explicitly replies exactly `UNKNOWN TRANSFER TRANSACTION`
for the original ID. Generic E replies and UART timeouts still never
unlock an active operation. The invariant that BDOS must not be
cancelled after entry is unchanged.

Latest functional CI before documentation:
- HP ROM v0.06c: run 42925567861 success; latest doc CI 42925786986 success.
- ESP32 latest code and static regression: run 42925401565 success;
  doc-only CI 42925793066 also succeeded, including both ESP32 targets
  and native checks.
- Teensy export rename code run 42925439056 success; latest orphan-ID
  change run 42926065631 native passed; firmware was still running
  at last inspection; confirm before coordinated hardware flash.

The next physical test MUST first inspect a failure with `CPXSTAT`.
`CPXCODE=2` means the ESP32 reported terminal F (real file/staging/
BDOS failure); `CPXCODE=3` means E/U transport or ownership uncertainty.
`CPEXPORT "A:PIP.COM"` can fail legitimately if default SD file
`/cpm/transfers/A_PIP.COM` already exists; use a genuinely new explicit
target like `"PIPB2D.COM"` to eliminate this confounder.

The optional second CPEXPORT string is implemented but **not yet
verified on physical HP-85**. It uses an optional-comma HP parser with
stack arity checked against TOS. Validate one- and two-argument forms
before declaring it accepted. No main merge.

---

## 2026-11-11 — Physical v0.06b both-direction errors and optional CPEXPORT SD rename (v0.06c)

User reported that **neither** literal `CPIMPORT "A_STAT.COM"` nor the
historical `"A:STAT.COM"` worked (both failed with HP BASIC Error 67),
and `CPEXPORT "A:PIP.COM"` also produced Error 67. This invalidates any
claim that the regression was solely the literal source naming or the
v0.06a transient CCP busy phase. The source inspection confirms
`CpmFileReadService::parseName` is still used for **all** CP/M
drive-qualified 8.3 operands; SD literal validation is a separate step.
A file error can also mean SD destination already exists or other BDOS
or staging failure; current HP BASIC code intentionally maps all F
failures to 67. Further hardware diagnostics are needed to determine
the actual status and cause.

The first STALL followed by immediate FILE NAME errors is also
consistent with a **separate unresolved transfer**: the Teensy protects
an uncertain old BDOS transaction by rejecting the next START with a
wire `E` reply and text `PREVIOUS TRANSFER UNRESOLVED`. v0.06b
incorrectly mapped all wire E replies to Error 67 FILE NAME. **v0.06c**
maps E to BASIC Error 74 (transport/ownership), CPXCODE 3, leaving
actual terminal `F` file/service failures as Error 67, CPXCODE 2.
Use `CPXSTAT` / `CPABORT` to inspect/recover after uncertain failure;
do not defeat the ownership interlock.

User also requested optional explicit SD destination for CPEXPORT:
- `CPEXPORT "A:PIP.COM"` preserves default `/cpm/transfers/A_PIP.COM`.
- `CPEXPORT "A:PIP.COM","PIPBACK.COM"` publishes the same CP/M
  source to literal `/cpm/transfers/PIPBACK.COM`.
- Relative subdirectories such as `UTILS/PIP.COM` allowed under
  `/cpm/transfers` if directory exists; no arbitrary SD access.
- CP/M name parser remains strict drive-qualified 8.3, separate
  from literal SD filename path validator. Neither existing destination
  nor existing `.part` file can be overwritten.

Implementation v0.06c:
- HP85 assembler custom `OPTEXP` parser checks optional comma with
  `CMB R14,=54`, uses `STREX+` and optionally GETCMA/STREXP.
  At runtime checks R12 against HP85 TOS 101132 after subtracting
  the 4-byte source string stack frame; emits the original CP/M source
  or `source|literalSdFilename`.
- ESP32 starts the operation with distinct CP/M FCB and optional
  SD name, stores optional leaf in the existing 72-byte heap transfer
  struct, and publishes it through the existing shared gate literal
  slot, avoiding any new static TFT DRAM usage.
- Existing B2b export RPC now has optional `sd_name` JSON field
  sent only when an explicit destination is provided.
- Teensy `Cpm85SdExport::begin` validates relative SD name,
  stages exclusive `.part` next to destination even in subdirs,
  verifies total CRC via SD readback, and atomically publishes only
  if destination absent; original one-arg B2b paths unchanged.

**Validation:** ROM CAPASM, static keyword/parsing tests, and LIF package
passed GitHub Actions run 42925567861. Teensy firmware and native tests
passed run 42925439056; ESP32 native tests passed run 42925401565;
the latest ESP32 firmware/DRAM job was still in progress when this
note was drafted. Hardware has **not** yet verified either the actual
cause of previous Error 67 or new two-argument syntax.

Next test: matched ESP32 + Teensy + CPM85 v0.06c.
First, with CP/M idle, execute `CPXSTAT` and if unresolved use
`CPABORT` and requery. Do not proceed until no active transfer.
Use fresh names which do NOT already exist:
`CPIMPORT "A_STAT.COM","F:ISAFE.COM"`;
`CPEXPORT "A:PIP.COM","PIPB2D.COM"` (verify absent on SD).
On error report exact statement, BASIC error, `CPXCODE`,
`CPXSTAT` (including server reason). Do not overwrite any .DSK or
CP/M system file; keep B2c backups. Do not merge main yet.

---

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
