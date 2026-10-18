#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "quick retry FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsInput retry_input(void) {
    QsInput in=idle();
    in.back=1;in.dash=1;
    return in;
}
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void touch_flag(QsGame *g) {
    g->hero.x=QS_CITY_CHECKPOINT_X-5;g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(g->city_checkpoint_active==1);
}
static void touch_bonus(QsGame *g,int index) {
    g->hero.x=(index?QS_CITY_BONUS_B_X:QS_CITY_BONUS_A_X)-5;
    g->hero.y=188;g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
}
static void retry(QsGame *g) { qs_step(g,retry_input()); }
static void expect_fresh_stage(const QsGame *g) {
    CHECK(g->mode==QS_STAGE && g->stage==0);
    CHECK(g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g->hero.x==40 && g->hero.y==188);
    CHECK(g->hero.hearts==3 && g->hero.hurt_frames==0);
    CHECK(g->hero.vx==0 && g->hero.vy==0);
    CHECK(g->hero.facing==1 && g->hero.grounded==0);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(g->city_attempt_frames==0);
    CHECK(g->city_checkpoint_active==0);
    CHECK(g->city_checkpoint_split_frames==0);
    CHECK(g->city_checkpoint_gap_frames==0);
    CHECK(g->city_checkpoint_gap_valid==0);
    CHECK(g->city_bonus_mask==0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    CHECK(!g->reference_jump_steps);
    for(int i=0;i<QS_MAX_SHOTS;i++)
        CHECK(!g->shots[i].active && !g->shots[i].attached);
}
static void quick_retry_keeps_stage_instead_of_map(void) {
    QsGame g=city();int f=g.frames;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(g.frames==f+1);
}
static void reset_attempt_progress_without_losing_best(void) {
    QsGame g=city();
    touch_flag(&g);
    touch_bonus(&g,0);touch_bonus(&g,1);
    CHECK(g.city_best_checkpoint_frames>=1);
    CHECK(g.city_best_bonus_score==1000);
    int best_checkpoint=g.city_best_checkpoint_frames;
    int before=g.frames;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(g.city_best_checkpoint_frames==best_checkpoint);
    CHECK(g.city_best_bonus_score==1000);
    CHECK(qs_city_best_checkpoint_time(&g)==best_checkpoint);
    CHECK(qs_city_best_bonus_score(&g)==1000);
    CHECK(qs_city_bonus_score(&g)==0);
    CHECK(g.frames==before+1);
}
static void preserve_clear_and_fastest_finish(void) {
    QsGame g=city();
    g.cleared[0]=1;g.cleared[1]=1;
    g.city_best_clear_frames=31;
    g.city_best_checkpoint_frames=10;
    g.city_best_bonus_score=500;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(g.cleared[0] && g.cleared[1]);
    CHECK(g.city_best_clear_frames==31);
    CHECK(qs_city_best_clear_time(&g)==31);
    CHECK(g.city_best_checkpoint_frames==10);
    CHECK(g.city_best_bonus_score==500);
}
static void keep_weapon_unlock_and_pickup_rules(void) {
    QsGame g=city();
    g.red_unlocked=1;g.weapon=QS_RED;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active);
    CHECK(g.pickups[1].active && g.pickups[1].kind==QS_PICKUP_HEART);
    CHECK(g.enemies[0].active && g.enemies[1].active);
}
static void discard_pending_shots_and_actors(void) {
    QsGame g=city();
    g.shots[0].active=g.shots[0].attached=1;
    g.shots[0].frames=80;g.shots[0].x=100;g.shots[0].y=160;
    g.pending_reference_shot=g.pending_reference_red=1;
    g.reference_jump_steps=10;
    g.hero.hurt_frames=61;g.hero.hearts=1;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(g.enemies[0].active && g.enemies[1].active);
}
static void held_input_consumed_once_and_no_movement(void) {
    QsGame g=city();QsInput in=retry_input();
    in.right=in.fire=in.jump=1;
    qs_step(&g,in);
    expect_fresh_stage(&g);
    CHECK(g.previous.back && g.previous.dash);
    CHECK(g.previous.right && g.previous.fire && g.previous.jump);
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE);
    CHECK(g.city_attempt_frames==1);
    CHECK(g.frames==2);
    /* A held button cannot queue an extra shot on the retry tick. */
    CHECK(!g.pending_reference_shot);
}
static void release_repress_allows_second_retry(void) {
    QsGame g=city();QsInput in=retry_input();
    qs_step(&g,in);
    qs_step(&g,in);
    CHECK(g.city_attempt_frames==1);
    qs_step(&g,idle());
    CHECK(g.city_attempt_frames==2);
    qs_step(&g,in);
    expect_fresh_stage(&g);
}
static void start_without_shoulder_returns_to_map(void) {
    QsGame g=city();QsInput in=idle();in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
}
static void prototype_chord_keeps_existing_map_exit(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    qs_step(&g,retry_input());
    CHECK(g.mode==QS_MAP);
}
static void other_stages_keep_existing_map_exit(void) {
    for(int st=1;st<QS_STAGE_COUNT;st++) {
        QsGame g=city();
        qs_begin_stage(&g,st);
        CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
        retry(&g);
        CHECK(g.mode==QS_MAP);
    }
}
static void finishing_level_wins_over_quick_retry(void) {
    QsGame g=city();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    g.city_attempt_frames=13;
    retry(&g);
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(qs_city_best_clear_time(&g)==14);
    CHECK(!g.city_attempt_frames);
}
static void combo_at_enemy_contact_does_not_kill(void) {
    QsGame g=city();
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=100;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    retry(&g);
    expect_fresh_stage(&g);
}
static void combo_at_pickup_does_not_consume_reward(void) {
    QsGame g=city();
    g.hero.x=179;g.hero.y=140;
    g.pickups[0].active=1;g.pickups[0].kind=QS_PICKUP_RED;
    retry(&g);
    expect_fresh_stage(&g);
    CHECK(!g.red_unlocked);
    CHECK(g.pickups[0].active);
}
static void invalid_position_then_retry_still_restores_start(void) {
    QsGame g=city();
    g.hero.x=NAN;g.camera_x=INFINITY;
    retry(&g);
    expect_fresh_stage(&g);
}
static void quick_retry_keeps_session_records_across_repeated_attempts(void) {
    QsGame g=city();
    g.city_best_checkpoint_frames=5;
    g.city_best_clear_frames=23;
    g.city_best_bonus_score=1000;
    for(int i=0;i<20;i++) {
        retry(&g);
        expect_fresh_stage(&g);
        CHECK(g.city_best_checkpoint_frames==5);
        CHECK(g.city_best_clear_frames==23);
        CHECK(g.city_best_bonus_score==1000);
        qs_step(&g,idle());
    }
}
static void deterministic_500_tick_quick_retry_trace(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<500;t++) {
        QsInput in=idle();
        int phase=t%100;
        if(phase==3 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(phase==5 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_BONUS_A_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
        }
        if(phase==11 && a.mode==QS_STAGE)
            in=retry_input();
        if(phase==20 && a.mode==QS_STAGE)
            in.back=1;
        if(phase==25 && a.mode==QS_MAP) {
            in.select=in.dash=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.frames==b.frames && a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.city_checkpoint_active==b.city_checkpoint_active);
        CHECK(a.city_best_checkpoint_frames==b.city_best_checkpoint_frames);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(a.city_best_bonus_score==b.city_best_bonus_score);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
    }
    CHECK(a.frames==500 && b.frames==500);
}
int main(void) {
    quick_retry_keeps_stage_instead_of_map();
    reset_attempt_progress_without_losing_best();
    preserve_clear_and_fastest_finish();
    keep_weapon_unlock_and_pickup_rules();
    discard_pending_shots_and_actors();
    held_input_consumed_once_and_no_movement();
    release_repress_allows_second_retry();
    start_without_shoulder_returns_to_map();
    prototype_chord_keeps_existing_map_exit();
    other_stages_keep_existing_map_exit();
    finishing_level_wins_over_quick_retry();
    combo_at_enemy_contact_does_not_kill();
    combo_at_pickup_does_not_consume_reward();
    invalid_position_then_retry_still_restores_start();
    quick_retry_keeps_session_records_across_repeated_attempts();
    deterministic_500_tick_quick_retry_trace();
    puts("PASS: 16 city quick-retry scenarios and 500 deterministic ticks");
    return EXIT_SUCCESS;
}
