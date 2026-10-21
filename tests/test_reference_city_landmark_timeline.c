#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage170 — source-asset-free causal landmark timeline.
 *
 * The *original* first-city observation has:
 *   falling begins at frame 2281 (relative to button frame 2070: 211),
 *   lower floor at 2297 (relative: 227), stop at wall at 2344 (274),
 *   reverse input at 2360; measured X at 2390 / 2430: 455 / 397.
 * Our independently authored level is NOT an extracted original map.
 * Event deltas printed here are diagnostics, NOT assertions of ROM parity.
 */
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "Stage170 city landmark FAIL line %d: %s\n", \
            __LINE__, #test); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { ORIGINAL_FALL_TICK = 211, ORIGINAL_LAND_TICK = 227,
       ORIGINAL_WALL_TICK = 274, ORIGINAL_REVERSE_CAMERA_LAG = 15,
       ORIGINAL_REVERSE_X30 = 455, ORIGINAL_REVERSE_X70 = 397 };

typedef struct {
    int first_camera_right_tick;
    int first_fall_tick;
    int lower_landing_tick;
    int wall_stop_tick;
    int reverse_camera_lag;
    float fall_world_x;
    float land_world_x;
    float wall_world_x;
    float reverse_x30;
    float reverse_x70;
} LandmarkTimeline;

static QsGame synthetic_course(void) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);

    /* Completely independently authored first-city experiment:
     * an upper floor ending at x400, a 32px drop, a wall at x512. */
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col) {
        g.terrain[13][col] = '#';
        g.terrain[14][col] = '#';
        if (col < 25) {
            g.terrain[11][col] = '#';
            g.terrain[12][col] = '#';
        }
    }
    g.terrain[12][32] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = 156.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void check_repeated_state(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(b->physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(a->hero.x == b->hero.x);
    CHECK(a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx);
    CHECK(a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static LandmarkTimeline observe_synthetic_timeline(void) {
    QsGame a = synthetic_course();
    QsGame b = synthetic_course();
    LandmarkTimeline t;
    int frame;
    float previous_camera;
    memset(&t, 0, sizeof(t));
    t.first_camera_right_tick = -1;
    t.first_fall_tick = -1;
    t.lower_landing_tick = -1;
    t.wall_stop_tick = -1;
    t.reverse_camera_lag = -1;

    /* Long held RIGHT crosses the upper ledge and collides with the
     * independently placed wall. Keep pressing until a stable stop. */
    for (frame = 0; frame < 320; ++frame) {
        QsInput in = {0};
        in.right = 1;
        previous_camera = a.camera_x;
        qs_step(&a, in);
        qs_step(&b, in);
        check_repeated_state(&a, &b);
        CHECK(a.render_camera_x == previous_camera);
        if (t.first_camera_right_tick < 0 && a.camera_x > 0.0f)
            t.first_camera_right_tick = frame;
        if (t.first_fall_tick < 0 && a.hero.y > 156.01f) {
            t.first_fall_tick = frame;
            t.fall_world_x = a.hero.x;
        }
        if (t.lower_landing_tick < 0 && t.first_fall_tick >= 0 &&
            a.hero.grounded && fabsf(a.hero.y - 188.0f) < 0.01f) {
            t.lower_landing_tick = frame;
            t.land_world_x = a.hero.x;
        }
        if (t.wall_stop_tick < 0 && t.lower_landing_tick >= 0 &&
            fabsf(a.hero.vx) < 0.00001f && a.hero.x > 480.0f) {
            t.wall_stop_tick = frame;
            t.wall_world_x = a.hero.x;
        }
        if (t.wall_stop_tick >= 0) {
            CHECK(a.hero.x <= 502.0f);
            CHECK(a.hero.y == 188.0f);
            CHECK(a.hero.grounded);
        }
    }
    CHECK(t.first_camera_right_tick >= 0);
    CHECK(t.first_fall_tick > t.first_camera_right_tick);
    CHECK(t.lower_landing_tick > t.first_fall_tick);
    CHECK(t.wall_stop_tick > t.lower_landing_tick);
    CHECK(t.wall_stop_tick < 320);
    CHECK(t.fall_world_x < t.land_world_x);
    CHECK(t.land_world_x < t.wall_world_x);
    CHECK(t.fall_world_x >= 385.0f && t.fall_world_x < 430.0f);
    CHECK(t.land_world_x > 400.0f && t.land_world_x < 460.0f);
    CHECK(t.wall_world_x >= 498.0f && t.wall_world_x <= 502.0f);
    CHECK(a.hero.vx == 0.0f);

    /* Reverse only after the wall has been held. This measures both
     * control recovery and the two-sided camera hysteresis. */
    {
        const float stop_x = a.hero.x;
        const float camera_at_wall = a.camera_x;
        for (frame = 0; frame < 70; ++frame) {
            QsInput in = {0};
            in.left = 1;
            previous_camera = a.camera_x;
            qs_step(&a, in);
            qs_step(&b, in);
            check_repeated_state(&a, &b);
            CHECK(a.render_camera_x == previous_camera);
            if (t.reverse_camera_lag < 0 &&
                a.camera_x < camera_at_wall - 0.001f)
                t.reverse_camera_lag = frame + 1;
            if (frame == 29) t.reverse_x30 = a.hero.x;
            if (frame == 69) t.reverse_x70 = a.hero.x;
        }
        CHECK(t.reverse_camera_lag > 1 && t.reverse_camera_lag < 25);
        CHECK(t.reverse_x30 < stop_x - 25.0f);
        CHECK(t.reverse_x70 < t.reverse_x30 - 40.0f);
        /* After recovering LEFT, the independently authored upper
         * platform has a vertical face at X=400. It eventually stops
         * the hero a second time; requiring vx<0 after 70 frames
         * incorrectly treats this valid collision as a regression. */
        CHECK(a.hero.vx <= 0.0f);
        CHECK(t.reverse_x70 >= 399.9f && t.reverse_x70 <= 410.0f);
        CHECK(a.camera_x < camera_at_wall);
        CHECK(a.hero.x - a.camera_x >= 122.0f);
        CHECK(a.hero.x - a.camera_x <= 124.0f);
    }
    return t;
}

int main(void) {
    LandmarkTimeline result = observe_synthetic_timeline();
    /*
     * Numerical *difference* from previously measured original events.
     * We deliberately DO NOT demand zero error: authored geometry, sprite
     * anchor and simulation tick origin differ from the Mega Drive map.
     */
    printf("STAGE170_LANDMARK_SEQUENCE_PASS "
           "synthetic_drop=%d synthetic_landing=%d synthetic_wall=%d "
           "relative_original_deltas=[%+d,%+d,%+d] "
           "synthetic_reverse_lag=%d original_reverse_lag=%d "
           "synthetic_reverse_x30=%.3f original_reverse_x30=%d "
           "synthetic_reverse_x70=%.3f original_reverse_x70=%d\n",
           result.first_fall_tick, result.lower_landing_tick,
           result.wall_stop_tick,
           result.first_fall_tick - ORIGINAL_FALL_TICK,
           result.lower_landing_tick - ORIGINAL_LAND_TICK,
           result.wall_stop_tick - ORIGINAL_WALL_TICK,
           result.reverse_camera_lag, ORIGINAL_REVERSE_CAMERA_LAG,
           result.reverse_x30, ORIGINAL_REVERSE_X30,
           result.reverse_x70, ORIGINAL_REVERSE_X70);
    return EXIT_SUCCESS;
}
