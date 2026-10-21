#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage178: verify jump-edge latching on a synthetic raised platform.
 * Holding the jump input across landing must not trigger a new jump.
 * Release+fresh press must enable exactly the next jump.
 *
 * The platform is independently authored, not an original ROM map.
 */
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "Stage178 landing jump edge FAIL line %d: %s\n", \
            __LINE__, #test); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    ROOF_LEFT = 25 * QS_TILE,
    ROOF_RIGHT = 33 * QS_TILE,
    ROOF_TOP = 10 * QS_TILE,
    ROOF_PLAYER_Y = ROOF_TOP - 20,
    HOLD_TICKS = 50,
    OFFSET_VARIANTS = 8
};

static QsGame course(int side, int fraction_index) {
    QsGame g;
    int row, col;
    const float overlap = (fraction_index + 1) / 8.0f;

    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    for (row = 10; row <= 12; ++row)
        for (col = 25; col <= 32; ++col)
            g.terrain[row][col] = '#';

    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;

    g.hero.x = side ? ROOF_RIGHT - overlap : ROOF_LEFT - 12.0f + overlap;
    g.hero.y = 115.0f;
    g.hero.vx = 0.0f;
    g.hero.vy = 3.0f;
    g.hero.grounded = 0;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    CHECK(g.hero.x < ROOF_RIGHT && g.hero.x + 12 > ROOF_LEFT);
    return g;
}

static void check_pair(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->stage == 0 && b->stage == 0);
    CHECK(a->hero.x == b->hero.x);
    CHECK(a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx);
    CHECK(a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->hero.hearts == b->hero.hearts);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static void step_both(QsGame *a, QsGame *b, int jump) {
    QsInput input = {0};
    const float old_camera = a->camera_x;
    input.jump = jump;
    qs_step(a, input);
    qs_step(b, input);
    check_pair(a, b);
    CHECK(a->render_camera_x == old_camera);
    CHECK(a->hero.hearts == 3);
    CHECK(a->hero.x < ROOF_RIGHT && a->hero.x + 12 > ROOF_LEFT);
    CHECK(a->hero.y <= ROOF_PLAYER_Y + 0.001f);
}

static void run_case(int side, int offset_index) {
    QsGame a = course(side, offset_index);
    QsGame b = course(side, offset_index);
    int frame, landed = 0, landed_second = 0;
    float roof_x = a.hero.x;

    /*
     * First held jump begins IN AIR. Even after landing, input.jump=1
     * cannot become a fresh edge; the hero must stay on the platform.
     */
    for (frame = 0; frame < HOLD_TICKS; ++frame) {
        step_both(&a, &b, 1);
        if (a.hero.grounded && a.hero.y == ROOF_PLAYER_Y)
            landed = 1;
        if (landed) {
            CHECK(a.hero.grounded);
            CHECK(a.hero.y == ROOF_PLAYER_Y);
            CHECK(a.hero.vy == 0.0f);
        }
    }
    CHECK(landed);
    CHECK(a.previous.jump == 1);
    CHECK(a.hero.x == roof_x);

    /* A release clears the edge latch without moving the hero. */
    step_both(&a, &b, 0);
    CHECK(a.hero.grounded && a.hero.y == ROOF_PLAYER_Y);
    CHECK(a.previous.jump == 0);

    /* The NEXT rising edge must initiate a jump immediately. */
    step_both(&a, &b, 1);
    CHECK(!a.hero.grounded);
    CHECK(a.hero.vy < 0.0f);
    CHECK(a.hero.y < ROOF_PLAYER_Y);

    /* Release early: a short jump returns to the SAME roof. */
    for (frame = 0; frame < 80; ++frame) {
        step_both(&a, &b, 0);
        if (a.hero.grounded && a.hero.y == ROOF_PLAYER_Y)
            landed_second = 1;
        if (landed_second) {
            CHECK(a.hero.grounded);
            CHECK(a.hero.y == ROOF_PLAYER_Y);
            CHECK(a.hero.vy == 0.0f);
        }
    }
    CHECK(landed_second);
    CHECK(a.previous.jump == 0);

    /* A second fresh edge after the new landing must work as well. */
    step_both(&a, &b, 1);
    CHECK(!a.hero.grounded);
    CHECK(a.hero.vy < 0.0f);
    CHECK(a.hero.y < ROOF_PLAYER_Y);
}

int main(void) {
    int side, i, cases = 0;
    for (side = 0; side < 2; ++side)
        for (i = 0; i < OFFSET_VARIANTS; ++i) {
            run_case(side, i);
            ++cases;
        }
    CHECK(cases == 16);
    printf("STAGE178_LANDING_JUMP_EDGE_PASS cases=%d held_input=%d "
           "original_assets=none\n", cases, HOLD_TICKS);
    return EXIT_SUCCESS;
}
