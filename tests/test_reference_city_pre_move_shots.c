#include "qs_game.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr,"pre-move shots FAIL line %d: %s\n",__LINE__,#c); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame city(void) {
    QsGame g;int col;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    for(col=0;col<QS_COLUMNS;col++)
        g.terrain[13][col]=g.terrain[14][col]='#';
    g.hero.x=100;g.hero.y=139.92f;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=0;
    return g;
}
static void shot(QsGame *g,int index,float x,float y,float vx,int frames,int attached,int red) {
    QsShot *s=&g->shots[index];
    memset(s,0,sizeof(*s));
    s->active=1;s->x=x;s->y=y;s->vx=vx;
    s->frames=frames;s->attached=attached;s->red=red;
}
static void no_bad_anchor(const QsGame *g,int index) {
    CHECK(!g->shots[index].active && !g->shots[index].attached);
    CHECK(isfinite(g->hero.x) && isfinite(g->hero.y));
    CHECK(isfinite(g->hero.vx) && isfinite(g->hero.vy));
}
static void nan_anchor_must_not_poison_valid_landing(void) {
    QsGame g=city();
    shot(&g,0,100,NAN,0,40,1,1);
    shot(&g,1,100,160.02f,0,40,1,1);
    qs_step(&g,idle());
    no_bad_anchor(&g,0);
    CHECK(g.shots[1].active && g.shots[1].attached);
    CHECK(g.hero.grounded && fabsf(g.hero.y-140.02f)<0.0001f);
}
static void invalid_anchor_coordinates_are_removed_before_movement(void) {
    const float bad[]={NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX};
    for(int i=0;i<5;i++) {
        QsGame x=city(),y=city();
        shot(&x,0,bad[i],160,0,40,1,1);
        shot(&y,0,100,bad[i],0,40,1,1);
        qs_step(&x,idle());qs_step(&y,idle());
        no_bad_anchor(&x,0);no_bad_anchor(&y,0);
        CHECK(!x.hero.grounded && !y.hero.grounded);
        CHECK(x.hero.y>139.92f && y.hero.y>139.92f);
    }
}
static void invalid_attached_velocity_cannot_be_used(void) {
    QsGame g=city();
    shot(&g,0,100,160,NAN,40,1,1);
    qs_step(&g,idle());
    no_bad_anchor(&g,0);
    CHECK(!g.hero.grounded);
}
static void invalid_attachment_flags_cannot_create_platform(void) {
    QsGame a=city(),b=city(),c=city();
    shot(&a,0,100,160,0,40,INT_MAX,1);
    shot(&b,0,100,160,0,40,1,INT_MIN);
    shot(&c,0,100,160,0,40,1,2);
    qs_step(&a,idle());qs_step(&b,idle());qs_step(&c,idle());
    no_bad_anchor(&a,0);no_bad_anchor(&b,0);no_bad_anchor(&c,0);
    CHECK(!a.hero.grounded && !b.hero.grounded && !c.hero.grounded);
}
static void out_of_world_platform_cannot_intercept(void) {
    QsGame a=city(),b=city();
    shot(&a,0,-1,160,0,40,1,1);
    shot(&b,0,1274,160,0,40,1,1);
    qs_step(&a,idle());qs_step(&b,idle());
    no_bad_anchor(&a,0);no_bad_anchor(&b,0);
}
static void expiring_platform_is_removed_before_hero_steps(void) {
    QsGame g=city();
    shot(&g,0,100,160,0,1,1,1);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active);
    CHECK(!g.hero.grounded && g.hero.y>139.92f);
}
static void two_frame_platform_keeps_exact_last_safe_tick(void) {
    QsGame g=city();
    shot(&g,0,100,160,0,2,1,1);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].frames==1);
    CHECK(g.hero.grounded && g.hero.y==140);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.hero.grounded);
    CHECK(g.hero.y>140);
}
static void invalid_flying_shots_are_retired_before_hero_motion(void) {
    QsGame g=city();
    shot(&g,0,100,160,INFINITY,65,0,0);
    shot(&g,1,NAN,180,5.5f,65,0,0);
    shot(&g,2,100,180,5.5f,INT_MIN,0,1);
    qs_step(&g,idle());
    for(int i=0;i<3;i++)no_bad_anchor(&g,i);
    CHECK(g.shots[2].frames==INT_MIN); /* Legacy metadata preservation. */
}
static void valid_red_and_yellow_shots_keep_existing_physics(void) {
    QsGame g=city();
    shot(&g,0,100,160.02f,0,40,1,1);
    shot(&g,1,200,110,5.5f,65,0,0);
    qs_step(&g,idle());
    CHECK(g.hero.grounded && fabsf(g.hero.y-140.02f)<0.0001f);
    CHECK(g.shots[0].active && g.shots[0].frames==39);
    CHECK(g.shots[1].active && !g.shots[1].attached);
    CHECK(g.shots[1].x>200 && g.shots[1].frames==64);
}
static void fatal_retry_still_retires_all_shots(void) {
    QsGame g=city();
    g.hero.x=40;g.hero.y=188;g.hero.hearts=1;
    g.enemies[0].active=1;
    g.enemies[0].x=g.enemies[0].min_x=g.enemies[0].max_x=40;
    g.enemies[0].y=188;g.enemies[0].dir=1;
    shot(&g,0,100,NAN,0,40,1,1);
    shot(&g,1,150,165,0,40,1,1);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(g.hero.hurt_frames==89);
    for(int i=0;i<QS_MAX_SHOTS;i++)CHECK(!g.shots[i].active);
}
static void back_exit_cannot_leave_invalid_active_shot(void) {
    QsGame g=city();QsInput in=idle();in.back=1;
    shot(&g,0,100,NAN,0,40,1,1);
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.shots[0].active && !g.shots[0].attached);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void prototype_keeps_preexisting_anchor_behavior(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    shot(&g,0,100,160,0,30,1,0);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.hero.grounded && g.hero.y==140);
}
static void replay_700_ticks_with_bad_anchors(void) {
    QsGame a=city(),b=city();
    b=a;
    for(int t=0;t<700;t++) {
        if(t%29==0) {
            shot(&a,0,100,NAN,0,40,1,1);
            shot(&b,0,100,NAN,0,40,1,1);
        }
        if(t%43==0) {
            shot(&a,1,100,160.02f,0,40,1,1);
            shot(&b,1,100,160.02f,0,40,1,1);
        }
        if(t%67==0) {
            shot(&a,2,100,160,INFINITY,40,1,1);
            shot(&b,2,100,160,INFINITY,40,1,1);
        }
        qs_step(&a,idle());qs_step(&b,idle());
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.vx==b.hero.vx && a.hero.vy==b.hero.vy);
        CHECK(a.hero.grounded==b.hero.grounded);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(isfinite(a.hero.y) && isfinite(a.camera_x));
        for(int i=0;i<QS_MAX_SHOTS;i++) {
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].attached==b.shots[i].attached);
            CHECK(a.shots[i].frames==b.shots[i].frames);
            if(a.shots[i].active) {
                CHECK(isfinite(a.shots[i].x) && isfinite(a.shots[i].y));
                CHECK(isfinite(a.shots[i].vx));
            }
        }
    }
    CHECK(a.frames==700 && b.frames==700);
}
int main(void) {
    nan_anchor_must_not_poison_valid_landing();
    invalid_anchor_coordinates_are_removed_before_movement();
    invalid_attached_velocity_cannot_be_used();
    invalid_attachment_flags_cannot_create_platform();
    out_of_world_platform_cannot_intercept();
    expiring_platform_is_removed_before_hero_steps();
    two_frame_platform_keeps_exact_last_safe_tick();
    invalid_flying_shots_are_retired_before_hero_motion();
    valid_red_and_yellow_shots_keep_existing_physics();
    fatal_retry_still_retires_all_shots();
    back_exit_cannot_leave_invalid_active_shot();
    prototype_keeps_preexisting_anchor_behavior();
    replay_700_ticks_with_bad_anchors();
    puts("PASS: 13 pre-move shot/anchor guards and 700 deterministic frames");
    return EXIT_SUCCESS;
}
