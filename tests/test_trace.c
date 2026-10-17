#include "qs_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "test_trace:%d: %s\n", \
                         __LINE__, #x); exit(1); } } while (0)

int main(void) {
    FILE *stream = tmpfile();
    QsTrace trace;
    char line[256];
    unsigned index, total = 0;
    CHECK(stream != NULL);
    CHECK(qs_trace_attach(&trace, stream));
    CHECK(qs_trace_emit(&trace, 0, 0, QS_TRACE_BOOT, 0, 0, 0, 0));
    CHECK(qs_trace_emit(&trace, 100, 6, QS_TRACE_FIRST_FRAME, 0, 0, 1, 0));
    CHECK(qs_trace_emit(&trace, 1000, 60, QS_TRACE_HEARTBEAT, 1, 2, 1, 0));
    CHECK(!qs_trace_emit(&trace, 1000, 60, (QsTraceEvent)99, 0, 0, 0, 0));
    for (index = 3; index < QS_TRACE_MAX_RECORDS; index++) {
        CHECK(qs_trace_emit(&trace, 1010 + index, 60 + index,
                            QS_TRACE_HEARTBEAT, 1, 2, 1, 0));
    }
    CHECK(!qs_trace_emit(&trace, 9999, 999, QS_TRACE_HEARTBEAT, 1, 2, 1, 0));
    CHECK(trace.records == QS_TRACE_MAX_RECORDS);
    rewind(stream);
    CHECK(fgets(line, sizeof(line), stream) != NULL);
    CHECK(strcmp(line, "seq,ticks_ms,frames,event,mode,stage,controllers,detail\n") == 0);
    while (fgets(line, sizeof(line), stream)) {
        unsigned seq, ticks, frames;
        int event, mode, stage, controllers, detail;
        CHECK(sscanf(line, "%u,%u,%u,%d,%d,%d,%d,%d",
              &seq, &ticks, &frames, &event, &mode, &stage, &controllers, &detail) == 8);
        CHECK(seq == total);
        if (total == 0) CHECK(event == QS_TRACE_BOOT);
        if (total == 1) CHECK(event == QS_TRACE_FIRST_FRAME && frames == 6);
        if (total == 2) CHECK(event == QS_TRACE_HEARTBEAT && stage == 2);
        ++total;
    }
    CHECK(total == QS_TRACE_MAX_RECORDS);
    qs_trace_close(&trace);
    CHECK(trace.stream == NULL);
    CHECK(!qs_trace_open(&trace, NULL));
    CHECK(!qs_trace_emit(&trace, 0, 0, QS_TRACE_BOOT, 0, 0, 0, 0));
    puts("PASS: bounded runtime trace, CSV roundtrip, events and failure cases");
    return 0;
}
