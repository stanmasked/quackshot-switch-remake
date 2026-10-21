#include "qs_game.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage169: independent 16.16 camera/deadzone oracle. No original ROM,
 * graphics, copied map or original camera code is included.
 *
 * Measured original first-city evidence: rightward worldX-scroll =
 * [140,142], leftward [122,124]. This prototype intentionally chooses
 * 141 and 123 in those bands. These are model contracts, not a claim
 * that the actual 68000 camera algorithm has been recovered.
 */
#define FP_ONE INT64_C(65536)
#define FP_ACCEL INT64_C(10240)
#define FP_WALK INT64_C(106496)
#define RIGHT_ANCHOR (INT64_C(141) * FP_ONE)
#define LEFT_ANCHOR (INT64_C(123) * FP_ONE)
#define CAMERA_MAX ((int64_t)(QS_COLUMNS * QS_TILE - QS_VIEW_W) * FP_ONE)
#define EPS 0.04f
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "Stage169 camera lockstep FAIL line %d: %s\n", \
            __LINE__, #x); \
    exit(EXIT_FAILURE); \
} } while (0)

static QsInput input_at(unsigned frame) {
    QsInput input = {0};
    if (frame < 120u || (frame >= 180u && frame < 240u))
        input.right = 1;
    else if (frame < 155u)
        input.left = 1;
    /* 155..179: release, braking, no held direction. */
    return input;
}

static QsGame reference_flat_city(void) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.physics_profile == QS_PHYSICS_REFERENCE_CITY);

    /* Entirely authored test fixture; remove obstacles and actors so a
     * camera guard is not mistaken for original city collision evidence. */
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    return g;
}

static int64_t velocity_step(int64_t current, int64_t target) {
    if (current < target) {
        current += FP_ACCEL;
        return current > target ? target : current;
    }
    if (current > target) {
        current -= FP_ACCEL;
        return current < target ? target : current;
    }
    return current;
}

static void framewise_two_way_camera_and_latch(void) {
    QsGame a = reference_flat_city();
    QsGame b = reference_flat_city();
    int64_t x = 61 * FP_ONE;
    int64_t vx = 0, camera = 0;
    unsigned frame, right_moves = 0, left_moves = 0, deadzone = 0;
    unsigned right_return_moves = 0;
    for (frame = 0; frame < 240; ++frame) {
        const QsInput in = input_at(frame);
        const int64_t old_camera = camera;
        const int64_t requested = in.right ? FP_WALK :
                                  (in.left ? -FP_WALK : 0);
        const int64_t old_x = x;
        int64_t screen_x;

        vx = velocity_step(vx, requested);
        x += vx;
        screen_x = x - camera;
        if (screen_x > RIGHT_ANCHOR)
            camera = x - RIGHT_ANCHOR;
        else if (screen_x < LEFT_ANCHOR)
            camera = x - LEFT_ANCHOR;
        if (camera < 0) camera = 0;
        if (camera > CAMERA_MAX) camera = CAMERA_MAX;

        qs_step(&a, in);
        qs_step(&b, in);
        CHECK(a.mode == QS_STAGE && a.stage == 0);
        CHECK(a.physics_profile == QS_PHYSICS_REFERENCE_CITY);
        CHECK(a.hero.grounded == 1);
        CHECK(fabsf(a.hero.x - (float)x / (float)FP_ONE) < EPS);
        CHECK(fabsf(a.hero.vx - (float)vx / (float)FP_ONE) < EPS);
        CHECK(fabsf(a.camera_x - (float)camera / (float)FP_ONE) < EPS);
        CHECK(fabsf(a.render_camera_x - (float)old_camera / (float)FP_ONE) < EPS);

        /* Two cold initial states must generate identical camera states,
         * including the one-frame presentation latch. */
        CHECK(a.hero.x == b.hero.x && a.hero.vx == b.hero.vx);
        CHECK(a.camera_x == b.camera_x);
        CHECK(a.render_camera_x == b.render_camera_x);

        if (camera > old_camera) {
            CHECK(x - camera == RIGHT_ANCHOR);
            ++right_moves;
            if (frame >= 180) ++right_return_moves;
        } else if (camera < old_camera) {
            CHECK(x - camera == LEFT_ANCHOR);
            ++left_moves;
        } else if (camera > 0 && x != old_x) {
            ++deadzone;
            CHECK(x - camera >= LEFT_ANCHOR);
            CHECK(x - camera <= RIGHT_ANCHOR);
        }
    }
    CHECK(right_moves > 20);
    CHECK(left_moves > 15);
    CHECK(right_return_moves > 20);
    CHECK(deadzone >= 5);
}

int main(void) {
    framewise_two_way_camera_and_latch();
    puts("STAGE169_CITY_CAMERA_LOCKSTEP_PASS: 240 numeric steps, two-sided tracking, hysteresis and render latch");
    return EXIT_SUCCESS;
}
