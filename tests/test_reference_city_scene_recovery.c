#include "qs_game.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"scene recovery FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;
    return g;
}
static void pending_shot(QsGame *g) {
    g->pending_reference_shot=1;
    g->pending_reference_red=1;
    g->shots[0].active=1;
    g->shots[0].attached=1;
    g->shots[0].red=1;
    g->shots[0].x=100;g->shots[0].y=160;
    g->shots[0].frames=20;
}
static void recovered(const QsGame *g) {
    CHECK(g->mode==QS_MAP);
    CHECK(g->stage==0 && g->selected_stage==0);
    CHECK(g->physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    CHECK(!g->shots[0].active && !g->shots[0].attached);
}
static void out_of_range_stage_becomes_safe_map(void) {
    const int bad[]={INT_MIN,-1,1,2,QS_STAGE_COUNT,INT_MAX};
    for(int i=0;i<6;i++) {
        QsGame g=city();
        g.stage=bad[i];
        g.hero.x=(QS_COLUMNS-4)*QS_TILE+2;
        g.camera_x=70;g.render_camera_x=60;
        g.selected_stage=INT_MAX;
        pending_shot(&g);
        qs_step(&g,idle());
        recovered(&g);
        CHECK(!g.cleared[0] && !g.cleared[1] && !g.cleared[2]);
        CHECK(g.frames==1);
    }
}
static void invalid_mode_becomes_safe_map(void) {
    const int bad[]={INT_MIN,-1,2,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.mode=(QsMode)bad[i];
        g.hero.x=(QS_COLUMNS-4)*QS_TILE+2;
        pending_shot(&g);
        qs_step(&g,idle());
        recovered(&g);
        CHECK(!g.cleared[0] && g.frames==1);
    }
}
static void invalid_mode_with_invalid_stage_is_atomic(void) {
    QsGame g=city();
    g.mode=(QsMode)INT_MAX;
    g.stage=INT_MIN;
    g.selected_stage=INT_MIN;
    g.camera_x=800;g.render_camera_x=760;
    g.cleared[0]=1;g.cleared[1]=INT_MAX;g.cleared[2]=-1;
    g.red_unlocked=1;g.weapon=QS_RED;
    pending_shot(&g);
    qs_step(&g,idle());
    recovered(&g);
    CHECK(g.cleared[0]==1 && g.cleared[1]==0 && g.cleared[2]==0);
    CHECK(g.red_unlocked==1 && g.weapon==QS_RED);
}
static void recovery_chord_does_not_launch_or_move_menu(void) {
    QsGame g=city();
    QsInput chord=idle();
    chord.select=1;chord.dash=1;chord.right=1;chord.fire=1;
    g.stage=INT_MAX;g.selected_stage=INT_MAX;
    qs_step(&g,chord);
    recovered(&g);
    CHECK(g.frames==1);
    qs_step(&g,chord);
    CHECK(g.mode==QS_MAP && g.selected_stage==0);
    qs_step(&g,idle());
    qs_step(&g,chord);
    CHECK(g.mode==QS_MAP && g.selected_stage==1);
}
static void held_confirm_does_not_reenter_after_recovery(void) {
    QsGame g=city();
    QsInput in=idle();in.select=in.dash=1;
    g.stage=INT_MAX;
    qs_step(&g,in);
    recovered(&g);
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    qs_step(&g,idle());
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void invalid_stage_with_back_cannot_clear_progress(void) {
    QsGame g=city();QsInput in=idle();
    in.back=in.select=in.dash=1;
    g.stage=INT_MIN;
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,in);
    recovered(&g);
    CHECK(g.cleared[0]==0 && g.cleared[1]==0);
}
static void legitimate_stage0_completion_retains_priority(void) {
    QsGame g=city();
    QsInput in=idle();in.back=1;
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+0.5f;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0]==1);
    CHECK(g.stage==0 && g.frames==1);
}
static void ordinary_prototype_map_is_not_changed(void) {
    QsGame g;qs_init(&g);
    g.stage=INT_MAX;
    g.selected_stage=1;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.stage==INT_MAX);
    CHECK(g.selected_stage==1);
}
static void normal_stage1_switches_away_from_reference_profile(void) {
    QsGame g=city();QsInput in=idle();
    in.back=1;qs_step(&g,in);
    in=idle();qs_step(&g,in);
    in.right=1;qs_step(&g,in);
    in=idle();qs_step(&g,in);
    in.select=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    qs_step(&g,idle());
    CHECK(g.mode==QS_STAGE && g.stage==1);
}
static void deterministic_600_tick_recovery_replay(void) {
    QsGame a=city(),b;
    b=a;
    for(int t=0;t<600;t++) {
        QsInput in=idle();
        if(t%17==0) {
            a.mode=b.mode=(QsMode)INT_MAX;
            a.stage=b.stage=INT_MIN;
            a.selected_stage=b.selected_stage=INT_MAX;
        }
        if(t%17==1) {
            a.mode=b.mode=QS_STAGE;
            a.stage=b.stage=INT_MAX;
        }
        if(t%17==2) {
            a.mode=b.mode=QS_MAP;
            a.stage=b.stage=0;
            a.selected_stage=b.selected_stage=INT_MIN;
            a.cleared[1]=b.cleared[1]=INT_MAX;
        }
        if(t%17==0) { in.select=1;in.dash=1;in.fire=1; }
        if(t%17==1) in.back=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode && a.stage==b.stage);
        CHECK(a.mode==QS_MAP || a.mode==QS_STAGE);
        CHECK(a.stage==0);
        CHECK(a.selected_stage==b.selected_stage);
        CHECK(a.selected_stage>=0 && a.selected_stage<QS_STAGE_COUNT);
        CHECK(a.cleared[0]==b.cleared[0]);
        CHECK(a.cleared[1]==b.cleared[1]);
        CHECK(a.cleared[2]==b.cleared[2]);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    out_of_range_stage_becomes_safe_map();
    invalid_mode_becomes_safe_map();
    invalid_mode_with_invalid_stage_is_atomic();
    recovery_chord_does_not_launch_or_move_menu();
    held_confirm_does_not_reenter_after_recovery();
    invalid_stage_with_back_cannot_clear_progress();
    legitimate_stage0_completion_retains_priority();
    ordinary_prototype_map_is_not_changed();
    normal_stage1_switches_away_from_reference_profile();
    deterministic_600_tick_recovery_replay();
    puts("PASS: 10 scene-mode/index recovery regressions and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
