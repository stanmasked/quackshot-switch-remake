#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"hero status FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    return g;
}
static void red_spawn(QsGame *g) {
    g->pickups[0].active=1;
    g->pickups[0].x=36;
    g->pickups[0].y=189;
    g->pickups[0].kind=QS_PICKUP_RED;
}
static void foe(QsGame *g,int id,float x) {
    QsEnemy *e=&g->enemies[id];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;
    e->min_x=e->max_x=x;e->dir=1;
}
static void corrupt_health_creates_one_protected_retry(void) {
    const int bad[]={0,-1,INT_MIN,4,INT_MAX};
    for(int i=0;i<5;i++) {
        QsGame g=city();
        g.hero.hearts=bad[i];
        g.hero.hurt_frames=0;
        g.hero.facing=-1;
        g.camera_x=50;g.render_camera_x=40;
        red_spawn(&g);
        qs_step(&g,idle());
        CHECK(g.mode==QS_STAGE && !g.cleared[0]);
        CHECK(g.hero.x==40 && g.hero.y==188);
        CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
        CHECK(g.hero.facing==1);
        CHECK(g.camera_x==0 && g.render_camera_x==0);
        CHECK(g.pickups[0].active && !g.red_unlocked);
        qs_step(&g,idle());
        CHECK(!g.pickups[0].active && g.red_unlocked);
        CHECK(g.hero.hurt_frames==88);
    }
}
static void malformed_health_cannot_earn_goal(void) {
    const int bad[]={INT_MIN,0,4,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        g.hero.hearts=bad[i];
        qs_step(&g,idle());
        CHECK(g.mode==QS_STAGE && !g.cleared[0]);
        CHECK(g.hero.x==40 && g.hero.hearts==3);
    }
}
static void valid_one_heart_still_dies_once(void) {
    QsGame g=city();
    foe(&g,0,100);foe(&g,1,100);
    g.hero.hearts=1;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.enemies[1].x==100);
}
static void valid_two_and_three_hearts_do_not_retry(void) {
    for(int hearts=2;hearts<=3;hearts++) {
        QsGame g=city();
        g.hero.hearts=hearts;
        qs_step(&g,idle());
        CHECK(g.hero.hearts==hearts && g.hero.hurt_frames==0);
        CHECK(g.hero.x==100);
    }
}
static void malformed_facing_normalizes_and_shoots_bounded_velocity(void) {
    const int values[]={INT_MIN,-99,0,99,INT_MAX};
    for(int i=0;i<5;i++) {
        QsGame g=city();
        QsInput in=idle();
        int expected=values[i]<0 ? -1 : 1;
        g.hero.facing=values[i];
        in.fire=1;
        qs_step(&g,in);
        CHECK(g.hero.facing==expected && g.pending_reference_shot==1);
        qs_step(&g,idle());
        CHECK(g.shots[0].active);
        CHECK(g.shots[0].vx==5.5f*expected);
        CHECK(isfinite(g.shots[0].x) && isfinite(g.shots[0].y));
        CHECK(g.hero.hearts==3);
    }
}
static void already_valid_facing_is_preserved(void) {
    QsGame a=city(),b=city();
    a.hero.facing=-1;
    b.hero.facing=1;
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.hero.facing==-1 && b.hero.facing==1);
}
static void malformed_grounded_ints_do_not_cause_spurious_jump(void) {
    QsGame a=city(),b=city();
    QsInput in=idle();in.jump=1;
    a.hero.y=b.hero.y=160;
    a.hero.grounded=INT_MIN;
    b.hero.grounded=INT_MAX;
    qs_step(&a,in);qs_step(&b,in);
    CHECK(a.hero.y>160 && a.hero.vy>0);
    CHECK(b.hero.y<160 && b.hero.vy<0);
    CHECK(a.hero.hearts==3 && b.hero.hearts==3);
}
static void jump_hold_corruption_is_clamped_before_increment(void) {
    QsGame a=city(),b=city(),c=city();
    a.hero.y=b.hero.y=c.hero.y=160;
    a.hero.grounded=b.hero.grounded=c.hero.grounded=0;
    a.reference_jump_steps=INT_MIN;
    b.reference_jump_steps=INT_MAX;
    c.reference_jump_steps=15;
    qs_step(&a,idle());qs_step(&b,idle());qs_step(&c,idle());
    CHECK(a.reference_jump_steps==1);
    CHECK(b.reference_jump_steps==16);
    CHECK(c.reference_jump_steps==16);
    CHECK(a.hero.hearts==3 && b.hero.hearts==3);
}
static void corrupted_health_with_back_cleans_pending_shot(void) {
    QsGame g=city();
    QsInput in=idle();in.back=1;
    g.hero.hearts=INT_MIN;
    g.pending_reference_shot=g.pending_reference_red=1;
    g.shots[0].active=g.shots[0].attached=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.cleared[0]);
    CHECK(g.hero.hearts==3 && g.hero.x==40);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(!g.shots[0].active && !g.shots[0].attached);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void corrupted_health_with_fire_queues_only_fresh_shot(void) {
    QsGame g=city();
    QsInput in=idle();in.fire=1;
    g.hero.hearts=4;
    g.pending_reference_shot=1;
    g.shots[0].active=1;
    qs_step(&g,in);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.pending_reference_shot==1 && !g.shots[0].active);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && !g.pending_reference_shot);
}
static void prototype_invalid_status_is_unchanged(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    g.hero.hearts=4;
    g.hero.facing=INT_MAX;
    g.reference_jump_steps=INT_MAX;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==4 && g.hero.facing==INT_MAX);
    CHECK(g.reference_jump_steps==INT_MAX);
}
static void deterministic_700_tick_mixed_status_recovery(void) {
    QsGame a=city(),b;int i;
    a.red_unlocked=1;a.weapon=QS_RED;
    b=a;
    for(int tick=0;tick<700;tick++) {
        QsInput in=idle();
        if(tick%41==0) {
            a.hero.hearts=b.hero.hearts=INT_MIN;
            red_spawn(&a);red_spawn(&b);
        }
        if(tick%53==0) {
            a.hero.facing=b.hero.facing=INT_MAX;
            a.hero.grounded=b.hero.grounded=INT_MIN;
        }
        if(tick%67==0)
            a.reference_jump_steps=b.reference_jump_steps=INT_MAX;
        if(tick%73==13) {
            a.hero.x=b.hero.x=100;
            a.hero.hearts=b.hero.hearts=1;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
            foe(&a,0,100);foe(&b,0,100);
        }
        if(tick%107==17) in.fire=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hearts>=1 && a.hero.hearts<=3);
        CHECK(a.hero.facing==b.hero.facing);
        CHECK(a.hero.facing==1 || a.hero.facing==-1);
        CHECK(a.hero.grounded==b.hero.grounded);
        CHECK(a.reference_jump_steps==b.reference_jump_steps);
        CHECK(a.reference_jump_steps>=0 && a.reference_jump_steps<=16);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        for(i=0;i<QS_MAX_SHOTS;i++)
            CHECK(a.shots[i].active==b.shots[i].active);
    }
    CHECK(a.frames==700 && b.frames==700);
}
int main(void) {
    corrupt_health_creates_one_protected_retry();
    malformed_health_cannot_earn_goal();
    valid_one_heart_still_dies_once();
    valid_two_and_three_hearts_do_not_retry();
    malformed_facing_normalizes_and_shoots_bounded_velocity();
    already_valid_facing_is_preserved();
    malformed_grounded_ints_do_not_cause_spurious_jump();
    jump_hold_corruption_is_clamped_before_increment();
    corrupted_health_with_back_cleans_pending_shot();
    corrupted_health_with_fire_queues_only_fresh_shot();
    prototype_invalid_status_is_unchanged();
    deterministic_700_tick_mixed_status_recovery();
    puts("PASS: 12 hero status regressions, bounded shots and 700 deterministic ticks");
    return EXIT_SUCCESS;
}
