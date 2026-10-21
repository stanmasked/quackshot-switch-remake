#include "qs_game.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage181: independently calculate a 16.16 jump envelope and compare
 * with the actual opt-in CITY TEST vertical simulation. Source data
 * are public numeric observation summaries, never original ROM bytes.
 *
 * Integer oracle models ideal continuous 16.16 motion and a level
 * floor. Float prototype subdivides each displacement for collision;
 * small y deviations are expected, not a claim of ROM exactness.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage181 jump oracle FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

#define Q16 INT64_C(65536)
#define IMPULSE (-INT64_C(192000))
#define GRAVITY INT64_C(12800)
#define FLOOR_TOP 208
#define HERO_HEIGHT 20
#define GROUND_Y ((FLOOR_TOP - HERO_HEIGHT) * Q16)
#define MAX_FALL (INT64_C(491520))
#define MAX_HOLD 15
#define TICKS 100

typedef struct {
    int64_t y, vy, apex;
    int grounded, steps, landing_tick;
} Oracle;

static QsGame flat_city(int start_x) {
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
    g.hero.x = (float)start_x;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static Oracle initial_oracle(void) {
    Oracle o;
    o.y = GROUND_Y;
    o.vy = 0;
    o.apex = GROUND_Y;
    o.grounded = 1;
    o.steps = 0;
    o.landing_tick = -1;
    return o;
}

static void oracle_step(Oracle *o, int tick, int hold) {
    /* A fresh rising edge exists only at tick zero. */
    if (tick == 0 && o->grounded) {
        o->vy = IMPULSE;
        o->steps = 0;
        o->grounded = 0;
    }
    if (!(tick < hold && o->vy < 0 && o->steps < MAX_HOLD)) {
        o->vy += GRAVITY;
        if (o->vy > MAX_FALL) o->vy = MAX_FALL;
        if (o->vy < -INT64_C(524288)) o->vy = -INT64_C(524288);
    }
    if (o->steps < MAX_HOLD + 1) ++o->steps;

    o->y += o->vy;
    o->grounded = 0;
    /* The 16.16 oracle is mathematical, not the tiled float engine.
     * At exact equality the AABB only touches the floor; true
     * collision requires a positive overlap. */
    if (o->vy > 0 && o->y > GROUND_Y) {
        o->y = GROUND_Y;
        o->vy = 0;
        o->grounded = 1;
        o->steps = 0;
        if (o->landing_tick < 0) o->landing_tick = tick + 1;
    }
    if (o->y < o->apex) o->apex = o->y;
}

static void same_state(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->hero.hearts == b->hero.hearts);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static void run_case(int start_x, int hold) {
    QsGame a = flat_city(start_x), b = flat_city(start_x);
    Oracle o = initial_oracle();
    int tick, actual_land = -1, was_air = 0, airborne_comparisons = 0;
    float actual_apex = a.hero.y;
    float max_air_y_error = 0;

    for (tick = 0; tick < TICKS; ++tick) {
        QsInput input = {0};
        const float old_camera = a.camera_x;
        input.jump = tick < hold;
        oracle_step(&o, tick, hold);
        qs_step(&a, input);
        qs_step(&b, input);
        same_state(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.x == (float)start_x);
        CHECK(a.hero.hearts == 3);
        CHECK(a.hero.y <= 188.0001f);

        if (a.hero.y < actual_apex) actual_apex = a.hero.y;
        if (!a.hero.grounded) was_air = 1;
        if (was_air && actual_land < 0 && a.hero.grounded)
            actual_land = tick + 1;

        /* Only compare two airborne numerical states: landing events
         * can differ by one sample due to float tile substep order. */
        if (!a.hero.grounded && !o.grounded) {
            const float ideal_y = (float)((double)o.y / (double)Q16);
            const float ideal_vy = (float)((double)o.vy / (double)Q16);
            float y_error = fabsf(a.hero.y - ideal_y);
            CHECK(fabsf(a.hero.vy - ideal_vy) <= 1.0f/2048.0f);
            CHECK(y_error <= 1.0f/16.0f);
            if (y_error > max_air_y_error) max_air_y_error = y_error;
            ++airborne_comparisons;
        }
    }
    CHECK(was_air);
    CHECK(airborne_comparisons >= 20);
    CHECK(actual_land > 20 && actual_land < 80);
    CHECK(o.landing_tick > 20 && o.landing_tick < 80);
    CHECK(abs(actual_land - o.landing_tick) <= 2);
    CHECK(fabsf(actual_apex -
                (float)((double)o.apex / (double)Q16)) <= 1.0f/8.0f);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(o.grounded && o.y == GROUND_Y);
    CHECK(a.hero.vy == 0.0f && o.vy == 0);

    printf("STAGE181_JUMP hold=%d start_x=%d oracle_rise=%.6f "
           "actual_rise=%.6f oracle_land=%d actual_land=%d "
           "max_air_y_error=%.6f\n",
           hold, start_x,
           188.0 - (double)o.apex / (double)Q16,
           188.0f - actual_apex, o.landing_tick, actual_land,
           max_air_y_error);
}

int main(void) {
    static const int holds[] = {1, 8, 15, 50};
    static const int x_positions[] = {61, 396};
    unsigned i, j;
    for (i = 0; i < sizeof(x_positions)/sizeof(x_positions[0]); ++i)
        for (j = 0; j < sizeof(holds)/sizeof(holds[0]); ++j)
            run_case(x_positions[i], holds[j]);
    puts("STAGE181_VERTICAL_Q16_ORACLE_PASS cases=8 original_assets=none");
    return EXIT_SUCCESS;
}
