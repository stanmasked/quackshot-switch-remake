#include "qs_game.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage182: combined XY/camera/Jump numeric lockstep against an
 * independent integer 16.16 oracle on an independently authored
 * obstacle-free CITY TEST surface.
 *
 * This is regression coverage of our opt-in model, NOT a replay of
 * original ROM frames or the recovered original 68000 algorithm.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage182 combined lockstep FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

#define ONE INT64_C(65536)
#define ACCEL INT64_C(10240)
#define MAX_WALK INT64_C(106496)
#define IMPULSE (-INT64_C(192000))
#define GRAVITY INT64_C(12800)
#define MAX_DOWN INT64_C(491520)
#define MAX_UP (-INT64_C(524288))
#define FLOOR_Y (INT64_C(188) * ONE)
#define LEFT_BAND (INT64_C(123) * ONE)
#define RIGHT_BAND (INT64_C(141) * ONE)
#define MAX_CAMERA ((int64_t)(QS_COLUMNS * QS_TILE - QS_VIEW_W) * ONE)
#define TEST_TICKS 360

typedef struct {
    int64_t x, y, vx, vy, camera;
    int grounded, jump_steps, previous_jump;
    int jump_starts, landing_count, camera_left, camera_right;
    int64_t min_y;
} Oracle;

static float pixels(int64_t raw) {
    return (float)((double)raw / (double)ONE);
}

static int approach_dir(int frame) {
    if (frame < 110) return 1;
    if (frame < 120) return 0;
    if (frame < 175) return -1;
    if (frame < 190) return 0;
    if (frame < 250) return 1;
    if (frame < 260) return 0;
    if (frame < 320) return -1;
    return 1;
}

static int jump_held(int frame) {
    return frame == 20 || (frame >= 135 && frame < 143) ||
           (frame >= 255 && frame < 305);
}

static int64_t clamp64(int64_t n, int64_t lo, int64_t hi) {
    return n < lo ? lo : (n > hi ? hi : n);
}

static int64_t approach64(int64_t n, int64_t target) {
    if (n < target) return n + ACCEL < target ? n + ACCEL : target;
    if (n > target) return n - ACCEL > target ? n - ACCEL : target;
    return n;
}

static Oracle oracle_start(int initial_x) {
    Oracle o = {0};
    o.x = (int64_t)initial_x * ONE;
    o.y = FLOOR_Y;
    o.min_y = FLOOR_Y;
    o.grounded = 1;
    return o;
}

static void oracle_tick(Oracle *o, int frame) {
    const int dir = approach_dir(frame);
    const int held = jump_held(frame);
    const int was_grounded = o->grounded;
    const int64_t old_camera = o->camera;
    int64_t screen;

    o->vx = approach64(o->vx, (int64_t)dir * MAX_WALK);
    o->x += o->vx;

    if (held && !o->previous_jump && o->grounded) {
        o->vy = IMPULSE;
        o->jump_steps = 0;
        o->grounded = 0;
        ++o->jump_starts;
    }
    if (!(held && o->vy < 0 && o->jump_steps < 15))
        o->vy = clamp64(o->vy + GRAVITY, MAX_UP, MAX_DOWN);
    if (o->jump_steps < 16) ++o->jump_steps;
    o->y += o->vy;
    o->grounded = 0;
    if (o->vy > 0 && o->y > FLOOR_Y) {
        o->y = FLOOR_Y;
        o->vy = 0;
        o->grounded = 1;
        o->jump_steps = 0;
        if (!was_grounded) ++o->landing_count;
    }
    if (o->y < o->min_y) o->min_y = o->y;

    screen = o->x - o->camera;
    if (screen > RIGHT_BAND) o->camera = o->x - RIGHT_BAND;
    else if (screen < LEFT_BAND) o->camera = o->x - LEFT_BAND;
    o->camera = clamp64(o->camera, 0, MAX_CAMERA);
    if (o->camera > old_camera) ++o->camera_right;
    if (o->camera < old_camera) ++o->camera_left;
    o->previous_jump = held;
}

static QsGame flat_course(int initial_x) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = (float)initial_x;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void equal_live(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == b->mode && a->stage == b->stage);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
    CHECK(a->previous.jump == b->previous.jump);
}

static void run_case(int initial_x) {
    QsGame a = flat_course(initial_x), b = flat_course(initial_x);
    Oracle oracle = oracle_start(initial_x);
    int frame, prev_live_grounded = 1;
    int live_starts = 0, live_landings = 0, simultaneous_air = 0;
    int64_t before_render;
    float max_xy_error = 0.0f;

    for (frame = 0; frame < TEST_TICKS; ++frame) {
        QsInput input = {0};
        int dir = approach_dir(frame), held = jump_held(frame);
        int fresh = held && !a.previous.jump;
        float difference;
        input.right = dir > 0;
        input.left = dir < 0;
        input.jump = held;
        before_render = oracle.camera;
        oracle_tick(&oracle, frame);
        qs_step(&a, input);
        qs_step(&b, input);
        equal_live(&a, &b);
        CHECK(a.mode == QS_STAGE && a.hero.hearts == 3);
        CHECK(a.hero.y <= 188.0f + 0.001f);
        CHECK(a.hero.x >= 1.0f && a.hero.x < 1200.0f);
        CHECK(fabsf(a.hero.x - pixels(oracle.x)) <= 0.065f);
        CHECK(fabsf(a.hero.vx - pixels(oracle.vx)) <= 1.0f / 4096.0f);
        CHECK(fabsf(a.camera_x - pixels(oracle.camera)) <= 0.065f);
        CHECK(fabsf(a.render_camera_x - pixels(before_render)) <= 0.065f);
        CHECK(a.render_camera_x == b.render_camera_x);
        CHECK(a.previous.jump == held);

        if (fresh && prev_live_grounded) {
            CHECK(!a.hero.grounded && a.hero.vy < 0.0f);
            ++live_starts;
        }
        if (!prev_live_grounded && a.hero.grounded)
            ++live_landings;
        if (!a.hero.grounded && !oracle.grounded) {
            difference = fabsf(a.hero.y - pixels(oracle.y));
            CHECK(difference <= 0.065f);
            CHECK(fabsf(a.hero.vy - pixels(oracle.vy)) <= 1.0f / 2048.0f);
            if (difference > max_xy_error) max_xy_error = difference;
            ++simultaneous_air;
        }
        prev_live_grounded = a.hero.grounded;
    }

    CHECK(live_starts == 3 && live_landings == 3);
    CHECK(oracle.jump_starts == 3 && oracle.landing_count == 3);
    CHECK(oracle.camera_right > 60 && oracle.camera_left > 60);
    CHECK(simultaneous_air > 100);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(oracle.grounded && oracle.y == FLOOR_Y);
    CHECK(max_xy_error <= 0.065f);

    printf("STAGE182_COMBINED_Q16_PASS start_x=%d frames=%d jumps=%d "
           "landings=%d cam_right=%d cam_left=%d "
           "max_air_y_delta=%.6f original_assets=none\n",
           initial_x, TEST_TICKS, live_starts, live_landings,
           oracle.camera_right, oracle.camera_left, max_xy_error);
}

int main(void) {
    run_case(61);
    run_case(396);
    puts("STAGE182_MOTION_CAMERA_INTEGRATION_PASS cases=2 frames_per_case=360");
    return EXIT_SUCCESS;
}
