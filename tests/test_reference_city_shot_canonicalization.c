#include "qs_game.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr,"shot canonicalization FAIL %d: %s\n",__LINE__,#c); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput q={0}; return q; }
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
static void shot(QsGame *g,int slot,float x,float y,float vx,
                 int frames,int attached,int red) {
    QsShot *s=&g->shots[slot];
    memset(s,0,sizeof(*s));
    s->active=1;s->x=x;s->y=y;s->vx=vx;
    s->frames=frames;s->attached=attached;s->red=red;
}
static void cleared_geometry(const QsGame *g,int slot,int frames) {
    const QsShot *s=&g->shots[slot];
    CHECK(!s->active && !s->attached);
    CHECK(s->x==0 && s->y==0 && s->vx==0 && !s->red);
    CHECK(s->frames==frames);
}
static void nan_and_inf_coordinates_canonicalized(void) {
    QsGame g=city();
    shot(&g,0,NAN,160,0,40,1,1);
    shot(&g,1,100,NAN,0,41,1,1);
    shot(&g,2,INFINITY,160,0,42,0,0);
    shot(&g,3,100,-INFINITY,0,43,0,0);
    shot(&g,4,FLT_MAX,160,0,44,1,1);
    shot(&g,5,100,-FLT_MAX,0,45,1,1);
    qs_step(&g,idle());
    for(int i=0;i<6;i++)cleared_geometry(&g,i,40+i);
}
static void nan_inf_and_excessive_velocities_canonicalized(void) {
    QsGame g=city();
    shot(&g,0,100,160,NAN,60,1,1);
    shot(&g,1,100,160,INFINITY,61,0,0);
    shot(&g,2,100,160,-INFINITY,62,0,0);
    shot(&g,3,100,160,FLT_MAX,63,0,1);
    shot(&g,4,100,160,1280.01f,64,0,0);
    qs_step(&g,idle());
    for(int i=0;i<5;i++)cleared_geometry(&g,i,60+i);
}
static void off_world_coordinates_canonicalized(void) {
    QsGame g=city();
    shot(&g,0,-1,160,0,70,1,1);
    shot(&g,1,1274,160,0,71,1,1);
    shot(&g,2,100,-5,0,72,1,1);
    shot(&g,3,100,241,0,73,1,1);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)cleared_geometry(&g,i,70+i);
}
static void damaged_boolean_flags_canonicalized(void) {
    QsGame g=city();
    shot(&g,0,100,160,0,33,INT_MAX,1);
    shot(&g,1,100,160,0,34,1,INT_MIN);
    shot(&g,2,100,160,0,35,-1,-1);
    qs_step(&g,idle());
    for(int i=0;i<3;i++)cleared_geometry(&g,i,33+i);
}
static void expiry_does_not_rewrite_valid_geometry(void) {
    QsGame g=city();
    shot(&g,0,100,160,5.5f,1,1,1);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[0].attached);
    CHECK(g.shots[0].x==100 && g.shots[0].y==160);
    CHECK(g.shots[0].vx==5.5f && g.shots[0].red==1);
    CHECK(g.shots[0].frames==1);
}
static void negative_lifetime_preserves_int_min_metadata(void) {
    QsGame g=city();
    shot(&g,0,100,160,0,INT_MIN,1,1);
    qs_step(&g,idle());
    CHECK(!g.shots[0].active && !g.shots[0].attached);
    CHECK(g.shots[0].x==100 && g.shots[0].y==160);
    CHECK(g.shots[0].frames==INT_MIN);
}
static void valid_zero_speed_shot_is_not_falsely_retired(void) {
    QsGame g=city();
    shot(&g,0,100,115,0,65,0,0);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && !g.shots[0].attached);
    CHECK(g.shots[0].x==100 && g.shots[0].y==115);
    CHECK(g.shots[0].frames==64);
}
static void valid_anchor_still_supports_player(void) {
    QsGame g=city();
    g.hero.y=139.92f;g.hero.grounded=0;
    shot(&g,0,100,NAN,0,40,1,1);
    shot(&g,1,100,160.02f,0,40,1,1);
    qs_step(&g,idle());
    cleared_geometry(&g,0,40);
    CHECK(g.shots[1].active && g.shots[1].attached);
    CHECK(g.shots[1].frames==39);
    CHECK(g.hero.grounded && fabsf(g.hero.y-140.02f)<0.0001f);
}
static void exit_cleans_bad_and_good_shots(void) {
    QsGame g=city();QsInput in=idle();
    shot(&g,0,100,NAN,0,40,1,1);
    shot(&g,1,100,160,0,40,1,1);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    for(int i=0;i<QS_MAX_SHOTS;i++) {
        CHECK(!g.shots[i].active && !g.shots[i].attached);
        CHECK(g.shots[i].x==0 && g.shots[i].y==0 && g.shots[i].vx==0);
    }
}
static void prototype_does_not_canonicalize_valid_anchor(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    shot(&g,0,100,160,0,40,1,0);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==100 && g.shots[0].red==0);
}
static void snapshot_equality_after_bad_shot_is_literal(void) {
    QsGame a=city(),b=city();
    shot(&a,0,NAN,160,0,40,1,1);
    shot(&b,0,INFINITY,160,0,40,1,1);
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.shots[0].x==b.shots[0].x);
    CHECK(a.shots[0].y==b.shots[0].y);
    CHECK(a.shots[0].vx==b.shots[0].vx);
    CHECK(a.shots[0].red==b.shots[0].red);
    CHECK(a.shots[0].frames==b.shots[0].frames);
}
static void deterministic_700_step_corrupt_shot_replay(void) {
    QsGame a=city(),b;
    b=a;
    for(int t=0;t<700;t++) {
        if(t%31==0) {
            shot(&a,0,100,NAN,0,70,1,1);
            shot(&b,0,100,NAN,0,70,1,1);
        }
        if(t%43==0) {
            shot(&a,1,100,160,INFINITY,71,1,1);
            shot(&b,1,100,160,INFINITY,71,1,1);
        }
        if(t%67==0) {
            shot(&a,2,100,160,0,72,INT_MAX,1);
            shot(&b,2,100,160,0,72,INT_MAX,1);
        }
        if(t%79==0) {
            shot(&a,3,100,160,0,1,0,0);
            shot(&b,3,100,160,0,1,0,0);
        }
        qs_step(&a,idle());qs_step(&b,idle());
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(int j=0;j<4;j++) {
            const QsShot *x=&a.shots[j],*y=&b.shots[j];
            CHECK(x->active==y->active && x->attached==y->attached);
            CHECK(x->x==y->x && x->y==y->y && x->vx==y->vx);
            CHECK(x->frames==y->frames && x->red==y->red);
            CHECK(isfinite(x->x) && isfinite(x->y) && isfinite(x->vx));
        }
    }
    CHECK(a.frames==700 && b.frames==700);
}
int main(void) {
    nan_and_inf_coordinates_canonicalized();
    nan_inf_and_excessive_velocities_canonicalized();
    off_world_coordinates_canonicalized();
    damaged_boolean_flags_canonicalized();
    expiry_does_not_rewrite_valid_geometry();
    negative_lifetime_preserves_int_min_metadata();
    valid_zero_speed_shot_is_not_falsely_retired();
    valid_anchor_still_supports_player();
    exit_cleans_bad_and_good_shots();
    prototype_does_not_canonicalize_valid_anchor();
    snapshot_equality_after_bad_shot_is_literal();
    deterministic_700_step_corrupt_shot_replay();
    puts("PASS: 12 projectile canonicalization regressions and 700 deterministic ticks");
    return EXIT_SUCCESS;
}
