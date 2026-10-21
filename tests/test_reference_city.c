#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Numeric-only anchors derived from a privately observed original Mega Drive
 * city-stage run: X=61 -> 87 at +21 ticks, ->201 at +91; jump rise 65 px
 * over about 56 ticks; yellow plunger activates on the NEXT frame.
 * These tests check an explicitly approximated envelope, NOT original code. */
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "reference city FAIL line %d: %s\n", __LINE__, #cond); \
    exit(1); } } while (0)

static QsInput none(void) { QsInput i = {0}; return i; }
static void steps(QsGame *g, unsigned n, QsInput input) {
    while (n--) qs_step(g, input);
}
static QsGame enter_city(int reference) {
    QsGame g;
    QsInput entry = none();
    qs_init(&g);
    entry.select = 1;
    entry.dash = reference;
    qs_step(&g, entry);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.physics_profile == (reference ? QS_PHYSICS_REFERENCE_CITY :
                                            QS_PHYSICS_PROTOTYPE));
    steps(&g, 15, none());
    CHECK(g.hero.grounded);
    g.hero.x = 61.0f;
    return g;
}

static void measured_walking_envelope(void) {
    QsGame g = enter_city(1);
    QsInput right = none();
    right.right = 1;
    steps(&g, 21, right);
    CHECK(fabsf(g.hero.x - 87.0f) < 0.5f);
    steps(&g, 70, right);
    CHECK(fabsf(g.hero.x - 201.0f) < 0.6f);
    CHECK(g.hero.grounded && fabsf(g.hero.y - 188.0f) < 0.01f);
}

static void measured_jump_envelope(void) {
    QsGame g = enter_city(1);
    QsInput jump = none();
    float bottom = g.hero.y;
    float apex;
    unsigned ticks;
    jump.jump = 1;
    /* Genesis: initial upward velocity is CONSTANT for 15 held frames.
     * Subsequent held input cannot prolong it beyond that bound. */
    steps(&g, 15, jump);
    CHECK(fabsf(g.hero.vy - (-2.9296875f)) < 0.0001f);
    CHECK(g.reference_jump_steps == 15);
    jump.jump = 0;
    steps(&g, 15, jump);
    ticks = 30;
    apex = g.hero.y;
    CHECK(fabsf((bottom - apex) - 65.0f) < 1.0f);
    CHECK(!g.hero.grounded);
    while (!g.hero.grounded && ticks < 60) {
        qs_step(&g, jump);
        ++ticks;
    }
    CHECK(g.hero.grounded);
    CHECK(ticks >= 55 && ticks <= 58);
    CHECK(fabsf(g.hero.y - bottom) < 0.02f);
}
 
static void measured_short_jump(void) {
    QsGame g = enter_city(1);
    QsInput jump = none();
    float bottom = g.hero.y, apex = bottom;
    unsigned tick;
    jump.jump = 1;
    qs_step(&g, jump);
    jump.jump = 0;
    CHECK(fabsf(g.hero.vy - (-2.9296875f)) < 0.0001f);
    for (tick = 2; tick <= 35 && !g.hero.grounded; ++tick) {
        qs_step(&g, jump);
        if (g.hero.y < apex) apex = g.hero.y;
    }
    CHECK(g.hero.grounded);
    CHECK(bottom - apex > 23 && bottom - apex < 25);
    CHECK(tick-1 >= 30 && tick-1 <= 32);
}
 
static void measured_fixed_point_acceleration_and_braking(void) {
    QsGame g = enter_city(1);
    QsInput right = none();
    unsigned i;
    right.right = 1;
    for (i = 1; i <= 11; ++i) {
        qs_step(&g, right);
        CHECK(fabsf(g.hero.vx - fminf(i * 0.15625f, 1.625f)) < 0.00001f);
    }
    CHECK(fabsf(g.hero.x - (61.0f + 10.21875f)) < 0.0001f);
    right.right = 0;
    for (i = 1; i <= 11; ++i) {
        qs_step(&g, right);
        CHECK(fabsf(g.hero.vx - fmaxf(1.625f - i*0.15625f, 0.0f)) < 0.00001f);
    }
    CHECK(fabsf(g.hero.vx) < 0.00001f);
    /* A short eight-frame tap coasts a predictable 8 more frames. */
    g = enter_city(1);
    right.right = 1;
    steps(&g, 8, right);
    right.right = 0;
    steps(&g, 8, right);
    CHECK(fabsf(g.hero.x - 71.0f) < 0.0001f);
    CHECK(g.hero.vx == 0.0f);
}

static void projectile_is_one_tick_deferred(void) {
    QsGame g = enter_city(1);
    QsInput fire = none();
    unsigned i, active;
    fire.fire = 1;
    qs_step(&g, fire);
    CHECK(g.pending_reference_shot == 1);
    for (i = 0, active = 0; i < QS_MAX_SHOTS; ++i)
        active += (unsigned)g.shots[i].active;
    CHECK(active == 0);
    fire.fire = 0;
    qs_step(&g, fire);
    CHECK(g.pending_reference_shot == 0);
    for (i = 0, active = 0; i < QS_MAX_SHOTS; ++i)
        active += (unsigned)g.shots[i].active;
    CHECK(active == 1);
}

static void original_prototype_is_unchanged(void) {
    QsGame g = enter_city(0);
    QsInput right = none();
    float start = g.hero.x;
    right.right = 1;
    steps(&g, 21, right);
    CHECK(fabsf(g.hero.x - (start + 21.0f * 1.9f)) < 0.02f);
    CHECK(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    right.right = 0;
    right.fire = 1;
    qs_step(&g, right);
    CHECK(g.pending_reference_shot == 0);
    CHECK(g.shots[0].active); /* Prototype shoots without delay. */
}

static void profile_never_leaks_to_unmeasured_levels(void) {
    QsGame g = enter_city(1);
    qs_begin_stage(&g, 1);
    CHECK(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    CHECK(g.pending_reference_shot == 0);
    CHECK(g.stage == 1);
    qs_init(&g);
    CHECK(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    g.selected_stage = 2;
    {
        QsInput both = none();
        both.select = 1; both.dash = 1;
        qs_step(&g, both);
    }
    CHECK(g.stage == 2 && g.physics_profile == QS_PHYSICS_PROTOTYPE);
}

static void reference_replay_is_deterministic(void) {
    QsGame a = enter_city(1);
    QsGame b = enter_city(1);
    unsigned t;
    for (t = 0; t < 145; ++t) {
        QsInput in = none();
        in.right = t < 91;
        in.jump = t == 8;
        in.fire = t == 75;
        qs_step(&a, in);
        qs_step(&b, in);
    }
    CHECK(a.hero.x == b.hero.x && a.hero.y == b.hero.y);
    CHECK(a.hero.vx == b.hero.vx && a.hero.vy == b.hero.vy);
    CHECK(a.pending_reference_shot == b.pending_reference_shot);
}

int main(void) {
    measured_walking_envelope();
    measured_jump_envelope();
    measured_short_jump();
    measured_fixed_point_acceleration_and_braking();
    projectile_is_one_tick_deferred();
    original_prototype_is_unchanged();
    profile_never_leaks_to_unmeasured_levels();
    reference_replay_is_deterministic();
    puts("PASS: 8 measured city physics checks, fixed ramp, variable-height jump, prototype unchanged");
    return 0;
}
