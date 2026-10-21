#ifndef QS_PARALLAX_H
#define QS_PARALLAX_H

#include "qs_game.h"

/*
 * Pixel offsets for two independently drawn procedural background planes.
 * The experimental first city uses the directly observed Mega Drive ratio:
 * scene plane A = -camera, distant plane B = -floor(camera / 2).
 * No ROM/VRAM, art, tiles, or Genesis executable are needed.
 */
typedef struct {
    int front_px;
    int distant_px;
} QsParallax;

QsParallax qs_parallax_offsets(const QsGame *game);
int qs_parallax_is_reference_city(const QsGame *game);

#endif
