# QuackShot Stage280: synthetic 16-step counter audit

Added a standalone ROM-free numeric auditor for the candidate counter observed at 0xFFA2E6. It requires one initialization to 16, sixteen consecutive read/write decrements, the zero bit of CCR only on the final branch, and a single zero-path reset of the nearby byte at 0xFFA2C8. Negative controls without the counter are checked independently.

The independent test suite injects dropped/duplicated steps, a wrong decrement, early zero flag, wrong A0 pointer, corrupt windows, bad privacy flags and extra payload fields. The CLI returns a compact numeric verdict only.

Scope: structural validation of synthetic numeric observations, NOT original-ROM parity or confirmed gameplay semantics. No ROM, raw RAM/VRAM, instruction bytes, graphics, audio or proprietary game assets are required for CI.
