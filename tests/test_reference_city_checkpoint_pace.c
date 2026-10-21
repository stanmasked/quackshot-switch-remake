#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"checkpoint pace FAIL %d: %s\n",__LINE__,#x); exit(EXIT_FAILURE); \
} } while(0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame city(void) {
    QsGame g;qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void ticks(QsGame *g,int n) {
    for(int i=0;i<n;i++)qs_step(g,idle());
}
static void touch(QsGame *g) {
    g->hero.x=QS_CITY_CHECKPOINT_X-5;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(g->city_checkpoint_active==1);
}
static void reenter(QsGame *g) {
    QsInput in=idle();in.back=1;qs_step(g,in);
    CHECK(g->mode==QS_MAP);
    in=idle();qs_step(g,in);
    in.select=in.dash=1;qs_step(g,in);
    CHECK(g->mode==QS_STAGE);
    CHECK(g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
}
static void no_comparison_before_first_flag(void) {
    QsGame g=city();
    CHECK(!qs_city_checkpoint_gap_valid(NULL));
    CHECK(!qs_city_checkpoint_gap_time(NULL));
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    ticks(&g,9);touch(&g);
    CHECK(g.city_checkpoint_split_frames==10);
    CHECK(g.city_best_checkpoint_frames==10);
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==0);
}
static void faster_second_run_shows_negative_gap(void) {
    QsGame g=city();ticks(&g,9);touch(&g);reenter(&g);
    ticks(&g,3);touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==-6);
    CHECK(qs_city_best_checkpoint_time(&g)==4);
}
static void slower_second_run_shows_positive_gap(void) {
    QsGame g=city();ticks(&g,2);touch(&g);reenter(&g);
    ticks(&g,12);touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==10);
    CHECK(qs_city_best_checkpoint_time(&g)==3);
}
static void exactly_tied_time_shows_valid_zero(void) {
    QsGame g=city();ticks(&g,4);touch(&g);reenter(&g);
    ticks(&g,4);touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==5);
}
static void repeated_touch_never_recomputes_gap(void) {
    QsGame g=city();ticks(&g,9);touch(&g);reenter(&g);
    ticks(&g,2);touch(&g);
    for(int i=0;i<30;i++) {
        touch(&g);
        CHECK(qs_city_checkpoint_gap_valid(&g));
        CHECK(qs_city_checkpoint_gap_time(&g)==-7);
        CHECK(qs_city_checkpoint_split_time(&g)==3);
    }
}
static void death_after_flag_keeps_negative_gap(void) {
    QsGame g=city();ticks(&g,9);touch(&g);reenter(&g);
    ticks(&g,2);touch(&g);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    QsEnemy *e=&g.enemies[0];e->active=1;
    e->x=e->min_x=e->max_x=g.hero.x;e->y=188;e->dir=1;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(g.hero.hearts==3);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==-7);
}
static void lethal_touch_cannot_produce_comparison(void) {
    QsGame g=city();touch(&g);reenter(&g);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.hearts=1;
    QsEnemy *e=&g.enemies[0];e->active=1;
    e->x=e->min_x=e->max_x=g.hero.x;e->y=188;e->dir=1;
    qs_step(&g,idle());
    CHECK(g.hero.x==40);
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==0);
}
static void back_resets_gap_without_erasing_record(void) {
    QsGame g=city();ticks(&g,9);touch(&g);reenter(&g);
    touch(&g);
    CHECK(qs_city_checkpoint_gap_time(&g)==-9);
    QsInput in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    CHECK(!g.city_checkpoint_gap_valid);
    CHECK(g.city_checkpoint_gap_frames==0);
    CHECK(qs_city_best_checkpoint_time(&g)==1);
}
static void finish_resets_gap_but_keeps_records(void) {
    QsGame g=city();ticks(&g,7);touch(&g);reenter(&g);
    touch(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(!g.city_checkpoint_gap_valid);
    CHECK(g.city_checkpoint_gap_frames==0);
    CHECK(qs_city_best_checkpoint_time(&g)==1);
    CHECK(qs_city_best_clear_time(&g)>0);
}
static void invalid_gap_is_sanitized(void) {
    const int bad[]={INT_MIN,-9999,9999,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_checkpoint_active=1;
        g.city_checkpoint_split_frames=2;
        g.city_checkpoint_gap_valid=1;
        g.city_checkpoint_gap_frames=bad[i];
        CHECK(!qs_city_checkpoint_gap_valid(&g));
        qs_step(&g,idle());
        CHECK(!g.city_checkpoint_gap_valid && g.city_checkpoint_gap_frames==0);
    }
}
static void invalid_flag_cannot_claim_comparison(void) {
    QsGame g=city();
    g.city_checkpoint_active=1;g.city_checkpoint_split_frames=5;
    g.city_checkpoint_gap_valid=INT_MAX;g.city_checkpoint_gap_frames=5;
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_gap_valid && !g.city_checkpoint_gap_frames);
}
static void normal_prototype_does_not_reveal_gap(void) {
    QsGame g=city();touch(&g);reenter(&g);touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==0);
}
static void session_boundary_restarts_comparison(void) {
    QsGame g=city();touch(&g);reenter(&g);touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    touch(&g);
    CHECK(!qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_best_checkpoint_time(&g)==1);
}
static void extreme_valid_gap_is_bounded(void) {
    QsGame g=city();touch(&g);reenter(&g);
    g.city_attempt_frames=QS_CITY_TIMER_MAX-1;
    touch(&g);
    CHECK(qs_city_checkpoint_gap_valid(&g));
    CHECK(qs_city_checkpoint_gap_time(&g)==QS_CITY_TIMER_MAX-1);
}
static void deterministic_480_tick_pace_replay(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<480;t++) {
        int phase=t%80;
        QsInput in=idle();
        if(phase==10 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(phase==30 && a.mode==QS_STAGE)
            in.back=1;
        if(phase==33 && a.mode==QS_MAP)
            in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_checkpoint_gap_frames==b.city_checkpoint_gap_frames);
        CHECK(a.city_checkpoint_gap_valid==b.city_checkpoint_gap_valid);
        CHECK(qs_city_checkpoint_gap_time(&a)==qs_city_checkpoint_gap_time(&b));
        CHECK(a.city_best_checkpoint_frames==b.city_best_checkpoint_frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==480);
}
int main(void) {
    no_comparison_before_first_flag();
    faster_second_run_shows_negative_gap();
    slower_second_run_shows_positive_gap();
    exactly_tied_time_shows_valid_zero();
    repeated_touch_never_recomputes_gap();
    death_after_flag_keeps_negative_gap();
    lethal_touch_cannot_produce_comparison();
    back_resets_gap_without_erasing_record();
    finish_resets_gap_but_keeps_records();
    invalid_gap_is_sanitized();
    invalid_flag_cannot_claim_comparison();
    normal_prototype_does_not_reveal_gap();
    session_boundary_restarts_comparison();
    extreme_valid_gap_is_bounded();
    deterministic_480_tick_pace_replay();
    puts("PASS: 15 checkpoint pace scenarios and 480 deterministic ticks");
    return EXIT_SUCCESS;
}
