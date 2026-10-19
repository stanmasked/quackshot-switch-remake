#ifndef QS_LIVE_PROBE_H
#define QS_LIVE_PROBE_H
#include "qs_game.h"
#include <stdio.h>

/* Stage 89. Dedicated evidence from real read_input(pad) and qs_step,
 * never from the simulated Stage 88 route and never sent to the player. */
typedef struct {
    FILE *file;
    unsigned records;
    int phase;
    int error;
    float right_highwater;
    int fire_frame;
    int shots_on_fire;
} QsLiveProbe;

int qs_live_probe_attach(QsLiveProbe *probe, FILE *stream);
int qs_live_probe_open(QsLiveProbe *probe, const char *path);
int qs_live_probe_tick(QsLiveProbe *probe, const QsGame *g, QsInput input,
                       QsMode old_mode, float old_x, float old_y,
                       int controller_present, unsigned previous_input_bits,
                       unsigned shots_before);
void qs_live_probe_close(QsLiveProbe *probe);
#endif
