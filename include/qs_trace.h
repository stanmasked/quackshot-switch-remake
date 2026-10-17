#ifndef QS_TRACE_H
#define QS_TRACE_H

#include <stdio.h>

/* Bounded, text-only and asset-free runtime event log. */
#define QS_TRACE_MAX_RECORDS 256

typedef enum {
    QS_TRACE_BOOT = 1,
    QS_TRACE_SDL_READY = 2,
    QS_TRACE_WINDOW_READY = 3,
    QS_TRACE_RENDERER_READY = 4,
    QS_TRACE_FIRST_FRAME = 5,
    QS_TRACE_CONTROLLER = 6,
    QS_TRACE_STAGE_CHANGE = 7,
    QS_TRACE_HEARTBEAT = 8,
    QS_TRACE_SHUTDOWN = 9,
    QS_TRACE_FAILURE = 10,
    QS_TRACE_SMOKE = 11,
    QS_TRACE_PARALLAX = 12, /* rendered plane A and B px, packed */
    QS_TRACE_CAMERA_PHASE = 13 /* simulation and render camera px, packed */
} QsTraceEvent;

typedef struct {
    FILE *stream;
    unsigned records;
    int failed;
} QsTrace;

/* Both open and attach transfer ownership of the file to QsTrace. */
int qs_trace_open(QsTrace *trace, const char *path);
int qs_trace_attach(QsTrace *trace, FILE *stream);
int qs_trace_emit(QsTrace *trace, unsigned ticks_ms, unsigned frames,
                  QsTraceEvent event, int mode, int stage, int controllers,
                  int detail);
void qs_trace_close(QsTrace *trace);

#endif
