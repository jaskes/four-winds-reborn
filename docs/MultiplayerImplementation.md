# Multiplayer implementation

The accepted local feature set is frozen at `v0.5.0`, commit
`88147fce6ac553e8dd68a66ff35a4480aa29f8ee`, on `main`. The tag is pushed;
GitHub Release publication is deliberately deferred. Development continues on
`develop`. Never move the tag to include multiplayer work.

## Completion contract

Working multiplayer means a complete match between independent clients, through
Rune Game, island movement, battles and the same final scores. A connection or a
shared board alone does not meet this contract. First validate two-player Duel
over LAN, then private Internet rooms and FFA/Coalition with up to four people
and AI-filled seats. Windows and Android must use the same protocol.

The host owns commands, RNG, deck, AI, rules, phase transitions and results.
Clients receive a view filtered for their assigned avatar. Opponent hands,
future draws and RNG state must never enter another player's transcript.
Commands bind to the authenticated connection, carry an ordered sequence and
expected state revision, and cannot execute twice after an acknowledgement is
lost. A disconnected seat pauses progress and can reconnect to its pending
choice. Summary pages use an explicit readiness barrier.

## Work in progress

- [x] Run the source freeze gate: 52 matches and 14 identical CSV contracts.
- [x] Push `main` and annotated `v0.5.0`; disable publication on tag pushes.
- [x] Create a Codex goal and active continuation heartbeat (30 minutes).
- [x] Native bounded nonblocking TCP transport and strict JSON decoder.
- [x] One authority driver, multiplayer discard claims and phase barriers.
- [x] Recipient views, private prompts and presentation-state hydration.
- [x] Session protocol, admission, exact-once commands and in-memory reconnect.
- [x] Host/join lobby and existing gameplay UI integration.
- [x] Independent-process complete Duel and separate lost-ack reconnect tests.
- [x] FFA/Coalition, AI seats and complete team results over the network.
- [x] Direct private-room path with documented security and hosting model.
- [x] Installable Windows/Android review builds and a concrete play guide.
- [x] Offline regression gate.
- [ ] Cross-platform CI on the final multiplayer code.
- [ ] Android lifecycle on a physical device and a separate-network Internet match.

September 6 evidence: independent Quick Duel processes completed all six phases
with identical final categories, team standings and revision. A second complete
Duel exercises real summons, invasion moves, attacker-only battle choices and
shared battle results. Coalition with two humans and two AI also completed.
The full TLS matrix passes five cases: Duel, combat Duel, four-human FFA,
four-human Coalition, and Coalition with two humans/two AI. Readiness starvation
and a late summary acknowledgement that could cancel the next island command
are fixed and covered by focused regressions.
The sixth process case cuts the encrypted stream during an active Rune hand,
requires an automatic reconnect, then verifies the same final scores/revision.
The final 12 offline tests pass; the full cohort gate in
`diagnostics/multiplayer-gate-20260906` passes 52 matches and 14 byte-identical
CSV contracts. Russian Reborn UI rendering also passes after final layout fixes.
Authority coverage additionally completes Classic Duel through East, South,
West and North: eight Rune deals, eight island phases, all human summary
barriers and final standings, without starting an extra deal.

Recipient-view tests cover private hands, Scry, invisible creatures, final unit
scores and atomic malformed-input rejection. Session tests cover concurrent
claims, repeated ready votes, delayed presentation, ordered command queues,
lost acknowledgements and token reconnect without double execution, wrong
actors/forced actions, cross-phase acknowledgements and stale state packets.
The graphical test uses real TLS connections and game buttons; clipboard,
wrong-secret retry and full lobby rendering are included.

TLS 1.3 PSK plus ephemeral key exchange uses a pinned Mbed TLS 3.6.7 source
archive and 128-bit random invitations. Security tests cover wrong secrets,
encrypted capture, tampering/replay, partial I/O, bounded queues and deadlines.
Windows tests and Android API23 compilation pass. The Android arm64 APK builds,
passes ABI/package/signature verification and lint. No Android device was
attached to ADB during this run, so physical lifecycle acceptance remains open.

The first cross-platform CI run passed Windows Debug/Release, Linux Debug/Release
and Android. All six full network matches also passed on macOS. Two focused
macOS checks exposed a test that failed to consume the automatic first draw and
a real ciphertext-queue overflow when sender and receiver progress differed.
The fixture now waits for an actual human choice and exact acknowledgements;
TLS framing now pauses bounded reads until queue space is available. Both
failures were reproduced locally before their fixes. The corrected CI run is
required before closing the remaining CI item.

The next run passed every job except the macOS Release command FIFO check.
Splitting acknowledgement and state delivery reproduced a real client race:
the next command could use the previous revision before its resulting view
arrived. Protocol 2 now identifies the request covered by each state and keeps
dispatch behind that state's UI consumption. Focused tests explicitly delay
the state, cover no-op acknowledgements and same-revision rejection resumes,
and reconnect with a fresh resume arriving before the cached acknowledgement.
Reconnect welcome also discards views queued on the previous connection.
The full local Windows suite passes 25/25, including all six independent-process
network matches, after this correction. The updated cross-platform CI remains
the final automated gate. All devices must use the protocol-2 build together.

CI for gameplay commit `07f442b` passed Windows, Linux, Android and all six
network matches on both macOS configurations. Later protocol assertions exposed
fixture ordering assumptions: a queued command could legitimately be sent before
the test delivered a changed turn, and an unacknowledged final-discard view could
block the test's delayed request. The fixtures now use ordered ping/pong fences
and explicit presentation consumption, including the remaining host and client
negative assertions. Both failing cases were reproduced locally first. The
twenty-case offline matrix also exceeded its aggregate Debug deadline while
continuing to make progress; CTest now runs those same seeds and replay/save
assertions as twenty isolated, individually timed cases with flushed progress.
The affected local checks pass 21/21, with another five consecutive protocol
runs passing and an invalid matrix selector rejected explicitly. These
follow-ups change tests only; the protocol-2 review binaries remain valid.

CI `16b82c9` then passed the protocol, all twenty matrix cases and all network
matches on every platform. The remaining macOS Debug transport failure was
reproduced by delivering the first byte after the fixture's initial empty poll.
A read-only partial-frame byte count now lets header/payload deadline tests
start measuring after confirmed receipt; transport deadlines are unchanged.
Windows Release again reported DbgHelp error `0x800706F8`. Its fixture now
causes an actual write to a protected page and validates the dump's exception,
register context and stack, including rejection of a corrupted stream directory.
The former synthetic AV supplied none of the operation/address fields described
in the [Windows exception-record contract](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record).
Ten local real-AV captures pass. This does not establish the cause of the runner's
DbgHelp error; the production reporter remains unchanged, and CI now retains
its test-generated dump files if further investigation is needed. The focused
TCP, TLS and native-crash checks pass 3/3. An isolated transport mutation that
incorrectly refreshes the header timer is rejected by the updated deadline test.

The direct Internet route uses a reachable host IPv4/port or shared VPN; there
is no public relay or matchmaking service. See the
[play guide](MultiplayerPlayGuide.md) for exact setup and in-memory reconnect
limits. Cross-platform CI, final matrix and package evidence must be recorded
before declaring the goal complete. A two-device Android lifecycle test and an
actual route between separate Internet networks remain manual acceptance items.

## Review builds

Local review artifacts are built from `develop`. The exact gameplay commit and
SHA256 values are recorded in `dist/multiplayer-review-builds.json`; Windows
also includes a sibling `build-info.json`. Workflow/test/documentation-only
follow-ups do not change that game code.

- Windows: `dist/windows-v0.6.0-dev-network/four-winds-reborn.exe` (keep its
  sibling DLLs and `themes` directory).
- Android: `dist/android/four-winds-reborn-v0.6.0-dev-network-android-arm64-debug.apk`.

The accepted v0.5.0 local artifacts remain in their original locations. No
GitHub Release or multiplayer release tag has been created.

The network modules live under `src/network`. Transport handles bytes only;
all GameData access stays on the SDL/UI thread. Scene-level polling continues
while modal dialogs or summary pages are open. A view and its events travel in
one revision so a client cannot animate an event against unrelated state.

Automatic continuation must first inspect this plan, the active goal and git
state. Resume incomplete work; do not spawn competing implementations or repeat
the source freeze. Disable the heartbeat and complete the goal only after the
completion contract has evidence. The implementation is a playable development
candidate; the remaining checks above must not be silently treated as passed.
