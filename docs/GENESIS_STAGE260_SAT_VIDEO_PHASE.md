# Stage260 — numeric sprite/video phase check

A private read-only Genesis Plus GX replay established a one-frame SAT gap during right-facing stationary upward fire (UP+B, animation index 29). The original ROM and emulator binary were hash-verified; no source assets enter CI.

At relative frames 0, 1 and 2 of the triggered action, candidate sprite counts were 4, 0, 3. Blue pixel counts in a bounded nearby display region were 175, 0, 214. The first action frame used cursor zero and performed no ordinary animation-field writes. The sprite gap was observed on the following cursor-one frame.

The pattern reproduced in two independent original runs and shifted by exactly ten frames when the input was moved ten frames. Matched controls: left-facing upward fire (index 28) kept three candidate sprites on relative frame 1; regular right-facing fire (index 19) kept four.

This is first-city numeric evidence. The blue-pixel heuristic does not prove sprite identity or collision geometry, and the exact rendering mechanism is not established. Nintendo Switch gameplay is unchanged. Original ROM, media and raw traces remain private.
