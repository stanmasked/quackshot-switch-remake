#include "qs_game.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"enemy numeric FAIL %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}} while(0)

/* Independent synthetic actors; never loads copyrighted game files. */
static QsInput idle(void){QsInput x={0};return x;}
static QsGame city(void) {
    QsGame g;int i;
    qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(i=0;i<QS_COLUMNS;i++)
        g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void enemy(QsGame *g,int id,float x,float y,
                  float lo,float hi,int stun){
    QsEnemy *e=&g->enemies[id];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->min_x=lo;e->max_x=hi;
    e->dir=1;e->stunned_frames=stun;
}
static void canonical(const QsGame *g,int id){
    const QsEnemy *e=&g->enemies[id];
    CHECK(!e->active);
    CHECK(e->x==0 && e->y==0 && e->min_x==0 && e->max_x==0);
    CHECK(e->stunned_frames==0 && e->dir==0);
}
static void all_six_nonfinite_positions_are_removed(void){
    QsGame g=city();
    enemy(&g,0,NAN,188,20,100,0);
    enemy(&g,1,100,NAN,20,100,0);
    enemy(&g,2,INFINITY,188,20,100,0);
    enemy(&g,3,-INFINITY,188,20,100,0);
    enemy(&g,4,100,INFINITY,20,100,0);
    enemy(&g,5,100,-INFINITY,20,100,0);
    qs_step(&g,idle());
    for(int i=0;i<QS_MAX_ENEMIES;i++)canonical(&g,i);
}
static void invalid_patrol_bounds_are_removed(void){
    QsGame g=city();
    enemy(&g,0,100,188,NAN,140,0);
    enemy(&g,1,100,188,20,NAN,0);
    enemy(&g,2,100,188,-INFINITY,140,0);
    enemy(&g,3,100,188,20,INFINITY,0);
    enemy(&g,4,100,188,FLT_MAX,NAN,0);
    qs_step(&g,idle());
    for(int i=0;i<5;i++)canonical(&g,i);
}
static void extreme_finite_coordinates_cannot_be_drawn(void){
    QsGame g=city();
    enemy(&g,0,FLT_MAX,188,0,100,0);
    enemy(&g,1,-FLT_MAX,188,0,100,0);
    enemy(&g,2,100,FLT_MAX,0,100,0);
    enemy(&g,3,100,-FLT_MAX,0,100,0);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)canonical(&g,i);
}
static void fully_outside_world_bounds_are_removed(void){
    QsGame g=city();
    enemy(&g,0,-15,188,-15,10,0);
    enemy(&g,1,1281,188,1250,1300,0);
    enemy(&g,2,120,-17,100,140,0);
    enemy(&g,3,120,241,100,140,0);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)canonical(&g,i);
}
static void partly_visible_actor_is_preserved(void){
    QsGame g=city();
    enemy(&g,0,-14,188,0,10,1);
    qs_step(&g,idle());
    CHECK(g.enemies[0].active);
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.enemies[0].x==-14);
    qs_step(&g,idle());
    CHECK(g.enemies[0].active && g.enemies[0].x>=0);
}
static void invalid_foe_cannot_hurt_player(void){
    QsGame g=city();
    enemy(&g,0,NAN,188,NAN,100,0);
    g.hero.hearts=1;
    qs_step(&g,idle());
    canonical(&g,0);
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==0);
}
static void other_foe_still_takes_a_normal_plunger_hit(void){
    QsGame g=city();
    enemy(&g,0,NAN,188,0,100,0);
    enemy(&g,1,135,184,135,135,0);
    g.shots[0].active=1;g.shots[0].x=129;g.shots[0].y=194;
    g.shots[0].vx=1;g.shots[0].frames=65;
    qs_step(&g,idle());
    canonical(&g,0);
    CHECK(g.enemies[1].active && g.enemies[1].stunned_frames==179);
    CHECK(!g.shots[0].active);
}
static void ordinary_profile_does_not_apply_city_validation(void){
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    enemy(&g,0,NAN,188,0,100,3);
    qs_step(&g,idle());
    CHECK(g.enemies[0].active);
    CHECK(isnan(g.enemies[0].x));
    CHECK(g.enemies[0].stunned_frames==2);
}
static void completion_also_sanitizes_enemy_data(void){
    QsGame g=city();
    enemy(&g,0,NAN,188,0,100,0);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    canonical(&g,0);
}
static void seeded_replay_400_steps_with_invalid_actors(void){
    QsGame a=city(),b;int tick,i;
    b=a;
    for(tick=0;tick<400;tick++){
        QsInput in=idle();
        if(tick%17==0){
            enemy(&a,0,NAN,188,0,200,0);
            enemy(&b,0,NAN,188,0,200,0);
            enemy(&a,1,135,184,135,135,3);
            enemy(&b,1,135,184,135,135,3);
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.enemies[0].active==b.enemies[0].active);
        CHECK(a.enemies[0].x==b.enemies[0].x);
        CHECK(a.enemies[1].stunned_frames==b.enemies[1].stunned_frames);
        for(i=0;i<QS_STAGE_COUNT;i++)
            CHECK(a.cleared[i]==b.cleared[i]);
    }
    CHECK(a.frames==400);
}
int main(void){
    all_six_nonfinite_positions_are_removed();
    invalid_patrol_bounds_are_removed();
    extreme_finite_coordinates_cannot_be_drawn();
    fully_outside_world_bounds_are_removed();
    partly_visible_actor_is_preserved();
    invalid_foe_cannot_hurt_player();
    other_foe_still_takes_a_normal_plunger_hit();
    ordinary_profile_does_not_apply_city_validation();
    completion_also_sanitizes_enemy_data();
    seeded_replay_400_steps_with_invalid_actors();
    puts("PASS: 10 numeric enemy-safety regressions and 400 deterministic ticks");
    return 0;
}
