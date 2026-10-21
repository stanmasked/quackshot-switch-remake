#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"bonus score FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput q={0};return q; }
static QsGame city(void) {
    QsGame g; qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void token(QsGame *g,int index) {
    g->hero.x=(index ? QS_CITY_BONUS_B_X : QS_CITY_BONUS_A_X)-5;
    g->hero.y=188;g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
}
static void score_baseline_and_null_are_zero(void) {
    QsGame g=city();
    CHECK(qs_city_bonus_score(NULL)==0);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void first_token_A_worth_500(void) {
    QsGame g=city();
    token(&g,0);
    CHECK(g.city_bonus_mask==1 && qs_city_bonus_score(&g)==500);
}
static void first_token_B_worth_500(void) {
    QsGame g=city();
    token(&g,1);
    CHECK(g.city_bonus_mask==2 && qs_city_bonus_score(&g)==500);
}
static void both_orders_score_1000(void) {
    QsGame a=city(),b=city();
    token(&a,0);token(&a,1);
    token(&b,1);token(&b,0);
    CHECK(a.city_bonus_mask==3 && b.city_bonus_mask==3);
    CHECK(qs_city_bonus_score(&a)==1000 && qs_city_bonus_score(&b)==1000);
}
static void score_never_awards_more_for_repeated_contact(void) {
    QsGame g=city();
    for(int i=0;i<100;i++)token(&g,i%2);
    CHECK(g.city_bonus_mask==3 && qs_city_bonus_score(&g)==1000);
}
static void values_outside_two_bit_mask_are_rejected(void) {
    const int bad[]={INT_MIN,-1,4,7,INT_MAX};
    for(int i=0;i<5;i++) {
        QsGame g=city();
        g.city_bonus_mask=bad[i];
        CHECK(qs_city_bonus_score(&g)==0);
        qs_step(&g,idle());
        CHECK(g.city_bonus_mask==0 && qs_city_bonus_score(&g)==0);
    }
}
static void inferred_score_does_not_depend_on_health(void) {
    QsGame g=city();
    token(&g,0);token(&g,1);
    for(int hearts=1;hearts<=3;hearts++) {
        g.hero.hearts=hearts;
        CHECK(qs_city_bonus_score(&g)==1000);
    }
}
static void score_survives_fatal_contact_and_checkpoint_retry(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    token(&g,0);token(&g,1);
    CHECK(qs_city_bonus_score(&g)==1000);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X && g.hero.hearts==3);
    CHECK(qs_city_bonus_score(&g)==1000);
}
static void score_survives_pit_retry(void) {
    QsGame g=city();
    token(&g,0);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && qs_city_bonus_score(&g)==500);
}
static void score_clears_on_map_exit_and_new_attempt(void) {
    QsGame g=city();QsInput in=idle();
    token(&g,0);token(&g,1);
    CHECK(qs_city_bonus_score(&g)==1000);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_bonus_mask);
    CHECK(qs_city_bonus_score(&g)==0);
    qs_step(&g,idle());
    in=idle();in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void no_score_outside_opt_in_city_scene(void) {
    QsGame g=city();
    g.city_bonus_mask=3;
    g.mode=QS_MAP;CHECK(qs_city_bonus_score(&g)==0);
    g.mode=QS_STAGE;g.physics_profile=QS_PHYSICS_PROTOTYPE;
    CHECK(qs_city_bonus_score(&g)==0);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;g.stage=1;
    CHECK(qs_city_bonus_score(&g)==0);
}
static void prototype_keeps_old_gameplay_with_no_score(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.city_bonus_mask=3;
    CHECK(qs_city_bonus_score(&g)==0);
    g.city_bonus_mask=0;
    g.hero.x=QS_CITY_BONUS_A_X-5;
    qs_step(&g,idle());
    CHECK(g.city_bonus_mask==0 && qs_city_bonus_score(&g)==0);
}
static void deterministic_600_tick_score_replay(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<600;t++) {
        int phase=t%100;
        QsInput in=idle();
        if(phase==2 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_B_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=0;a.hero.vy=b.hero.vy=0;
        }
        if(phase==4 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_A_X-5;
        }
        if(phase==10 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==30 && a.mode==QS_STAGE)
            a.hero.x=b.hero.x=NAN;
        if(phase==60)in.back=1;
        if(phase==63)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(qs_city_bonus_score(&a)==qs_city_bonus_score(&b));
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(qs_city_bonus_score(&a)>=0 &&
              qs_city_bonus_score(&a)<=1000);
        CHECK(a.mode==b.mode && a.frames==b.frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    score_baseline_and_null_are_zero();
    first_token_A_worth_500();
    first_token_B_worth_500();
    both_orders_score_1000();
    score_never_awards_more_for_repeated_contact();
    values_outside_two_bit_mask_are_rejected();
    inferred_score_does_not_depend_on_health();
    score_survives_fatal_contact_and_checkpoint_retry();
    score_survives_pit_retry();
    score_clears_on_map_exit_and_new_attempt();
    no_score_outside_opt_in_city_scene();
    prototype_keeps_old_gameplay_with_no_score();
    deterministic_600_tick_score_replay();
    puts("PASS: 13 city bonus score cases and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
