#include "qs_game.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"hero camera numeric FAIL %d: %s\n",__LINE__,#x); exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame city(void) {
    QsGame g;
    int i;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    for(i=0;i<QS_COLUMNS;i++)
        g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void finite_and_bounded(const QsGame *g) {
    const float max_camera=(float)(QS_COLUMNS*QS_TILE-QS_VIEW_W);
    CHECK(isfinite(g->hero.x) && isfinite(g->hero.y));
    CHECK(isfinite(g->hero.vx) && isfinite(g->hero.vy));
    CHECK(isfinite(g->camera_x) && isfinite(g->render_camera_x));
    CHECK(g->camera_x>=0 && g->camera_x<=max_camera);
    CHECK(g->render_camera_x>=0 && g->render_camera_x<=max_camera);
}
static void check_new_spawn(const QsGame *g) {
    CHECK(g->mode==QS_STAGE);
    CHECK(g->hero.x==40 && g->hero.y==188);
    CHECK(g->hero.vx==0 && g->hero.vy==0);
    CHECK(g->hero.hearts==3 && g->hero.hurt_frames==89);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(!g->cleared[0]);
    finite_and_bounded(g);
}
static void nonfinite_positions_trigger_safe_respawn(void) {
    const float values[]={NAN,INFINITY,-INFINITY};
    int i;
    for(i=0;i<3;i++) {
        QsGame a=city(),b=city();
        a.hero.x=values[i];
        b.hero.y=values[i];
        qs_step(&a,idle()); qs_step(&b,idle());
        check_new_spawn(&a); check_new_spawn(&b);
    }
}
static void huge_finite_positions_cannot_complete_stage(void) {
    const float values[]={FLT_MAX,-FLT_MAX};
    int i;
    for(i=0;i<2;i++) {
        QsGame a=city(),b=city();
        a.hero.x=values[i];
        b.hero.y=values[i];
        qs_step(&a,idle()); qs_step(&b,idle());
        check_new_spawn(&a); check_new_spawn(&b);
    }
}
static void nonfinite_horizontal_speeds_cannot_enter_substep_cast(void) {
    const float values[]={NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX};
    int i;
    for(i=0;i<5;i++) {
        QsGame g=city();
        g.hero.vx=values[i];
        qs_step(&g,idle());
        CHECK(g.hero.x==40 && g.hero.vx==0);
        CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
        finite_and_bounded(&g);
    }
}
static void nonfinite_vertical_speeds_cannot_enter_substep_cast(void) {
    const float values[]={NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX};
    int i;
    for(i=0;i<5;i++) {
        QsGame g=city();
        g.hero.y=160;
        g.hero.vy=values[i];
        g.hero.grounded=1;
        g.reference_jump_steps=12;
        qs_step(&g,idle());
        CHECK(g.hero.y>=160 && g.hero.y<162);
        CHECK(g.hero.vy>=0 && g.hero.vy<1);
        CHECK(g.hero.grounded==0);
        CHECK(g.reference_jump_steps==1);
        finite_and_bounded(&g);
    }
}
static void excessive_finite_speeds_are_bounded(void) {
    QsGame a=city(),b=city();
    a.hero.vx=9.0f;
    b.hero.vy=-17.0f;
    b.hero.y=160;
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.hero.x==40 && a.hero.vx==0);
    CHECK(b.hero.y>=160 && b.hero.y<162);
    CHECK(b.hero.vy>=0 && b.hero.vy<1);
    finite_and_bounded(&a);finite_and_bounded(&b);
}
static void valid_extreme_motion_and_near_world_edge_survive(void) {
    QsGame g=city(),edge=city();
    g.hero.vx=8.0f;
    qs_step(&g,idle());
    CHECK(g.hero.x>47 && g.hero.x<49);
    edge.hero.x=-12.0f;
    qs_step(&edge,idle());
    CHECK(edge.mode==QS_STAGE && edge.hero.x==-12.0f);
    CHECK(edge.hero.hurt_frames==0);
    finite_and_bounded(&g);finite_and_bounded(&edge);
}
static void damaged_camera_phases_recover_together(void) {
    QsGame a=city(),b=city(),c=city();
    a.hero.x=b.hero.x=c.hero.x=320;
    a.camera_x=NAN; a.render_camera_x=INFINITY;
    b.camera_x=-INFINITY; b.render_camera_x=NAN;
    c.camera_x=FLT_MAX; c.render_camera_x=-FLT_MAX;
    qs_step(&a,idle());qs_step(&b,idle());qs_step(&c,idle());
    CHECK(a.camera_x==179 && a.render_camera_x==179);
    CHECK(b.camera_x==179 && b.render_camera_x==179);
    CHECK(c.camera_x==179 && c.render_camera_x==179);
    finite_and_bounded(&a);finite_and_bounded(&b);finite_and_bounded(&c);
}
static void valid_camera_deadzone_is_not_rebased(void) {
    QsGame g=city();
    g.hero.x=280;
    g.camera_x=150;g.render_camera_x=133;
    qs_step(&g,idle());
    CHECK(g.camera_x==150 && g.render_camera_x==150);
    CHECK(g.hero.x==280);
}
static void corrupt_coordinates_are_checked_before_exit_or_shoot(void) {
    QsGame g=city();
    QsInput in=idle();
    g.hero.x=FLT_MAX;
    g.hero.y=NAN;
    g.pending_reference_shot=1;
    in.fire=1;
    qs_step(&g,in);
    check_new_spawn(&g);
    for(int i=0;i<QS_MAX_SHOTS;i++) CHECK(!g.shots[i].active);
    CHECK(g.pending_reference_shot==1); /* fresh fire edge, no old queued shot */
}
static void legacy_profile_is_left_unchanged(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    g.hero.vx=NAN;
    qs_step(&g,idle());
    CHECK(isnan(g.hero.vx));
    CHECK(g.hero.x==40);
}
static void deterministic_400_tick_recovery(void) {
    QsGame a=city(),b=city();
    int t;
    b=a;
    for(t=0;t<400;t++) {
        QsInput in=idle();
        in.right=(t%37)<18;
        if(t%29==0) { a.hero.vx=b.hero.vx=NAN; }
        if(t%43==0) { a.hero.vy=b.hero.vy=INFINITY; }
        if(t%61==0) { a.camera_x=b.camera_x=NAN; }
        if(t%79==0) { a.render_camera_x=b.render_camera_x=FLT_MAX; }
        if(t==251) { a.hero.x=b.hero.x=-FLT_MAX; }
        qs_step(&a,in);qs_step(&b,in);
        finite_and_bounded(&a);finite_and_bounded(&b);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.vx==b.hero.vx && a.hero.vy==b.hero.vy);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
    }
    CHECK(a.frames==400 && b.frames==400);
}
int main(void) {
    nonfinite_positions_trigger_safe_respawn();
    huge_finite_positions_cannot_complete_stage();
    nonfinite_horizontal_speeds_cannot_enter_substep_cast();
    nonfinite_vertical_speeds_cannot_enter_substep_cast();
    excessive_finite_speeds_are_bounded();
    valid_extreme_motion_and_near_world_edge_survive();
    damaged_camera_phases_recover_together();
    valid_camera_deadzone_is_not_rebased();
    corrupt_coordinates_are_checked_before_exit_or_shoot();
    legacy_profile_is_left_unchanged();
    deterministic_400_tick_recovery();
    puts("PASS: 11 hero/camera numeric regressions and 400 deterministic ticks");
    return 0;
}
