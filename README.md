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
