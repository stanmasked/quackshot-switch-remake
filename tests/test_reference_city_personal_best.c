#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"city personal best FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput q={0};return q; }
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void collect(QsGame *g,int which) {
    g->hero.x=(which ? QS_CITY_BONUS_B_X : QS_CITY_BONUS_A_X)-5;
    g->hero.y=188;g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
}
static void reset_attempt(QsGame *g) {
    QsInput in=idle();in.back=1;qs_step(g,in);
    CHECK(g->mode==QS_MAP && g->city_bonus_mask==0);
    in=idle();qs_step(g,in);
    in.select=in.dash=1;qs_step(g,in);
    CHECK(g->mode==QS_STAGE && g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
    g->hero.vx=g->hero.vy=0;
}
static void initial_record_zero(void) {
    QsGame g=city();
    CHECK(qs_city_best_bonus_score(NULL)==0);
    CHECK(qs_city_best_bonus_score(&g)==0);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void first_beacon_sets_500_personal_best(void) {
    QsGame g=city();
    collect(&g,0);
    CHECK(g.city_bonus_mask==1);
    CHECK(qs_city_bonus_score(&g)==500);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void either_first_beacon_sets_500(void) {
    QsGame g=city();
    collect(&g,1);
    CHECK(g.city_bonus_mask==2 && qs_city_best_bonus_score(&g)==500);
}
static void second_beacon_sets_1000(void) {
    QsGame g=city();
    collect(&g,1);collect(&g,0);
    CHECK(qs_city_bonus_score(&g)==1000);
    CHECK(qs_city_best_bonus_score(&g)==1000);
}
static void back_preserves_best_but_resets_current(void) {
    QsGame g=city();QsInput in=idle();
    collect(&g,0);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_bonus_score(&g)==0);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void reentry_shows_best_and_zero_current(void) {
    QsGame g=city();
    collect(&g,0);collect(&g,1);
    reset_attempt(&g);
    CHECK(qs_city_bonus_score(&g)==0);
    CHECK(qs_city_best_bonus_score(&g)==1000);
    CHECK(g.hero.x==40 && !g.city_checkpoint_active);
}
static void second_attempt_worse_does_not_lower_record(void) {
    QsGame g=city();
    collect(&g,0);collect(&g,1);
    reset_attempt(&g);
    collect(&g,0);
    CHECK(qs_city_bonus_score(&g)==500);
    CHECK(qs_city_best_bonus_score(&g)==1000);
}
static void second_attempt_can_improve_record(void) {
    QsGame g=city();
    collect(&g,1);
    reset_attempt(&g);
    CHECK(qs_city_best_bonus_score(&g)==500);
    collect(&g,0);collect(&g,1);
    CHECK(qs_city_best_bonus_score(&g)==1000);
}
static void repeated_contacts_cannot_raise_score_past_1000(void) {
    QsGame g=city();
    for(int i=0;i<90;i++)collect(&g,i%2);
    CHECK(qs_city_bonus_score(&g)==1000);
    CHECK(qs_city_best_bonus_score(&g)==1000);
}
static void lethal_retry_keeps_current_and_best(void) {
    QsGame g=city();
    collect(&g,1);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=g.hero.x;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(qs_city_bonus_score(&g)==500);
    CHECK(qs_city_best_bonus_score(&g)==500);
}
static void finish_preserves_record_on_map(void) {
    QsGame g=city();
    collect(&g,0);collect(&g,1);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]==1);
    CHECK(g.city_bonus_mask==0 && qs_city_best_bonus_score(&g)==1000);
}
static void invalid_record_is_zero_and_sanitized(void) {
    const int bad[]={INT_MIN,-500,1,499,501,999,1001,INT_MAX};
    for(int i=0;i<8;i++) {
        QsGame g=city();g.city_best_bonus_score=bad[i];
        CHECK(qs_city_best_bonus_score(&g)==0);
        qs_step(&g,idle());
        CHECK(g.city_best_bonus_score==0);
        CHECK(qs_city_best_bonus_score(&g)==0);
    }
}
static void forged_mask_without_new_contact_cannot_raise_best(void) {
    QsGame g=city();
    g.city_bonus_mask=3;g.city_best_bonus_score=0;
    qs_step(&g,idle());
    CHECK(qs_city_best_bonus_score(&g)==0);
}
static void normal_prototype_does_not_set_or_expose_record(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.city_best_bonus_score=500;
    CHECK(qs_city_best_bonus_score(&g)==0);
    g.hero.x=QS_CITY_BONUS_A_X-5;
    qs_step(&g,idle());
    CHECK(g.city_best_bonus_score==500);
    CHECK(qs_city_best_bonus_score(&g)==0);
}
static void deterministic_600_tick_record_replay(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<600;t++) {
        int phase=t%100;
        QsInput in=idle();
        if(phase==2 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_A_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=0;a.hero.vy=b.hero.vy=0;
        }
        if(phase==5 && a.mode==QS_STAGE)
            a.hero.x=b.hero.x=QS_CITY_BONUS_B_X-5;
        if(phase==20 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==35 && a.mode==QS_STAGE)
            a.hero.x=b.hero.x=NAN;
        if(phase==60)in.back=1;
        if(phase==64)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_best_bonus_score==b.city_best_bonus_score);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(qs_city_best_bonus_score(&a)==qs_city_best_bonus_score(&b));
        CHECK(qs_city_bonus_score(&a)==qs_city_bonus_score(&b));
        CHECK(a.city_best_bonus_score==0 ||
              a.city_best_bonus_score==500 ||
              a.city_best_bonus_score==1000);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    initial_record_zero();
    first_beacon_sets_500_personal_best();
    either_first_beacon_sets_500();
    second_beacon_sets_1000();
    back_preserves_best_but_resets_current();
    reentry_shows_best_and_zero_current();
    second_attempt_worse_does_not_lower_record();
    second_attempt_can_improve_record();
    repeated_contacts_cannot_raise_score_past_1000();
    lethal_retry_keeps_current_and_best();
    finish_preserves_record_on_map();
    invalid_record_is_zero_and_sanitized();
    forged_mask_without_new_contact_cannot_raise_best();
    normal_prototype_does_not_set_or_expose_record();
    deterministic_600_tick_record_replay();
    puts("PASS: 15 personal best city bonus scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
