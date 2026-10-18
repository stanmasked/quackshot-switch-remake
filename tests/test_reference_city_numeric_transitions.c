#include "qs_game.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"numeric transitions FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.cleared[2]=1;
    g.red_unlocked=1;
    g.weapon=QS_RED;
    return g;
}
static void staged_shot(QsGame *g) {
    QsShot *s=&g->shots[0];
    s->active=s->attached=s->red=1;
    s->x=120;s->y=166;s->frames=230;
    g->pending_reference_shot=1;
    g->pending_reference_red=1;
    g->reference_jump_steps=8;
}
static void map_clean(const QsGame *g) {
    int i;
    CHECK(g->mode==QS_MAP);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    CHECK(!g->reference_jump_steps);
    for(i=0;i<QS_MAX_SHOTS;i++)
        CHECK(!g->shots[i].active && !g->shots[i].attached);
}
static void corrupted_position_with_back_cannot_grant_finish(void) {
    QsGame g=city();
    QsInput in=idle();
    staged_shot(&g);
    g.hero.x=FLT_MAX;
    g.hero.y=NAN;
    g.camera_x=NAN;
    g.render_camera_x=INFINITY;
    in.back=in.select=in.dash=in.fire=in.change=in.right=1;
    qs_step(&g,in);
    map_clean(&g);
    CHECK(!g.cleared[0] && g.cleared[2] && g.red_unlocked);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.previous.back && g.previous.select && g.previous.fire);
}
static void legitimate_finish_survives_corrupt_camera_and_back(void) {
    QsGame g=city();
    QsInput in=idle();
    staged_shot(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE;
    g.camera_x=NAN;
    g.render_camera_x=-INFINITY;
    in.back=1;
    qs_step(&g,in);
    map_clean(&g);
    CHECK(g.cleared[0] && g.cleared[2]);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
}
static void fresh_shot_during_corrupt_recovery_cannot_cross_map_boundary(void) {
    QsGame g=city();
    QsInput in=idle();
    staged_shot(&g);
    g.hero.x=-FLT_MAX;
    in.fire=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && !g.shots[0].active);
    CHECK(g.pending_reference_shot==1); /* fresh edge scheduled after retry */
    in=idle();in.back=1;
    qs_step(&g,in);
    map_clean(&g);
    CHECK(!g.cleared[0] && g.cleared[2]);
}
static void held_select_after_corrupt_exit_requires_new_edge(void) {
    QsGame g=city();
    QsInput in=idle();
    g.hero.x=INFINITY;
    in.back=in.select=in.dash=1;
    qs_step(&g,in);
    map_clean(&g);
    /* Back takes us to MAP; held Select must not relaunch CITY TEST. */
    in.back=0;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    in=idle();qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
    CHECK(!g.cleared[0] && g.cleared[2]);
}
static void corrupt_map_residuals_do_not_survive_new_stage(void) {
    QsGame g;
    QsInput in=idle();
    qs_init(&g);
    g.hero.x=NAN;g.hero.y=INFINITY;
    g.hero.vx=FLT_MAX;g.hero.vy=-FLT_MAX;
    g.camera_x=NAN;g.render_camera_x=FLT_MAX;
    staged_shot(&g);
    in.select=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.hero.vx==0 && g.hero.vy==0);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(!g.shots[0].active && g.reference_jump_steps==0);
}
static void corrupt_speed_then_manual_exit_is_atomic(void) {
    QsGame g=city();
    QsInput in=idle();
    staged_shot(&g);
    g.hero.vx=INFINITY;
    g.hero.vy=NAN;
    g.camera_x=FLT_MAX;
    g.render_camera_x=NAN;
    in.back=in.fire=in.change=1;
    qs_step(&g,in);
    map_clean(&g);
    CHECK(!g.cleared[0]);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
}
static void different_stage_then_return_to_city_has_fresh_numeric_state(void) {
    QsGame g=city();
    QsInput in=idle();
    g.hero.x=NAN;
    in.back=1;qs_step(&g,in);
    map_clean(&g);
    in=idle();in.right=1;qs_step(&g,in);
    CHECK(g.selected_stage==1);
    in=idle();in.select=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(g.hero.x==40 && g.camera_x==0);
    in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    in=idle();in.left=1;qs_step(&g,in);
    CHECK(g.selected_stage==0);
    in=idle();in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
    CHECK(g.cleared[2] && g.red_unlocked);
}
static void ordinary_profile_keeps_existing_transition_priority(void) {
    QsGame g;
    QsInput in=idle();
    qs_init(&g);qs_begin_stage(&g,0);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.cleared[0]);
}
static void deterministic_600_tick_numeric_map_replay(void) {
    QsGame a=city(),b;
    int t,i;
    b=a;
    for(t=0;t<600;t++) {
        QsInput in=idle();
        int phase=t%50;
        if(phase==0 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=NAN;
            a.camera_x=b.camera_x=INFINITY;
            in.back=in.select=in.dash=in.fire=1;
        } else if((phase==4 || phase==27 || phase==43) && a.mode==QS_MAP) {
            in.select=in.dash=1;
        } else if(phase==15 && a.mode==QS_STAGE) {
            a.hero.vx=b.hero.vx=NAN;
            a.hero.vy=b.hero.vy=INFINITY;
            a.render_camera_x=b.render_camera_x=FLT_MAX;
            in.right=1;
        } else if(phase==20 && a.mode==QS_STAGE) {
            in.back=1;
        } else if(phase==35 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=(QS_COLUMNS-4)*QS_TILE;
            a.camera_x=b.camera_x=NAN;
            in.back=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode && a.stage==b.stage);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.vx==b.hero.vx && a.hero.vy==b.hero.vy);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(isfinite(a.camera_x) && isfinite(a.render_camera_x));
        CHECK(a.weapon==b.weapon && a.red_unlocked==b.red_unlocked);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        for(i=0;i<QS_STAGE_COUNT;i++) CHECK(a.cleared[i]==b.cleared[i]);
        for(i=0;i<QS_MAX_SHOTS;i++)
            CHECK(a.shots[i].active==b.shots[i].active);
        if(a.mode==QS_MAP) map_clean(&a);
    }
    CHECK(a.frames==600 && b.frames==600);
    CHECK(a.cleared[0] && a.cleared[2] && a.red_unlocked);
}
int main(void) {
    corrupted_position_with_back_cannot_grant_finish();
    legitimate_finish_survives_corrupt_camera_and_back();
    fresh_shot_during_corrupt_recovery_cannot_cross_map_boundary();
    held_select_after_corrupt_exit_requires_new_edge();
    corrupt_map_residuals_do_not_survive_new_stage();
    corrupt_speed_then_manual_exit_is_atomic();
    different_stage_then_return_to_city_has_fresh_numeric_state();
    ordinary_profile_keeps_existing_transition_priority();
    deterministic_600_tick_numeric_map_replay();
    puts("PASS: 9 numeric map/reentry scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
