#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage171: 20 deterministic geometry perturbations of an original-free
 * ledge/drop/wall scene. The tile positions are authored, NOT extracted
 * from QuackShot; only the 32px drop resembles a reported original
 * first-city observation. Tests protect our collision invariants.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage171 boundary FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { MIN_EDGE_COL = 23, MAX_EDGE_COL = 27,
       MIN_WALL_COL = 31, MAX_WALL_COL = 34 };

typedef struct {
    int fall, land, wall;
    float fall_x, land_x, wall_x;
    float reverse_x12, reverse_x32;
} Events;

static QsGame course(int edge_col, int wall_col) {
    QsGame g;
    int row, col;
    CHECK(edge_col >= MIN_EDGE_COL && edge_col <= MAX_EDGE_COL);
    CHECK(wall_col >= MIN_WALL_COL && wall_col <= MAX_WALL_COL);
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);

    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col) {
        g.terrain[13][col] = '#';
        g.terrain[14][col] = '#';
        if (col < edge_col) {
            g.terrain[11][col] = '#';
            g.terrain[12][col] = '#';
        }
    }
    g.terrain[12][wall_col] = '#';
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

static void same_state(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == b->mode && a->stage == b->stage);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static Events run(int edge_col, int wall_col) {
    QsGame a = course(edge_col, wall_col);
    QsGame b = course(edge_col, wall_col);
    Events e;
    int tick;
    float wall_camera;
    memset(&e, 0, sizeof(e));
    e.fall = e.land = e.wall = -1;
    for (tick = 0; tick < 350; ++tick) {
        QsInput right = {0};
        float old_camera = a.camera_x;
        right.right = 1;
        qs_step(&a, right);
        qs_step(&b, right);
        same_state(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.mode == QS_STAGE && a.hero.hearts == 3);

        if (e.fall < 0 && a.hero.y > 156.01f) {
            e.fall = tick;
            e.fall_x = a.hero.x;
        }
        if (e.land < 0 && e.fall >= 0 &&
            a.hero.grounded && fabsf(a.hero.y - 188.0f) < 0.001f) {
            e.land = tick;
            e.land_x = a.hero.x;
        }
        if (e.wall < 0 && e.land >= 0 &&
            a.hero.x > (float)wall_col * QS_TILE - 30.0f &&
            a.hero.vx == 0.0f) {
            e.wall = tick;
            e.wall_x = a.hero.x;
        }
    }

    CHECK(e.fall >= 0 && e.fall < e.land);
    CHECK(e.land < e.wall && e.wall < 350);
    CHECK(e.fall_x < e.land_x && e.land_x < e.wall_x);
    CHECK(e.fall_x >= (edge_col * QS_TILE) - 12.1f);
    CHECK(e.fall_x < edge_col * QS_TILE + 2.0f);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(a.hero.vx == 0.0f);
    CHECK(a.hero.x <= (float)wall_col * QS_TILE - 12.0f + 0.02f);
    CHECK(a.hero.x >= (float)wall_col * QS_TILE - 13.0f);
    CHECK(a.camera_x > 0.0f);
    wall_camera = a.camera_x;

    /* A left turn first frees the hero from the wall. Camera scroll
     * must recover only AFTER the actor traverses the measured
     * synthetic 141-to-123 screen-space deadzone. */
    for (tick = 0; tick < 32; ++tick) {
        QsInput left = {0};
        float old_camera = a.camera_x;
        left.left = 1;
        qs_step(&a, left);
        qs_step(&b, left);
        same_state(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.mode == QS_STAGE);
        if (tick == 11) {
            CHECK(a.hero.vx < 0.0f);
            CHECK(a.camera_x == wall_camera);
            e.reverse_x12 = a.hero.x;
        }
        if (tick == 31) e.reverse_x32 = a.hero.x;
    }
    CHECK(e.reverse_x12 < e.wall_x - 5.0f);
    CHECK(e.reverse_x32 < e.reverse_x12 - 20.0f);
    CHECK(a.camera_x < wall_camera);
    CHECK(a.hero.x - a.camera_x >= 122.0f);
    CHECK(a.hero.x - a.camera_x <= 124.0f);
    return e;
}

int main(void) {
    Events table[MAX_EDGE_COL - MIN_EDGE_COL + 1]
                [MAX_WALL_COL - MIN_WALL_COL + 1];
    int edge, wall, variants = 0;

    for (edge = MIN_EDGE_COL; edge <= MAX_EDGE_COL; ++edge) {
        for (wall = MIN_WALL_COL; wall <= MAX_WALL_COL; ++wall) {
            table[edge - MIN_EDGE_COL][wall - MIN_WALL_COL] = run(edge, wall);
            ++variants;
        }
    }

    /* Metamorphic expectations: moving the ledge one tile farther
     * right delays the fall, while moving the wall one tile farther
     * right delays the wall stop. We do not assert original timestamps. */
    for (edge = 1; edge <= MAX_EDGE_COL - MIN_EDGE_COL; ++edge) {
        const Events prior = table[edge - 1][1];
        const Events later = table[edge][1];
        CHECK(later.fall > prior.fall);
        CHECK(later.land > prior.land);
        CHECK(later.wall == prior.wall || later.wall > prior.wall);
    }
    for (wall = 1; wall <= MAX_WALL_COL - MIN_WALL_COL; ++wall) {
        const Events prior = table[2][wall - 1];
        const Events later = table[2][wall];
        CHECK(later.wall > prior.wall);
        CHECK(later.wall_x > prior.wall_x + 15.0f);
    }

    CHECK(variants == 20);
    printf("STAGE171_BOUNDARY_VARIANTS_PASS scenarios=%d edge_columns=%d..%d "
           "wall_columns=%d..%d original_assets=none\n",
           variants, MIN_EDGE_COL, MAX_EDGE_COL, MIN_WALL_COL, MAX_WALL_COL);
    return EXIT_SUCCESS;
}
