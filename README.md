# QuackShot Switch Remake — experimental engine

An **unofficial, asset-free** technical prototype inspired by the observable
mechanics of *QuackShot Starring Donald Duck* (Mega Drive/Genesis, 1991).

> This is **not yet** a port, full remake, or playable copy of the original game.
> Its three test areas, geometry and timings are placeholder designs.
> Original ROM, levels, animation and music are not part of this repository.

## Status

- Portable C99 simulation (60 fixed steps/second, deterministic for equal inputs).
- Test collision, jumps, pits, camera, basic enemy behavior, yellow and red plungers.
- SDL2 window/gamepad demonstration using **only geometry**.
- CTest suites checked with GCC and Clang in Debug and Release, including exact CITY TEST timing and numeric-safety regressions.
- **Optional city-reference physics envelope**, fitted to numeric observations from privately emulated original Mega Drive gameplay. Default prototype movement is unchanged.
- Nintendo Switch homebrew Makefile; NRO0/ASET/NACP cross-build and artifact verified in CI (not runtime-tested on a console).
- Bounded asset-free SDL2 boot/first-frame/controller/heartbeat diagnostics on Switch SD, with host-side integration tests.
- Independent original-ROM metadata inspector lives in PR #1.

## Experimental gameplay addition: city checkpoint (stage 58)

The opt-in **CITY TEST** now has an **original-free blue/gold flag at
world x=640**. Reach the flag to enable a mid-level restart after
a pit or fatal enemy hit, with three hearts, brief immunity and the
camera re-centered at the checkpoint. Before touching it, retries
still start at x=40. Returning to the map clears the flag for the
next attempt; the default prototype is unchanged.

This is a self-authored convenience mechanic, **not** a claim about
the original 1991 QuackShot. See
[checkpoint gameplay and tests](docs/REFERENCE_CITY_CHECKPOINT_GAMEPLAY_RU.md).

Stage 59 also shows a cyan/gold checkpoint indicator in the CITY TEST
HUD at all camera positions, including when the world marker has
scrolled away. A genuine retry keeps the indicator gold; leaving and
reentering restores cyan. SDL2 raster smoke tests verify both colors,
the distant-camera state, and isolation from the regular prototype.
See [checkpoint HUD proof](docs/REFERENCE_CITY_CHECKPOINT_HUD_RU.md).

Stage 60 adds two **synthetic optional gold bonus beacons** at x=900
and x=1040 in the opt-in CITY TEST. Each is worth 500 practice points,
and a corresponding permanent-in-attempt HUD stamp lights up when
collected. A fatal hit or pit retry keeps the stamps but cannot award
a token during the failed frame. Returning to the map resets both
beacons for a fresh attempt. The default prototype remains untouched.
See [bonus beacon gameplay](docs/REFERENCE_CITY_BONUS_BEACONS_RU.md).

Stage 61 adds an optional one-heart recovery **only when the second
bonus beacon is first collected**. The reward never exceeds three
hearts and never repeats after later damage; a green HUD stamp
indicates the set has already been completed. Dead/pit retry frames
cannot claim it. See
[bonus set health reward](docs/REFERENCE_CITY_BONUS_SET_HEAL_RU.md).

Stage 62 adds a **four-digit 0000 / 0500 / 1000 practice score**
on the experimental city HUD. It is derived directly from the two
collected-token bits and therefore remains correct through retries;
a new attempt resets to 0000. The seven-segment display is drawn
with original-free procedural geometry. A dedicated SDL2 regression
checks every pixel of the full 29×7 raster against an independent
digit oracle. See [decimal bonus score](docs/REFERENCE_CITY_BONUS_SCORE_RU.md).

Stage 63 adds a **cyan four-digit session personal best** beside the
current attempt's gold score. New attempts reset current points to
0000 but keep the best 0500 or 1000 until a completely new session
begins. A later weaker attempt cannot lower the record. Exact SDL2
digit-raster tests check both counters and normal prototype isolation.
See [session personal best](docs/REFERENCE_CITY_PERSONAL_BEST_RU.md).

Stages 64–65 also show that record directly on the CITY TEST map
row. It remains visible after visiting normal RUINS/ROOFTOP stages,
even when they switch the active physics profile back to the base
prototype. A dedicated CTest and SDL route regression cover the
cross-stage transition. See
[map record visibility](docs/REFERENCE_CITY_CROSS_STAGE_BEST_RU.md).

Stage 67 introduces an optional **CITY TEST time trial**. A green
four-digit HUD timer counts simulated frames only while the research
level is active; retries remain part of the same attempt. Completing
the level records the fastest clear of the session, visible on the
CITY map row. Back without a clear never awards a time, and ordinary
prototype stages remain unchanged. See
[time-trial gameplay and tests](docs/REFERENCE_CITY_TIME_TRIAL_RU.md).

Stage 69 adds an **optional checkpoint split** to the same time trial.
The first safe contact with the CITY TEST flag freezes an amber
per-attempt split; the fastest split in the game session remains visible
on the map. Fatal contact cannot earn a split, retries keep the current
one, and the ordinary prototype is unaffected. See
[checkpoint split verification](docs/REFERENCE_CITY_CHECKPOINT_SPLIT_RU.md).

Stage 70 adds **live split pace feedback** against the previous
session record. A green negative delta means the current checkpoint
was reached faster, red positive means slower, and cyan zero is a
tie. The first split in a fresh session has no comparison. The
opt-in CITY TEST HUD uses self-drawn digits and the regular prototype
is unchanged. See
[checkpoint pace verification](docs/REFERENCE_CITY_CHECKPOINT_PACE_RU.md).

Stage 71 adds an **instant CITY TEST practice retry**: on Switch,
press **R + Start** (desktop: Left Shift + Escape) to restart at the
beginning without leaving the level. Attempt-local checkpoints,
bonuses, split deltas and time reset; earned session bests,
unlocked ammo and clears remain intact. Ordinary Start still
returns to the stage map, and legacy gameplay is untouched.
See [quick retry behavior](docs/REFERENCE_CITY_QUICK_RETRY_RU.md).

Stage 72 introduces a small **procedurally drawn breakable obstacle**
between the two CITY TEST bonus beacons. The crate blocks ordinary
movement but may be jumped over or destroyed by a yellow/red shot.
Its destruction remains for the current life-retry sequence and the
object returns on a new attempt. Neither other stages nor the
ordinary prototype is changed. See
[breakable crate gameplay](docs/REFERENCE_CITY_BREAKABLE_CRATE_RU.md).

Stage 74 adds **18 simulation ticks of procedural debris** when that
crate is destroyed. Three non-colliding pixel fragments briefly move
outward, disappear completely, and reset with the next attempt.
The original-free animation is checked by native C replay tests and
exact SDL2 ARGB8888 pixel probes. See
[crate debris feedback](docs/REFERENCE_CITY_CRATE_DEBRIS_RU.md).

Stage 75 makes crate destruction **occlusion-aware**: a closer
solid tile or enemy stops the shot before it can destroy the crate,
while a direct yellow/red hit still shatters it. The synthetic
projectile timeline retains its deterministic ordering for multiple
shots, and a dedicated CTest plus SDL ARGB8888 scene checks
wall/foe/box priority. See
[crate impact priority](docs/REFERENCE_CITY_CRATE_PRIORITY_RU.md).

Stage 76 adds a **CITY TEST training pause**: hold Nintendo
**X + Start** (desktop **K + Escape**) to stop/resume the
synthetic level without advancing gameplay, camera, projectiles
or time-trial clocks. Bare Start still returns to the map;
R + Start restarts the attempt even while paused. The pause
overlay is drawn using original-free geometry and verified
using actual SDL2 frame comparisons. See
[training pause behavior](docs/REFERENCE_CITY_TRAINING_PAUSE_RU.md).

Stage 77 clarifies that **X + Start** is a dedicated pause gesture,
not a replacement for existing Back+Fire/Confirm/Jump safety chords.
Those mixed inputs still exit to the map, protecting old transition
and corrupted-state behavior. GCC/Clang and SDL regressions explicitly
enforce the distinction. See
[pause chord arbitration](docs/REFERENCE_CITY_PAUSE_CHORD_ARBITRATION_RU.md).

Stage 78 adds **a second-sector time trial**: after reaching the
CITY TEST checkpoint, a lavender HUD counter shows the elapsed
post-checkpoint frames. Completing the level after the flag records
the fastest second-half time independently of full-run and
checkpoint-split PBs. Abandoning, quick retries, and legacy stages
cannot fabricate sector records. See
[second-sector timing](docs/REFERENCE_CITY_SECOND_SECTOR_RU.md).

Stage 79 adds a compact **sector personal-best frame budget bar**.
After a real completed second-sector record exists, green indicates
frames still remaining, cyan indicates a tie, and coral indicates
the elapsed second sector has surpassed that record. The budget
is calculated without extra mutable state; pause freezes it and
retries clear the live indicator. See
[second-sector budget feedback](docs/REFERENCE_CITY_SECTOR_BUDGET_RU.md).

Stage 80 adds a **procedural spring pad** on the final CITY TEST
ground segment. Landing triggers a fixed bounce and a brief cyan
flash. It integrates with jump-hold physics, time-trial pause and
quick retry, while the ordinary prototype stays unaffected. See
[spring pad gameplay and tests](docs/REFERENCE_CITY_SPRING_PAD_RU.md).

Stage 84 adds a **one-way sky ledge** near the exit of CITY TEST.
The ledge catches descending landings, allows rising passage from below,
and uses the uppermost crossed surface when red-shot anchors are also
present. No standard prototype physics are changed. See
[sky ledge checks](docs/REFERENCE_CITY_SKY_LEDGE_RU.md).

Stage 85 adds a **controller-input final-route regression**: spring,
airborne heart, one-way ledge and completion all occur in order,
with no character teleport after final-segment fixture setup. Both
walking and jumping finishes are checked, along with pause timing,
early Back exit and opt-in isolation. See
[final segment route checks](docs/REFERENCE_CITY_FINAL_ROUTE_RU.md).

Stage 86 adds a **real spawn-to-checkpoint movement test** starting at
x=40 with all authored city enemies, pickups, pit and walls intact.
A deterministic gamepad-input script runs and jumps to the first city
checkpoint; it checks respawn, replay and voluntary map exit. See
[spawn-to-checkpoint checks](docs/REFERENCE_CITY_SPAWN_CHECKPOINT_RU.md).

Stage 87 joins both city halves with an **uninterrupted spawn-to-exit
regression** using actual QsInput commands: pit, roof, checkpoint,
shootable box, spring, airborne heart, one-way ledge, and stage clear.
It also checks pause and re-entry. See
[full city route checks](docs/REFERENCE_CITY_FULL_ROUTE_RU.md).

Stage 81 adds a **single-use airborne heart** above the spring pad.
The fixed spring launch reaches it without steering; the ordinary city
jump from the floor does not. Collecting it heals at most one heart;
collection persists through life respawns but resets on a fresh attempt.
It does not change the two-beacon score or the legacy prototype. See
[airborne heart gameplay and checks](docs/REFERENCE_CITY_AIR_REWARD_RU.md).

Stage 82 adds a **short collection sparkle** for the airborne heart,
plus regression coverage for enemy-contact ordering, stun, checkpoint
respawn, camera phase and pause. Its visual timer is not a hitbox and
cannot change movement or score. See
[airborne heart interaction checks](docs/REFERENCE_CITY_AIR_REWARD_INTERACTIONS_RU.md).

Stage 83 adds a **persistent airborne-heart HUD marker** at the far
right of the city-only status strip. It is grey until collection and
mint after collection, independent of the world camera or short sparkle.
The status survives a life respawn and pause, resets on practice
restart, and does not appear in the normal prototype. The SDL2 smoke
suite samples the actual pixels. See
[airborne-heart HUD checks](docs/REFERENCE_CITY_AIR_REWARD_HUD_RU.md).

## Recent CITY TEST safeguards (stages 50–55)

The opt-in research stage now checks the order of damage, item awards and
respawns, limits damaged immunity/stun timers and player state, retires bad
red anchors before movement, verifies one-tick projectile color latching,
and recovers invalid map selections/completion flags before navigation.

These remain **synthetic mechanics**, not recovered original game code.
See the individual validation notes:
[frame order](docs/REFERENCE_CITY_FRAME_ARBITRATION_RU.md),
[immunity/stun](docs/REFERENCE_CITY_IMMUNITY_TIMER_BOUNDS_RU.md),
[hero status](docs/REFERENCE_CITY_HERO_STATUS_INTEGRITY_RU.md),
[pre-move shot guards](docs/REFERENCE_CITY_PRE_MOVE_SHOT_GUARD_RU.md),
[weapon queue](docs/REFERENCE_CITY_WEAPON_QUEUE_INTEGRITY_RU.md), and
[map integrity](docs/REFERENCE_CITY_MAP_INTEGRITY_RU.md).
Do not treat uncompleted GitHub Actions as verified; the status of a
given revision is defined by its commit-specific workflow checks.

## Portable tests

    cmake -S . -B build-desktop -DQS_BUILD_DESKTOP=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build-desktop --parallel
    ctest --test-dir build-desktop --output-on-failure

For the SDL2 viewer, install SDL2 development headers and run:

    cmake -S . -B build-desktop -DQS_BUILD_DESKTOP=ON
    cmake --build build-desktop --parallel
    ./build-desktop/quackshot-prototype

Keyboard: arrows or A/D, Space (jump), J (fire), Shift (run),
K (plunger), Enter (select), Esc (back), Q (quit).
Gamepad: directional stick/D-pad, B, Y, R, X, A, Plus.

**Experimental reference-city mode (stage 0 only):** on the map hold
**R + A** simultaneously (desktop: Shift + Enter) to enter CITY TEST with
a fitted walk acceleration, ~65-pixel jump and one-step delayed yellow
plunger. Ordinary A entry uses the original prototype profile. Other
test stages do not use the fitted values. The model is *not* verified
original QuackShot CPU code or precise original collision geometry.
See [calibration methodology](docs/REFERENCE_CITY_PHYSICS_RU.md).
Stage 20 also keeps a separately latched one-simulation-tick presentation camera
in the optional city mode. This is a testable timing **hypothesis**, not yet
a confirmed recreation of the Mega Drive VDP buffer schedule. See
[city camera render timing](docs/REFERENCE_CITY_CAMERA_PHASE_RU.md).
Stage 21 additionally requires two authentic 24-tick NRO camera bursts,
verified against an independent host-side consecutive-tick oracle with
negative mutation tests. See [camera-timing protocol](docs/CAMERA_TIMING_BURSTS_RU.md).
The stage-32 CI rerun also refreshes the pinned NeXium nightly SHA-256
using the official GitHub Releases asset digest (2026-10-18). The
download remains fail-closed: future nightly changes require a new
independently verified pin. See [NeXium provenance](docs/NEXIUM_PIN_2026_10_18_RU.md).
Stage 46 extends CITY TEST numeric validation to enemy positions
and patrol bounds before any projectile collision or SDL render:
NaN/Infinity, extreme finite coordinates and completely off-world
actors are discarded and their data canonicalized. Valid edge
patrols remain unchanged; the ordinary prototype is unaffected.
A new regression group runs in all four GCC/Clang configurations,
and SDL2 checks that the corrupted actor vanishes while a valid
stunned enemy still appears. See
[city enemy numeric guard](docs/REFERENCE_CITY_ENEMY_NUMERIC_RU.md).
Stage 45 replaces floating-point fractions for projectile event
ordering with exact rational comparisons using bounded 32-bit integer
cross-products. Same-instant impacts stay simultaneous; genuinely
later shots pass earlier-stunned enemies, even when their timestamps
differ by less than 0.00001 of a frame. GitHub Actions compares an
independent collision-outcome digest across GCC/Clang in Debug/Release.
See [rational timeline validation](docs/REFERENCE_CITY_RATIONAL_TIMELINE_RU.md).
Stage 44 bounds the CITY TEST projectile scheduler: reject NaN,
infinite or out-of-world coordinates, unsafe lifetimes, and any
velocity exceeding the 1280-pixel synthetic world width per frame.
The supported endpoint of 1280 px/tick and all eight active shot
slots remain functional, with at most 10,240 swept substeps per tick.
The regular prototype's historic shot logic remains unchanged.
Native tests plus an actual SDL two-lane eight-shot raster check
cover the limit and invalid numeric inputs. See
[city projectile numeric safety](docs/REFERENCE_CITY_VELOCITY_SAFETY_RU.md).
Stage 43 advances ALL opt-in CITY TEST projectiles on one
fractional-frame timeline instead of sorting WHOLE projectiles by
their first predicted contact. This prevents a shot from consuming
a far enemy at a later time before another shot can hit that enemy
earlier, including when the first shot passed through a different
already-hit enemy. Real walls still stop both directions and
simultaneous enemy ties still consume both projectiles. New native
regressions and SDL2 pixel comparison verify the corrected event
order. See [global collision timeline](docs/REFERENCE_CITY_GLOBAL_TIMELINE_RU.md).
Stage 42 distinguishes exact same-time projectile arrivals at a shared
enemy from strictly later projectiles in the opt-in CITY TEST. Opposing
shots arriving at the same fractional simulation instant are both
consumed; later shots can still pass a foe hit earlier that tick.
Wall precedence, co-planar enemy hits, prior-tick stuns, and legacy
prototype behavior remain unchanged. Native tests plus SDL pixel
readback verify the outcome. See
[opposing projectile ties](docs/REFERENCE_CITY_OPPOSING_TIES_RU.md).
Stage 41 resolves simultaneous projectiles by estimated time to their
nearest overlapping enemy, independent of shot slots. A foe stunned
earlier within the SAME tick is transparent to a later plunger, which
can reach the next enemy; a preexisting stun from an earlier tick still
absorbs shots, and walls remain solid. New native replay tests and an
SDL2 two-target pixel check verify no unnecessary duplicate shot
consumption. See [multishot collision ordering](docs/REFERENCE_CITY_MULTISHOT_PRIORITY_RU.md).
Stage 40 makes simultaneous projectile hits on co-planar enemy
faces independent of array-slot order in opt-in CITY TEST. Both
vertically overlapping enemies at exactly the nearest leading face
are stunned; farther enemies and wall-occluded actors are not.
The ordinary prototype retains its slot-first collision behavior.
Tests verify both shot colors and directions, multiple projectiles,
wall precedence, stun recovery, and real SDL sprite pixels. See
[coplanar projectile arbitration](docs/REFERENCE_CITY_COPLANAR_IMPACTS_RU.md).
Stage 39 verifies multi-enemy contact and exact immunity transitions,
including recovery of stunned enemies and several consecutive life
cycles. It also normalizes malformed negative stun timers in CITY TEST
(the legacy nonzero check would keep such an enemy stunned forever).
New native regressions and an SDL HUD-pixel check verify the precise
frame in which three hearts become two after immunity expires. See
[multi-enemy immunity](docs/REFERENCE_CITY_MULTI_ENEMY_IMMUNITY_RU.md).
Stage 38 prevents CITY TEST pickups from triggering in the SAME tick
as a remote lethal hit or a pit retry. Spawn pickups work on the next
simulation tick; hurt-frame timing, finishing and persistent progress
remain unchanged. The original prototype keeps the older event order.
New CTests and an SDL pixel sample verify the retry boundary. See
[retry pickup boundary](docs/REFERENCE_CITY_RETRY_PICKUP_BOUNDARY_RU.md).
Stage 37 resolves the finish boundary crossed DURING a CITY TEST
simulation tick: overlapping exit pickups are applied before finishing,
while enemy contact after crossing no longer causes a fatal respawn
that erases the finish. Initial-boundary completion and premature Back
retain existing semantics, and the default prototype remains unchanged.
An independent collision regression suite and SDL raster check cover
the goal, red unlock and map reentry. See
[exit interaction arbitration](docs/REFERENCE_CITY_EXIT_INTERACTIONS_RU.md).
Stage 36 makes an already-reached CITY TEST exit win over a
same-tick Back/Select/Fire chord. A premature Back still aborts an
unfinished run, normal prototype rules are unchanged, and persistent
progress remains stable through repeat re-entry. Dedicated CTest
scenarios and an SDL rendered-frame comparison guard the finish/map
handoff against held inputs. See [stage finish chord priority](docs/STAGE_FINISH_CHORD_PRIORITY_RU.md).
Stage 35 prevents staggered opposing button holds from moving the
map and blocks stage entry until all directions are released. Short
rapid-input regressions and a 1,200-step reproducible state-machine
stress test verify the selection, all completion flags, and profile
isolation. SDL2 now checks the actual highlighted row after each
chord and valid entry. See [map chord guard](docs/MAP_INPUT_CHORD_GUARD_RU.md).
Stage 34 makes simultaneous map navigation/confirmation unambiguous:
navigation wins, a new confirmation edge is required to launch a stage,
and opposing directions cancel. Synthetic three-stage progress tests
verify all completion flags, persistent red weapon unlocks, map
wraparound, CITY TEST opt-in, and 240-tick replay. An additional SDL
software-raster test reads the actual green completion markers on all
three map rows. See [multi-stage routing](docs/MULTISTAGE_MAP_PROGRESS_RU.md).
Stage 33 unifies manual and completed CITY TEST exits: cleared
milestones and unlocked ammo persist, while transient shots and camera
history are cleared before returning to the map. A new pixel-level SDL
smoke compares the drawn completed-stage marker and verifies fresh stage
presentation after re-entry. See [stage progress and raster regression](docs/REFERENCE_CITY_PROGRESS_MAP_RU.md).
Stage 32 isolates CITY TEST retries from previous lives: pit and
combat respawns clear active plungers, attached red platforms, queued
one-tick shots and camera history without removing unlocks or stage
completion progress. Returning to the stage map also drops temporary
city shot state. See [retry and stage transition regressions](docs/REFERENCE_CITY_RETRY_TRANSITIONS_RU.md).
Stage 31 checks world-edge collision safety: red plungers cannot
anchor on *invisible sentinel columns*, while authored last-column
walls still block shots. CITY TEST enemy patrols clamp the full
14-pixel hitbox to the world and reflect at exact patrol endpoints.
Normal prototype semantics remain unchanged. See
[world-edge regression](docs/REFERENCE_CITY_WORLD_BOUNDS_RU.md).
Stage 30 snapshots the CITY TEST projectile's color when the fire
button is pressed, so one-tick deferred appearance cannot change weapon
under a same-interval toggle. A new long-running CTest exercises enemy
stun transitions, the hero's hurt immunity, simultaneous enemy hits and
respawn while retaining the original-free prototype. See
[combat transition verification](docs/REFERENCE_CITY_COMBAT_TRANSITIONS_RU.md).
Stage 29 adds first-contact arbitration when a moving city projectile
intersects an enemy and a wall in the same one-pixel step. The nearest
leading contact wins and solid walls win exact geometric ties.
City tests also check simultaneous hero/enemy/shot collisions, while
the legacy prototype keeps its original processing. See
[contact ordering and concurrent impacts](docs/REFERENCE_CITY_CONTACT_ORDER_RU.md).
Stage 28 chooses the nearest physical enemy hitbox when a CITY TEST projectile overlaps multiple targets, independently of enemy slot ordering. ROM-free regressions cover stun timers, both shot colors, re-hit, wall occlusion, and original prototype isolation. See [enemy-impact checks](docs/REFERENCE_CITY_ENEMY_IMPACTS_RU.md).
Stage 27 adds simultaneous tile/anchor height arbitration plus
long original-free city physics replay: controlled jump/fire/reverse traces,
permuted anchor lifetimes, and expiry-to-floor transitions. See
[extended anchor collision tests](docs/REFERENCE_CITY_ANCHOR_LONG_RUN_RU.md).
Stage 26 strengthens red-anchor lifecycle and landing: the closest
world-space anchor surface wins irrespective of shot slot order, expiry
drops the hero onto a surviving lower anchor, and only attached red
shots act as platforms in CITY TEST. Debug/Release and Switch builds
exercise this independently from the original game assets.
See [anchor lifecycle tests](docs/REFERENCE_CITY_ANCHOR_LIFECYCLE_RU.md).
Stage 25 adds reference-city-only **swept projectile collision**: the first
impacted wall face fixes red anchor placement, enemies before walls are
hit first, and fast shots cannot tunnel across test tiles. The ordinary
prototype preserves its earlier collision routine unchanged. See
[projectile collision regression](docs/REFERENCE_CITY_SHOT_SWEEP_RU.md).
Stage 24 adds a separate **real SDL pixel readback** which verifies that the
hero, enemy, pickup, projectile, tile geometry and two planes all use the
latched display camera, while the HUD remains stationary. The deliberate
synthetic mismatch of game/display camera exposes accidental coupling.
See [pixel-level camera binding](docs/REFERENCE_CITY_CAMERA_RASTER_BINDING_RU.md).
The asset-free city's 2:1 hardware-observed parallax is checked with rendered SDL pixels and guest NRO telemetry. See [parallax baseline](docs/REFERENCE_CITY_PARALLAX_RU.md) and [reverse/odd-even regression](docs/REFERENCE_CITY_PARALLAX_REVERSAL_RU.md).

## Switch homebrew

With devkitPro/devkitA64, libnx and Switch SDL2 installed:

    make -j2

CI-confirmed build artifact: quackshot-prototype.nro.
Runtime in the **NeXium Nintendo Switch emulator has now been verified**: guest SDL2 first frame, 600+ simulation frames, and A/Plus scene enter/return. This does not validate physical Switch hardware or every emulator.
For a runtime diagnostic log, the Switch build attempts to write
sdmc:/switch/quackshot-runtime.csv (up to 256 records); failure to open it
never prevents gameplay.

See [documentation in Russian](docs/PROTOTYPE_RU.md) and
[Switch runtime protocol](docs/SWITCH_RUNTIME_TEST_RU.md) and
[NeXium emulator evidence (Russian)](docs/NEXIUM_EMULATOR_RU.md).

Host-only diagnostics for Linux CI:

    QS_TRACE_PATH=probe-runtime.csv SDL_VIDEODRIVER=dummy \
      SDL_RENDER_DRIVER=software ./build-desktop/quackshot-prototype --probe-frames 605

The finite probe verifies SDL initialization, first rendered frame,
600 simulation steps, a heartbeat and clean shutdown. It is **not**
a Nintendo Switch emulator or console run.

## Rights and asset isolation

The original game, Disney characters, art and music belong to their
respective rights holders. This repository contains independently written
prototype code. Do **not** commit ROM images or proprietary extracted assets.
Any original dump remains outside version control. Public distribution of
a derivative game using protected content requires appropriate permissions.
