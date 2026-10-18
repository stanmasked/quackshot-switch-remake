#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define VERIFY(x) do { if (!(x)) { \
    fprintf(stderr, "anchor lifecycle FAIL line %d: %s\n", __LINE__, #x); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in = {0}; return in; }

static QsGame scene(QsPhysicsProfile profile) {
    QsGame g;
    int i, row, col;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col) g.terrain[row][col] = '.';
    for (i = 0; i < QS_MAX_ENEMIES; ++i) g.enemies[i].active = 0;
    for (i = 0; i < QS_MAX_PICKUPS; ++i) g.pickups[i].active = 0;
    g.hero.x = 100; g.hero.y = 140; g.hero.vx = g.hero.vy = 0;
    g.hero.grounded = 0;
    return g;
}

static void platform(QsGame *g, int index, float x, float y, int life, int red) {
    QsShot *s = &g->shots[index];
    s->active = 1; s->attached = 1; s->red = red;
    s->x = x; s->y = y; s->vx = 0; s->frames = life;
}

static void nearer_anchor_beats_lower_index(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    /* Both surfaces crossed in one gravity substep, but slot 1 is
     * higher and must be selected even though slot 0 is visited first. */
    g.hero.y = 139.92f;
    platform(&g, 0, 100, 160.11f, 40, 1);
    platform(&g, 1, 100, 160.02f, 40, 1);
    qs_step(&g, idle());
    VERIFY(g.hero.grounded);
    VERIFY(fabsf(g.hero.y - 140.02f) < 0.0001f);
    VERIFY(g.shots[0].active && g.shots[1].active);
}

static void slot_permutation_cannot_change_landing(void) {
    QsGame a = scene(QS_PHYSICS_REFERENCE_CITY);
    QsGame b = scene(QS_PHYSICS_REFERENCE_CITY);
    a.hero.y = b.hero.y = 139.92f;
    platform(&a, 0, 100, 160.11f, 40, 1);
    platform(&a, 1, 100, 160.02f, 40, 1);
    platform(&b, 0, 100, 160.02f, 40, 1);
    platform(&b, 1, 100, 160.11f, 40, 1);
    qs_step(&a, idle()); qs_step(&b, idle());
    VERIFY(a.hero.grounded && b.hero.grounded);
    VERIFY(a.hero.y == b.hero.y);
    VERIFY(a.hero.vy == b.hero.vy);
}

static void expiry_removes_support_on_exact_step(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.grounded = 1;
    platform(&g, 0, 100, 160, 2, 1);
    qs_step(&g, idle());
    VERIFY(g.hero.grounded && g.hero.y == 140);
    VERIFY(g.shots[0].active && g.shots[0].frames == 1);
    qs_step(&g, idle());
    VERIFY(!g.shots[0].active);
    VERIFY(!g.hero.grounded && g.hero.y > 140);
    VERIFY(g.hero.vy > 0);
}

static void drops_to_lower_anchor_after_upper_expires(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    int tick;
    g.hero.grounded = 1;
    platform(&g, 0, 100, 160, 2, 1);
    platform(&g, 1, 100, 168, 30, 1);
    qs_step(&g, idle());
    VERIFY(g.hero.grounded && g.hero.y == 140);
    qs_step(&g, idle());
    VERIFY(!g.shots[0].active);
    VERIFY(!g.hero.grounded);
    for (tick = 0; tick < 15; ++tick) qs_step(&g, idle());
    VERIFY(g.shots[1].active);
    VERIFY(g.hero.grounded && fabsf(g.hero.y - 148) < 0.001f);
}

static void leaving_platform_loses_grounded_contact(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    QsInput right = idle();
    int tick;
    right.right = 1;
    g.hero.grounded = 1;
    platform(&g, 0, 100, 160, 100, 1);
    for (tick = 0; tick < 32; ++tick) qs_step(&g, right);
    VERIFY(g.hero.x >= 116);
    VERIFY(!g.hero.grounded && g.hero.y > 140);
    VERIFY(g.shots[0].active);
}

static void jump_does_not_stick_to_support(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    QsInput jump = idle();
    g.hero.grounded = 1;
    platform(&g, 0, 100, 160, 40, 1);
    jump.jump = 1;
    qs_step(&g, jump);
    VERIFY(!g.hero.grounded && g.hero.y < 140);
    VERIFY(g.hero.vy < 0);
    VERIFY(g.shots[0].active && g.shots[0].attached);
}

static void invalid_yellow_anchor_is_not_a_red_platform(void) {
    QsGame city = scene(QS_PHYSICS_REFERENCE_CITY);
    QsGame normal = scene(QS_PHYSICS_PROTOTYPE);
    platform(&city, 0, 100, 160, 20, 0);
    platform(&normal, 0, 100, 160, 20, 0);
    qs_step(&city, idle());
    qs_step(&normal, idle());
    VERIFY(!city.hero.grounded && city.hero.y > 140);
    /* Backward compatibility: the prototype does not inspect red flag. */
    VERIFY(normal.hero.grounded && normal.hero.y == 140);
}

static void inactive_or_expired_platform_cannot_intercept(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.y = 139.92f;
    platform(&g, 0, 100, 160.02f, 1, 1);
    platform(&g, 1, 100, 160.10f, 30, 1);
    qs_step(&g, idle());
    VERIFY(!g.shots[0].active);
    VERIFY(g.hero.grounded && fabsf(g.hero.y - 140.10f) < 0.0001f);
}

static void stage_transition_clears_all_supports(void) {
    QsGame g = scene(QS_PHYSICS_REFERENCE_CITY);
    platform(&g, 0, 100, 160, 60, 1);
    platform(&g, 3, 102, 168, 60, 1);
    qs_begin_stage(&g, 1);
    VERIFY(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    VERIFY(!g.shots[0].active && !g.shots[3].active);
    VERIFY(g.render_camera_x == 0 && g.camera_x == 0);
}

static void repeated_anchor_lifecycle_is_deterministic(void) {
    QsGame a = scene(QS_PHYSICS_REFERENCE_CITY);
    QsGame b = scene(QS_PHYSICS_REFERENCE_CITY);
    int tick;
    a.hero.grounded = b.hero.grounded = 1;
    platform(&a, 0, 100, 160, 2, 1);
    platform(&b, 0, 100, 160, 2, 1);
    platform(&a, 1, 100, 168, 45, 1);
    platform(&b, 1, 100, 168, 45, 1);
    for (tick = 0; tick < 24; ++tick) {
        qs_step(&a, idle()); qs_step(&b, idle());
        VERIFY(a.hero.x == b.hero.x && a.hero.y == b.hero.y);
        VERIFY(a.hero.vy == b.hero.vy && a.hero.grounded == b.hero.grounded);
        VERIFY(a.shots[0].frames == b.shots[0].frames);
        VERIFY(a.shots[1].frames == b.shots[1].frames);
    }
}

int main(void) {
    nearer_anchor_beats_lower_index();
    slot_permutation_cannot_change_landing();
    expiry_removes_support_on_exact_step();
    drops_to_lower_anchor_after_upper_expires();
    leaving_platform_loses_grounded_contact();
    jump_does_not_stick_to_support();
    invalid_yellow_anchor_is_not_a_red_platform();
    inactive_or_expired_platform_cannot_intercept();
    stage_transition_clears_all_supports();
    repeated_anchor_lifecycle_is_deterministic();
    puts("PASS: 10 reference-city red anchor lifecycle, nearest-face, expiry and profile isolation cases");
    return 0;
}
