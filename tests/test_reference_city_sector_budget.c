#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr,"city sector budget FAIL line %d: %s\n",__LINE__,#c); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void){QsInput in={0};return in;}
static QsGame city(void){
    QsGame g;qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    return g;
}
static void tick(QsGame *g,int n){
    for(int i=0;i<n;i++)qs_step(g,idle());
}
static void checkpoint(QsGame *g){
    g->hero.x=QS_CITY_CHECKPOINT_X-5;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(qs_city_checkpoint_split_time(g)>0);
}
static void finish(QsGame *g){
    g->hero.x=(QS_COLUMNS-4)*QS_TILE+1;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(g->mode==QS_MAP && g->cleared[0]);
}
static void reenter(QsGame *g){
    CHECK(g->mode==QS_MAP);
    qs_step(g,idle());
    QsInput in=idle();in.select=in.dash=1;qs_step(g,in);
    CHECK(g->mode==QS_STAGE &&
          g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
}
static void seed_pb(QsGame *g,int frames){
    /* Only an earned, completed checkpoint->exit route can create PB. */
    checkpoint(g);
    tick(g,frames-1);
    finish(g);
    CHECK(qs_city_best_sector_time(g)==frames);
    reenter(g);
}
static void no_record_is_not_a_zero_budget(void){
    QsGame g=city();
    CHECK(!qs_city_sector_budget_valid(NULL));
    CHECK(qs_city_sector_budget_frames(NULL)==0);
    checkpoint(&g);
    CHECK(!qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==0);
    tick(&g,8);
    CHECK(!qs_city_sector_budget_valid(&g));
}
static void best_needs_current_checkpoint(void){
    QsGame g=city();seed_pb(&g,6);
    CHECK(qs_city_best_sector_time(&g)==6);
    CHECK(!qs_city_sector_budget_valid(&g));
    CHECK(!qs_city_sector_budget_frames(&g));
}
static void fresh_checkpoint_starts_at_full_budget(void){
    QsGame g=city();seed_pb(&g,6);
    checkpoint(&g);
    CHECK(qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==6);
    CHECK(qs_city_sector_time(&g)==0);
}
static void remaining_budget_counts_down_each_live_tick(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    for(int i=0;i<6;i++){
        CHECK(qs_city_sector_budget_frames(&g)==6-i);
        tick(&g,1);
    }
    CHECK(qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==0);
}
static void tie_is_valid_zero_not_missing_result(void){
    QsGame g=city();seed_pb(&g,4);checkpoint(&g);
    tick(&g,4);
    CHECK(qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==0);
}
static void slower_time_produces_negative_budget(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    tick(&g,9);
    CHECK(qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==-3);
    CHECK(qs_city_best_sector_time(&g)==6);
}
static void pause_never_consumes_budget(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    tick(&g,2);
    QsInput in=idle();in.back=in.change=1;
    qs_step(&g,in);
    CHECK(g.city_paused);
    for(int i=0;i<70;i++)qs_step(&g,idle());
    CHECK(qs_city_sector_budget_frames(&g)==4);
    qs_step(&g,in);
    CHECK(!g.city_paused);
    CHECK(qs_city_sector_budget_frames(&g)==4);
    tick(&g,1);
    CHECK(qs_city_sector_budget_frames(&g)==3);
}
static void death_retry_does_not_replenish_budget(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    tick(&g,3);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(qs_city_sector_budget_frames(&g)==2);
}
static void quick_retry_hides_bar_without_losing_pb(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && !g.city_checkpoint_active);
    CHECK(qs_city_best_sector_time(&g)==6);
    CHECK(!qs_city_sector_budget_valid(&g));
}
static void back_discards_live_budget_without_losing_pb(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    QsInput in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_sector_time(&g)==6);
    CHECK(!qs_city_sector_budget_valid(&g));
    CHECK(!qs_city_sector_budget_frames(&g));
}
static void finishing_clears_live_budget_and_can_improve_best(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    tick(&g,1);
    CHECK(qs_city_sector_budget_frames(&g)==5);
    finish(&g);
    CHECK(qs_city_best_sector_time(&g)==2);
    CHECK(!qs_city_sector_budget_valid(&g));
    CHECK(!qs_city_sector_budget_frames(&g));
}
static void ordinary_profile_and_other_stages_hide_budget(void){
    QsGame g=city();seed_pb(&g,6);checkpoint(&g);
    CHECK(qs_city_sector_budget_valid(&g));
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    CHECK(!qs_city_sector_budget_valid(&g));
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    g.stage=1;
    CHECK(!qs_city_sector_budget_valid(&g));
}
static void invalid_pb_never_provides_budget(void){
    const int bad[]={INT_MIN,-1,0,QS_CITY_TIMER_MAX,INT_MAX};
    for(int i=0;i<5;i++){
        QsGame g=city();checkpoint(&g);
        g.city_best_sector_frames=bad[i];
        CHECK(!qs_city_sector_budget_valid(&g));
        CHECK(!qs_city_sector_budget_frames(&g));
    }
}
static void invalid_counters_cannot_forge_budget(void){
    QsGame g=city();seed_pb(&g,6);
    checkpoint(&g);
    g.city_attempt_frames=g.city_checkpoint_split_frames-1;
    CHECK(!qs_city_sector_budget_valid(&g));
    g.city_attempt_frames=INT_MAX;
    CHECK(!qs_city_sector_budget_valid(&g));
    g.city_attempt_frames=QS_CITY_TIMER_MAX;
    g.city_checkpoint_split_frames=INT_MIN;
    CHECK(!qs_city_sector_budget_valid(&g));
    g.city_checkpoint_split_frames=INT_MAX;
    CHECK(!qs_city_sector_budget_valid(&g));
    g.city_checkpoint_split_frames=1;
    g.city_checkpoint_active=0;
    CHECK(!qs_city_sector_budget_valid(&g));
}
static void saturated_clock_never_overflows_signed_budget(void){
    QsGame g=city();seed_pb(&g,1);
    checkpoint(&g);
    g.city_attempt_frames=QS_CITY_TIMER_MAX;
    CHECK(qs_city_sector_budget_valid(&g));
    CHECK(qs_city_sector_budget_frames(&g)==-(QS_CITY_TIMER_MAX-2));
}
static void deterministic_480_tick_budget_trace(void){
    QsGame a=city(),b;
    seed_pb(&a,6);
    b=a;
    for(int t=0;t<480;t++){
        int phase=t%80;
        QsInput in=idle();
        if(phase==3 && a.mode==QS_STAGE){
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(phase==27 && a.mode==QS_STAGE){in.back=in.change=1;}
        if(phase==40 && a.mode==QS_STAGE)in.back=in.change=1;
        if(phase==56 && a.mode==QS_STAGE)in.back=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_paused==b.city_paused);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(qs_city_sector_budget_valid(&a)==qs_city_sector_budget_valid(&b));
        CHECK(qs_city_sector_budget_frames(&a)==qs_city_sector_budget_frames(&b));
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==480);
}
int main(void){
    no_record_is_not_a_zero_budget();
    best_needs_current_checkpoint();
    fresh_checkpoint_starts_at_full_budget();
    remaining_budget_counts_down_each_live_tick();
    tie_is_valid_zero_not_missing_result();
    slower_time_produces_negative_budget();
    pause_never_consumes_budget();
    death_retry_does_not_replenish_budget();
    quick_retry_hides_bar_without_losing_pb();
    back_discards_live_budget_without_losing_pb();
    finishing_clears_live_budget_and_can_improve_best();
    ordinary_profile_and_other_stages_hide_budget();
    invalid_pb_never_provides_budget();
    invalid_counters_cannot_forge_budget();
    saturated_clock_never_overflows_signed_budget();
    deterministic_480_tick_budget_trace();
    puts("PASS: 16 sector budget cases and 480 deterministic ticks");
    return EXIT_SUCCESS;
}
