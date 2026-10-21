#include "qs_motion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Motion check failed at line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); } } while (0)

int main(void) {
    QsMotion trace = {0};
    QsGame game;
    QsInput input = {0};
    FILE *file = tmpfile();
    char line[256];
    unsigned rows = 0;
    CHECK(file != NULL);
    CHECK(qs_motion_attach(&trace, file));
    qs_init(&game);
    qs_begin_stage(&game, 0);
    CHECK(!qs_motion_input_bits(input));
    input.right = 1; input.jump = 1; input.fire = 1;
    CHECK(qs_motion_input_bits(input) ==
          (QS_MOTION_RIGHT | QS_MOTION_JUMP | QS_MOTION_FIRE));
    qs_step(&game, input);
    CHECK(qs_motion_emit(&trace, &game, 16, input));
    rewind(file);
    CHECK(fgets(line, sizeof(line), file) != NULL);
    CHECK(strstr(line, "x_milli,y_milli") != NULL);
    CHECK(fgets(line, sizeof(line), file) != NULL);
    CHECK(strstr(line, ",14,") != NULL); /* 2+4+8 input mask */
    CHECK(strchr(line, '\n') != NULL);
    /* Check bounded output with a separate stream after rewinding. */
    fseek(file, 0, SEEK_END);
    while (trace.samples < QS_MOTION_MAX_SAMPLES)
        CHECK(qs_motion_emit(&trace, &game, 16, input));
    CHECK(!qs_motion_emit(&trace, &game, 16, input));
    rewind(file);
    CHECK(fgets(line, sizeof(line), file) != NULL);
    while (fgets(line, sizeof(line), file)) ++rows;
    CHECK(rows == QS_MOTION_MAX_SAMPLES);
    qs_motion_close(&trace);
    CHECK(trace.file == NULL);
    CHECK(!qs_motion_open(&trace, NULL));
    CHECK(!qs_motion_emit(&trace, &game, 10, input));
    puts("PASS: motion CSV flags, coordinates, bounds, no-record paths");
    return 0;
}
