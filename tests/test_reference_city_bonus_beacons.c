#include "qs_game.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"bonus beacons FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(QsPhysicsProfile profile) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=profile;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void at(QsGame *g,float x) {
    g->hero.x=x;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
}
static void foe(QsGame *g,float x) {
    QsEnemy *e=&g->enemies[0];memset(e,0,sizeof(*e));
    e->active=1;e->x=e->min_x=e->max_x=x;
    e->y=188;e->dir=1;
}
static void clean_bonus_start(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.city_bonus_mask==0);
    CHECK(QS_CITY_BONUS_VALUE==500);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0);
}
static void first_token_awards_one_stamp(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    CHECK(g.hero.hearts==3);
}
static void second_token_alone_awards_second_stamp(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_B_X-5);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==2);
}
static void either_order_yields_exactly_two_stamps(void) {
    QsGame a=city(QS_PHYSICS_REFERENCE_CITY),b=city(QS_PHYSICS_REFERENCE_CITY);
    at(&a,QS_CITY_BONUS_A_X-5);qs_step(&a,idle());
    at(&a,QS_CITY_BONUS_B_X-5);qs_step(&a,idle());
    at(&b,QS_CITY_BONUS_B_X-5);qs_step(&b,idle());
    at(&b,QS_CITY_BONUS_A_X-5);qs_step(&b,idle());
    CHECK(a.city_bonus_mask==3 && b.city_bonus_mask==3);
}
static void edge_touch_without_overlap_does_not_award(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-12);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0);
    at(&g,QS_CITY_BONUS_A_X+10);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0);
}
static void repeat_crossing_cannot_multiply_reward(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    for(int t=0;t<100;t++) {
        at(&g,QS_CITY_BONUS_A_X-5);
        qs_step(&g,idle());
        CHECK(g.city_bonus_mask==1);
    }
}
static void fatal_contact_does_not_award_during_old_life(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0);
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(g.hero.hurt_frames==89);
}
static void nonfatal_contact_can_award(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);
    g.hero.hearts=2;g.hero.hurt_frames=0;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==69);
}
static void pit_retry_keeps_earned_bonus_but_awards_none(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    g.hero.x=21*QS_TILE;g.hero.y=261;
    g.hero.vy=7.5f;g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hurt_frames==89);
    CHECK(g.city_bonus_mask==1);
}
static void numeric_retry_preserves_earned_bonus(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(g.hero.hurt_frames==89);
}
static void checkpoint_and_beacons_survive_life_restart(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_CHECKPOINT_X-5);qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1 && g.city_bonus_mask==1);
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(g.hero.hurt_frames==89);
    memset(g.enemies,0,sizeof(g.enemies));
    at(&g,QS_CITY_BONUS_B_X-5);
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==3);
}
static void back_clears_progress_and_new_attempt_restores_tokens(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=idle();
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    at(&g,QS_CITY_BONUS_B_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==3);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.city_bonus_mask==0);
    in=idle();qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.city_bonus_mask==0);
    memset(g.enemies,0,sizeof(g.enemies));
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
}
static void finish_clears_earned_bonus_but_keeps_stage_clear(void) {
    QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]==1);
    CHECK(g.city_bonus_mask==0);
}
static void invalid_mask_is_not_forged_progress(void) {
    const int invalid[]={-1,4,INT_MIN,INT_MAX};
    for(int j=0;j<4;j++) {
        QsGame g=city(QS_PHYSICS_REFERENCE_CITY);
        g.city_bonus_mask=invalid[j];
        qs_step(&g,idle());
        CHECK(g.city_bonus_mask==0);
    }
}
static void ordinary_prototype_ignores_bonus_objects(void) {
    QsGame g=city(QS_PHYSICS_PROTOTYPE);
    at(&g,QS_CITY_BONUS_A_X-5);qs_step(&g,idle());
    at(&g,QS_CITY_BONUS_B_X-5);qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0);
    g.city_bonus_mask=1;
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==1);
}
static void deterministic_600_tick_bonus_route(void) {
    QsGame a=city(QS_PHYSICS_REFERENCE_CITY),b=a;
    for(int t=0;t<600;t++) {
        int phase=t%90;
        QsInput in=idle();
        if(phase==2) {
            at(&a,QS_CITY_CHECKPOINT_X-5);at(&b,QS_CITY_CHECKPOINT_X-5);
        }
        if(phase==4) {
            at(&a,QS_CITY_BONUS_B_X-5);at(&b,QS_CITY_BONUS_B_X-5);
        }
        if(phase==6) {
            at(&a,QS_CITY_BONUS_A_X-5);at(&b,QS_CITY_BONUS_A_X-5);
        }
        if(phase==9) {
            a.hero.hearts=b.hero.hearts=1;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
            foe(&a,a.hero.x);foe(&b,b.hero.x);
        }
        if(phase==16) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==30) {
            a.hero.x=b.hero.x=NAN;
        }
        if(phase==60) in.back=1;
        if(phase==63) in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(a.city_bonus_mask>=0 && a.city_bonus_mask<=3);
        CHECK(a.city_checkpoint_active==b.city_checkpoint_active);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.camera_x==b.camera_x);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    clean_bonus_start();
    first_token_awards_one_stamp();
    second_token_alone_awards_second_stamp();
    either_order_yields_exactly_two_stamps();
    edge_touch_without_overlap_does_not_award();
    repeat_crossing_cannot_multiply_reward();
    fatal_contact_does_not_award_during_old_life();
    nonfatal_contact_can_award();
    pit_retry_keeps_earned_bonus_but_awards_none();
    numeric_retry_preserves_earned_bonus();
    checkpoint_and_beacons_survive_life_restart();
    back_clears_progress_and_new_attempt_restores_tokens();
    finish_clears_earned_bonus_but_keeps_stage_clear();
    invalid_mask_is_not_forged_progress();
    ordinary_prototype_ignores_bonus_objects();
    deterministic_600_tick_bonus_route();
    puts("PASS: 16 optional bonus beacon scenarios with 600 deterministic steps");
    return EXIT_SUCCESS;
}
