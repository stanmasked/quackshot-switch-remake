#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if(!(e)) {fprintf(stderr,"sky ledge FAIL %d: %s\n",__LINE__,#e);exit(1);} } while(0)
static QsInput idle(void){QsInput a={0};return a;}
static QsGame city(void) {
    QsGame g;qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=QS_CITY_SKY_LEDGE_X+4;g.hero.y=120;g.hero.vy=6;
    g.hero.vx=0;g.hero.grounded=0;return g;
}
static void land(QsGame *g) {
    qs_step(g,idle());
    CHECK(g->hero.y==QS_CITY_SKY_LEDGE_TOP-20);
    CHECK(g->hero.grounded && g->hero.vy==0);
}
static void valid_layout(void) {
    CHECK(QS_CITY_SKY_LEDGE_X>=QS_CITY_SPRING_X+QS_CITY_SPRING_WIDTH);
    CHECK(QS_CITY_SKY_LEDGE_X+QS_CITY_SKY_LEDGE_WIDTH<(QS_COLUMNS-4)*QS_TILE);
}
static void descend_and_stay(void) {
    QsGame g=city();land(&g);
    for(int i=0;i<25;i++){qs_step(&g,idle());CHECK(g.hero.y==124 && g.hero.grounded);}
}
static void jump_off(void) {
    QsGame g=city();land(&g);QsInput in=idle();in.jump=1;qs_step(&g,in);
    CHECK(g.hero.y<124 && g.hero.vy<0 && !g.hero.grounded);
}
static void pass_from_below(void) {
    QsGame g=city();g.hero.y=154;g.hero.vy=-6;qs_step(&g,idle());
    CHECK(g.hero.y<154 && !g.hero.grounded);
}
static void outside_edges_miss(void) {
    QsGame a=city(),b=city();
    a.hero.x=QS_CITY_SKY_LEDGE_X-12;
    b.hero.x=QS_CITY_SKY_LEDGE_X+QS_CITY_SKY_LEDGE_WIDTH;
    qs_step(&a,idle());qs_step(&b,idle());
    CHECK(a.hero.y>124 && !a.hero.grounded);
    CHECK(b.hero.y>124 && !b.hero.grounded);
}
static void narrow_edges_contact(void) {
    QsGame a=city(),b=city();
    a.hero.x=QS_CITY_SKY_LEDGE_X-11.5f;
    b.hero.x=QS_CITY_SKY_LEDGE_X+QS_CITY_SKY_LEDGE_WIDTH-0.5f;
    land(&a);land(&b);
}
static void highest_red_anchor_wins(void) {
    QsGame g=city();QsShot *s=&g.shots[0];
    s->active=1;s->attached=1;s->red=1;
    s->x=QS_CITY_SKY_LEDGE_X+4;s->y=QS_CITY_SKY_LEDGE_TOP-5;
    s->vx=0;s->frames=60;
    g.hero.y=113;g.hero.vy=9;
    qs_step(&g,idle());
    CHECK(g.hero.y==s->y-20 && g.hero.grounded);
}
static void spring_keeps_air_reward(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_SPRING_X+6;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    qs_step(&g,idle());CHECK(g.hero.vy==QS_CITY_SPRING_BOUNCE);
    for(int i=0;i<45 && !g.city_air_reward_collected;i++)qs_step(&g,idle());
    CHECK(g.city_air_reward_collected);
}
static void pause_and_retry(void) {
    QsGame g=city();land(&g);
    QsInput in=idle();in.back=in.change=1;qs_step(&g,in);
    CHECK(g.city_paused);
    for(int i=0;i<40;i++)qs_step(&g,idle());
    CHECK(g.hero.y==124 && g.hero.grounded);
    qs_step(&g,in);CHECK(!g.city_paused);
    qs_step(&g,idle());CHECK(g.hero.y==124);
    in=idle();in.back=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.hero.x==40);
}
static void legacy_has_no_ledge(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=QS_CITY_SKY_LEDGE_X+4;g.hero.y=120;
    g.hero.vx=0;g.hero.vy=6;g.hero.grounded=0;
    qs_step(&g,idle());CHECK(g.hero.y>124 && !g.hero.grounded);
}
static void deterministic_480_frames(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<480;t++) {
        QsInput in=idle();
        if(t%80==0) {
            a.hero.x=b.hero.x=QS_CITY_SKY_LEDGE_X+4;
            a.hero.y=b.hero.y=120;
            a.hero.vx=b.hero.vx=0;
            a.hero.vy=b.hero.vy=6;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(t%80==12 || t%80==22){in.back=in.change=1;}
        if(t%80==46){in.back=in.dash=1;}
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.vy==b.hero.vy && a.hero.grounded==b.hero.grounded);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.mode==b.mode && a.frames==b.frames);
    }
    CHECK(a.frames==480 && b.frames==480);
}
int main(void) {
    valid_layout();descend_and_stay();jump_off();pass_from_below();
    outside_edges_miss();narrow_edges_contact();highest_red_anchor_wins();
    spring_keeps_air_reward();pause_and_retry();legacy_has_no_ledge();
    deterministic_480_frames();
    puts("PASS: 11 one-way ledge scenarios and 480 deterministic frames");
    return 0;
}
