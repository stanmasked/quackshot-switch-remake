#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(v) do { if (!(v)) { \
    fprintf(stderr, "city sequence FAIL line %d: %s\n", __LINE__, #v); \
    exit(EXIT_FAILURE); } } while (0)

/* 100% independent and ROM-free test geometry, not original QuackShot data. */
static QsInput idle(void) { QsInput x = {0}; return x; }

static QsGame sandbox(void) {
    QsGame g;
    int row, col, k;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col) g.terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col)
        g.terrain[13][col] = g.terrain[14][col] = '#';
    g.terrain[12][11] = '#'; /* stand-alone target tile, x=176..191 */
    for (k = 0; k < QS_MAX_ENEMIES; ++k) g.enemies[k].active = 0;
    for (k = 0; k < QS_MAX_PICKUPS; ++k) g.pickups[k].active = 0;
    g.hero.x = 100;
    g.hero.y = 188;
    g.hero.vx = g.hero.vy = 0;
    g.hero.grounded = 1;
    g.red_unlocked = 1;
    g.weapon = QS_RED;
    return g;
}

static void anchor(QsGame *g, int slot, float x, float y, int life) {
    QsShot *s = &g->shots[slot];
    s->active = s->attached = s->red = 1;
    s->x = x; s->y = y; s->vx = 0; s->frames = life;
}
static void check_state(const QsGame *g) {
    int i;
    CHECK(g->mode == QS_STAGE);
    CHECK(g->physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(isfinite(g->hero.x) && isfinite(g->hero.y));
    CHECK(isfinite(g->hero.vx) && isfinite(g->hero.vy));
    CHECK(isfinite(g->camera_x) && isfinite(g->render_camera_x));
    CHECK(g->camera_x >= 0 && g->camera_x <= QS_COLUMNS * QS_TILE - QS_VIEW_W);
    CHECK(g->render_camera_x >= 0 && g->render_camera_x <= QS_COLUMNS * QS_TILE - QS_VIEW_W);
    CHECK(g->hero.hearts > 0 && g->hero.hearts <= 3);
    CHECK(g->hero.x >= 0 && g->hero.x < QS_COLUMNS * QS_TILE);
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        const QsShot *s = &g->shots[i];
        if (!s->active) continue;
        CHECK(s->frames > 0);
        CHECK(isfinite(s->x) && isfinite(s->y) && isfinite(s->vx));
        CHECK(s->x >= 0 && s->x < QS_COLUMNS * QS_TILE);
        CHECK(!s->attached || s->red);
    }
}
static void scripted_reference_city_replay(void) {
    QsGame a = sandbox(), b = sandbox();
    int t, k, anchors = 0, flying = 0;
    for (t = 0; t < 1200; ++t) {
        QsInput in = idle();
        int phase = t % 240;
        in.right = phase < 96;
        in.left = phase >= 110 && phase < 216;
        in.jump = (t % 97) < 6;
        in.fire = (t % 53) == 0;
        in.change = (t % 281) == 140;
        qs_step(&a, in);
        qs_step(&b, in);
        check_state(&a);
        check_state(&b);
        CHECK(a.hero.x == b.hero.x && a.hero.y == b.hero.y);
        CHECK(a.hero.vx == b.hero.vx && a.hero.vy == b.hero.vy);
        CHECK(a.hero.grounded == b.hero.grounded);
        CHECK(a.camera_x == b.camera_x);
        CHECK(a.render_camera_x == b.render_camera_x);
        CHECK(a.reference_jump_steps == b.reference_jump_steps);
        CHECK(a.pending_reference_shot == b.pending_reference_shot);
        CHECK(a.weapon == b.weapon);
        for (k = 0; k < QS_MAX_SHOTS; ++k) {
            CHECK(a.shots[k].active == b.shots[k].active);
            CHECK(a.shots[k].attached == b.shots[k].attached);
            CHECK(a.shots[k].x == b.shots[k].x);
            CHECK(a.shots[k].frames == b.shots[k].frames);
            if (a.shots[k].active && a.shots[k].attached) ++anchors;
            if (a.shots[k].active && !a.shots[k].attached) ++flying;
        }
    }
    CHECK(a.frames == 1200 && b.frames == 1200);
    CHECK(anchors > 0 && flying > 0);
}
static void long_permuted_anchor_course(void) {
    QsGame a = sandbox(), b = sandbox();
    int t, ground_ticks = 0, air_ticks = 0;
    a.hero.y = b.hero.y = 140;
    a.hero.grounded = b.hero.grounded = 1;
    anchor(&a, 0, 100, 160, 46);
    anchor(&a, 1, 100, 176, 500);
    anchor(&b, 0, 100, 176, 500);
    anchor(&b, 1, 100, 160, 46);
    for (t = 0; t < 360; ++t) {
        QsInput in = idle();
        int phase = t % 120;
        in.right = phase >= 30 && phase < 75;
        in.left = phase >= 85 && phase < 118;
        in.jump = t > 50 && (t % 61) < 5;
        qs_step(&a, in);
        qs_step(&b, in);
        check_state(&a);
        check_state(&b);
        CHECK(a.hero.x == b.hero.x && a.hero.y == b.hero.y);
        CHECK(a.hero.vy == b.hero.vy);
        CHECK(a.hero.grounded == b.hero.grounded);
        CHECK(a.camera_x == b.camera_x);
        CHECK(a.render_camera_x == b.render_camera_x);
        ground_ticks += a.hero.grounded != 0;
        air_ticks += a.hero.grounded == 0;
    }
    CHECK(ground_ticks > 0 && air_ticks > 0);
}
static void expired_platform_does_not_reactivate(void) {
    QsGame g = sandbox();
    int t;
    g.hero.x = 100; g.hero.y = 140; g.hero.grounded = 1;
    anchor(&g, 0, 100, 160, 3);
    anchor(&g, 1, 100, 176, 45);
    for (t = 0; t < 190; ++t) {
        qs_step(&g, idle());
        check_state(&g);
        if (t >= 2) CHECK(!g.shots[0].active);
        if (t >= 44) CHECK(!g.shots[1].active);
    }
    CHECK(g.hero.grounded && fabsf(g.hero.y - 188) < 0.001f);
}
int main(void) {
    scripted_reference_city_replay();
    long_permuted_anchor_course();
    expired_platform_does_not_reactivate();
    puts("PASS: 1750-tick ROM-free city anchor, jump, shot, reverse and expiry regression");
    return EXIT_SUCCESS;
}
