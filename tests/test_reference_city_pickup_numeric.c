#include "qs_game.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"pickup numeric FAIL %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}} while(0)

static QsInput idle(void){QsInput in={0};return in;}
static QsGame city(void){
    QsGame g;
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    return g;
}
static void pickup(QsGame *g,int index,float x,float y,int kind){
    QsPickup *p=&g->pickups[index];
    memset(p,0,sizeof(*p));
    p->active=1;p->x=x;p->y=y;p->kind=kind;
}
static void discarded(const QsGame *g,int index){
    const QsPickup *p=&g->pickups[index];
    CHECK(!p->active && p->x==0 && p->y==0 && p->kind==0);
}
static void all_nonfinite_coordinates_retired(void){
    QsGame a=city(),b=city();
    pickup(&a,0,NAN,180,QS_PICKUP_RED);
    pickup(&a,1,INFINITY,180,QS_PICKUP_HEART);
    pickup(&a,2,-INFINITY,180,QS_PICKUP_RED);
    pickup(&b,0,180,NAN,QS_PICKUP_RED);
    pickup(&b,1,180,INFINITY,QS_PICKUP_HEART);
    pickup(&b,2,180,-INFINITY,QS_PICKUP_RED);
    qs_step(&a,idle());qs_step(&b,idle());
    for(int i=0;i<3;i++){discarded(&a,i);discarded(&b,i);}
}
static void extreme_finite_coordinates_retired(void){
    QsGame g=city();
    pickup(&g,0,FLT_MAX,180,QS_PICKUP_RED);
    pickup(&g,1,-FLT_MAX,180,QS_PICKUP_HEART);
    pickup(&g,2,180,FLT_MAX,QS_PICKUP_RED);
    pickup(&g,3,180,-FLT_MAX,QS_PICKUP_HEART);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)discarded(&g,i);
}
static void wholly_outside_world_retired(void){
    QsGame g=city();
    pickup(&g,0,-11,140,QS_PICKUP_RED);
    pickup(&g,1,1281,140,QS_PICKUP_HEART);
    pickup(&g,2,120,-11,QS_PICKUP_RED);
    pickup(&g,3,120,241,QS_PICKUP_HEART);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)discarded(&g,i);
}
static void unknown_kinds_cannot_be_collected(void){
    QsGame g=city();
    pickup(&g,0,40,188,-1);
    pickup(&g,1,40,188,2);
    pickup(&g,2,40,188,INT_MAX);
    qs_step(&g,idle());
    for(int i=0;i<3;i++)discarded(&g,i);
    CHECK(g.hero.hearts==3 && !g.red_unlocked && g.weapon==QS_YELLOW);
}
static void partial_world_edge_is_valid(void){
    QsGame g=city();
    pickup(&g,0,-10,130,QS_PICKUP_RED);
    pickup(&g,1,1280,130,QS_PICKUP_HEART);
    pickup(&g,2,110,-10,QS_PICKUP_RED);
    pickup(&g,3,110,240,QS_PICKUP_HEART);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)CHECK(g.pickups[i].active);
    CHECK(g.pickups[0].x==-10 && g.pickups[3].y==240);
}
static void invalid_red_never_unlocks(void){
    QsGame g=city();
    pickup(&g,0,NAN,188,QS_PICKUP_RED);
    pickup(&g,1,FLT_MAX,188,QS_PICKUP_RED);
    qs_step(&g,idle());
    discarded(&g,0);discarded(&g,1);
    CHECK(!g.red_unlocked && g.weapon==QS_YELLOW);
}
static void valid_red_can_still_be_collected(void){
    QsGame g=city();
    pickup(&g,0,NAN,188,QS_PICKUP_RED);
    pickup(&g,1,40,188,QS_PICKUP_RED);
    qs_step(&g,idle());
    discarded(&g,0);
    CHECK(!g.pickups[1].active && g.red_unlocked && g.weapon==QS_RED);
}
static void valid_heart_maintains_cap(void){
    QsGame a=city(),b=city();
    a.hero.hearts=1;
    pickup(&a,0,40,188,QS_PICKUP_HEART);
    pickup(&b,0,40,188,QS_PICKUP_HEART);
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.hero.hearts==2 && !a.pickups[0].active);
    CHECK(b.hero.hearts==3 && !b.pickups[0].active);
}
static void invalid_objects_retired_on_early_exit(void){
    QsGame a=city(),b=city();
    QsInput back=idle();back.back=1;
    a.hero.x=(QS_COLUMNS-4)*QS_TILE;
    pickup(&a,0,NAN,188,QS_PICKUP_RED);
    pickup(&b,0,FLT_MAX,188,QS_PICKUP_HEART);
    qs_step(&a,idle());qs_step(&b,back);
    CHECK(a.mode==QS_MAP && a.cleared[0]);
    CHECK(b.mode==QS_MAP && !b.cleared[0]);
    discarded(&a,0);discarded(&b,0);
}
static void standard_profile_is_not_modified(void){
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    pickup(&g,0,NAN,188,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.pickups[0].active && isnan(g.pickups[0].x));
}
static void deterministic_400_tick_malformed_item_replay(void){
    QsGame a=city(),b;int tick,i;
    a.hero.hearts=1;
    b=a;
    for(tick=0;tick<400;tick++){
        if(tick%17==0){
            pickup(&a,0,NAN,188,QS_PICKUP_RED);
            pickup(&b,0,NAN,188,QS_PICKUP_RED);
        }
        if(tick%53==0){
            pickup(&a,1,40,188,QS_PICKUP_HEART);
            pickup(&b,1,40,188,QS_PICKUP_HEART);
        }
        if(tick%97==0){
            pickup(&a,2,FLT_MAX,160,QS_PICKUP_RED);
            pickup(&b,2,FLT_MAX,160,QS_PICKUP_RED);
        }
        qs_step(&a,idle());qs_step(&b,idle());
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.red_unlocked==b.red_unlocked);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(i=0;i<QS_MAX_PICKUPS;i++){
            CHECK(a.pickups[i].active==b.pickups[i].active);
            CHECK(a.pickups[i].x==b.pickups[i].x);
            CHECK(a.pickups[i].y==b.pickups[i].y);
            CHECK(a.pickups[i].kind==b.pickups[i].kind);
            if(a.pickups[i].active)
                CHECK(isfinite(a.pickups[i].x) && isfinite(a.pickups[i].y));
        }
    }
    CHECK(a.frames==400 && a.hero.hearts==3);
}
int main(void){
    all_nonfinite_coordinates_retired();
    extreme_finite_coordinates_retired();
    wholly_outside_world_retired();
    unknown_kinds_cannot_be_collected();
    partial_world_edge_is_valid();
    invalid_red_never_unlocks();
    valid_red_can_still_be_collected();
    valid_heart_maintains_cap();
    invalid_objects_retired_on_early_exit();
    standard_profile_is_not_modified();
    deterministic_400_tick_malformed_item_replay();
    puts("PASS: 11 numeric pickup regressions and 400 deterministic ticks");
    return EXIT_SUCCESS;
}
