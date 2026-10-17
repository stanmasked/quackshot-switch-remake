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
- Experimental Nintendo Switch homebrew Makefile; NRO cross-build under CI validation.
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

## Switch homebrew

With devkitPro/devkitA64, libnx and Switch SDL2 installed:

    make -j2

Expected build artifact: quackshot-prototype.nro, **if** the cross-build succeeds.
Actual Switch runtime behavior still needs hardware/emulator validation.

See [documentation in Russian](docs/PROTOTYPE_RU.md).

## Rights and asset isolation

The original game, Disney characters, art and music belong to their
respective rights holders. This repository contains independently written
prototype code. Do **not** commit ROM images or proprietary extracted assets.
Any original dump remains outside version control. Public distribution of
a derivative game using protected content requires appropriate permissions.
