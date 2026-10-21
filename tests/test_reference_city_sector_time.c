#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"city sector FAIL line %d: %s\n",__LINE__,#x); \
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
static void ticks(QsGame *g,int n){
    for(int i=0;i<n;i++)qs_step(g,idle());
}
static void touch(QsGame *g){
    g->hero.x=QS_CITY_CHECKPOINT_X-5;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());CHECK(g->city_checkpoint_active==1);
}
static void finish(QsGame *g){
    CHECK(g->mode==QS_STAGE);
    g->hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    g->hero.y=188;g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(g->mode==QS_MAP && g->cleared[0]);
}
static void abandon(QsGame *g){
    QsInput in=idle();in.back=1;qs_step(g,in);
    CHECK(g->mode==QS_MAP);
}
static void reenter(QsGame *g){
    CHECK(g->mode==QS_MAP);
    qs_step(g,idle());
    QsInput in=idle();in.select=in.dash=1;qs_step(g,in);
    CHECK(g->mode==QS_STAGE && g->stage==0 &&
          g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
}
static void initially_no_sector(void){
    QsGame g=city();
    CHECK(qs_city_sector_time(NULL)==0);
    CHECK(qs_city_best_sector_time(NULL)==0);
    CHECK(qs_city_sector_time(&g)==0 && qs_city_best_sector_time(&g)==0);
    ticks(&g,9);
    CHECK(qs_city_attempt_time(&g)==9 && qs_city_sector_time(&g)==0);
}
static void zero_length_on_checkpoint_contact(void){
    QsGame g=city();ticks(&g,6);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==7);
    CHECK(qs_city_sector_time(&g)==0);
    CHECK(qs_city_best_sector_time(&g)==0);
}
static void sector_counts_only_post_checkpoint_ticks(void){
    QsGame g=city();ticks(&g,8);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==9);
    ticks(&g,11);
    CHECK(qs_city_attempt_time(&g)==20);
    CHECK(qs_city_sector_time(&g)==11);
    CHECK(!qs_city_best_sector_time(&g));
}
static void completing_without_checkpoint_records_no_sector(void){
    QsGame g=city();ticks(&g,10);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==11);
    CHECK(qs_city_best_sector_time(&g)==0);
    CHECK(qs_city_sector_time(&g)==0);
}
static void actual_finish_records_elapsed_minus_split(void){
    QsGame g=city();ticks(&g,5);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==6);
    ticks(&g,9);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==16);
    CHECK(qs_city_best_checkpoint_time(&g)==6);
    CHECK(qs_city_best_sector_time(&g)==10);
    CHECK(qs_city_sector_time(&g)==0);
}
static void back_after_checkpoint_does_not_record_sector(void){
    QsGame g=city();touch(&g);ticks(&g,25);abandon(&g);
    CHECK(qs_city_best_checkpoint_time(&g)==1);
    CHECK(qs_city_best_sector_time(&g)==0);
    CHECK(qs_city_best_clear_time(&g)==0);
}
static void quick_retry_discards_current_without_best(void){
    QsGame g=city();ticks(&g,4);touch(&g);ticks(&g,12);
    QsInput in=idle();in.back=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.city_attempt_frames==0);
    CHECK(qs_city_sector_time(&g)==0);
    CHECK(qs_city_best_sector_time(&g)==0);
}

static void best_sector_can_differ_from_fastest_total(void){
    QsGame g=city();ticks(&g,99);touch(&g);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==101);
    CHECK(qs_city_best_sector_time(&g)==1);
    reenter(&g);touch(&g);ticks(&g,12);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==14);
    CHECK(qs_city_best_sector_time(&g)==1);
}
static void improved_sector_from_slower_total(void){
    QsGame g=city();touch(&g);ticks(&g,20);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==22);
    CHECK(qs_city_best_sector_time(&g)==21);
    reenter(&g);ticks(&g,40);touch(&g);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==22);
    CHECK(qs_city_best_sector_time(&g)==1);
}
static void pit_respawn_does_not_reset_live_sector(void){
    QsGame g=city();touch(&g);ticks(&g,5);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(qs_city_sector_time(&g)==6);
    CHECK(!qs_city_best_sector_time(&g));
}
static void training_pause_does_not_advance_sector(void){
    QsGame g=city();touch(&g);ticks(&g,3);
    QsInput in=idle();in.change=in.back=1;
    qs_step(&g,in);CHECK(g.city_paused);
    for(int t=0;t<75;t++)qs_step(&g,idle());
    CHECK(qs_city_sector_time(&g)==3);
    qs_step(&g,in);CHECK(!g.city_paused);
    CHECK(qs_city_sector_time(&g)==3);
    qs_step(&g,idle());CHECK(qs_city_sector_time(&g)==4);
}
static void corrupted_sector_metadata_cannot_forge_record(void){
    QsGame g=city();
    g.city_checkpoint_active=1;
    g.city_checkpoint_split_frames=QS_CITY_TIMER_MAX;
    g.city_attempt_frames=8;
    CHECK(qs_city_sector_time(&g)==0);
    finish(&g);
    CHECK(!qs_city_best_sector_time(&g));
}
static void invalid_session_bests_are_hidden(void){
    int values[]={INT_MIN,-1,0,QS_CITY_TIMER_MAX,INT_MAX};
    for(int k=0;k<5;k++){
        QsGame g=city();
        g.city_best_sector_frames=values[k];
        CHECK(!qs_city_best_sector_time(&g));
        abandon(&g);
        CHECK(!qs_city_best_sector_time(&g));
    }
}
static void longest_saturated_sector_is_safe(void){
    QsGame g=city();touch(&g);
    g.city_attempt_frames=QS_CITY_TIMER_MAX-1;
    qs_step(&g,idle());
    CHECK(qs_city_sector_time(&g)==QS_CITY_TIMER_MAX-1);
    for(int k=0;k<40;k++)qs_step(&g,idle());
    CHECK(qs_city_sector_time(&g)==QS_CITY_TIMER_MAX-1);
    finish(&g);
    CHECK(qs_city_best_sector_time(&g)==QS_CITY_TIMER_MAX-1);
}
static void best_persists_across_other_stages_on_map(void){
    QsGame g=city();touch(&g);ticks(&g,7);finish(&g);
    CHECK(qs_city_best_sector_time(&g)==8);
    qs_step(&g,idle());
    QsInput in=idle();in.right=1;qs_step(&g,in);
    qs_step(&g,idle());in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.stage==1 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_sector_time(&g)==0);
    in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_sector_time(&g)==8);
}
static void base_profile_and_fresh_session_have_no_sector(void){
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.city_best_sector_frames=30;
    CHECK(qs_city_sector_time(&g)==0 && qs_city_best_sector_time(&g)==0);
    qs_init(&g);CHECK(qs_city_best_sector_time(&g)==0);
}
static void deterministic_600_tick_sector_route(void){
    QsGame a=city(),b=a;
    for(int t=0;t<600;t++){
        QsInput in=idle();int phase=t%100;
        if(phase==9 && a.mode==QS_STAGE){
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(phase==20 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        }
        if(phase==25 && a.mode==QS_MAP)in.select=in.dash=1;
        if(phase==50 && a.mode==QS_STAGE)in.back=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.city_best_sector_frames==b.city_best_sector_frames);
        CHECK(a.city_checkpoint_split_frames==b.city_checkpoint_split_frames);
        CHECK(qs_city_sector_time(&a)==qs_city_sector_time(&b));
        CHECK(qs_city_best_sector_time(&a)==qs_city_best_sector_time(&b));
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==600);
}
int main(void){
    initially_no_sector();
    zero_length_on_checkpoint_contact();
    sector_counts_only_post_checkpoint_ticks();
    completing_without_checkpoint_records_no_sector();
    actual_finish_records_elapsed_minus_split();
    back_after_checkpoint_does_not_record_sector();
    quick_retry_discards_current_without_best();
    best_sector_can_differ_from_fastest_total();
    improved_sector_from_slower_total();
    pit_respawn_does_not_reset_live_sector();
    training_pause_does_not_advance_sector();
    corrupted_sector_metadata_cannot_forge_record();
    invalid_session_bests_are_hidden();
    longest_saturated_sector_is_safe();
    best_persists_across_other_stages_on_map();
    base_profile_and_fresh_session_have_no_sector();
    deterministic_600_tick_sector_route();
    puts("PASS: 17 post-checkpoint sector cases and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
