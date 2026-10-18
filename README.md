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
- Twelve executable regression checks in Release and Debug builds.
- **Optional city-reference physics envelope**, fitted to numeric observations from privately emulated original Mega Drive gameplay. Default prototype movement is unchanged.
- Nintendo Switch homebrew Makefile; NRO0/ASET/NACP cross-build and artifact verified in CI (not runtime-tested on a console).
- Bounded asset-free SDL2 boot/first-frame/controller/heartbeat diagnostics on Switch SD, with host-side integration tests.
- Independent original-ROM metadata inspector lives in PR #1.

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
