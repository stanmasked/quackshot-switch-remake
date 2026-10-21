#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"time trial FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)
static QsInput idle(void) { QsInput in={0};return in; }
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
static void tick(QsGame *g,int count) {
    for(int i=0;i<count;i++)qs_step(g,idle());
}
static void finish(QsGame *g) {
    CHECK(g->mode==QS_STAGE && g->stage==0);
    g->hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    g->hero.y=188;g->hero.vx=g->hero.vy=0;
    qs_step(g,idle());
    CHECK(g->mode==QS_MAP && g->cleared[0]==1);
}
static void reenter(QsGame *g) {
    QsInput in=idle();
    CHECK(g->mode==QS_MAP);
    qs_step(g,in);
    in.select=in.dash=1;qs_step(g,in);
    CHECK(g->mode==QS_STAGE && g->stage==0);
    CHECK(g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
}
static void new_game_has_no_clear_time(void) {
    QsGame g=city();
    CHECK(!qs_city_attempt_time(NULL) && !qs_city_best_clear_time(NULL));
    CHECK(!qs_city_attempt_time(&g) && !qs_city_best_clear_time(&g));
}
static void counted_frames_exclude_map_start(void) {
    QsGame g=city();
    tick(&g,4);
    CHECK(qs_city_attempt_time(&g)==4);
    CHECK(qs_city_best_clear_time(&g)==0);
}
static void exit_back_disqualifies_partial_run(void) {
    QsGame g=city();QsInput in=idle();
    tick(&g,17);in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.city_attempt_frames==0);
    CHECK(qs_city_attempt_time(&g)==0);
    CHECK(qs_city_best_clear_time(&g)==0);
    tick(&g,19);
    CHECK(!g.city_attempt_frames && !qs_city_best_clear_time(&g));
}
static void crossed_exit_finishes_first_tick(void) {
    QsGame g=city();
    finish(&g);
    CHECK(qs_city_best_clear_time(&g)==1);
    CHECK(g.city_attempt_frames==0);
}
static void movement_exit_finishes_and_counts_tick(void) {
    QsGame g=city();QsInput in=idle();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE-0.1f;
    in.right=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_clear_time(&g)==1);
}
static void finish_beat_back_on_exit_boundary(void) {
    QsGame g=city();QsInput in=idle();
    tick(&g,3);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_clear_time(&g)==4);
}
static void map_does_not_advance_personal_record(void) {
    QsGame g=city();
    tick(&g,8);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==9);
    tick(&g,50);
    CHECK(qs_city_best_clear_time(&g)==9);
    CHECK(g.city_attempt_frames==0);
}
static void faster_second_attempt_wins(void) {
    QsGame g=city();
    tick(&g,30);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==31);
    reenter(&g);tick(&g,3);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==4);
}
static void slower_second_attempt_does_not_replace_best(void) {
    QsGame g=city();
    tick(&g,3);finish(&g);
    reenter(&g);tick(&g,50);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==4);
}
static void failed_attempt_between_clears_does_not_change_best(void) {
    QsGame g=city();QsInput in=idle();
    tick(&g,9);finish(&g);
    reenter(&g);tick(&g,29);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_clear_time(&g)==10);
    reenter(&g);tick(&g,4);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==5);
}
static void pit_respawn_consumes_attempt_time(void) {
    QsGame g=city();
    tick(&g,3);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hurt_frames==89);
    CHECK(qs_city_attempt_time(&g)==4);
    tick(&g,1);
    CHECK(qs_city_attempt_time(&g)==5);
}
static void damaged_position_does_not_forge_clear(void) {
    QsGame g=city();
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.mode==QS_STAGE && !g.cleared[0]);
    CHECK(!qs_city_best_clear_time(&g));
    CHECK(qs_city_attempt_time(&g)==1);
}
static void corrupted_timers_are_bounded_and_safe(void) {
    const int bad[]={INT_MIN,-1,QS_CITY_TIMER_MAX+1,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_attempt_frames=bad[i];
        g.city_best_clear_frames=bad[i];
        CHECK(!qs_city_attempt_time(&g) && !qs_city_best_clear_time(&g));
        qs_step(&g,idle());
        CHECK(g.city_attempt_frames==1);
        CHECK(g.city_best_clear_frames==0);
        finish(&g);
        CHECK(qs_city_best_clear_time(&g)==2);
    }
}
static void saturated_time_does_not_overflow(void) {
    QsGame g=city();
    g.city_attempt_frames=QS_CITY_TIMER_MAX-1;
    qs_step(&g,idle());
    CHECK(qs_city_attempt_time(&g)==QS_CITY_TIMER_MAX);
    for(int i=0;i<40;i++)qs_step(&g,idle());
    CHECK(qs_city_attempt_time(&g)==QS_CITY_TIMER_MAX);
    finish(&g);
    CHECK(qs_city_best_clear_time(&g)==QS_CITY_TIMER_MAX);
}
static void other_stages_do_not_modify_city_trial(void) {
    QsGame g=city();QsInput in=idle();
    tick(&g,2);finish(&g);
    CHECK(qs_city_best_clear_time(&g)==3);
    qs_step(&g,idle());in.right=1;qs_step(&g,in);
    qs_step(&g,idle());in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_clear_time(&g)==0);
    CHECK(qs_city_attempt_time(&g)==0);
    tick(&g,15);in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_clear_time(&g)==3);
}
static void ordinary_prototype_has_no_time_trial(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    tick(&g,17);
    CHECK(g.city_attempt_frames==0 && g.city_best_clear_frames==0);
    CHECK(qs_city_attempt_time(&g)==0);
    CHECK(qs_city_best_clear_time(&g)==0);
}
static void new_session_resets_best(void) {
    QsGame g=city();finish(&g);
    CHECK(qs_city_best_clear_time(&g)==1);
    qs_init(&g);CHECK(qs_city_best_clear_time(&g)==0);
}
static void replay_640_ticks(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<640;t++) {
        int phase=t%80;
        QsInput in=idle();
        if(phase==20 && a.mode==QS_STAGE && b.mode==QS_STAGE) {
            a.hero.x=b.hero.x=NAN;
        }
        if(phase==45 && a.mode==QS_STAGE && b.mode==QS_STAGE) {
            a.hero.x=b.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        }
        if(phase==47 && a.mode==QS_MAP && b.mode==QS_MAP) {
            in.select=in.dash=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.city_best_clear_frames==b.city_best_clear_frames);
        CHECK(qs_city_attempt_time(&a)==qs_city_attempt_time(&b));
        CHECK(qs_city_best_clear_time(&a)==qs_city_best_clear_time(&b));
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.city_attempt_frames>=0 && a.city_attempt_frames<=QS_CITY_TIMER_MAX);
    }
    CHECK(a.frames==640 && b.frames==640);
}
int main(void) {
    new_game_has_no_clear_time();
    counted_frames_exclude_map_start();
    exit_back_disqualifies_partial_run();
    crossed_exit_finishes_first_tick();
    movement_exit_finishes_and_counts_tick();
    finish_beat_back_on_exit_boundary();
    map_does_not_advance_personal_record();
    faster_second_attempt_wins();
    slower_second_attempt_does_not_replace_best();
    failed_attempt_between_clears_does_not_change_best();
    pit_respawn_consumes_attempt_time();
    damaged_position_does_not_forge_clear();
    corrupted_timers_are_bounded_and_safe();
    saturated_time_does_not_overflow();
    other_stages_do_not_modify_city_trial();
    ordinary_prototype_has_no_time_trial();
    new_session_resets_best();
    replay_640_ticks();
    puts("PASS: 18 time-trial scenarios and 640 deterministic ticks");
    return EXIT_SUCCESS;
}
