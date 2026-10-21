#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage176: diagonally approach/leave synthetic ceiling corners.
 *
 * AABB non-overlap is an independent geometric invariant; replay
 * equality is only an auxiliary determinism check. No original
 * stage geometry, sprites or ROM are embedded.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage176 corner FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    CEILING_LEFT = 25 * QS_TILE,
    CEILING_RIGHT = 33 * QS_TILE,
    CEILING_TOP = 8 * QS_TILE,
    CEILING_BOTTOM = 10 * QS_TILE,
    FLOOR_PLAYER_Y = 188,
    TOP_PLAYER_Y = CEILING_TOP - 20,
    TRACE_TICKS = 120,
    CORNER_OFFSETS = 4
};

static QsGame course(void) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    for (row = 8; row <= 9; ++row)
        for (col = 25; col <= 32; ++col)
            g.terrain[row][col] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.pickups, 0, sizeof(g.pickups));
    memset(g.shots, 0, sizeof(g.shots));
    g.city_crate_active = 0;
    g.hero.x = 64.0f;
    g.hero.y = FLOOR_PLAYER_Y;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void no_solid_overlap(const QsPlayer *h) {
    const float margin = 0.001f;
    int intersects_horizontal =
        h->x < (float)CEILING_RIGHT - margin &&
        h->x + 12.0f > (float)CEILING_LEFT + margin;
    int intersects_vertical =
        h->y < (float)CEILING_BOTTOM - margin &&
        h->y + 20.0f > (float)CEILING_TOP + margin;
    CHECK(!(intersects_horizontal && intersects_vertical));
}

static void same_state(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == b->mode && a->stage == b->stage);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->hero.hearts == b->hero.hearts);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static void run_case(int side, int inward, int offset, unsigned jump_hold) {
    QsGame a = course();
    QsGame b = course();
    const float fraction = (float)(offset + 1) * 0.25f;
    int frame, direction, saw_air = 0, saw_ground_after = 0;
    float start_x;

    /*
     * side 0: left edge, side 1: right edge
     * inward 1: start outside edge and travel beneath ceiling
     * inward 0: start just inside edge and travel outward
     */
    if (!side) {
        start_x = inward ? CEILING_LEFT - 12.0f - fraction :
                           CEILING_LEFT - 12.0f + fraction;
        direction = inward ? +1 : -1;
    } else {
        start_x = inward ? CEILING_RIGHT + fraction :
                           CEILING_RIGHT - fraction;
        direction = inward ? -1 : +1;
    }
    a.hero.x = b.hero.x = start_x;
    no_solid_overlap(&a.hero);

    for (frame = 0; frame < TRACE_TICKS; ++frame) {
        QsInput input = {0};
        const float old_camera = a.camera_x;
        input.jump = (unsigned)frame < jump_hold;
        if (frame < 54) {
            input.right = direction > 0;
            input.left = direction < 0;
        }

        qs_step(&a, input);
        qs_step(&b, input);
        same_state(&a, &b);
        CHECK(a.mode == QS_STAGE);
        CHECK(a.render_camera_x == old_camera);
        CHECK(isfinite(a.hero.x) && isfinite(a.hero.y));
        CHECK(isfinite(a.hero.vx) && isfinite(a.hero.vy));
        CHECK(a.hero.hearts == 3);
        CHECK(a.hero.x >= 0.0f && a.hero.x < QS_COLUMNS * QS_TILE);
        CHECK(a.hero.y >= 0.0f && a.hero.y <= FLOOR_PLAYER_Y + 0.001f);
        no_solid_overlap(&a.hero);
        if (!a.hero.grounded) saw_air = 1;
        if (saw_air && a.hero.grounded) saw_ground_after = 1;
    }
    CHECK(saw_air);
    CHECK(saw_ground_after);
    CHECK(a.hero.grounded);
    CHECK(a.hero.vy == 0.0f);
    /* Depending on whether movement cleared a corner, the hero may
     * end on the synthetic roof or the synthetic floor, never inside. */
    CHECK(fabsf(a.hero.y - FLOOR_PLAYER_Y) < 0.001f ||
          fabsf(a.hero.y - TOP_PLAYER_Y) < 0.001f);
}

int main(void) {
    const unsigned holds[] = {1, 8, 50};
    int side, inward, offset, cases = 0;
    unsigned h;
    for (side = 0; side < 2; ++side)
        for (inward = 0; inward < 2; ++inward)
            for (offset = 0; offset < CORNER_OFFSETS; ++offset)
                for (h = 0; h < sizeof(holds)/sizeof(holds[0]); ++h) {
                    run_case(side, inward, offset, holds[h]);
                    ++cases;
                }
    CHECK(cases == 48);
    printf("STAGE176_DIAGONAL_CEILING_PASS traces=%d hold=1,8,50 "
           "original_assets=none\n", cases);
    return EXIT_SUCCESS;
}
