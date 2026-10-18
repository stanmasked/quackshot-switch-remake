#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "finish chord FAIL line %d: %s\n",__LINE__,#x); \
  exit(EXIT_FAILURE); } } while (0)

/* Completely independent synthetic stage geometry and game states. */
static QsInput idle(void){ QsInput q={0}; return q; }
static QsInput chord(void){
    QsInput q={0}; q.back=q.select=q.fire=q.change=q.right=q.jump=1;
    q.dash=1; return q;
}
static QsGame city(void){
    QsGame g; qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.red_unlocked=1; g.weapon=QS_RED;
    g.cleared[2]=1;
    return g;
}
static void plant_transients(QsGame *g){
    g->pending_reference_shot=1;
    g->pending_reference_red=1;
    g->reference_jump_steps=6;
    g->shots[0].active=g->shots[0].attached=g->shots[0].red=1;
    g->shots[0].frames=180;g->shots[0].x=500;g->shots[0].y=150;
    g->camera_x=710;g->render_camera_x=689;
}
static void check_clean_map(QsGame *g){
    int i;CHECK(g->mode==QS_MAP);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    CHECK(g->reference_jump_steps==0);
    for(i=0;i<QS_MAX_SHOTS;i++)
        CHECK(!g->shots[i].active && !g->shots[i].attached);
}
static void achieved_finish_wins_full_button_chord(void){
    QsGame g=city();
    plant_transients(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.25f;
    g.hero.vx=0;
    qs_step(&g,chord());
    check_clean_map(&g);
    CHECK(g.cleared[0]&&g.cleared[2]);
    CHECK(g.selected_stage==0);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(g.previous.back && g.previous.right && g.previous.select);
}
static void unfinished_stage_back_still_aborts(void){
    QsGame g=city();QsInput q=chord();
    plant_transients(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE-30;
    qs_step(&g,q);
    check_clean_map(&g);
    CHECK(!g.cleared[0] && g.cleared[2]);
}
static void back_on_goal_exact_boundary_counts(void){
    QsGame g=city();QsInput q=idle();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE;
    q.back=1;
    qs_step(&g,q);
    CHECK(g.mode==QS_MAP && g.cleared[0]);
}
static void legacy_stage_retains_back_priority(void){
    QsGame g;QsInput q=idle();
    qs_init(&g);qs_begin_stage(&g,0);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.25f;
    q.back=1;qs_step(&g,q);
    CHECK(g.mode==QS_MAP && !g.cleared[0]);
}
static void held_buttons_cannot_trigger_immediate_map_enter(void){
    QsGame g=city();QsInput q=chord();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,q);
    qs_step(&g,q);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    qs_step(&g,idle());
    q=idle();q.select=1;q.dash=1;
    qs_step(&g,q);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.cleared[0]&&g.cleared[2]&&g.red_unlocked);
}
static void completion_ignores_queued_shot_and_weapon_toggle(void){
    QsGame g=city();
    QsInput q=chord();
    plant_transients(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+3;
    qs_step(&g,q);
    CHECK(g.mode==QS_MAP && g.weapon==QS_RED);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(!g.shots[0].active);
    CHECK(g.cleared[0]);
}
static void finished_stage_not_cleared_by_following_manual_map_inputs(void){
    QsGame g=city();QsInput q=idle();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    q.back=1;qs_step(&g,q);
    CHECK(g.cleared[0]);
    q=idle();qs_step(&g,q);
    q.right=1;qs_step(&g,q);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
    CHECK(g.cleared[0]&&g.cleared[2]);
}
static void two_replays_equal_at_boundary_chords(void){
    QsGame a=city(),b;
    int i,j,turns=0;
    a.cleared[1]=1;
    b=a;
    for(i=0;i<600;i++){
        QsInput q=idle();
        int phase=i%50;
        if(phase==0){
            if(a.mode==QS_STAGE)a.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
            if(b.mode==QS_STAGE)b.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
            q=chord();
        }else if(phase==1) q=chord();
        else if(phase==3){q.select=1;q.dash=1;}
        else if(phase==9){q.back=1;}
        else if(phase==16){q.left=1;q.select=1;}
        else if(phase==21){q.right=1;}
        else if(phase==27){q.select=1;q.dash=1;}
        qs_step(&a,q);qs_step(&b,q);
        if(a.mode!=QS_MAP)++turns;
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.weapon==b.weapon && a.red_unlocked==b.red_unlocked);
        for(j=0;j<QS_STAGE_COUNT;j++)CHECK(a.cleared[j]==b.cleared[j]);
        CHECK(a.frames==b.frames);
    }
    CHECK(turns>0);
    CHECK(a.cleared[0]&&a.cleared[1]&&a.cleared[2]);
}
int main(void){
    achieved_finish_wins_full_button_chord();
    unfinished_stage_back_still_aborts();
    back_on_goal_exact_boundary_counts();
    legacy_stage_retains_back_priority();
    held_buttons_cannot_trigger_immediate_map_enter();
    completion_ignores_queued_shot_and_weapon_toggle();
    finished_stage_not_cleared_by_following_manual_map_inputs();
    two_replays_equal_at_boundary_chords();
    puts("PASS: 8 finish/map chord scenarios, 600-tick reproducibility and legacy isolation");
    return EXIT_SUCCESS;
}
