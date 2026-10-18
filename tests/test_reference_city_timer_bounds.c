#include "qs_game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr,"timer bounds FAIL %d: %s\n",__LINE__,#c); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void foe(QsGame *g,int slot,float x,int stun) {
    QsEnemy *e=&g->enemies[slot];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;
    e->min_x=e->max_x=x;e->dir=1;
    e->stunned_frames=stun;
}
static void negative_immunity_no_longer_protects(void) {
    QsGame g=city();
    foe(&g,0,100,0);
    g.hero.hurt_frames=-1;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void int_min_immunity_no_longer_underflows(void) {
    QsGame g=city();
    foe(&g,0,100,0);
    g.hero.hurt_frames=INT_MIN;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void int_max_immunity_is_capped_at_spawn_duration(void) {
    QsGame g=city();
    foe(&g,0,100,0);
    g.hero.hurt_frames=INT_MAX;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
    for(int i=0;i<89;i++) {
        qs_step(&g,idle());
        CHECK(g.hero.hearts==3);
    }
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void just_above_cap_is_normalized(void) {
    QsGame g=city();
    g.hero.hurt_frames=91;
    qs_step(&g,idle());
    CHECK(g.hero.hurt_frames==89);
}
static void legitimate_immunity_max_is_unchanged(void) {
    QsGame g=city();
    g.hero.hurt_frames=90;
    qs_step(&g,idle());
    CHECK(g.hero.hurt_frames==89);
}
static void last_safe_tick_still_blocks_contact(void) {
    QsGame g=city();
    foe(&g,0,100,0);
    g.hero.hurt_frames=1;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void regular_hurt_period_expires_exactly(void) {
    QsGame g=city();
    foe(&g,0,100,0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    for(int i=0;i<69;i++) {
        qs_step(&g,idle());
        CHECK(g.hero.hearts==2);
    }
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==69);
}
static void int_max_enemy_stun_has_finite_duration(void) {
    QsGame g=city();
    foe(&g,0,100,INT_MAX);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179 && g.hero.hearts==3);
    for(int i=0;i<179;i++) {
        qs_step(&g,idle());
        CHECK(g.hero.hearts==3);
    }
    CHECK(g.enemies[0].stunned_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void negative_enemy_stun_makes_contact_immediately(void) {
    QsGame g=city();
    foe(&g,0,100,INT_MIN);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void last_enemy_stun_tick_does_not_hurt_early(void) {
    QsGame g=city();
    foe(&g,0,100,1);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.enemies[0].stunned_frames==0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void mixed_wakeup_contacts_only_damage_once(void) {
    QsGame g=city();
    foe(&g,0,100,1);
    foe(&g,1,100,INT_MAX);
    foe(&g,2,100,0);
    g.hero.hurt_frames=0;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.enemies[1].stunned_frames==179);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void negative_immunity_can_trigger_one_fatal_respawn(void) {
    QsGame g=city();
    foe(&g,0,100,0);foe(&g,1,100,INT_MAX);foe(&g,2,40,INT_MIN);
    g.hero.hearts=1;g.hero.hurt_frames=INT_MIN;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.enemies[1].x==100);
    CHECK(g.enemies[1].stunned_frames==180);
    CHECK(g.enemies[2].stunned_frames==0);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void legacy_profile_stays_bytewise_semantic(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    foe(&g,0,100,INT_MAX);
    g.hero.hurt_frames=-3;
    qs_step(&g,idle());
    CHECK(g.hero.hurt_frames==-4);
    CHECK(g.enemies[0].stunned_frames==INT_MAX-1);
    CHECK(g.hero.hearts==3);
}
static void deterministic_600_ticks_with_corrupt_timers(void) {
    QsGame a=city(),b;
    b=a;
    for(int t=0;t<600;t++) {
        if(t%37==0) {
            a.hero.hurt_frames=b.hero.hurt_frames=INT_MIN;
            foe(&a,0,100,INT_MAX);
            foe(&b,0,100,INT_MAX);
        }
        if(t%47==0) {
            a.hero.hurt_frames=b.hero.hurt_frames=INT_MAX;
            foe(&a,1,100,-1);
            foe(&b,1,100,-1);
        }
        if(t%73==0) {
            a.hero.hurt_frames=b.hero.hurt_frames=91;
            foe(&a,2,100,1);
            foe(&b,2,100,1);
        }
        qs_step(&a,idle());qs_step(&b,idle());
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.hero.hurt_frames>=0 && a.hero.hurt_frames<=89);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(int j=0;j<3;j++) {
            CHECK(a.enemies[j].stunned_frames==b.enemies[j].stunned_frames);
            CHECK(a.enemies[j].stunned_frames>=0);
            CHECK(a.enemies[j].stunned_frames<=179);
        }
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    negative_immunity_no_longer_protects();
    int_min_immunity_no_longer_underflows();
    int_max_immunity_is_capped_at_spawn_duration();
    just_above_cap_is_normalized();
    legitimate_immunity_max_is_unchanged();
    last_safe_tick_still_blocks_contact();
    regular_hurt_period_expires_exactly();
    int_max_enemy_stun_has_finite_duration();
    negative_enemy_stun_makes_contact_immediately();
    last_enemy_stun_tick_does_not_hurt_early();
    mixed_wakeup_contacts_only_damage_once();
    negative_immunity_can_trigger_one_fatal_respawn();
    legacy_profile_stays_bytewise_semantic();
    deterministic_600_ticks_with_corrupt_timers();
    puts("PASS: 14 timer-boundary regressions and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
