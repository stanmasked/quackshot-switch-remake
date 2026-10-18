#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "city bounds FAIL line %d: %s\n", __LINE__, #expr); \
  exit(EXIT_FAILURE); } } while (0)

/* Independent, asset-free 80x15 tile research playground. */
static QsInput idle(void) { QsInput in = {0}; return in; }
static QsGame empty(QsPhysicsProfile profile) {
    QsGame g;
    int row, col, i;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (i = 0; i < QS_MAX_ENEMIES; ++i) g.enemies[i].active = 0;
    for (i = 0; i < QS_MAX_PICKUPS; ++i) g.pickups[i].active = 0;
    g.hero.x = 40; g.hero.y = 140; g.hero.vx = g.hero.vy = 0;
    return g;
}
static void shot(QsGame *g, float x, float velocity, int red) {
    QsShot *s = &g->shots[0];
    memset(s, 0, sizeof(*s));
    s->active = 1; s->red = red; s->frames = 65;
    s->x = x; s->y = 181; s->vx = velocity;
}
static void foe(QsGame *g, float x, float min_x, float max_x, int dir) {
    QsEnemy *e = &g->enemies[0];
    memset(e, 0, sizeof(*e));
    e->active = 1;
    e->x = x; e->y = 181; e->min_x = min_x; e->max_x = max_x;
    e->dir = dir;
}
static void right_world_edge_is_not_an_anchor(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    shot(&g, QS_COLUMNS * QS_TILE - 8.0f, 5.5f, 1);
    qs_step(&g, idle());
    CHECK(!g.shots[0].active);
    CHECK(!g.shots[0].attached);
}
static void yellow_also_expires_at_right_boundary(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    shot(&g, QS_COLUMNS * QS_TILE - 8.0f, 5.5f, 0);
    qs_step(&g, idle());
    CHECK(!g.shots[0].active);
}
static void left_world_edge_does_not_create_anchor(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    shot(&g, 1, -4.0f, 1);
    qs_step(&g, idle());
    CHECK(!g.shots[0].active && !g.shots[0].attached);
}
static void authored_last_column_wall_still_attaches(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    g.terrain[11][QS_COLUMNS - 1] = '#';
    shot(&g, QS_COLUMNS * QS_TILE - 23.0f, 5.5f, 1);
    qs_step(&g, idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x == (QS_COLUMNS - 1) * QS_TILE - 8);
}
static void authored_first_column_wall_still_attaches(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    g.terrain[11][0] = '#';
    shot(&g, 17.0f, -5.5f, 1);
    qs_step(&g, idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x == QS_TILE);
}
static void right_edge_patrol_clamps_entire_enemy(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    int k, reflected = 0;
    const float last = QS_COLUMNS * QS_TILE - 14;
    foe(&g, last - 0.10f, last - 24, last + 30, 1);
    for (k = 0; k < 220; ++k) {
        qs_step(&g, idle());
        CHECK(g.enemies[0].x <= last);
        CHECK(g.enemies[0].x >= last - 24);
        CHECK(g.enemies[0].x + 14 <= QS_COLUMNS * QS_TILE);
        if (g.enemies[0].dir < 0) reflected = 1;
    }
    CHECK(reflected);
}
static void left_edge_patrol_never_goes_negative(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    int k, reflected = 0;
    foe(&g, 0.1f, -33, 24, -1);
    for (k = 0; k < 220; ++k) {
        qs_step(&g, idle());
        CHECK(g.enemies[0].x >= 0 && g.enemies[0].x <= 24);
        if (g.enemies[0].dir > 0) reflected = 1;
    }
    CHECK(reflected);
}
static void exact_patrol_endpoint_and_reverse(void) {
    QsGame g = empty(QS_PHYSICS_REFERENCE_CITY);
    foe(&g, 99.75f, 80, 100, 1);
    qs_step(&g, idle());
    CHECK(g.enemies[0].x == 100.0f && g.enemies[0].dir == -1);
    qs_step(&g, idle());
    CHECK(g.enemies[0].x < 100 && g.enemies[0].x >= 80);
}
static void ordinary_prototype_retains_patrol_overshoot(void) {
    QsGame g = empty(QS_PHYSICS_PROTOTYPE);
    foe(&g, 99.75f, 80, 100, 1);
    qs_step(&g, idle());
    CHECK(g.enemies[0].x > 100.0f && g.enemies[0].dir == -1);
}
static void rightmost_edge_replay_is_deterministic(void) {
    QsGame a = empty(QS_PHYSICS_REFERENCE_CITY), b;
    int k;
    foe(&a, QS_COLUMNS * QS_TILE - 17.0f,
        QS_COLUMNS * QS_TILE - 32.0f,
        QS_COLUMNS * QS_TILE + 30.0f, 1);
    shot(&a, QS_COLUMNS * QS_TILE - 20.0f, 9.0f, 1);
    b = a;
    for (k = 0; k < 300; ++k) {
        qs_step(&a, idle()); qs_step(&b, idle());
        CHECK(a.enemies[0].x == b.enemies[0].x);
        CHECK(a.enemies[0].dir == b.enemies[0].dir);
        CHECK(a.shots[0].active == b.shots[0].active);
        CHECK(a.shots[0].attached == b.shots[0].attached);
        CHECK(a.camera_x == b.camera_x);
    }
    CHECK(a.enemies[0].x >= 0);
}
int main(void) {
    right_world_edge_is_not_an_anchor();
    yellow_also_expires_at_right_boundary();
    left_world_edge_does_not_create_anchor();
    authored_last_column_wall_still_attaches();
    authored_first_column_wall_still_attaches();
    right_edge_patrol_clamps_entire_enemy();
    left_edge_patrol_never_goes_negative();
    exact_patrol_endpoint_and_reverse();
    ordinary_prototype_retains_patrol_overshoot();
    rightmost_edge_replay_is_deterministic();
    puts("PASS: 10 world-border projectile and enemy-patrol regressions (original-free)");
    return EXIT_SUCCESS;
}
