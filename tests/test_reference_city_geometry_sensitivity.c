#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage185 — first-city event sensitivity to independently authored
 * tile boundaries. These tiles are not recovered QuackShot assets.
 * Identical input; vary ONLY edge/wall column. No physics changes.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage185 geometry sensitivity FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    EDGE_FIRST = 23, EDGE_LAST = 27,
    WALL_FIRST = 31, WALL_LAST = 34,
    RIGHT_TICKS = 350,
    EDGE_COUNT = EDGE_LAST - EDGE_FIRST + 1,
    WALL_COUNT = WALL_LAST - WALL_FIRST + 1
};

typedef struct {
    int fall, land, stop;
    float fall_x, land_x, stop_x;
} Events;

static QsGame authored_city(int edge, int wall) {
    QsGame g;
    int row, col;
    CHECK(edge >= EDGE_FIRST && edge <= EDGE_LAST);
    CHECK(wall >= WALL_FIRST && wall <= WALL_LAST);
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col) {
        g.terrain[13][col] = '#';
        g.terrain[14][col] = '#';
        if (col < edge) {
            g.terrain[11][col] = '#';
            g.terrain[12][col] = '#';
        }
    }
    g.terrain[12][wall] = '#';
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

static void assert_repeat(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
    CHECK(a->hero.hearts == 3 && b->hero.hearts == 3);
}

static Events measure(int edge, int wall) {
    QsGame a = authored_city(edge, wall);
    QsGame b = authored_city(edge, wall);
    Events e;
    int t;
    e.fall = e.land = e.stop = -1;
    e.fall_x = e.land_x = e.stop_x = 0.0f;
    for (t = 0; t < RIGHT_TICKS; ++t) {
        QsInput input = {0};
        const float old_camera = a.camera_x;
        input.right = 1;
        qs_step(&a, input);
        qs_step(&b, input);
        assert_repeat(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.x > 0.0f && a.hero.x < QS_COLUMNS * QS_TILE);
        if (e.fall < 0 && a.hero.y > 156.01f) {
            e.fall = t;
            e.fall_x = a.hero.x;
        }
        if (e.land < 0 && e.fall >= 0 && a.hero.grounded &&
            fabsf(a.hero.y - 188.0f) < 0.001f) {
            e.land = t;
            e.land_x = a.hero.x;
        }
        if (e.stop < 0 && e.land >= 0 &&
            a.hero.x > wall * QS_TILE - 30.0f && a.hero.vx == 0.0f) {
            e.stop = t;
            e.stop_x = a.hero.x;
        }
    }
    CHECK(0 <= e.fall && e.fall < e.land);
    CHECK(e.land < e.stop && e.stop < RIGHT_TICKS);
    CHECK(e.fall_x < e.land_x && e.land_x < e.stop_x);
    CHECK(e.fall_x >= edge * QS_TILE - 12.1f);
    CHECK(e.fall_x < edge * QS_TILE + 2.0f);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(a.hero.vx == 0.0f);
    CHECK(a.hero.x <= wall * QS_TILE - 12.0f + 0.02f);
    CHECK(a.hero.x >= wall * QS_TILE - 13.0f);
    return e;
}

int main(void) {
    Events results[EDGE_COUNT][WALL_COUNT];
    int edge, wall, total = 0;
    for (edge = EDGE_FIRST; edge <= EDGE_LAST; ++edge) {
        for (wall = WALL_FIRST; wall <= WALL_LAST; ++wall) {
            const Events e = measure(edge, wall);
            results[edge - EDGE_FIRST][wall - WALL_FIRST] = e;
            printf("STAGE185_GEOMETRY_ROW edge=%d wall=%d fall=%d "
                   "land=%d stop=%d fall_x=%.5f land_x=%.5f "
                   "stop_x=%.5f\n",
                   edge, wall, e.fall, e.land, e.stop,
                   e.fall_x, e.land_x, e.stop_x);
            ++total;
        }
    }
    for (edge = 1; edge < EDGE_COUNT; ++edge) {
        const Events earlier = results[edge - 1][1];
        const Events later = results[edge][1];
        CHECK(later.fall > earlier.fall);
        CHECK(later.land > earlier.land);
        CHECK(later.stop >= earlier.stop);
    }
    for (wall = 1; wall < WALL_COUNT; ++wall) {
        const Events earlier = results[2][wall - 1];
        const Events later = results[2][wall];
        CHECK(later.stop > earlier.stop);
        CHECK(later.stop_x > earlier.stop_x + 15.0f);
    }
    CHECK(total == EDGE_COUNT * WALL_COUNT);
    printf("STAGE185_GEOMETRY_SWEEP_PASS rows=%d "
           "edge_tiles=%d..%d wall_tiles=%d..%d "
           "source_geometry=synthetic original_assets=none\n",
           total, EDGE_FIRST, EDGE_LAST, WALL_FIRST, WALL_LAST);
    return EXIT_SUCCESS;
}
