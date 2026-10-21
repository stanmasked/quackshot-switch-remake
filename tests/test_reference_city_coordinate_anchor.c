#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage195: ROM-free coordinate-origin audit for the authored CITY TEST.
 *
 * The Switch prototype's hero.y is the TOP of a 12x20 body. Its
 * collidable foot is hero.y + 20. Original Genesis RAM world Y=176/208
 * might refer to another anchor; this test DOES NOT resolve that.
 */
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "STAGE195_ANCHOR_FAIL line=%d expr=%s\n", \
            __LINE__, #c); exit(EXIT_FAILURE); \
} } while (0)
#define HERO_HEIGHT 20.0f
#define HERO_WIDTH 12.0f
enum { EDGE_FIRST=23, EDGE_LAST=27, HEIGHT_FIRST=1, HEIGHT_LAST=4,
       LOWER_FLOOR_ROW=13, MAX_STEPS=350 };

static QsGame make_authored_drop(int edge, int height_tiles) {
    QsGame g;
    int row, col, first_upper_row = LOWER_FLOOR_ROW - height_tiles;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(QS_TILE == 16);
    for (row=0; row<QS_ROWS; ++row)
        for (col=0; col<QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (col=0; col<QS_COLUMNS; ++col) {
        g.terrain[13][col] = '#';
        g.terrain[14][col] = '#';
        if (col < edge)
            for (row=first_upper_row; row<=12; ++row)
                g.terrain[row][col] = '#';
    }
    g.terrain[12][32] = '#';
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = first_upper_row * QS_TILE - HERO_HEIGHT;
    g.hero.vx = g.hero.vy = 0;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0;
    memset(&g.previous,0,sizeof(g.previous));
    return g;
}

static void audit_case(int edge, int height_tiles) {
    QsGame g = make_authored_drop(edge,height_tiles);
    QsGame repeat = make_authored_drop(edge,height_tiles);
    const int top_row = LOWER_FLOOR_ROW-height_tiles;
    const float upper_surface = top_row * QS_TILE;
    const float lower_surface = LOWER_FLOOR_ROW * QS_TILE;
    const float initial_top = g.hero.y;
    int unsupported = -1, landed = -1;
    int t;
    CHECK(g.hero.grounded);
    CHECK(fabsf(g.hero.y + HERO_HEIGHT - upper_surface) < 0.00001f);
    CHECK(fabsf(initial_top - (upper_surface-HERO_HEIGHT)) < 0.00001f);
    for (t=0; t<MAX_STEPS; ++t) {
        QsInput in = {0};
        in.right = 1;
        qs_step(&g,in);
        qs_step(&repeat,in);
        CHECK(g.hero.x == repeat.hero.x && g.hero.y == repeat.hero.y);
        CHECK(g.hero.vx == repeat.hero.vx && g.hero.vy == repeat.hero.vy);
        CHECK(g.hero.grounded == repeat.hero.grounded);
        if (unsupported == -1 && !g.hero.grounded)
            unsupported = t;
        if (landed == -1 && unsupported >= 0 && g.hero.grounded &&
            fabsf(g.hero.y + HERO_HEIGHT - lower_surface) < 0.00001f)
            landed = t;
    }
    CHECK(unsupported >= 0 && landed > unsupported);
    CHECK(g.hero.grounded);
    CHECK(fabsf(g.hero.y + HERO_HEIGHT - lower_surface) < 0.00001f);
    CHECK(fabsf(g.hero.y - (lower_surface-HERO_HEIGHT)) < 0.00001f);
    CHECK(g.hero.vx == 0.0f);
    CHECK(g.hero.x >= (32*QS_TILE-HERO_WIDTH-1.0f));
    CHECK(g.hero.x <= (32*QS_TILE-HERO_WIDTH+0.02f));
    printf("STAGE195_AUTHOR_ANCHOR_ROW drop_px=%d edge=%d "
           "body_width=%d body_height=%d upper_surface=%d lower_surface=%d "
           "start_top=%d landing_top=%d unsupported=%d landing=%d "
           "original_ram_y_anchor=UNKNOWN\n",
           height_tiles*QS_TILE,edge,(int)HERO_WIDTH,(int)HERO_HEIGHT,
           (int)upper_surface,(int)lower_surface,
           (int)initial_top,(int)g.hero.y,unsupported,landed);
}

int main(void) {
    int edge, drop, rows=0;
    for (drop=HEIGHT_FIRST; drop<=HEIGHT_LAST; ++drop)
        for (edge=EDGE_FIRST; edge<=EDGE_LAST; ++edge) {
            audit_case(edge,drop);
            ++rows;
        }
    CHECK(rows == 20);
    puts("STAGE195_AUTHOR_ANCHOR_PASS rows=20 "
         "authored_top_plus_body_height_is_foot=yes "
         "original_y_semantics=UNKNOWN original_assets=none");
    return EXIT_SUCCESS;
}
