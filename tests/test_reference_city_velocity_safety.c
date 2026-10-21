#include "qs_game.h"
#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"velocity safety FAIL %d: %s\n",__LINE__,#x);exit(EXIT_FAILURE);}}while(0)

/* Synthetic-only CITY TEST, never the private Mega Drive ROM. */
static QsInput idle(void){QsInput x={0};return x;}
static QsGame city(void){
    QsGame g;int i;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=40;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    for(i=0;i<QS_COLUMNS;i++)
        g.terrain[13][i]=g.terrain[14][i]='#';
    return g;
}
static void shot(QsGame *g,int i,float x,float y,float vx,int red,int frames){
    QsShot *s=&g->shots[i];memset(s,0,sizeof(*s));
    s->active=1;s->red=red;s->x=x;s->y=y;s->vx=vx;
    s->frames=frames;
}
static void foe(QsGame *g,int i,float x,float y){
    QsEnemy *e=&g->enemies[i];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=y;e->dir=1;
    e->min_x=e->max_x=x;
}
static void check_discarded(const QsGame *g,int i){
    CHECK(!g->shots[i].active && !g->shots[i].attached);
}
static void nan_and_infinite_velocities_are_discarded(void){
    QsGame g=city();
    shot(&g,0,40,194,NAN,1,65);
    shot(&g,1,40,194,INFINITY,1,65);
    shot(&g,2,40,194,-INFINITY,0,65);
    shot(&g,3,40,194,FLT_MAX,1,65);
    shot(&g,4,40,194,-FLT_MAX,0,65);
    qs_step(&g,idle());
    for(int i=0;i<5;i++)check_discarded(&g,i);
}
static void invalid_coordinates_are_discarded(void){
    QsGame g=city();
    shot(&g,0,NAN,194,2,1,65);
    shot(&g,1,40,NAN,2,0,65);
    shot(&g,2,INFINITY,194,2,1,65);
    shot(&g,3,40,-INFINITY,2,0,65);
    shot(&g,4,FLT_MAX,194,2,1,65);
    shot(&g,5,40,FLT_MAX,2,0,65);
    shot(&g,6,-1,194,2,1,65);
    shot(&g,7,1274,194,2,1,65);
    qs_step(&g,idle());
    for(int i=0;i<QS_MAX_SHOTS;i++)check_discarded(&g,i);
}
static void excessive_finite_velocity_is_rejected(void){
    QsGame g=city();
    shot(&g,0,40,194,1280.01f,1,65);
    shot(&g,1,40,194,-1280.01f,0,65);
    shot(&g,2,40,194,50000,1,65);
    shot(&g,3,40,194,-50000,0,65);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)check_discarded(&g,i);
}
static void invalid_attached_shot_is_retired(void){
    QsGame g=city();
    shot(&g,0,NAN,194,0,1,180);
    g.shots[0].attached=1;
    qs_step(&g,idle());
    check_discarded(&g,0);
}
static void corrupt_lifetimes_cannot_underflow(void){
    QsGame g=city();
    shot(&g,0,40,194,1,1,INT_MIN);
    shot(&g,1,40,194,1,0,0);
    shot(&g,2,40,194,1,1,-1);
    shot(&g,3,40,194,1,0,1);
    qs_step(&g,idle());
    for(int i=0;i<4;i++)check_discarded(&g,i);
    CHECK(g.shots[0].frames==INT_MIN);
}
static void maximum_velocity_can_hit_a_real_enemy(void){
    QsGame g=city();
    foe(&g,0,135,184);
    shot(&g,0,40,194,1280,0,65);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    check_discarded(&g,0);
}
static void all_eight_maximum_speed_shots_run_in_one_tick(void){
    QsGame g=city();
    foe(&g,0,135,170);
    foe(&g,1,235,192);
    for(int i=0;i<QS_MAX_SHOTS;i++)
        shot(&g,i,40,i<4?177:195,1280,(i&1),65);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    for(int i=0;i<QS_MAX_SHOTS;i++)
        check_discarded(&g,i);
}
static void full_width_red_shot_attaches_at_real_wall(void){
    QsGame g=city();
    /* Built-in city col35 spans rows10..12, x560 and y160..207. */
    shot(&g,0,40,194,1280,1,65);
    qs_step(&g,idle());
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.shots[0].x==552 && g.shots[0].frames==230);
}
static void full_width_yellow_shot_expires_at_world_edge(void){
    QsGame g=city();
    for(int col=0;col<QS_COLUMNS;col++)
        for(int row=0;row<QS_ROWS;row++)g.terrain[row][col]='.';
    shot(&g,0,40,194,1280,0,65);
    qs_step(&g,idle());
    check_discarded(&g,0);
}
static void zero_velocity_still_hits_overlapping_enemy(void){
    QsGame g=city();
    foe(&g,0,135,184);
    shot(&g,0,135,194,0,0,65);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    check_discarded(&g,0);
}
static void normal_profile_does_not_get_city_guard(void){
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    shot(&g,0,40,194,1281.0f,0,65);
    qs_step(&g,idle());
    /* The unmodified prototype moves by the full vx first, then
     * retires a shot only when its x crosses the right map edge. */
    CHECK(!g.shots[0].active);
    CHECK(g.shots[0].x==1321);
}
static void max_speed_replay_stays_deterministic(void){
    QsGame a=city(),b;int tick,j;
    b=a;
    for(tick=0;tick<32;tick++){
        if(tick%4==0){
            for(j=0;j<QS_MAX_SHOTS;j++){
                shot(&a,j,40,j<4?177:195,1280,j&1,65);
                shot(&b,j,40,j<4?177:195,1280,j&1,65);
            }
        }
        if(tick%3==0) {
            a.enemies[0].active=b.enemies[0].active=1;
            a.enemies[0].x=b.enemies[0].x=135;
            a.enemies[0].y=b.enemies[0].y=170;
            a.enemies[0].min_x=b.enemies[0].min_x=135;
            a.enemies[0].max_x=b.enemies[0].max_x=135;
        }
        qs_step(&a,idle());qs_step(&b,idle());
        CHECK(a.frames==b.frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.enemies[0].stunned_frames==b.enemies[0].stunned_frames);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(j=0;j<QS_MAX_SHOTS;j++){
            CHECK(a.shots[j].active==b.shots[j].active);
            CHECK(a.shots[j].attached==b.shots[j].attached);
            CHECK(a.shots[j].x==b.shots[j].x);
        }
    }
    CHECK(a.frames==32);
}
int main(void){
    nan_and_infinite_velocities_are_discarded();
    invalid_coordinates_are_discarded();
    excessive_finite_velocity_is_rejected();
    invalid_attached_shot_is_retired();
    corrupt_lifetimes_cannot_underflow();
    maximum_velocity_can_hit_a_real_enemy();
    all_eight_maximum_speed_shots_run_in_one_tick();
    full_width_red_shot_attaches_at_real_wall();
    full_width_yellow_shot_expires_at_world_edge();
    zero_velocity_still_hits_overlapping_enemy();
    normal_profile_does_not_get_city_guard();
    max_speed_replay_stays_deterministic();
    puts("PASS: 12 projectile numeric-boundary scenarios with 8 simultaneous max-speed shots");
    return 0;
}
