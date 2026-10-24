# QuackShot Remake — Stage277: leftward projectile world-boundary despawn

**Status:** implementation committed to the existing Draft PR; CI is pending.

## Scope

Stage276 proved the real NRO's one-tick delayed left-facing yellow and red shots, their measured horizontal trajectories, actual SDL pixel colors, same-frame background-negative controls, renderer restoration, and the real red pickup transition. Stage277 extends the same evidence to the **left world boundary**. It does not change gameplay physics or claim that this original-free prototype rule is recovered from the 1991 game.

## Existing NRO evidence used to define the invariant

The four archived Stage276 guest traces contain 320 consecutive guest frames each. The active shot remains visible until its next horizontal step would place its X coordinate below zero, then the next guest frame reports the canonical inactive state (`x=-1`, `y=-1`, `vx=0`, `attached=0`, `red=0`).

| Variant / repeat | Shot start X (milli-pixels) | Active ticks | Last active X | First inactive frame |
|---|---:|---:|---:|---:|
| Left yellow / 1 | 146656 | 27 | 3656 | 531 |
| Left yellow / 2 | 147281 | 27 | 4281 | 547 |
| Left red / 1 | 166718 | 31 | 1718 | 574 |
| Left red / 2 | 163031 | 30 | 3531 | 577 |

For speed 5500 milli-pixels per tick and leftward direction, expected active duration is `floor(start_x / 5500) + 1`. The verifier requires a contiguous active interval beginning exactly one tick after the physical FIRE edge, the expected count below the 65-tick lifetime, continuous measured X positions (one milli-pixel tolerance for float-to-integer export), a final active X in `[0, 5500)`, a negative projected next X, canonical inactive fields on the immediately following frame, and no later reappearance.

## Regression coverage

Synthetic adversarial tests reject premature disappearance, a shot remaining active after the boundary crossing, drift at the final active position, and a projectile reappearing after a gap. The existing color, direction, height, pickup, input-causality, pixel-readback, negative-control, restoration and frame-continuity tests remain in force.

## Acceptance gate

The existing `NeXium CITY Horizontal Variants` workflow reruns the NRO build, two independent left-yellow guests, two independent left-red guests, and both comparisons against the new verifier. Stage277 will be marked complete only when those jobs pass and their artifacts are archived privately with matching byte sizes and SHA-256 hashes. The NRO tests use the independently authored CITY TEST, not the original game's levels or assets.
