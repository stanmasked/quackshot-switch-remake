#include "qs_game.h"
#include "qs_reference_projectile_curve.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(c) do { if (!(c)) { \
    fprintf(stderr, "vertical projectile FAIL %d: %s\n", __LINE__, #c); \
    exit(EXIT_FAILURE); \
} } while (0)

static QsInput idle(void) { QsInput in = {0}; return in; }
static QsGame scenario(void) {
    QsGame g;
    int i;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    for (i=0; i<QS_MAX_ENEMIES; ++i) g.enemies[i].active=0;
    for (i=0; i<QS_MAX_PICKUPS; ++i) g.pickups[i].active=0;
    qs_step(&g, idle());
    REQUIRE(g.hero.grounded);
    return g;
}

static void measured_window_and_visibility(void) {
    QsGame g = scenario();
    QsInput up = idle();
    int x=987, y=654, start_x, start_y, dx, dy, age;
    up.up=up.fire=1;
    qs_step(&g,up);
    REQUIRE(g.reference_up_projectile.active &&
            g.reference_up_projectile.age==-3);
    REQUIRE(!g.pending_reference_shot && !g.shots[0].active);
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    REQUIRE(x==987 && y==654);
    up=idle();
    qs_step(&g,up);
    REQUIRE(g.reference_up_projectile.age==-2);
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    qs_step(&g,up);
    REQUIRE(g.reference_up_projectile.age==-1);
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    qs_step(&g,up);
    REQUIRE(g.reference_up_projectile.age==0);
    start_x=g.reference_up_projectile.world_x;
    start_y=g.reference_up_projectile.world_y;
    for(age=0;age<QS_REFERENCE_UPWARD_OBSERVED_FRAMES;++age) {
        REQUIRE(qs_city_up_projectile_position(&g,&x,&y));
        REQUIRE(qs_reference_projectile_offset(
                QS_REFERENCE_PROJECTILE_UPWARD,1,age,&dx,&dy));
        REQUIRE(x==start_x+dx && y==start_y+dy);
        REQUIRE(!g.shots[0].active);
        if(age < QS_REFERENCE_UPWARD_OBSERVED_FRAMES-1) {
            qs_step(&g,up);
            REQUIRE(g.reference_up_projectile.age==age+1);
        }
    }
    qs_step(&g,up);
    REQUIRE(!g.reference_up_projectile.active);
    x=111;y=222;
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    REQUIRE(x==111 && y==222);
}

static void left_facing_and_movement(void) {
    QsGame g=scenario();
    QsInput in=idle();
    int x,y;
    in.left=1;qs_step(&g,in);
    qs_step(&g,idle());
    REQUIRE(g.hero.facing==-1 && g.hero.grounded);
    in=idle();in.up=in.fire=1;
    qs_step(&g,in);
    REQUIRE(g.reference_up_projectile.active &&
            g.reference_up_projectile.facing==-1);
    REQUIRE(!g.pending_reference_shot);
    in=idle();in.right=1;qs_step(&g,in);
    REQUIRE(g.reference_up_fire_frames==0);
    REQUIRE(g.reference_up_projectile.active);
    qs_step(&g,in);
    qs_step(&g,in);
    REQUIRE(qs_city_up_projectile_position(&g,&x,&y));
    REQUIRE(x==g.reference_up_projectile.world_x);
    REQUIRE(g.reference_up_projectile.age==0);
}

static void pause_and_scene_lifecycle(void) {
    QsGame g=scenario();
    QsInput in=idle();
    in.up=in.fire=1;qs_step(&g,in);
    in=idle();in.back=in.change=1;qs_step(&g,in);
    REQUIRE(g.city_paused && g.reference_up_projectile.age==-3);
    qs_step(&g,idle());
    REQUIRE(g.reference_up_projectile.age==-3);
    qs_step(&g,in);
    REQUIRE(!g.city_paused && g.reference_up_projectile.age==-3);
    qs_step(&g,idle());
    REQUIRE(g.reference_up_projectile.age==-2);
    in=idle();in.back=in.dash=1;qs_step(&g,in);
    REQUIRE(g.mode==QS_STAGE && !g.reference_up_projectile.active);
    g=scenario();in=idle();in.up=in.fire=1;qs_step(&g,in);
    in=idle();in.back=1;qs_step(&g,in);
    REQUIRE(g.mode==QS_MAP && !g.reference_up_projectile.active);
}

static void collision_is_not_inferred(void) {
    QsGame g=scenario();
    QsInput in=idle();
    int i;
    in.up=in.fire=1;qs_step(&g,in);
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=
        (float)g.reference_up_projectile.world_x;
    g.enemies[0].y=g.hero.y-22.0f;
    g.enemies[0].stunned_frames=0;
    for(i=0;i<10;i++) qs_step(&g,idle());
    REQUIRE(g.enemies[0].stunned_frames==0);
    REQUIRE(g.hero.hearts==3);
    REQUIRE(!g.shots[0].active);
}
static void malformed_state_fails_closed(void) {
    QsGame g=scenario();
    int x=111,y=222;
    g.reference_up_projectile.active=1;
    g.reference_up_projectile.age=INT_MAX;
    g.reference_up_projectile.facing=1;
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    REQUIRE(x==111 && y==222);
    qs_step(&g,idle());
    REQUIRE(!g.reference_up_projectile.active);
    g.reference_up_projectile.active=1;
    g.reference_up_projectile.age=0;
    g.reference_up_projectile.facing=1;
    g.reference_up_projectile.world_x=INT_MAX;
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&y));
    qs_step(&g,idle());
    REQUIRE(!g.reference_up_projectile.active);
    REQUIRE(!qs_city_up_projectile_position(&g,&x,&x));
}
static void horizontal_and_legacy_unchanged(void) {
    QsGame g=scenario();
    QsInput in=idle();
    in.fire=1;qs_step(&g,in);
    REQUIRE(g.pending_reference_shot && !g.reference_up_projectile.active);
    qs_step(&g,idle());
    REQUIRE(g.shots[0].active && g.shots[0].vx==5.5f);
    qs_init(&g);qs_begin_stage(&g,0);
    in=idle();in.up=in.fire=1;qs_step(&g,in);
    REQUIRE(g.shots[0].active && !g.reference_up_projectile.active);
}
int main(void) {
    measured_window_and_visibility();
    left_facing_and_movement();
    pause_and_scene_lifecycle();
    collision_is_not_inferred();
    malformed_state_fails_closed();
    horizontal_and_legacy_unchanged();
    puts("PASS: 6 collision-free bounded vertical projectile scenarios");
    return EXIT_SUCCESS;
}
