#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define VERIFY(expr) do { if (!(expr)) { \
    fprintf(stderr,"city shot FAIL line %d: %s\n",__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while (0)

/* Independently designed 16px wall; not the original QuackShot layout. */
static QsGame wall(QsPhysicsProfile profile) {
    QsGame g;
    int i, col, row;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    g.terrain[11][10] = '#'; /* x=160..175, y=176..191 */
    for (i = 0; i < QS_MAX_ENEMIES; ++i) g.enemies[i].active = 0;
    for (i = 0; i < QS_MAX_PICKUPS; ++i) g.pickups[i].active = 0;
    g.hero.x = 40; g.hero.y = 140; g.hero.vy = 0; g.hero.vx = 0;
    g.hero.grounded = 0;
    return g;
}
static QsInput none(void) { QsInput in = {0}; return in; }
static void inject(QsGame *g, float x, float vx, int red) {
    QsShot *s = &g->shots[0];
    s->x = x; s->y = 181; s->vx = vx;
    s->active = 1; s->attached = 0; s->red = red; s->frames = 65;
}
static void red_hits_both_wall_faces(void) {
    QsGame right = wall(QS_PHYSICS_REFERENCE_CITY);
    QsGame left = wall(QS_PHYSICS_REFERENCE_CITY);
    inject(&right, 148, 5.5f, 1);
    inject(&left, 179, -5.5f, 1);
    qs_step(&right, none());
    qs_step(&left, none());
    VERIFY(right.shots[0].active && right.shots[0].attached);
    VERIFY(left.shots[0].active && left.shots[0].attached);
    VERIFY(right.shots[0].x == 152);  /* left face 160 - 8 */
    VERIFY(left.shots[0].x == 176);   /* right face 160 + 16 */
    VERIFY(right.shots[0].frames == 230);
    VERIFY(left.shots[0].frames == 230);
}
static void fast_projectile_cannot_tunnel_through_wall(void) {
    QsGame right = wall(QS_PHYSICS_REFERENCE_CITY);
    QsGame left = wall(QS_PHYSICS_REFERENCE_CITY);
    inject(&right, 100, 80.0f, 1);
    inject(&left, 220, -80.0f, 1);
    qs_step(&right, none());
    qs_step(&left, none());
    VERIFY(right.shots[0].active && right.shots[0].attached);
    VERIFY(left.shots[0].active && left.shots[0].attached);
    VERIFY(right.shots[0].x == 152);
    VERIFY(left.shots[0].x == 176);
}
static void yellow_disappears_at_both_faces(void) {
    QsGame right = wall(QS_PHYSICS_REFERENCE_CITY);
    QsGame left = wall(QS_PHYSICS_REFERENCE_CITY);
    inject(&right, 148, 5.5f, 0);
    inject(&left, 179, -5.5f, 0);
    qs_step(&right, none());
    qs_step(&left, none());
    VERIFY(!right.shots[0].active);
    VERIFY(!left.shots[0].active);
}
static void first_enemy_before_wall_wins(void) {
    QsGame g = wall(QS_PHYSICS_REFERENCE_CITY);
    inject(&g, 120, 60.0f, 1);
    g.enemies[0].active = 1;
    g.enemies[0].x = 140; g.enemies[0].y = 178;
    g.enemies[0].min_x = 140; g.enemies[0].max_x = 140;
    qs_step(&g, none());
    VERIFY(!g.shots[0].active && !g.shots[0].attached);
    VERIFY(g.enemies[0].stunned_frames > 0);
}
static void wall_occludes_enemy_behind_it(void) {
    QsGame g = wall(QS_PHYSICS_REFERENCE_CITY);
    inject(&g, 120, 60.0f, 1);
    g.enemies[0].active = 1;
    g.enemies[0].x = 175; g.enemies[0].y = 178;
    g.enemies[0].min_x = 175; g.enemies[0].max_x = 175;
    qs_step(&g, none());
    VERIFY(g.shots[0].active && g.shots[0].attached);
    VERIFY(g.shots[0].x == 152);
    VERIFY(g.enemies[0].stunned_frames == 0);
}
static void anchor_platform_supports_hero(void) {
    QsGame g = wall(QS_PHYSICS_REFERENCE_CITY);
    QsInput idle = none();
    int tick;
    inject(&g, 148, 5.5f, 1);
    qs_step(&g, idle);
    VERIFY(g.shots[0].attached);
    /* Hero ends at x157, clear of the solid wall's x=160 face,
     * but overlaps the anchored shot at x=152..168. */
    g.hero.x = 145; g.hero.y = 120; g.hero.vy = 2;
    for (tick = 0; tick < 26; ++tick) qs_step(&g, idle);
    VERIFY(g.shots[0].active && g.shots[0].attached);
    VERIFY(g.hero.grounded);
    VERIFY(fabsf(g.hero.y - 161.0f) < 0.01f);
}
static void regular_prototype_retains_legacy_contact_math(void) {
    QsGame legacy = wall(QS_PHYSICS_PROTOTYPE);
    inject(&legacy, 148, 5.5f, 1);
    qs_step(&legacy, none());
    VERIFY(legacy.shots[0].active && legacy.shots[0].attached);
    VERIFY(legacy.shots[0].x == 136); /* old floor(left_x/16)*16-8 */
}
static void city_replay_is_deterministic(void) {
    QsGame a = wall(QS_PHYSICS_REFERENCE_CITY), b = a;
    int tick;
    inject(&a, 90, 11.5f, 1);
    inject(&b, 90, 11.5f, 1);
    for (tick = 0; tick < 20; ++tick) {
        qs_step(&a, none());
        qs_step(&b, none());
        VERIFY(a.shots[0].active == b.shots[0].active);
        VERIFY(a.shots[0].attached == b.shots[0].attached);
        VERIFY(a.shots[0].x == b.shots[0].x);
        VERIFY(a.hero.y == b.hero.y);
    }
}
int main(void) {
    red_hits_both_wall_faces();
    fast_projectile_cannot_tunnel_through_wall();
    yellow_disappears_at_both_faces();
    first_enemy_before_wall_wins();
    wall_occludes_enemy_behind_it();
    anchor_platform_supports_hero();
    regular_prototype_retains_legacy_contact_math();
    city_replay_is_deterministic();
    puts("PASS: 8 city swept projectile tests; face anchors, two directions, enemy priority and prototype isolation");
    return EXIT_SUCCESS;
}
