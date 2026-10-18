#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"checkpoint split FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0}; return in; }

static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    return g;
}
static void step_n(QsGame *g,int n) {
    for(int i=0;i<n;i++) qs_step(g,idle());
}
static void touch(QsGame *g) {
    g->hero.x=QS_CITY_CHECKPOINT_X-5;
    g->hero.y=QS_CITY_CHECKPOINT_Y;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
}
static void leave(QsGame *g) {
    QsInput in=idle();in.back=1;
    qs_step(g,in);
    CHECK(g->mode==QS_MAP);
}
static void reenter(QsGame *g) {
    QsInput in=idle();
    CHECK(g->mode==QS_MAP);
    qs_step(g,in);
    in.select=in.dash=1;
    qs_step(g,in);
    CHECK(g->mode==QS_STAGE && g->stage==0);
    CHECK(g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    memset(g->enemies,0,sizeof(g->enemies));
    memset(g->pickups,0,sizeof(g->pickups));
}
static void foe(QsGame *g) {
    QsEnemy *e=&g->enemies[0];
    memset(e,0,sizeof(*e));
    e->active=1;
    e->x=e->min_x=e->max_x=g->hero.x;
    e->y=188;e->dir=1;
}
static void initially_no_split(void) {
    QsGame g=city();
    CHECK(qs_city_checkpoint_split_time(NULL)==0);
    CHECK(qs_city_best_checkpoint_time(NULL)==0);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==0);
    step_n(&g,6);
    CHECK(qs_city_attempt_time(&g)==6);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
}
static void first_safe_touch_records_exact_tick(void) {
    QsGame g=city();
    step_n(&g,5);
    touch(&g);
    CHECK(g.city_checkpoint_active==1);
    CHECK(qs_city_checkpoint_split_time(&g)==6);
    CHECK(qs_city_best_checkpoint_time(&g)==6);
    CHECK(qs_city_attempt_time(&g)==6);
}
static void instant_touch_earns_one_tick(void) {
    QsGame g=city();
    touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==1);
    CHECK(qs_city_best_checkpoint_time(&g)==1);
}
static void back_discards_split_but_keeps_best(void) {
    QsGame g=city();
    step_n(&g,8);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==9);
    leave(&g);
    CHECK(g.city_checkpoint_active==0);
    CHECK(g.city_checkpoint_split_frames==0);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==9);
    CHECK(qs_city_attempt_time(&g)==0);
}
static void reentry_restarts_split_but_not_best(void) {
    QsGame g=city();
    step_n(&g,9);touch(&g);
    leave(&g);reenter(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==10);
    CHECK(qs_city_attempt_time(&g)==0);
}
static void faster_split_improves_record(void) {
    QsGame g=city();
    step_n(&g,15);touch(&g);
    leave(&g);reenter(&g);
    step_n(&g,3);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==4);
    CHECK(qs_city_best_checkpoint_time(&g)==4);
}
static void slower_split_preserves_record(void) {
    QsGame g=city();
    step_n(&g,1);touch(&g);
    leave(&g);reenter(&g);
    step_n(&g,20);touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==21);
    CHECK(qs_city_best_checkpoint_time(&g)==2);
}
static void staying_on_flag_never_changes_first_split(void) {
    QsGame g=city();
    step_n(&g,6);touch(&g);
    for(int i=0;i<22;i++) {
        touch(&g);
        CHECK(qs_city_checkpoint_split_time(&g)==7);
        CHECK(qs_city_best_checkpoint_time(&g)==7);
    }
}
static void death_after_checkpoint_keeps_split_and_clock(void) {
    QsGame g=city();
    step_n(&g,2);touch(&g);
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g);qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X && g.hero.hearts==3);
    CHECK(g.city_checkpoint_active==1);
    CHECK(qs_city_checkpoint_split_time(&g)==3);
    CHECK(qs_city_best_checkpoint_time(&g)==3);
    CHECK(qs_city_attempt_time(&g)==4);
}
static void fatal_touch_never_records_split(void) {
    QsGame g=city();
    step_n(&g,2);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g);qs_step(&g,idle());
    CHECK(g.hero.x==40 && !g.city_checkpoint_active);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==0);
}
static void nonlethal_touch_records_after_damage(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    g.hero.hearts=2;g.hero.hurt_frames=0;
    foe(&g);qs_step(&g,idle());
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==69);
    CHECK(g.city_checkpoint_active==1);
    CHECK(qs_city_checkpoint_split_time(&g)==1);
}
static void pit_retry_does_not_grant_split(void) {
    QsGame g=city();
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40);
    CHECK(!g.city_checkpoint_active);
    CHECK(!qs_city_checkpoint_split_time(&g));
    CHECK(!qs_city_best_checkpoint_time(&g));
}
static void numeric_retry_does_not_grant_split(void) {
    QsGame g=city();
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.mode==QS_STAGE);
    CHECK(!qs_city_checkpoint_split_time(&g));
    CHECK(!qs_city_best_checkpoint_time(&g));
}
static void finish_keeps_best_and_discards_current_split(void) {
    QsGame g=city();
    step_n(&g,3);touch(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==4);
    CHECK(qs_city_best_clear_time(&g)==5);
}
static void corrupted_split_fields_are_sanitized_or_hidden(void) {
    const int invalid[]={INT_MIN,-1,QS_CITY_TIMER_MAX+1,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_checkpoint_active=1;
        g.city_checkpoint_split_frames=invalid[i];
        g.city_best_checkpoint_frames=invalid[i];
        CHECK(!qs_city_checkpoint_split_time(&g));
        CHECK(!qs_city_best_checkpoint_time(&g));
        qs_step(&g,idle());
        CHECK(g.city_checkpoint_split_frames==0);
        CHECK(g.city_best_checkpoint_frames==0);
    }
}
static void forged_checkpoint_flag_does_not_forge_a_split(void) {
    QsGame g=city();
    g.city_checkpoint_active=1;
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    CHECK(qs_city_checkpoint_split_time(&g)==0);
    CHECK(qs_city_best_checkpoint_time(&g)==0);
}
static void max_timer_split_never_overflows(void) {
    QsGame g=city();
    g.city_attempt_frames=QS_CITY_TIMER_MAX-1;
    touch(&g);
    CHECK(qs_city_checkpoint_split_time(&g)==QS_CITY_TIMER_MAX);
    CHECK(qs_city_best_checkpoint_time(&g)==QS_CITY_TIMER_MAX);
    step_n(&g,20);
    CHECK(qs_city_attempt_time(&g)==QS_CITY_TIMER_MAX);
    CHECK(qs_city_checkpoint_split_time(&g)==QS_CITY_TIMER_MAX);
}
static void best_visible_on_map_after_other_stage(void) {
    QsGame g=city();
    step_n(&g,4);touch(&g);
    leave(&g);
    CHECK(qs_city_best_checkpoint_time(&g)==5);
    QsInput in=idle();qs_step(&g,in);
    in.right=1;qs_step(&g,in);
    qs_step(&g,idle());
    in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(qs_city_best_checkpoint_time(&g)==0);
    in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && qs_city_best_checkpoint_time(&g)==5);
}
static void ordinary_prototype_is_not_a_split_trial(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;g.hero.y=188;
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active && !g.city_checkpoint_split_frames);
    CHECK(!qs_city_checkpoint_split_time(&g));
    CHECK(!qs_city_best_checkpoint_time(&g));
}
static void new_session_has_no_split_best(void) {
    QsGame g=city();touch(&g);
    CHECK(qs_city_best_checkpoint_time(&g)==1);
    qs_init(&g);
    CHECK(qs_city_best_checkpoint_time(&g)==0);
}
static void replay_600_ticks_across_split_and_attempts(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<600;t++) {
        int phase=t%120;
        QsInput in=idle();
        if(phase==8 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=0;
            a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(phase==12 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==30 && a.mode==QS_STAGE) a.hero.x=b.hero.x=NAN;
        if(phase==65 && a.mode==QS_STAGE) {
            in.back=1;
        }
        if(phase==70 && a.mode==QS_MAP) {
            in.select=in.dash=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_checkpoint_active==b.city_checkpoint_active);
        CHECK(a.city_checkpoint_split_frames==b.city_checkpoint_split_frames);
        CHECK(a.city_best_checkpoint_frames==b.city_best_checkpoint_frames);
        CHECK(qs_city_checkpoint_split_time(&a)==qs_city_checkpoint_split_time(&b));
        CHECK(qs_city_best_checkpoint_time(&a)==qs_city_best_checkpoint_time(&b));
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.city_best_checkpoint_frames>=0 &&
              a.city_best_checkpoint_frames<=QS_CITY_TIMER_MAX);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    initially_no_split();
    first_safe_touch_records_exact_tick();
    instant_touch_earns_one_tick();
    back_discards_split_but_keeps_best();
    reentry_restarts_split_but_not_best();
    faster_split_improves_record();
    slower_split_preserves_record();
    staying_on_flag_never_changes_first_split();
    death_after_checkpoint_keeps_split_and_clock();
    fatal_touch_never_records_split();
    nonlethal_touch_records_after_damage();
    pit_retry_does_not_grant_split();
    numeric_retry_does_not_grant_split();
    finish_keeps_best_and_discards_current_split();
    corrupted_split_fields_are_sanitized_or_hidden();
    forged_checkpoint_flag_does_not_forge_a_split();
    max_timer_split_never_overflows();
    best_visible_on_map_after_other_stage();
    ordinary_prototype_is_not_a_split_trial();
    new_session_has_no_split_best();
    replay_600_ticks_across_split_and_attempts();
    puts("PASS: 21 split-time scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
