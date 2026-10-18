#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"breakable city crate FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void bullet(QsGame *g,int red,float x,float vx) {
    QsShot *s=&g->shots[0];
    memset(s,0,sizeof(*s));
    s->active=1;s->red=red;s->x=x;s->y=197;
    s->vx=vx;s->frames=65;
}
static void fire_near_crate(QsGame *g,int red) {
    bullet(g,red,QS_CITY_CRATE_X-11,5.5f);
    for(int i=0;i<4 && g->city_crate_active;i++)
        qs_step(g,idle());
}
static void exit_map(QsGame *g) {
    QsInput in=idle();in.back=1;
    qs_step(g,in);CHECK(g->mode==QS_MAP);
}
static void enter_again(QsGame *g) {
    qs_step(g,idle());
    QsInput in=idle();in.select=in.dash=1;
    qs_step(g,in);
    CHECK(g->mode==QS_STAGE);
}
static void initial_crate_only_in_optional_city(void) {
    QsGame g=city(),proto;
    CHECK(g.city_crate_active==1);
    qs_init(&proto);qs_begin_stage(&proto,0);
    CHECK(proto.city_crate_active==0);
    qs_begin_stage(&g,1);
    CHECK(g.city_crate_active==0 && g.physics_profile==QS_PHYSICS_PROTOTYPE);
}
static void hero_cannot_walk_through_intact_crate(void) {
    QsGame g=city();QsInput in=idle();
    g.hero.x=QS_CITY_CRATE_X-12;g.hero.y=188;
    g.hero.grounded=1;
    in.right=1;
    for(int t=0;t<25;t++)qs_step(&g,in);
    CHECK(g.city_crate_active==1);
    CHECK(g.hero.x<=QS_CITY_CRATE_X-12);
}
static void player_can_bypass_crate_above_it(void) {
    QsGame g=city();QsInput in=idle();
    g.hero.x=QS_CITY_CRATE_X-12;g.hero.y=160;
    g.hero.grounded=0;g.hero.vx=1.2f;g.hero.vy=-2.0f;
    in.right=1;
    qs_step(&g,in);
    CHECK(g.hero.x>QS_CITY_CRATE_X-12);
    CHECK(g.city_crate_active==1);
}
static void yellow_shot_breaks_crate(void) {
    QsGame g=city();fire_near_crate(&g,0);
    CHECK(g.city_crate_active==0);
    CHECK(g.shots[0].active==0);
    CHECK(g.shots[0].attached==0);
}
static void red_shot_breaks_without_creating_anchor(void) {
    QsGame g=city();fire_near_crate(&g,1);
    CHECK(!g.city_crate_active);
    CHECK(!g.shots[0].active && !g.shots[0].attached);
}
static void shots_pass_after_destruction(void) {
    QsGame g=city();fire_near_crate(&g,0);
    memset(g.shots,0,sizeof(g.shots));
    bullet(&g,0,QS_CITY_CRATE_X-11,5.5f);
    for(int i=0;i<5;i++)qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.shots[0].active==1);
    CHECK(g.shots[0].x>QS_CITY_CRATE_X);
}
static void walk_onto_removed_crate_location(void) {
    QsGame g=city();fire_near_crate(&g,0);
    memset(g.shots,0,sizeof(g.shots));
    QsInput in=idle();in.right=1;
    g.hero.x=QS_CITY_CRATE_X-12;g.hero.y=188;
    g.hero.vx=0;g.hero.vy=0;g.hero.grounded=1;
    for(int t=0;t<20;t++)qs_step(&g,in);
    CHECK(!g.city_crate_active);
    CHECK(g.hero.x>QS_CITY_CRATE_X-12);
}
static void destruction_does_not_require_red_unlock(void) {
    QsGame g=city();
    CHECK(!g.red_unlocked && g.weapon==QS_YELLOW);
    fire_near_crate(&g,0);
    CHECK(!g.city_crate_active);
    CHECK(!g.red_unlocked);
}
static void destroyed_crate_stays_gone_after_life_retry(void) {
    QsGame g=city();fire_near_crate(&g,0);
    g.hero.x=21*QS_TILE;g.hero.y=261;
    g.hero.vy=7.5f;g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(g.city_crate_active==0);
}
static void quick_retry_restores_crate(void) {
    QsGame g=city();fire_near_crate(&g,0);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.city_crate_active==1);
    CHECK(g.city_attempt_frames==0 && g.hero.x==40);
}
static void map_reentry_restores_crate(void) {
    QsGame g=city();fire_near_crate(&g,0);
    exit_map(&g);
    CHECK(!g.city_crate_active);
    enter_again(&g);
    CHECK(g.city_crate_active==1);
}
static void shots_do_not_forge_collection_score(void) {
    QsGame g=city();fire_near_crate(&g,0);
    CHECK(!g.city_crate_active);
    CHECK(g.city_bonus_mask==0);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void invalid_crate_metadata_restores_safe_obstacle(void) {
    const int bad[]={INT_MIN,-1,2,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_crate_active=bad[i];
        qs_step(&g,idle());
        CHECK(g.city_crate_active==1);
    }
}
static void nominal_boolean_metadata_unchanged(void) {
    QsGame g=city();g.city_crate_active=0;
    qs_step(&g,idle());
    CHECK(g.city_crate_active==0);
    g.city_crate_active=1;
    qs_step(&g,idle());
    CHECK(g.city_crate_active==1);
}
static void simultaneous_shots_break_only_once(void) {
    QsGame g=city();
    bullet(&g,0,QS_CITY_CRATE_X-11,5.5f);
    g.shots[1]=g.shots[0];
    g.shots[1].red=1;
    for(int i=0;i<5;i++)qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(!g.shots[0].active || !g.shots[1].active);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void later_enemy_does_not_get_stunned_by_spent_shot(void) {
    QsGame g=city();
    QsEnemy *e=&g.enemies[0];
    e->active=1;e->x=e->min_x=e->max_x=QS_CITY_CRATE_X+2;
    e->y=192;e->dir=1;
    fire_near_crate(&g,0);
    CHECK(!g.city_crate_active && !g.shots[0].active);
    CHECK(e->stunned_frames==0);
}
static void deterministic_600_tick_crate_route(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<600;t++) {
        int p=t%100;
        QsInput in=idle();
        if(p==3 && a.mode==QS_STAGE) {
            bullet(&a,0,QS_CITY_CRATE_X-11,5.5f);
            bullet(&b,0,QS_CITY_CRATE_X-11,5.5f);
        }
        if(p==18 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(p==36 && a.mode==QS_STAGE) in.back=in.dash=1;
        if(p==67 && a.mode==QS_STAGE)in.back=1;
        if(p==70 && a.mode==QS_MAP)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_crate_active==b.city_crate_active);
        CHECK(a.city_bonus_mask==b.city_bonus_mask);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x);
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    initial_crate_only_in_optional_city();
    hero_cannot_walk_through_intact_crate();
    player_can_bypass_crate_above_it();
    yellow_shot_breaks_crate();
    red_shot_breaks_without_creating_anchor();
    shots_pass_after_destruction();
    walk_onto_removed_crate_location();
    destruction_does_not_require_red_unlock();
    destroyed_crate_stays_gone_after_life_retry();
    quick_retry_restores_crate();
    map_reentry_restores_crate();
    shots_do_not_forge_collection_score();
    invalid_crate_metadata_restores_safe_obstacle();
    nominal_boolean_metadata_unchanged();
    simultaneous_shots_break_only_once();
    later_enemy_does_not_get_stunned_by_spent_shot();
    deterministic_600_tick_crate_route();
    puts("PASS: 17 breakable crate scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
