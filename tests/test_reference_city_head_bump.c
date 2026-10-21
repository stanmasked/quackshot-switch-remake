#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage175: original-free ceiling/head-bump regression.
 * The suspended ceiling and its fractional edge overlaps are synthetic;
 * the test protects our movement/collision implementation only.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage175 head-bump FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    CEILING_FIRST_COL = 25,
    CEILING_LAST_COL = 32,
    CEILING_LEFT = CEILING_FIRST_COL * QS_TILE,
    CEILING_RIGHT = (CEILING_LAST_COL + 1) * QS_TILE,
    CEILING_FIRST_ROW = 8,
    CEILING_LAST_ROW = 9,
    CEILING_BOTTOM = (CEILING_LAST_ROW + 1) * QS_TILE,
    FLOOR_Y = 208,
    TRACE_TICKS = 80,
    OFFSET_COUNT = 4
};

static QsGame new_scene(void) {
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
    for (row = CEILING_FIRST_ROW; row <= CEILING_LAST_ROW; ++row)
        for (col = CEILING_FIRST_COL; col <= CEILING_LAST_COL; ++col)
            g.terrain[row][col] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 448.0f;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
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

static float start_x(int position, int offset_index) {
    const float fraction = (float)(offset_index + 1) * 0.25f;
    if (position == 0)
        return (float)CEILING_LEFT - 12.0f + fraction;
    if (position == 1)
        return 448.0f + fraction;
    return (float)CEILING_RIGHT - fraction;
}

static void run_case(unsigned hold, int position, int offset_index) {
    QsGame a = new_scene();
    QsGame b = new_scene();
    int tick, head_contact = 0, landed = 0;
    float min_y = a.hero.y;
    const float x = start_x(position, offset_index);
    a.hero.x = b.hero.x = x;

    /* Every start must overlap the ceiling by a positive width. */
    CHECK(x < (float)CEILING_RIGHT && x + 12.0f > (float)CEILING_LEFT);
    for (tick = 0; tick < TRACE_TICKS; ++tick) {
        QsInput in = {0};
        const float old_camera = a.camera_x;
        in.jump = (unsigned)tick < hold;
        qs_step(&a, in);
        qs_step(&b, in);
        same_state(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.hearts == 3);
        CHECK(a.hero.x == x);
        CHECK(a.hero.y >= (float)CEILING_BOTTOM - 0.001f);
        CHECK(a.hero.y < (float)FLOOR_Y + 0.001f);
        if (a.hero.y < min_y) min_y = a.hero.y;
        if (!a.hero.grounded && a.hero.vy == 0.0f &&
            a.hero.y < (float)CEILING_BOTTOM + 4.0f)
            head_contact = 1;
        if (tick > 0 && a.hero.grounded && fabsf(a.hero.y - 188.0f) < 0.001f)
            landed = 1;
    }

    if (hold == 1) {
        CHECK(!head_contact);
        CHECK(min_y > (float)CEILING_BOTTOM + 4.0f);
    } else {
        CHECK(head_contact);
        CHECK(min_y < (float)CEILING_BOTTOM + 4.0f);
    }
    CHECK(landed);
    CHECK(a.hero.grounded);
    CHECK(a.hero.y == 188.0f);
    CHECK(a.hero.vy == 0.0f);
}

int main(void) {
    const unsigned holds[] = {1, 8, 50};
    int position, offset;
    unsigned h;
    int cases = 0;
    for (position = 0; position < 3; ++position) {
        for (offset = 0; offset < OFFSET_COUNT; ++offset) {
            for (h = 0; h < sizeof(holds)/sizeof(holds[0]); ++h) {
                run_case(holds[h], position, offset);
                ++cases;
            }
        }
    }
    CHECK(cases == 36);
    printf("STAGE175_HEAD_BUMP_PASS traces=%d holds=1,8,50 "
           "edge_and_center_positions=3 original_assets=none\n", cases);
    return EXIT_SUCCESS;
}
