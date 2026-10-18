#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"city pause FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsInput pause_chord(void) {
    QsInput in=idle();in.change=in.back=1;return in;
}
static QsInput retry_chord(void) {
    QsInput in=idle();in.dash=in.back=1;return in;
}
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    g.hero.vx=g.hero.vy=0;
    return g;
}
static void toggle(QsGame *g) {
    qs_step(g,idle());
    qs_step(g,pause_chord());
}
static void no_pause_before_chord(void) {
    QsGame g=city();
    CHECK(!g.city_paused);
    qs_step(&g,idle());
    CHECK(!g.city_paused && g.city_attempt_frames==1);
}
static void chord_pauses_before_ticking_clock(void) {
    QsGame g=city();
    qs_step(&g,pause_chord());
    CHECK(g.city_paused && g.mode==QS_STAGE);
    CHECK(g.city_attempt_frames==0 && g.frames==1);
    CHECK(g.hero.x==100);
}
static void entire_game_state_frozen_except_input_and_global_frame(void) {
    QsGame g=city();
    qs_step(&g,pause_chord());
    for(int t=0;t<120;t++) {
        QsInput in=idle();
        in.right=in.jump=in.fire=in.change=in.dash=1;
        QsGame before=g;
        qs_step(&g,in);
        before.frames++;
        before.previous=in;
        CHECK(memcmp(&g,&before,sizeof(g))==0);
    }
    CHECK(g.city_paused==1 && g.city_attempt_frames==0);
}
static void holding_start_is_not_second_toggle(void) {
    QsGame g=city();QsInput p=pause_chord();
    qs_step(&g,p);
    qs_step(&g,p);
    qs_step(&g,p);
    CHECK(g.city_paused==1);
    qs_step(&g,idle());
    qs_step(&g,p);
    CHECK(!g.city_paused && !g.city_attempt_frames);
    qs_step(&g,p);
    CHECK(!g.city_paused);
}
static void resume_does_not_count_resume_input_frame(void) {
    QsGame g=city();
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    qs_step(&g,pause_chord());
    CHECK(!g.city_paused && !g.city_attempt_frames);
    qs_step(&g,idle());
    CHECK(g.city_attempt_frames==1);
}
static void pause_preserves_crate_debris_frames(void) {
    QsGame g=city();
    g.city_crate_active=0;
    g.city_crate_fx_frames=14;
    qs_step(&g,pause_chord());
    for(int t=0;t<35;t++)qs_step(&g,idle());
    CHECK(g.city_crate_fx_frames==14);
    qs_step(&g,pause_chord());
    CHECK(g.city_crate_fx_frames==14);
    qs_step(&g,idle());
    CHECK(g.city_crate_fx_frames==13);
}
static void pause_keeps_shot_and_enemy_from_updating(void) {
    QsGame g=city();
    g.shots[0].active=1;
    g.shots[0].x=QS_CITY_CRATE_X-7;g.shots[0].y=197;
    g.shots[0].vx=1;g.shots[0].frames=65;
    g.enemies[0].active=1;g.enemies[0].x=410;
    g.enemies[0].min_x=400;g.enemies[0].max_x=420;
    g.enemies[0].y=192;g.enemies[0].dir=1;
    qs_step(&g,pause_chord());
    for(int t=0;t<14;t++)qs_step(&g,idle());
    CHECK(g.shots[0].x==QS_CITY_CRATE_X-7);
    CHECK(g.shots[0].frames==65 && g.shots[0].active);
    CHECK(g.enemies[0].x==410);
    CHECK(g.city_crate_active==1);
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    CHECK(g.shots[0].x==QS_CITY_CRATE_X-6);
    CHECK(g.city_crate_active==0);
}
static void pause_cannot_earn_checkpoint_or_bonuses(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    qs_step(&g,pause_chord());
    for(int t=0;t<25;t++)qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
    CHECK(!g.city_checkpoint_split_frames);
    CHECK(!g.city_best_checkpoint_frames);
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
}
static void pause_keeps_timer_near_saturation(void) {
    QsGame g=city();
    g.city_attempt_frames=QS_CITY_TIMER_MAX-1;
    qs_step(&g,pause_chord());
    for(int t=0;t<30;t++)qs_step(&g,idle());
    CHECK(g.city_attempt_frames==QS_CITY_TIMER_MAX-1);
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    CHECK(g.city_attempt_frames==QS_CITY_TIMER_MAX);
}
static void start_alone_exits_paused_city(void) {
    QsGame g=city();
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    QsInput in=idle();in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_paused);
    CHECK(!g.city_attempt_frames);
}
static void r_start_retries_paused_city_without_map(void) {
    QsGame g=city();
    g.city_best_bonus_score=500;
    g.city_best_checkpoint_frames=15;
    g.city_best_clear_frames=44;
    qs_step(&g,pause_chord());
    qs_step(&g,idle());
    qs_step(&g,retry_chord());
    CHECK(g.mode==QS_STAGE && !g.city_paused);
    CHECK(g.hero.x==40 && g.city_attempt_frames==0);
    CHECK(g.city_best_bonus_score==500);
    CHECK(g.city_best_checkpoint_frames==15);
    CHECK(g.city_best_clear_frames==44);
}
static void completion_wins_over_new_pause(void) {
    QsGame g=city();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,pause_chord());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(!g.city_paused);
    CHECK(qs_city_best_clear_time(&g)==1);
}
static void invalid_pause_flag_fails_open_into_gameplay(void) {
    QsGame g=city();
    g.city_paused=INT_MIN;
    qs_step(&g,idle());
    CHECK(!g.city_paused && g.city_attempt_frames==1);
    g.city_paused=INT_MAX;
    qs_step(&g,idle());
    CHECK(!g.city_paused && g.city_attempt_frames==2);
}
static void ordinary_prototype_start_remains_map_action(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    qs_step(&g,pause_chord());
    CHECK(g.mode==QS_MAP && !g.city_paused);
}
static void other_stages_have_no_pause(void) {
    for(int stage=1;stage<QS_STAGE_COUNT;stage++) {
        QsGame g=city();
        qs_begin_stage(&g,stage);
        qs_step(&g,pause_chord());
        CHECK(g.mode==QS_MAP && !g.city_paused);
    }
}
static void paused_scene_does_not_change_camera_or_records(void) {
    QsGame g=city();
    g.camera_x=250;g.render_camera_x=250;
    g.city_best_bonus_score=1000;
    g.city_best_checkpoint_frames=7;
    qs_step(&g,pause_chord());
    for(int i=0;i<29;i++)qs_step(&g,idle());
    CHECK(g.camera_x==250 && g.render_camera_x==250);
    CHECK(g.city_best_bonus_score==1000 && g.city_best_checkpoint_frames==7);
}
/*
 * Stage 77: old safety chords deliberately include X (weapon change).
 * Adding pause must not steal Back+Fire or Back+Confirm transactions
 * from the legacy map/finish and numeric-recovery test paths.
 */
static void mixed_fire_change_back_is_plain_map_exit(void) {
    QsGame g=city();QsInput in=idle();
    in.back=in.change=in.fire=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_paused);
}
static void mixed_confirm_change_back_is_plain_map_exit(void) {
    QsGame g=city();QsInput in=idle();
    in.back=in.change=in.select=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_paused);
}
static void mixed_jump_change_back_is_plain_map_exit(void) {
    QsGame g=city();QsInput in=idle();
    in.back=in.change=in.jump=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_paused);
}
static void directional_x_start_still_pauses(void) {
    QsGame g=city();QsInput in=pause_chord();
    in.right=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.city_paused);
    CHECK(g.hero.x==100 && !g.city_attempt_frames);
}
static void mixed_chord_at_goal_commits_finish_not_pause(void) {
    QsGame g=city();QsInput in=pause_chord();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    in.fire=in.select=in.jump=in.right=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0] && !g.city_paused);
    CHECK(qs_city_best_clear_time(&g)==1);
}
static void deterministic_480_tick_pause_replay(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<480;t++) {
        int p=t%80;
        QsInput in=idle();
        if(p==7||p==23||p==37||p==51)in=pause_chord();
        if(p==28 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
        }
        if(p==57 && a.mode==QS_STAGE)in=retry_chord();
        if(p==63 && a.mode==QS_STAGE) {
            in.back=1;
        }
        if(p==66 && a.mode==QS_MAP)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_paused==b.city_paused);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.city_crate_fx_frames==b.city_crate_fx_frames);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(a.city_checkpoint_active==b.city_checkpoint_active);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
    }
    CHECK(a.frames==480 && b.frames==480);
}
int main(void) {
    no_pause_before_chord();
    chord_pauses_before_ticking_clock();
    entire_game_state_frozen_except_input_and_global_frame();
    holding_start_is_not_second_toggle();
    resume_does_not_count_resume_input_frame();
    pause_preserves_crate_debris_frames();
    pause_keeps_shot_and_enemy_from_updating();
    pause_cannot_earn_checkpoint_or_bonuses();
    pause_keeps_timer_near_saturation();
    start_alone_exits_paused_city();
    r_start_retries_paused_city_without_map();
    completion_wins_over_new_pause();
    invalid_pause_flag_fails_open_into_gameplay();
    ordinary_prototype_start_remains_map_action();
    other_stages_have_no_pause();
    paused_scene_does_not_change_camera_or_records();
    mixed_fire_change_back_is_plain_map_exit();
    mixed_confirm_change_back_is_plain_map_exit();
    mixed_jump_change_back_is_plain_map_exit();
    directional_x_start_still_pauses();
    mixed_chord_at_goal_commits_finish_not_pause();
    deterministic_480_tick_pause_replay();
    puts("PASS: 22 pause/chord scenarios and 480 deterministic ticks");
    return EXIT_SUCCESS;
}
