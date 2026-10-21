#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage174: top-surface landing and edge-overlap regression.
 * The platform is an independently authored tile fixture, not the
 * original QuackShot map. A 12x20 hero must land on its top at y=140.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage174 platform landing FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    PLATFORM_FIRST_COL = 25,
    PLATFORM_LAST_COL = 32,
    PLATFORM_LEFT = PLATFORM_FIRST_COL * QS_TILE,
    PLATFORM_RIGHT = (PLATFORM_LAST_COL + 1) * QS_TILE,
    PLATFORM_TOP_ROW = 10,
    PLATFORM_TOP = PLATFORM_TOP_ROW * QS_TILE,
    FLOOR_Y = 208,
    LAND_Y = PLATFORM_TOP - 20,
    JUMP_HOLD = 50,
    TOTAL_JUMP_TICKS = 100,
    EDGE_CASES = 4
};

static QsGame new_course(void) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE);
    CHECK(g.physics_profile == QS_PHYSICS_REFERENCE_CITY);

    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    for (row = PLATFORM_TOP_ROW; row <= 12; ++row)
        for (col = PLATFORM_FIRST_COL; col <= PLATFORM_LAST_COL; ++col)
            g.terrain[row][col] = '#';

    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 80.0f;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void compare(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == b->mode && a->stage == b->stage);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->hero.hearts == b->hero.hearts);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static int overlaps_platform(float x) {
    return x < (float)PLATFORM_RIGHT && x + 12.0f > (float)PLATFORM_LEFT;
}

static void check_no_side_tunnelling(const QsGame *g) {
    /* When overlapping the platform horizontally, a falling player
     * may be above it or snapped exactly to its top, never inside it. */
    if (overlaps_platform(g->hero.x) && g->hero.y > (float)LAND_Y + 0.001f &&
        g->hero.y < (float)FLOOR_Y - 0.001f)
        CHECK(0);
}

static void natural_jump_lands_on_top(int direction, int horizontal_hold,
                                      int edge_index) {
    const float fraction = edge_index * 0.25f;
    QsGame a = new_course();
    QsGame b = new_course();
    int tick, landing_tick = -1;
    float start_x;

    if (direction > 0)
        start_x = (float)PLATFORM_LEFT - 12.0f - fraction;
    else
        start_x = (float)PLATFORM_RIGHT + fraction;
    a.hero.x = b.hero.x = start_x;

    for (tick = 0; tick < TOTAL_JUMP_TICKS; ++tick) {
        QsInput in = {0};
        const float old_camera = a.camera_x;
        in.jump = tick < JUMP_HOLD;
        if (tick < horizontal_hold) {
            if (direction > 0) in.right = 1;
            else in.left = 1;
        }
        qs_step(&a, in);
        qs_step(&b, in);
        compare(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.hearts == 3);
        CHECK(a.hero.y < 209.0f);
        check_no_side_tunnelling(&a);

        if (tick > 0 && a.hero.grounded &&
            fabsf(a.hero.y - (float)LAND_Y) < 0.001f &&
            landing_tick < 0) {
            landing_tick = tick;
            CHECK(overlaps_platform(a.hero.x));
        }
        /* The character must not tunnel through the platform or land
         * on the lower floor while still horizontally overlapping it. */
        if (landing_tick >= 0 && tick > landing_tick) {
            CHECK(a.hero.y == (float)LAND_Y);
            CHECK(a.hero.grounded);
        }
    }

    CHECK(landing_tick >= 20 && landing_tick < JUMP_HOLD);
    CHECK(a.hero.y == (float)LAND_Y);
    CHECK(a.hero.grounded);
    CHECK(overlaps_platform(a.hero.x));
    CHECK(a.hero.x >= (float)PLATFORM_LEFT - 12.0f);
    CHECK(a.hero.x <= (float)PLATFORM_RIGHT);
    CHECK(a.hero.vx == 0.0f);
}

static void direct_fractional_edge_drop(int edge_index, int right_edge) {
    const float fraction = (edge_index + 1) * 0.25f;
    QsGame a = new_course();
    QsGame b = new_course();
    int tick, landed = 0;

    /* Exercise the minimum positive overlap on each top edge. */
    if (!right_edge)
        a.hero.x = b.hero.x = (float)PLATFORM_LEFT - 12.0f + fraction;
    else
        a.hero.x = b.hero.x = (float)PLATFORM_RIGHT - fraction;
    a.hero.y = b.hero.y = 118.0f;
    a.hero.vx = b.hero.vx = 0.0f;
    a.hero.vy = b.hero.vy = 3.0f;
    a.hero.grounded = b.hero.grounded = 0;
    a.reference_jump_steps = b.reference_jump_steps = QS_CITY_MAX_JUMP_HOLD + 1;

    for (tick = 0; tick < 12; ++tick) {
        QsInput in = {0};
        const float old_camera = a.camera_x;
        qs_step(&a, in);
        qs_step(&b, in);
        compare(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.hearts == 3);
        if (a.hero.grounded && fabsf(a.hero.y - (float)LAND_Y) < 0.001f) {
            landed = 1;
            break;
        }
    }
    CHECK(landed);
    CHECK(a.hero.y == (float)LAND_Y);
    CHECK(a.hero.grounded);
    CHECK(overlaps_platform(a.hero.x));
    CHECK(a.hero.vy == 0.0f);
}

int main(void) {
    int dir, edge, movement_hold, natural_cases = 0, edge_drops = 0;
    const int hold_steps[] = {24, 32, 40, 50};

    for (dir = -1; dir <= 1; dir += 2) {
        for (edge = 0; edge < EDGE_CASES; ++edge) {
            for (movement_hold = 0; movement_hold < 4; ++movement_hold) {
                natural_jump_lands_on_top(dir, hold_steps[movement_hold], edge);
                ++natural_cases;
            }
            direct_fractional_edge_drop(edge, 0);
            direct_fractional_edge_drop(edge, 1);
            edge_drops += 2;
        }
    }
    CHECK(natural_cases == 32);
    CHECK(edge_drops == 8);
    printf("STAGE174_PLATFORM_TOP_LANDING_PASS jump_traces=%d "
           "fractional_edge_drops=%d original_assets=none\n",
           natural_cases, edge_drops);
    return EXIT_SUCCESS;
}
