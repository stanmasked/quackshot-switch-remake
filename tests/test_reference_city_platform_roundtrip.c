#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage177: one continuous synthetic wall/top/edge/wall round trip.
 * The level geometry is authored entirely for this regression; it is
 * not recovered QuackShot tile data or a ROM-derived hitbox.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage177 platform roundtrip FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum { BLOCK_FIRST_COL = 25, BLOCK_LAST_COL = 32,
       BLOCK_LEFT = BLOCK_FIRST_COL * QS_TILE,
       BLOCK_RIGHT = (BLOCK_LAST_COL + 1) * QS_TILE,
       BLOCK_TOP = 160, BLOCK_BOTTOM = 208,
       FLOOR_PLAYER_Y = 188, TOP_PLAYER_Y = 140,
       APPROACH_TICKS = 24, JUMP_TICKS = 100,
       RETREAT_TICKS = 115, RETURN_TICKS = 155 };

typedef struct {
    QsGame a;
    QsGame b;
    int top_landed;
    int left_top;
    int returned_floor;
    int first_wall;
    int second_wall;
} Replay;

static QsGame course(void) {
    QsGame g;
    int col, row;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    for (row = 10; row <= 12; ++row)
        for (col = BLOCK_FIRST_COL; col <= BLOCK_LAST_COL; ++col)
            g.terrain[row][col] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = FLOOR_PLAYER_Y;
    g.hero.vx = g.hero.vy = 0;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void independent_aabb(const QsGame *g) {
    const QsPlayer *h = &g->hero;
    const float margin = 0.001f;
    CHECK(!(h->x < BLOCK_RIGHT - margin &&
            h->x + 12.0f > BLOCK_LEFT + margin &&
            h->y < BLOCK_BOTTOM - margin &&
            h->y + 20.0f > BLOCK_TOP + margin));
    CHECK(isfinite(h->x) && isfinite(h->y));
    CHECK(isfinite(h->vx) && isfinite(h->vy));
    CHECK(h->x > 0.0f && h->x < QS_COLUMNS * QS_TILE);
    CHECK(h->y >= 0.0f && h->y <= FLOOR_PLAYER_Y + 0.001f);
    CHECK(h->hearts == 3);
    CHECK(g->mode == QS_STAGE && g->stage == 0);
    CHECK(g->physics_profile == QS_PHYSICS_REFERENCE_CITY);
}

static void step_twice(Replay *s, QsInput input) {
    const float camera_before = s->a.camera_x;
    qs_step(&s->a, input);
    qs_step(&s->b, input);
    CHECK(s->a.hero.x == s->b.hero.x);
    CHECK(s->a.hero.y == s->b.hero.y);
    CHECK(s->a.hero.vx == s->b.hero.vx);
    CHECK(s->a.hero.vy == s->b.hero.vy);
    CHECK(s->a.hero.grounded == s->b.hero.grounded);
    CHECK(s->a.camera_x == s->b.camera_x);
    CHECK(s->a.render_camera_x == s->b.render_camera_x);
    CHECK(s->a.render_camera_x == camera_before);
    independent_aabb(&s->a);
}

static QsInput direction(int dir, int held_jump) {
    QsInput input = {0};
    input.left = dir < 0;
    input.right = dir > 0;
    input.jump = held_jump;
    return input;
}

static int at_side(int dir, float x) {
    return dir > 0 ? x < BLOCK_LEFT - 12.0f + 0.001f :
                     x > BLOCK_RIGHT - 0.001f;
}

static void run_roundtrip(int dir, int fractional_quarters,
                          unsigned move_hold) {
    Replay s;
    unsigned t;
    float first_wall_x, top_x;
    int saw_air = 0, saw_top = 0, saw_fall = 0;
    const float fraction = fractional_quarters * 0.25f;
    s.a = course();
    if (dir > 0)
        s.a.hero.x = BLOCK_LEFT - 12.0f - fraction;
    else
        s.a.hero.x = BLOCK_RIGHT + fraction;
    s.b = s.a;
    s.top_landed = s.left_top = s.returned_floor = 0;
    s.first_wall = s.second_wall = 0;

    /* 1. Both faces: pressing towards the wall must block motion. */
    for (t = 0; t < APPROACH_TICKS; ++t) {
        step_twice(&s, direction(dir, 0));
        CHECK(at_side(dir, s.a.hero.x));
        CHECK(s.a.hero.y == FLOOR_PLAYER_Y && s.a.hero.grounded);
        if (s.a.hero.vx == 0.0f && t >= 12) s.first_wall = 1;
    }
    CHECK(s.first_wall);
    CHECK(s.a.hero.vx == 0.0f);
    first_wall_x = s.a.hero.x;

    /* 2. Jump held for 50 ticks while horizontal input is released
     * after 32/40/48 ticks; a long jump should land on the roof. */
    for (t = 0; t < JUMP_TICKS; ++t) {
        QsInput input = {0};
        if (t < move_hold) input = direction(dir, 0);
        input.jump = t < 50;
        step_twice(&s, input);
        if (!s.a.hero.grounded) saw_air = 1;
        if (saw_air && s.a.hero.grounded &&
            fabsf(s.a.hero.y - TOP_PLAYER_Y) < 0.001f) saw_top = 1;
    }
    CHECK(saw_air && saw_top);
    CHECK(s.a.hero.grounded && s.a.hero.y == TOP_PLAYER_Y);
    CHECK(s.a.hero.vx == 0.0f);
    CHECK(s.a.hero.x + 12.0f > BLOCK_LEFT);
    CHECK(s.a.hero.x < BLOCK_RIGHT);
    top_x = s.a.hero.x;
    if (dir > 0) CHECK(top_x > first_wall_x);
    else CHECK(top_x < first_wall_x);

    /* 3. Reverse and walk completely off the near edge.
     * Floor landing must occur after leaving the top. */
    for (t = 0; t < RETREAT_TICKS; ++t) {
        step_twice(&s, direction(-dir, 0));
        if (s.a.hero.y > TOP_PLAYER_Y + 0.001f) saw_fall = 1;
        if (saw_fall && s.a.hero.grounded &&
            s.a.hero.y == FLOOR_PLAYER_Y) s.returned_floor = 1;
    }
    CHECK(saw_fall && s.returned_floor);
    CHECK(s.a.hero.grounded && s.a.hero.y == FLOOR_PLAYER_Y);
    if (dir > 0) CHECK(s.a.hero.x + 12.0f < BLOCK_LEFT);
    else CHECK(s.a.hero.x > BLOCK_RIGHT);

    /* 4. Turn back towards the wall and stop for the second time. */
    for (t = 0; t < RETURN_TICKS; ++t) {
        step_twice(&s, direction(dir, 0));
        if (t >= 70 && s.a.hero.vx == 0.0f &&
            at_side(dir, s.a.hero.x)) s.second_wall = 1;
        CHECK(s.a.hero.y == FLOOR_PLAYER_Y && s.a.hero.grounded);
    }
    CHECK(s.second_wall);
    CHECK(s.a.hero.vx == 0.0f);
    CHECK(at_side(dir, s.a.hero.x));
    CHECK(fabsf(s.a.hero.x - first_wall_x) < 1.6251f);
}

int main(void) {
    const unsigned move_holds[] = {32, 40, 48};
    int dir, offset, cases = 0;
    unsigned i;
    for (dir = -1; dir <= 1; dir += 2)
        for (offset = 0; offset < 4; ++offset)
            for (i = 0; i < sizeof(move_holds)/sizeof(move_holds[0]); ++i) {
                run_roundtrip(dir, offset, move_holds[i]);
                ++cases;
            }
    CHECK(cases == 24);
    printf("STAGE177_PLATFORM_ROUNDTRIP_PASS traces=%d "
           "steps_per_trace=%d original_assets=none\n", cases,
           APPROACH_TICKS + JUMP_TICKS + RETREAT_TICKS + RETURN_TICKS);
    return EXIT_SUCCESS;
}
