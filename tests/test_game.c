#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Unlike assert(), CHECK is active in both Debug and Release builds. */
#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void step(QsGame *g, int count, QsInput in) { while (count--) qs_step(g, in); }
static QsInput input(void) { QsInput i = {0}; return i; }

static void test_map(void) {
    QsGame g; QsInput in = input(); qs_init(&g);
    CHECK(g.mode == QS_MAP && g.selected_stage == 0);
    in.right = 1; step(&g, 1, in); CHECK(g.selected_stage == 1);
    step(&g, 4, in); CHECK(g.selected_stage == 1);
    in.right = 0; step(&g, 1, in);
    in.select = 1; step(&g, 1, in); CHECK(g.mode == QS_STAGE && g.stage == 1);
    in.select = 0; in.back = 1; step(&g, 1, in); CHECK(g.mode == QS_MAP);
}

static void test_physics_and_gravity(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    step(&g, 10, in); CHECK(g.hero.grounded && fabsf(g.hero.y - 188) < 0.1f);
    in.jump = 1; step(&g, 1, in); CHECK(g.hero.vy < 0 && !g.hero.grounded);
    in.jump = 0; step(&g, 65, in); CHECK(g.hero.grounded && g.hero.hearts == 3);
}

static void test_wall_collision(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    step(&g, 12, in);
    g.hero.x = 35 * QS_TILE - 17; g.hero.y = 188;
    in.right = 1; step(&g, 15, in);
    CHECK(g.hero.x + 12 <= 35 * QS_TILE + 0.01f);
}

static void test_shot_stuns_enemy(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    step(&g, 15, in);
    g.hero.x = 330;
    g.enemies[0].x = 390; g.enemies[0].min_x = 390; g.enemies[0].max_x = 390;
    in.fire = 1; step(&g, 14, in);
    CHECK(g.enemies[0].stunned_frames > 0);
}

static void test_pickup_and_red_anchor(void) {
    QsGame g; QsInput in = input(); int k, found = 0;
    qs_init(&g); qs_begin_stage(&g, 0);
    g.hero.x = 179; g.hero.y = 140;
    step(&g, 1, in);
    CHECK(g.red_unlocked && g.weapon == QS_RED);
    g.hero.x = 520; g.hero.y = 171; g.hero.facing = 1;
    in.fire = 1; step(&g, 7, in);
    for (k = 0; k < QS_MAX_SHOTS; ++k)
        if (g.shots[k].active && g.shots[k].attached) found = 1;
    CHECK(found);
    in.fire = 0; step(&g, 1, in);
    in.change = 1; step(&g, 1, in);
    CHECK(g.weapon == QS_YELLOW);
}

static void test_falling_into_pit_respawns(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    g.hero.x = 21 * QS_TILE;
    step(&g, 100, in);
    CHECK(g.hero.x == 40 && g.hero.hearts == 3);
}

static void test_red_anchor_can_be_stood_on(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    g.shots[0].active = 1; g.shots[0].attached = 1;
    g.shots[0].x = 80; g.shots[0].y = 160; g.shots[0].frames = 200;
    g.hero.x = 82; g.hero.y = 110; g.hero.vy = 3.5f;
    step(&g, 12, in);
    CHECK(g.hero.grounded && fabsf(g.hero.y - 140) < 0.1f);
}

static void test_replay_determinism(void) {
    QsGame a, b; int t; qs_init(&a); qs_init(&b);
    qs_begin_stage(&a, 0); qs_begin_stage(&b, 0);
    for (t = 0; t < 250; ++t) {
        QsInput in = input();
        in.right = t < 100 || t >= 180;
        in.jump = t == 20 || t == 92 || t == 180;
        in.fire = t == 11 || t == 40 || t == 181;
        in.dash = t >= 180;
        qs_step(&a, in); qs_step(&b, in);
    }
    CHECK(a.mode == b.mode && a.stage == b.stage);
    CHECK(a.hero.x == b.hero.x && a.hero.y == b.hero.y);
    CHECK(a.hero.hearts == b.hero.hearts && a.camera_x == b.camera_x);
}

static void test_stage_completion(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 2);
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE;
    step(&g, 1, in);
    CHECK(g.mode == QS_MAP && g.cleared[2]);
    qs_begin_stage(&g, 0); CHECK(g.cleared[2]);
}

static void test_invulnerable_hero_does_not_take_repeat_damage(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    step(&g, 15, in);
    g.enemies[0].x = g.hero.x;
    g.enemies[0].y = g.hero.y;
    g.enemies[0].min_x = g.hero.x;
    g.enemies[0].max_x = g.hero.x;
    step(&g, 1, in);
    CHECK(g.hero.hearts == 2);
    step(&g, 20, in);
    CHECK(g.hero.hearts == 2);
}

static void test_yellow_projectile_expires_on_wall(void) {
    QsGame g; QsInput in = input(); int i, live = 0;
    qs_init(&g); qs_begin_stage(&g, 0);
    step(&g, 15, in);
    g.hero.x = 530; g.hero.y = 171; g.hero.facing = 1;
    in.fire = 1; step(&g, 7, in);
    for (i = 0; i < QS_MAX_SHOTS; ++i)
        if (g.shots[i].active) ++live;
    CHECK(live == 0);
}

static void test_red_anchor_expires(void) {
    QsGame g; QsInput in = input(); qs_init(&g); qs_begin_stage(&g, 0);
    g.shots[0].active = 1;
    g.shots[0].attached = 1;
    g.shots[0].frames = 2;
    step(&g, 2, in);
    CHECK(!g.shots[0].active);
}

int main(void) {
    test_map(); test_physics_and_gravity(); test_wall_collision();
    test_shot_stuns_enemy(); test_pickup_and_red_anchor(); test_stage_completion();
    test_falling_into_pit_respawns(); test_red_anchor_can_be_stood_on();
    test_replay_determinism();
    test_invulnerable_hero_does_not_take_repeat_damage();
    test_yellow_projectile_expires_on_wall(); test_red_anchor_expires();
    puts("PASS: 12 gameplay checks: map, physics, walls, weapons, pickups, enemies, pits, replay");
    return EXIT_SUCCESS;
}
