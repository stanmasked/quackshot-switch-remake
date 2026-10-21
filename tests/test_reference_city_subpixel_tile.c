#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage179: sub-0.01 px collisions in the authored CITY reference
 * model. An AABB with positive float-representable overlap must
 * collide; an AABB touching an exclusive tile edge must not.
 *
 * No original QuackShot tile or asset is used here.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage179 subpixel boundary FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { TILE_COL = 25, TILE_X = TILE_COL * QS_TILE,
       HERO_W = 12, FLOOR_PLAYER_Y = 188, ROOF_PLAYER_Y = 140 };

static const float penetrations[] = {
    0.0f,
    1.0f/4096.0f, 1.0f/2048.0f, 1.0f/1024.0f,
    1.0f/512.0f, 1.0f/256.0f, 1.0f/128.0f,
    1.0f/64.0f
};

static QsGame synthetic_scene(int roof) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    if (roof) {
        for (row = 10; row <= 12; ++row)
            g.terrain[row][TILE_COL] = '#';
    } else {
        g.terrain[12][TILE_COL] = '#';
    }
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 80;
    g.hero.y = FLOOR_PLAYER_Y;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void exact_and_micro_horizontal_contacts(void) {
    unsigned i;
    for (i = 0; i < sizeof(penetrations)/sizeof(penetrations[0]); ++i) {
        QsGame g = synthetic_scene(0);
        QsInput right = {0};
        const float epsilon = penetrations[i];
        const float contact = TILE_X - HERO_W;
        float expected;
        right.right = 1;
        /*
         * Initial proposed movement is exactly +0.15625 px. Put its
         * first candidate at the wall edge + epsilon.
         */
        g.hero.x = contact - 0.15625f + epsilon;
        CHECK(g.hero.x + HERO_W < (float)TILE_X);
        expected = g.hero.x;
        qs_step(&g, right);
        CHECK(g.mode == QS_STAGE && g.hero.grounded);
        CHECK(g.hero.y == FLOOR_PLAYER_Y);
        if (i == 0) {
            /* Touching exclusively at the wall edge is legal. */
            CHECK(g.hero.x == contact);
            CHECK(fabsf(g.hero.vx - 0.15625f) < 0.000001f);
        } else {
            /* Even 1/4096 px of positive overlap must be rejected.
             * Old 0.01px tile-edge inset misses all of these. */
            CHECK(g.hero.x == expected);
            CHECK(g.hero.vx == 0.0f);
        }
        CHECK(g.hero.x + HERO_W <= TILE_X + 0.000001f);
        CHECK(g.hero.hearts == 3);
    }
}

static void micro_landing_overhangs(void) {
    unsigned i;
    for (i = 0; i < sizeof(penetrations)/sizeof(penetrations[0]); ++i) {
        QsGame g = synthetic_scene(1);
        const float epsilon = penetrations[i];
        const float x = TILE_X - HERO_W + epsilon;
        int frame;
        g.hero.x = x;
        g.hero.y = 136.0f;
        g.hero.vy = 3.0f;
        g.hero.grounded = 0;
        g.reference_jump_steps = 16;
        for (frame = 0; frame < 20; ++frame) {
            QsInput idle = {0};
            qs_step(&g, idle);
            CHECK(g.mode == QS_STAGE);
            CHECK(g.hero.x == x);
            CHECK(g.hero.hearts == 3);
        }
        CHECK(g.hero.grounded);
        if (i == 0) {
            /* Exactly zero overlap: land on lower floor, not roof. */
            CHECK(g.hero.y == FLOOR_PLAYER_Y);
        } else {
            /* Any strictly positive representable overlap lands on roof. */
            CHECK(g.hero.y == ROOF_PLAYER_Y);
            CHECK(g.hero.vy == 0.0f);
            CHECK(x + HERO_W > TILE_X);
        }
    }
}

int main(void) {
    exact_and_micro_horizontal_contacts();
    micro_landing_overhangs();
    printf("STAGE179_CITY_SUBPIXEL_PASS horizontal=%zu vertical=%zu "
           "min_overlap=1/4096_px original_assets=none\n",
           sizeof(penetrations)/sizeof(penetrations[0]),
           sizeof(penetrations)/sizeof(penetrations[0]));
    return EXIT_SUCCESS;
}
