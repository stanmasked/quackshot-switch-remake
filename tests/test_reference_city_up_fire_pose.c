#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "up-fire pose failure at line %d: %s\n", \
            __LINE__, #condition); exit(EXIT_FAILURE); \
} } while (0)

static QsInput none(void) {
    QsInput in = {0};
    return in;
}

static QsGame fresh(QsPhysicsProfile profile) {
    QsGame g;
    int k;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    for (k = 0; k < QS_MAX_ENEMIES; ++k)
        g.enemies[k].active = 0;
    for (k = 0; k < QS_MAX_PICKUPS; ++k)
        g.pickups[k].active = 0;
    qs_step(&g, none()); /* settle on an authored test floor */
    CHECK(g.hero.grounded);
    return g;
}

static void pose_lasts_exactly_twenty_ticks(void) {
    QsGame g = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsInput in = none();
    int tick;
    in.up = in.fire = 1;
    qs_step(&g, in);
    CHECK(g.reference_up_fire_frames == QS_CITY_UP_FIRE_POSE_TICKS);
    CHECK(g.hero.facing == 1 && !g.pending_reference_shot);
    CHECK(g.reference_up_projectile.active && g.reference_up_projectile.age == -3);
    CHECK(!g.shots[0].active);
    for (tick = 1; tick < QS_CITY_UP_FIRE_POSE_TICKS; ++tick) {
        qs_step(&g, in); /* held fire must not re-trigger the pose */
        CHECK(g.reference_up_fire_frames ==
              QS_CITY_UP_FIRE_POSE_TICKS - tick);
        if (tick == 1) {
            CHECK(!g.shots[0].active && g.reference_up_projectile.age == -2);
        }
    }
    qs_step(&g, in);
    CHECK(g.reference_up_fire_frames == 0);
}

static void facing_and_negative_controls(void) {
    QsGame left = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame fire = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame up = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame moving = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame jump = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame legacy = fresh(QS_PHYSICS_PROTOTYPE);
    QsInput in = none();
    in.left = 1; qs_step(&left, in);
    qs_step(&left, none());
    CHECK(left.hero.facing == -1 && left.hero.grounded);
    in = none(); in.up = in.fire = 1;
    qs_step(&left, in);
    CHECK(left.reference_up_fire_frames == 20);
    CHECK(left.hero.facing == -1);
    qs_step(&left, in);
    CHECK(!left.shots[0].active && left.reference_up_projectile.active);
    CHECK(left.reference_up_projectile.facing == -1);

    in = none(); in.up = 1; qs_step(&up, in);
    CHECK(up.reference_up_fire_frames == 0);
    in = none(); in.fire = 1; qs_step(&fire, in);
    CHECK(fire.reference_up_fire_frames == 0);
    CHECK(fire.pending_reference_shot == 1);
    in = none(); in.up = in.fire = in.right = 1;
    qs_step(&moving, in);
    CHECK(moving.reference_up_fire_frames == 0);
    CHECK(moving.pending_reference_shot == 1);

    in = none(); in.up = in.fire = in.jump = 1;
    qs_step(&jump, in);
    CHECK(jump.reference_up_fire_frames == 0);
    CHECK(jump.hero.vy < 0);
    in = none(); in.up = in.fire = 1;
    qs_step(&legacy, in);
    CHECK(legacy.reference_up_fire_frames == 0);
    CHECK(legacy.shots[0].active && legacy.shots[0].vx == 5.5f);
}

static void movement_pause_retry_and_damage(void) {
    QsGame g = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsInput in = none();
    in.up = in.fire = 1; qs_step(&g, in);
    CHECK(g.reference_up_fire_frames == 20);
    in = none(); in.change = in.back = 1;
    qs_step(&g, in);
    CHECK(g.city_paused && g.reference_up_fire_frames == 20);
    qs_step(&g, none());
    CHECK(g.reference_up_fire_frames == 20);
    qs_step(&g, in);
    CHECK(!g.city_paused && g.reference_up_fire_frames == 20);
    qs_step(&g, none());
    CHECK(g.reference_up_fire_frames == 19);
    in = none(); in.right = 1; qs_step(&g, in);
    CHECK(g.reference_up_fire_frames == 0);

    g = fresh(QS_PHYSICS_REFERENCE_CITY);
    in = none(); in.up = in.fire = 1; qs_step(&g, in);
    in = none(); in.back = in.dash = 1;
    qs_step(&g, in);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.reference_up_fire_frames == 0);
    CHECK(!g.pending_reference_shot);

    g = fresh(QS_PHYSICS_REFERENCE_CITY);
    in = none(); in.up = in.fire = 1; qs_step(&g, in);
    in = none(); in.back = 1; qs_step(&g, in);
    CHECK(g.mode == QS_MAP && g.reference_up_fire_frames == 0);

    g = fresh(QS_PHYSICS_REFERENCE_CITY);
    in = none(); in.up = in.fire = 1; qs_step(&g, in);
    g.enemies[0].active = 1;
    /* Put the foe behind the forward shot, but overlapping the hero. */
    g.enemies[0].x = g.enemies[0].min_x = g.enemies[0].max_x =
        g.hero.x - 12.0f;
    g.enemies[0].y = g.hero.y;
    qs_step(&g, none());
    CHECK(g.hero.hearts == 2 && g.reference_up_fire_frames == 0);

    g = fresh(QS_PHYSICS_REFERENCE_CITY);
    g.reference_up_fire_frames = 1000000;
    qs_step(&g, none());
    CHECK(g.reference_up_fire_frames == 0);
}

static void upward_shot_separate_from_horizontal(void) {
    QsGame aim = fresh(QS_PHYSICS_REFERENCE_CITY);
    QsGame plain = aim;
    QsInput up = none(), fire = none();
    up.up = up.fire = 1;
    fire.fire = 1;
    qs_step(&aim, up); qs_step(&plain, fire);
    qs_step(&aim, none()); qs_step(&plain, none());
    CHECK(aim.reference_up_projectile.active && !aim.shots[0].active);
    CHECK(!aim.pending_reference_shot);
    CHECK(!plain.reference_up_projectile.active && plain.shots[0].active);
    CHECK(plain.shots[0].vx == 5.5f);
    CHECK(plain.shots[0].y == plain.hero.y + 9);
    CHECK(aim.hero.x == plain.hero.x && aim.hero.y == plain.hero.y);
}

int main(void) {
    pose_lasts_exactly_twenty_ticks();
    facing_and_negative_controls();
    movement_pause_retry_and_damage();
    upward_shot_separate_from_horizontal();
    puts("PASS: synthetic reference-city upward-fire presentation tests");
    return EXIT_SUCCESS;
}
