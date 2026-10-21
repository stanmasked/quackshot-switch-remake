#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage186: ROM-free drop-height and event-definition sensitivity.
 *
 * Four independently authored vertical drops (16/32/48/64 pixels)
 * and five ledge columns. The lower floor, horizontal motion and
 * collision parameters remain identical across the entire matrix.
 * This is NOT a reconstruction of Mega Drive source tile geometry.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage186 vertical drop FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    EDGE_FIRST = 23, EDGE_LAST = 27,
    DROP_FIRST = 1, DROP_LAST = 4,
    WALL_COLUMN = 32,
    TRACE_TICKS = 350,
    EDGE_COUNT = EDGE_LAST - EDGE_FIRST + 1,
    DROP_COUNT = DROP_LAST - DROP_FIRST + 1
};

typedef struct {
    int unsupported, moved, moved_quarter, moved_one, landed;
    float fall_x, land_x;
} Events;

static QsGame course(int edge, int drop_tiles) {
    QsGame g;
    int row, col;
    const int first_upper_row = 13 - drop_tiles;

    CHECK(edge >= EDGE_FIRST && edge <= EDGE_LAST);
    CHECK(drop_tiles >= DROP_FIRST && drop_tiles <= DROP_LAST);
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
        if (col < edge)
            for (row = first_upper_row; row <= 12; ++row)
                g.terrain[row][col] = '#';
    }
    g.terrain[12][WALL_COLUMN] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = first_upper_row * QS_TILE - 20.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void repeat_equal(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
    CHECK(a->hero.hearts == 3 && b->hero.hearts == 3);
}

static Events run(int edge, int drop_tiles) {
    QsGame a = course(edge, drop_tiles);
    QsGame b = course(edge, drop_tiles);
    Events e;
    const float upper_y = a.hero.y;
    int tick;
    e.unsupported = e.moved = e.moved_quarter = -1;
    e.moved_one = e.landed = -1;
    e.fall_x = e.land_x = 0.0f;

    for (tick = 0; tick < TRACE_TICKS; ++tick) {
        QsInput input = {0};
        const float old_camera = a.camera_x;
        input.right = 1;
        qs_step(&a, input);
        qs_step(&b, input);
        repeat_equal(&a, &b);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.hearts == 3);
        CHECK(a.hero.x >= 61.0f && a.hero.x < QS_COLUMNS * QS_TILE);

        if (e.unsupported < 0 && !a.hero.grounded)
            e.unsupported = tick;
        if (e.moved < 0 && a.hero.y > upper_y + 0.01f) {
            e.moved = tick;
            e.fall_x = a.hero.x;
        }
        if (e.moved_quarter < 0 && a.hero.y > upper_y + 0.25f)
            e.moved_quarter = tick;
        if (e.moved_one < 0 && a.hero.y > upper_y + 1.0f)
            e.moved_one = tick;

        if (e.landed < 0 && e.moved >= 0 &&
            a.hero.grounded && fabsf(a.hero.y - 188.0f) < 0.001f) {
            e.landed = tick;
            e.land_x = a.hero.x;
        }
    }
    CHECK(e.unsupported >= 0 && e.unsupported <= e.moved);
    CHECK(e.moved <= e.moved_quarter && e.moved_quarter <= e.moved_one);
    CHECK(e.moved_one < e.landed && e.landed < TRACE_TICKS);
    CHECK(e.landed - e.moved >= 6 && e.landed - e.moved <= 40);
    CHECK(e.fall_x >= edge * QS_TILE - 12.1f);
    CHECK(e.fall_x < edge * QS_TILE + 2.0f);
    CHECK(e.land_x > e.fall_x);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(a.hero.vx == 0.0f);
    CHECK(a.hero.x <= WALL_COLUMN * QS_TILE - 12.0f + 0.02f);

    return e;
}

int main(void) {
    Events rows[DROP_COUNT][EDGE_COUNT];
    int drop, edge, cases = 0;
    for (drop = DROP_FIRST; drop <= DROP_LAST; ++drop) {
        for (edge = EDGE_FIRST; edge <= EDGE_LAST; ++edge) {
            const Events e = run(edge, drop);
            rows[drop - DROP_FIRST][edge - EDGE_FIRST] = e;
            printf("STAGE186_DROP_ROW drop_px=%d edge=%d "
                   "unsupported=%d moved=%d moved_quarter=%d "
                   "moved_one=%d land=%d fall_x=%.5f "
                   "land_x=%.5f\n",
                   drop * QS_TILE, edge, e.unsupported, e.moved,
                   e.moved_quarter, e.moved_one, e.landed,
                   e.fall_x, e.land_x);
            ++cases;
        }
    }

    /* Vertical-only changes preserve the first horizontal ledge exit
     * in this deliberately obstacle-free approach. They must also
     * increase the time until the lower floor is reached. */
    for (edge = 0; edge < EDGE_COUNT; ++edge) {
        for (drop = 1; drop < DROP_COUNT; ++drop) {
            const Events a = rows[drop - 1][edge];
            const Events b = rows[drop][edge];
            CHECK(a.unsupported == b.unsupported);
            CHECK(a.moved == b.moved);
            CHECK(a.fall_x == b.fall_x);
            CHECK(b.landed > a.landed);
            CHECK(b.landed - b.moved > a.landed - a.moved);
        }
    }
    for (drop = 0; drop < DROP_COUNT; ++drop) {
        for (edge = 1; edge < EDGE_COUNT; ++edge) {
            const Events a = rows[drop][edge - 1];
            const Events b = rows[drop][edge];
            CHECK(b.unsupported > a.unsupported);
            CHECK(b.moved > a.moved);
            CHECK(b.landed > a.landed);
            CHECK(b.landed - b.moved == a.landed - a.moved);
        }
    }

    CHECK(cases == 20);
    puts("STAGE186_DROP_SWEEP_PASS rows=20 heights_px=16,32,48,64 "
         "edges=23..27 event_thresholds=ungrounded,0.01,0.25,1.0 "
         "original_assets=none");
    return EXIT_SUCCESS;
}
