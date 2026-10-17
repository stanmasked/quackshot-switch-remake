#include "qs_trace.h"
#include <string.h>

int qs_trace_attach(QsTrace *trace, FILE *stream) {
    if (!trace) return 0;
    memset(trace, 0, sizeof(*trace));
    if (!stream) return 0;
    trace->stream = stream;
    if (fprintf(stream, "seq,ticks_ms,frames,event,mode,stage,controllers,detail\n") < 0 ||
        fflush(stream) != 0) {
        trace->failed = 1;
        fclose(stream);
        trace->stream = NULL;
        return 0;
    }
    return 1;
}

int qs_trace_open(QsTrace *trace, const char *path) {
    FILE *file;
    if (!trace) return 0;
    memset(trace, 0, sizeof(*trace));
    if (!path || !*path) return 0;
    file = fopen(path, "w");
    if (!file) return 0;
    return qs_trace_attach(trace, file);
}

int qs_trace_emit(QsTrace *trace, unsigned ticks_ms, unsigned frames,
                  QsTraceEvent event, int mode, int stage, int controllers,
                  int detail) {
    if (!trace || !trace->stream || trace->failed ||
        trace->records >= QS_TRACE_MAX_RECORDS ||
        event < QS_TRACE_BOOT || event > QS_TRACE_SMOKE) return 0;
    if (fprintf(trace->stream, "%u,%u,%u,%d,%d,%d,%d,%d\n",
                trace->records, ticks_ms, frames, (int)event,
                mode, stage, controllers, detail) < 0 ||
        fflush(trace->stream) != 0) {
        trace->failed = 1;
        return 0;
    }
    ++trace->records;
    return 1;
}

void qs_trace_close(QsTrace *trace) {
    if (!trace) return;
    if (trace->stream) fclose(trace->stream);
    trace->stream = NULL;
}
