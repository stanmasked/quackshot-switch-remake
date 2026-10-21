#include "qs_motion.h"
#include <string.h>

static int scaled(float value) {
    return (int)(value * 1000.0f);
}

unsigned qs_motion_input_bits(QsInput input) {
    return (input.left ? QS_MOTION_LEFT : 0u) |
           (input.right ? QS_MOTION_RIGHT : 0u) |
           (input.jump ? QS_MOTION_JUMP : 0u) |
           (input.fire ? QS_MOTION_FIRE : 0u) |
           (input.dash ? QS_MOTION_DASH : 0u) |
           (input.select ? QS_MOTION_SELECT : 0u) |
           (input.back ? QS_MOTION_BACK : 0u);
}

int qs_motion_attach(QsMotion *trace, FILE *file) {
    if (!trace) return 0;
    memset(trace, 0, sizeof(*trace));
    if (!file) return 0;
    trace->file = file;
    if (fprintf(file, "seq,frame,ticks_ms,stage,input_bits,x_milli,y_milli,vy_milli,grounded,active_shots,attached_shots,stunned_enemies\n") < 0 ||
        fflush(file) != 0) {
        trace->error = 1;
        fclose(file);
        trace->file = NULL;
        return 0;
    }
    return 1;
}

int qs_motion_open(QsMotion *trace, const char *path) {
    FILE *file;
    if (!trace) return 0;
    memset(trace, 0, sizeof(*trace));
    if (!path || !*path) return 0;
    file = fopen(path, "w");
    if (!file) return 0;
    return qs_motion_attach(trace, file);
}

int qs_motion_emit(QsMotion *trace, const QsGame *game,
                   unsigned ticks_ms, QsInput input) {
    int shot_count = 0, anchor_count = 0, stunned_count = 0;
    int i;
    if (!trace || !game || !trace->file || trace->error ||
        trace->samples >= QS_MOTION_MAX_SAMPLES || game->mode != QS_STAGE)
        return 0;
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        if (game->shots[i].active) {
            ++shot_count;
            if (game->shots[i].attached) ++anchor_count;
        }
    }
    for (i = 0; i < QS_MAX_ENEMIES; ++i) {
        if (game->enemies[i].active && game->enemies[i].stunned_frames > 0)
            ++stunned_count;
    }
    if (fprintf(trace->file, "%u,%d,%u,%d,%u,%d,%d,%d,%d,%d,%d,%d\n",
                trace->samples, game->frames, ticks_ms, game->stage,
                qs_motion_input_bits(input), scaled(game->hero.x),
                scaled(game->hero.y), scaled(game->hero.vy),
                game->hero.grounded, shot_count, anchor_count,
                stunned_count) < 0 ||
        fflush(trace->file) != 0) {
        trace->error = 1;
        return 0;
    }
    ++trace->samples;
    return 1;
}

void qs_motion_close(QsMotion *trace) {
    if (!trace) return;
    if (trace->file) fclose(trace->file);
    trace->file = NULL;
}
