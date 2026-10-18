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
