#include "qs_game.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Stage166: original-free integer 16.16 motion oracle, independent of
 * qs_game.c's floating-point movement implementation.
 *
 * Walking acceleration and jump-plateau constants derive from privately
 * observed numeric original-game fields at ONE city spawn. Reversal and
 * eight-frame hold are MODEL regression checks, not new ROM observations.
 * The CITY TEST level geometry remains separately authored.
 */
#define FP_ONE 65536
#define FP_ACCEL 10240
#define FP_MAX_WALK 106496
#define FP_JUMP (-192000)
#define FP_GRAVITY 12800

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "Stage166 curve FAIL line %d: %s\n", __LINE__, #cond); \
    exit(EXIT_FAILURE); \
} } while (0)

static QsInput no_input(void) {
    QsInput input = {0};
    return input;
}

static QsGame city_reference(void) {
    QsGame g;
    QsInput enter = no_input();
    unsigned t;
    qs_init(&g);
    enter.select = 1;
    enter.dash = 1;
    qs_step(&g, enter);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    for (t = 0; t < 15; ++t) qs_step(&g, no_input());
    CHECK(g.hero.grounded);
    CHECK(fabsf(g.hero.y - 188.0f) < 0.01f);
    g.hero.x = 61.0f;
    return g;
}

static int32_t approach(int32_t actual, int32_t target) {
    if (actual < target)
        return actual + FP_ACCEL < target ? actual + FP_ACCEL : target;
    if (actual > target)
        return actual - FP_ACCEL > target ? actual - FP_ACCEL : target;
    return actual;
}

static void walking_every_frame(void) {
    QsGame g = city_reference();
    int64_t expected_x = (int64_t)61 * FP_ONE;
    int32_t expected_vx = 0;
    unsigned t;
    for (t = 0; t < 80; ++t) {
        QsInput input = no_input();
        int32_t target = 0;
        /* Reversal, release, opposing buttons and a new short tap.
         * No measured original turnaround profile is asserted here. */
        if (t < 20 || (t >= 66 && t < 74)) {
            input.right = 1;
            target = FP_MAX_WALK;
        } else if (t < 40) {
            input.left = 1;
            target = -FP_MAX_WALK;
        } else if (t >= 60 && t < 66) {
            input.left = input.right = 1;
        }
        expected_vx = approach(expected_vx, target);
        expected_x += expected_vx;
        qs_step(&g, input);
        CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
        CHECK(fabsf(g.hero.vx - expected_vx / (float)FP_ONE) < 0.00015f);
        /* Float substeps are permitted insignificant rounding, not drift. */
        CHECK(fabsf(g.hero.x - expected_x / (float)FP_ONE) < 0.035f);
        CHECK(g.hero.grounded);
    }
}

static void jump_every_frame(unsigned hold_frames) {
    QsGame g = city_reference();
    const float starting_y = g.hero.y;
    int32_t expected_vy = FP_JUMP;
    int64_t displacement = 0;
    unsigned t;
    CHECK(hold_frames == 1 || hold_frames == 8 || hold_frames == 50);
    for (t = 0; t < 25; ++t) {
        QsInput input = no_input();
        input.jump = (t < hold_frames);
        if (!(input.jump && t < 15))
            expected_vy += FP_GRAVITY;
        displacement += expected_vy;
        qs_step(&g, input);
        CHECK(g.mode == QS_STAGE);
        CHECK(!g.hero.grounded); /* Open area; collision is not part of oracle. */
        CHECK(fabsf(g.hero.vy - expected_vy / (float)FP_ONE) < 0.00015f);
        CHECK(fabsf((g.hero.y - starting_y) -
                    displacement / (float)FP_ONE) < 0.035f);
    }
}

int main(void) {
    walking_every_frame();
    jump_every_frame(1);
    jump_every_frame(8);
    jump_every_frame(50);
    puts("STAGE166_FRAME_CURVE_PASS: 80 walking + 75 jumping steps matched independent 16.16 oracle");
    return 0;
}
