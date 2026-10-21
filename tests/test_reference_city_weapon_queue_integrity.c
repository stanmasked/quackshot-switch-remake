#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { \
    fprintf(stderr, "weapon queue FAIL line %d: %s\n", __LINE__, #e); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;
    return g;
}
static void no_shots(const QsGame *g) {
    for(int i=0;i<QS_MAX_SHOTS;i++)CHECK(!g->shots[i].active);
}
static void negative_unlock_cannot_keep_red_weapon(void) {
    const int invalid[]={INT_MIN,-100,-1};
    for(int i=0;i<3;i++) {
        QsGame g=city();
        g.red_unlocked=invalid[i];g.weapon=QS_RED;
        qs_step(&g,idle());
        CHECK(g.red_unlocked==0 && g.weapon==QS_YELLOW);
        no_shots(&g);
    }
}
static void positive_unlock_is_canonicalized(void) {
    QsGame g=city();
    g.red_unlocked=INT_MAX;
    g.weapon=QS_RED;
    qs_step(&g,idle());
    CHECK(g.red_unlocked==1 && g.weapon==QS_RED);
}
static void invalid_weapon_falls_back_to_yellow(void) {
    const int invalid[]={-1,2,INT_MIN,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.red_unlocked=1;g.weapon=(QsWeapon)invalid[i];
        qs_step(&g,idle());
        CHECK(g.red_unlocked==1 && g.weapon==QS_YELLOW);
    }
}
static void locked_red_selection_is_rejected(void) {
    QsGame g=city();
    g.red_unlocked=0;g.weapon=QS_RED;
    qs_step(&g,idle());
    CHECK(g.weapon==QS_YELLOW);
}
static void malformed_pending_shot_does_not_fire(void) {
    const int invalid[]={INT_MIN,-1,2,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.red_unlocked=1;g.weapon=QS_RED;
        g.pending_reference_shot=invalid[i];
        g.pending_reference_red=1;
        qs_step(&g,idle());
        CHECK(!g.pending_reference_shot && !g.pending_reference_red);
        no_shots(&g);
    }
}
static void orphan_red_metadata_is_removed(void) {
    QsGame g=city();
    g.red_unlocked=1;g.weapon=QS_RED;
    g.pending_reference_shot=0;
    g.pending_reference_red=1;
    qs_step(&g,idle());
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    no_shots(&g);
}
static void malformed_latched_red_fires_yellow(void) {
    const int invalid[]={-1,2,INT_MIN,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.red_unlocked=1;g.weapon=QS_RED;
        g.pending_reference_shot=1;
        g.pending_reference_red=invalid[i];
        qs_step(&g,idle());
        CHECK(!g.pending_reference_shot && !g.pending_reference_red);
        CHECK(g.shots[0].active && !g.shots[0].red);
        CHECK(g.shots[0].vx==5.5f);
    }
}
static void red_latch_without_unlock_cannot_fire_red(void) {
    QsGame g=city();
    g.pending_reference_shot=1;
    g.pending_reference_red=1;
    qs_step(&g,idle());
    CHECK(g.shots[0].active && !g.shots[0].red);
}
static void legitimate_red_latch_survives_weapon_change(void) {
    QsGame g=city();
    QsInput in=idle();
    g.red_unlocked=1;g.weapon=QS_RED;
    in.fire=1;
    qs_step(&g,in);
    CHECK(g.pending_reference_shot==1 && g.pending_reference_red==1);
    no_shots(&g);
    in=idle();in.change=1;
    qs_step(&g,in);
    CHECK(g.weapon==QS_YELLOW);
    CHECK(g.shots[0].active && g.shots[0].red);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
}
static void legitimate_yellow_latch_survives_weapon_change(void) {
    QsGame g=city();
    QsInput in=idle();
    g.red_unlocked=1;g.weapon=QS_YELLOW;
    in.fire=1;qs_step(&g,in);
    CHECK(g.pending_reference_shot==1 && !g.pending_reference_red);
    in=idle();in.change=1;qs_step(&g,in);
    CHECK(g.weapon==QS_RED);
    CHECK(g.shots[0].active && !g.shots[0].red);
}
static void change_and_fire_same_tick_latch_new_selection(void) {
    QsGame g=city();
    QsInput in=idle();
    g.red_unlocked=1;g.weapon=QS_YELLOW;
    in.change=in.fire=1;
    qs_step(&g,in);
    CHECK(g.weapon==QS_RED);
    CHECK(g.pending_reference_shot==1 && g.pending_reference_red==1);
    no_shots(&g);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].red);
}
static void malformed_metadata_then_fresh_fire_only_once(void) {
    QsGame g=city();
    QsInput in=idle();in.fire=1;
    g.red_unlocked=1;g.weapon=QS_RED;
    g.pending_reference_shot=INT_MAX;
    g.pending_reference_red=INT_MIN;
    qs_step(&g,in);
    CHECK(g.pending_reference_shot==1 && g.pending_reference_red==1);
    no_shots(&g);
    qs_step(&g,idle());
    CHECK(!g.pending_reference_shot && g.shots[0].active && g.shots[0].red);
    for(int i=1;i<QS_MAX_SHOTS;i++)CHECK(!g.shots[i].active);
}
static void back_consumes_bad_pending_flags(void) {
    QsGame g=city();QsInput in=idle();
    g.red_unlocked=1;g.weapon=QS_RED;
    g.pending_reference_shot=INT_MAX;
    g.pending_reference_red=INT_MAX;
    in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    no_shots(&g);
}
static void ordinary_profile_keeps_invalid_metadata(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    g.red_unlocked=INT_MIN;
    g.weapon=(QsWeapon)INT_MAX;
    qs_step(&g,idle());
    CHECK(g.red_unlocked==INT_MIN && (int)g.weapon==INT_MAX);
}
static void deterministic_500_ticks_of_bad_and_valid_latches(void) {
    QsGame a=city(),b;
    a.red_unlocked=1;a.weapon=QS_RED;
    b=a;
    for(int tick=0;tick<500;tick++) {
        QsInput in=idle();
        if(tick%29==0) {
            a.pending_reference_shot=b.pending_reference_shot=INT_MAX;
            a.pending_reference_red=b.pending_reference_red=INT_MIN;
        }
        if(tick%41==0) {
            a.weapon=b.weapon=(QsWeapon)INT_MAX;
        }
        if(tick%53==0) {
            a.red_unlocked=b.red_unlocked=INT_MAX;
        }
        if(tick%11==3)in.fire=1;
        if(tick%37==5)in.change=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.weapon==b.weapon && a.red_unlocked==b.red_unlocked);
        CHECK(a.red_unlocked==0 || a.red_unlocked==1);
        CHECK(a.weapon==QS_YELLOW || a.weapon==QS_RED);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        CHECK(a.pending_reference_red==b.pending_reference_red);
        CHECK(a.pending_reference_shot==0 || a.pending_reference_shot==1);
        CHECK(a.pending_reference_red==0 || a.pending_reference_red==1);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(int i=0;i<QS_MAX_SHOTS;i++) {
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].red==b.shots[i].red);
            CHECK(a.shots[i].x==b.shots[i].x);
        }
    }
    CHECK(a.frames==500 && b.frames==500);
}
int main(void) {
    negative_unlock_cannot_keep_red_weapon();
    positive_unlock_is_canonicalized();
    invalid_weapon_falls_back_to_yellow();
    locked_red_selection_is_rejected();
    malformed_pending_shot_does_not_fire();
    orphan_red_metadata_is_removed();
    malformed_latched_red_fires_yellow();
    red_latch_without_unlock_cannot_fire_red();
    legitimate_red_latch_survives_weapon_change();
    legitimate_yellow_latch_survives_weapon_change();
    change_and_fire_same_tick_latch_new_selection();
    malformed_metadata_then_fresh_fire_only_once();
    back_consumes_bad_pending_flags();
    ordinary_profile_keeps_invalid_metadata();
    deterministic_500_ticks_of_bad_and_valid_latches();
    puts("PASS: 15 city weapon and delayed-shot integrity scenarios with 500 ticks");
    return EXIT_SUCCESS;
}
