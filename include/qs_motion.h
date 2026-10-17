#ifndef QS_MOTION_H
#define QS_MOTION_H
#include "qs_game.h"
#include <stdio.h>

/* Compact numerical gameplay samples; no ROM or proprietary assets. */
#define QS_MOTION_MAX_SAMPLES 256
#define QS_MOTION_LEFT   1
#define QS_MOTION_RIGHT  2
#define QS_MOTION_JUMP   4
#define QS_MOTION_FIRE   8
#define QS_MOTION_DASH  16
#define QS_MOTION_SELECT 32
#define QS_MOTION_BACK  64

typedef struct {
    FILE *file;
    unsigned samples;
    int error;
} QsMotion;

int qs_motion_open(QsMotion *trace, const char *path);
int qs_motion_attach(QsMotion *trace, FILE *file);
unsigned qs_motion_input_bits(QsInput input);
int qs_motion_emit(QsMotion *trace, const QsGame *game,
                   unsigned ticks_ms, QsInput input);
void qs_motion_close(QsMotion *trace);
#endif
