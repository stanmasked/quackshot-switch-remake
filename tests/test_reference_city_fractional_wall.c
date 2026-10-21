#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage172: original-asset-free fractional wall contact regression.
 *
 * The measured first-city game-state coordinates have 16 fractional
 * bits, but this independently authored tile fixture is not the
 * original collision geometry. This test validates NO TUNNELLING and
 * recoverable motion at varied fractional positions and tile columns.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage172 fractional wall FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { WALL_COL_FIRST = 22, WALL_COL_LAST = 46, WALL_COL_STEP = 8,
       FRACTION_COUNT = 8 };

static QsGame synthetic_wall(int column, float fraction) {
    QsGame g;
    int r, c;
    const float tile_x = column * (float)QS_TILE;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);

    for (r = 0; r < QS_ROWS; ++r)
        for (c = 0; c < QS_COLUMNS; ++c)
            g.terrain[r][c] = r >= 13 ? '#' : '.';
    g.terrain[12][column] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = tile_x - 42.0f + fraction;
    g.hero.y = 188.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = 0.0f;
    g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void assert_same(const QsGame *a, const QsGame *b) {
    CHECK(a->hero.x == b->hero.x);
    CHECK(a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx);
    CHECK(a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
}

static void run_case(int column, int fractional_eighth) {
    const float wall = column * (float)QS_TILE;
    const float fraction = fractional_eighth / 8.0f;
    const float contact = wall - 12.0f;
    QsGame a = synthetic_wall(column, fraction);
    QsGame b = synthetic_wall(column, fraction);
    float first_stop, before_return;
    unsigned t;
    int had_block = 0, had_left_motion = 0, had_second_block = 0;

    for (t = 0; t < 100; ++t) {
        QsInput in = {0};
        float camera = a.camera_x;
        in.right = 1;
        qs_step(&a, in);
        qs_step(&b, in);
        assert_same(&a, &b);
        CHECK(a.render_camera_x == camera);
        CHECK(a.hero.grounded && a.hero.y == 188.0f);
        CHECK(a.hero.x <= contact + 0.0001f);
        CHECK(a.hero.x >= wall - 42.0f + fraction);
        if (a.hero.vx == 0.0f && t > 12) had_block = 1;
    }
    first_stop = a.hero.x;
    CHECK(had_block);
    CHECK(first_stop > contact - 1.6251f);
    CHECK(first_stop <= contact + 0.0001f);
    CHECK(a.hero.vx == 0.0f);

    for (t = 0; t < 24; ++t) {
        QsInput in = {0};
        float camera = a.camera_x;
        in.left = 1;
        qs_step(&a, in);
        qs_step(&b, in);
        assert_same(&a, &b);
        CHECK(a.render_camera_x == camera);
        CHECK(a.hero.y == 188.0f && a.hero.grounded);
        CHECK(a.hero.x <= first_stop + 0.0001f);
        if (a.hero.vx < -0.0001f) had_left_motion = 1;
    }
    CHECK(had_left_motion);
    CHECK(a.hero.vx == -1.625f);
    CHECK(a.hero.x < first_stop - 15.0f);
    before_return = a.hero.x;

    for (t = 0; t < 90; ++t) {
        QsInput in = {0};
        float camera = a.camera_x;
        in.right = 1;
        qs_step(&a, in);
        qs_step(&b, in);
        assert_same(&a, &b);
        CHECK(a.render_camera_x == camera);
        CHECK(a.hero.y == 188.0f && a.hero.grounded);
        CHECK(a.hero.x <= contact + 0.0001f);
        if (a.hero.vx == 0.0f && t > 25) had_second_block = 1;
    }
    CHECK(before_return < first_stop);
    CHECK(had_second_block);
    CHECK(a.hero.x > contact - 1.6251f);
    CHECK(a.hero.x <= contact + 0.0001f);
    CHECK(a.hero.vx == 0.0f);
}

int main(void) {
    int col, f, variants = 0;
    for (col = WALL_COL_FIRST; col <= WALL_COL_LAST; col += WALL_COL_STEP) {
        for (f = 0; f < FRACTION_COUNT; ++f) {
            run_case(col, f);
            ++variants;
        }
    }
    CHECK(variants == 32);
    printf("STAGE172_FRACTIONAL_WALL_PASS cases=%d fraction_steps=1/8 "
           "original_assets=none\n", variants);
    return EXIT_SUCCESS;
}
