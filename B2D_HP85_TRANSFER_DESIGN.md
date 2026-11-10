# B2d — blocking HP-85 BASIC CP/M single-file transfer implementation plan

Status: design checkpoint; **no executable B2d firmware changes yet**.
Branches in all three repos: `feature/cpm-file-transfer-b2d`.
Parents: ESP32 and Teensy from the hardware-accepted B2c branch;
HP85-Option-Roms from the accepted `feature/cpm85-esp-reboot` (CPM85 v0.04d).

## Existing plumbing verified in source

* CPM85 LOADBIN exposes `CPARG` / `CPOPC` / `CPSTATE` /
  `CPRPLY` at 0177743..0177746; parser supports `ONESTR` and
  `TWOSTR`; `CPGO` displays a final NUL-terminated reply. The
  implementation currently has *no* HP-side cooperative C/A input loop.
* Teensy `Cpm85Drive` is an ISR-safe 92-byte request / 768-byte reply
  mailbox; opcode 0 resets local state immediately. This **cannot**
  substitute for canceling a remotely accepted transfer. Existing
  `offer()` retries the same sequence number after 150 ms.
* ESP32 `Cpm85DriveControl::receiveCommand()` currently runs
  synchronously inside the poll path; `catalog()` can wait up to
  12 seconds and sends a *single* final ACK. The cached ACK is keyed
  by 16-bit sequence; the B2d state machine must not reuse this cache
  as its only operation identity.
* `CpmSafePoint::Gate::request()` rejects immediately when the guest
  mode isn't `CCP_INPUT`, and accepts a `WAIT_EMPTY_LINE` only from
  verified native CCP. `cancelPending()` uses CAS to cancel only
  `WAIT_EMPTY_LINE`, never `RUNNING`. `complete()` records
  success/error and sticky partial warnings.
* B2b/B2c transfer engines run only on the existing pinned Z80 CPU task.
  Keep them intact, including the **8192-byte task stack** and current
  63/64-record limits.

## Required sequencing and safety contract

1. `CPEXPORT "A:STAT.COM"` and
   `CPIMPORT "A:STAT.COM","A:COPY.COM"` parse BASIC string
   expressions. The first argument of import names the published Teensy
   SD transfer source, the second a *new* native CP/M destination.
   Preserve the existing strict 8.3, A:–P:, CREATE-NEW and
   CRC/readback checks.
2. Introduce **separate transfer transaction IDs** and status queries
   over HP -> Teensy -> ESP32. The ISR only copies ASCII, opcode and
   status bytes; ESP32 owns gate/BDOS, Teensy owns SD transport and
   mailbox delivery. A repeat of the same ID may query existing
   status but must never rearm the gate or replay CREATE-NEW.
3. A request while CP/M is executing a program remains **not started**;
   the controller should report `waiting_for_ccp` and attempt a new
   `Gate::request()` only while the transaction is still authorized.
   Once accepted at `WAIT_EMPTY_LINE`, do not enqueue it again on
   Continue. Distinguish this from `RUNNING` BDOS.
4. After approximately 10 s without an actual safe-point start,
   HP BASIC shows `CP/M NOT IDLE - RETURN TO A> PROMPT` and offers
   `C=CONTINUE A=ABORT` using **native HP CRT/keyboard**, not the
   captured K8 console or a synthetic CP/M keyboard character.
   Continue extends the wait **for the same transaction**.
5. Abort is a synchronous handshake: ESP32 atomically transitions
   `WAIT_EMPTY_LINE` to cancelled, or retires the not-yet-accepted
   request. It must return **cancelled** only after the operation is
   guaranteed never to start. If the gate has already reached
   `RUNNING`, report **already_started** and block for completion
   instead; never interrupt active BDOS writes.
6. Do not use Teensy opcode 0 alone as abort, since it forgets the
   mailbox but does not cancel a pending ESP32 gate. Lost acknowledgments
   must not turn cancellation into a queued future write.
7. Transfer outcome is terminal, including error/source reason,
   number of 128-byte records, bytes and CRC16-CCITT-FALSE. If MAKE
   may have modified a destination, display a prominent **possible
   partial file** warning. Never auto-delete or overwrite.
8. Once `RUNNING`, no C/A cancel. For stalled active BDOS, use
   bounded operator-visible status/warnings without resetting the
   Z80 or abandoning a still-live operation. Do not claim that
   returning to BASIC is safe until completion is confirmed.
9. Preserve `CPM`, K8 detach/re-entry, `CPRESET`, `CPREBOOT`,
   and existing CPDCAT. Check new keyword spellings against the
   HP BASIC shortest-unique-match mechanism.

## Implementation order

1. Build a small ESP32 transfer-controller state machine with
   generation/identity, explicit pending/running/terminal transitions,
   and native race tests (busy CCP, accepted gate, cancel vs start,
   duplicate requests, pre-existing target, terminal status).
2. Wire that controller through the ESP32 `cpm_keys` poll protocol
   without holding the UART poll worker in a multi-second blocking
   loop; retain existing disk traffic and deduplication.
3. Add Teensy mailbox request/status/cancel verbs and native tests,
   keeping all ISR callbacks wait-free. Do not reuse opcode 0 for a
   remote cancellation acknowledgment.
4. Extend CPM85 parse/runtime table and BASIC UI with a cooperative
   wait and C/A keyboard prompt. Assemble and inspect the LOADBIN
   artifact before hardware use; preserve 0.04d recovery file.
5. Run ESP32 native regressions and both PIO targets, Teensy native
   suite and teensy41 build, HP ROM assembler/size/parser tests. Flash
   only a *coordinated* matched triplet after green gates and backups.
6. Initial hardware validation on a disposable destination using
   existing `A:STAT.COM` and a **new** destination filename; first
   exercise idle/wait/abort with **no writes**. Only then test
   42-record copy and readback. No overwrite and no arbitrary length.

## Explicit deployment gate

**Do not build/flash B2d yet.** This checkpoint only creates new
feature branches and describes the protocol. The validated B2c
firmware remains the physical recovery baseline. No merges into
`main` without user approval.
