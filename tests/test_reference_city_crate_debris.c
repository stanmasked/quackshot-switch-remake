#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"crate debris FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput i={0};return i; }
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
static void shoot(QsGame *g,int red) {
    QsShot *s=&g->shots[0];memset(s,0,sizeof(*s));
    s->active=1;s->red=red;s->x=QS_CITY_CRATE_X-11;
    s->y=197;s->vx=5.5f;s->frames=65;
    for(int t=0;t<4&&g->city_crate_active;t++)qs_step(g,idle());
    CHECK(g->city_crate_active==0);
}
static void original_crate_has_no_debris(void) {
    QsGame g=city();
    CHECK(g.city_crate_active==1 && g.city_crate_fx_frames==0);
}
static void yellow_impact_starts_full_duration(void) {
    QsGame g=city();shoot(&g,0);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
    CHECK(!g.shots[0].active);
}
static void red_impact_starts_same_feedback(void) {
    QsGame g=city();shoot(&g,1);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
    CHECK(!g.shots[0].active && !g.shots[0].attached);
}
static void exact_eighteen_frame_decay(void) {
    QsGame g=city();shoot(&g,0);
    for(int t=1;t<=QS_CITY_CRATE_FX_TICKS;t++) {
        qs_step(&g,idle());
        CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS-t);
        CHECK(!g.city_crate_active);
    }
    for(int t=0;t<30;t++) {
        qs_step(&g,idle());
        CHECK(g.city_crate_fx_frames==0);
    }
}
static void particles_have_no_extra_collisions(void) {
    QsGame g=city();shoot(&g,0);
    g.hero.x=QS_CITY_CRATE_X-12;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    QsInput in=idle();in.right=1;
    for(int t=0;t<12;t++)qs_step(&g,in);
    CHECK(g.hero.x>QS_CITY_CRATE_X-12);
    CHECK(g.city_crate_fx_frames>0);
}
static void particles_do_not_respawn_crate_or_award_points(void) {
    QsGame g=city();shoot(&g,0);
    CHECK(qs_city_bonus_score(&g)==0);
    for(int t=0;t<12;t++)qs_step(&g,idle());
    CHECK(!g.city_crate_active && qs_city_bonus_score(&g)==0);
}
static void pit_retry_does_not_reset_visual_countdown(void) {
    QsGame g=city();shoot(&g,0);
    int old=g.city_crate_fx_frames;
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.city_crate_active==0);
    CHECK(g.city_crate_fx_frames==old-1);
}
static void quick_retry_clears_shards_and_rebuilds_box(void) {
    QsGame g=city();shoot(&g,0);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.hero.x==40);
    CHECK(g.city_crate_active==1 && g.city_crate_fx_frames==0);
}
static void map_exit_clears_shards(void) {
    QsGame g=city();shoot(&g,0);
    QsInput in=idle();in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_crate_fx_frames);
    CHECK(!g.city_crate_active);
}
static void map_reentry_has_intact_box_without_shards(void) {
    QsGame g=city();shoot(&g,0);
    QsInput in=idle();in.back=1;qs_step(&g,in);
    qs_step(&g,idle());
    in=idle();in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.city_crate_active==1);
    CHECK(g.city_crate_fx_frames==0);
}
static void malformed_fx_is_clamped_not_wrapped(void) {
    const int bad[]={INT_MIN,-1,QS_CITY_CRATE_FX_TICKS+1,INT_MAX};
    for(int t=0;t<4;t++) {
        QsGame g=city();
        g.city_crate_active=0;g.city_crate_fx_frames=bad[t];
        qs_step(&g,idle());
        CHECK(g.city_crate_fx_frames==0 && !g.city_crate_active);
    }
}
static void intact_box_rejects_forged_shards(void) {
    QsGame g=city();
    g.city_crate_fx_frames=QS_CITY_CRATE_FX_TICKS;
    qs_step(&g,idle());
    CHECK(g.city_crate_active==1 && g.city_crate_fx_frames==0);
}
static void one_hit_cannot_restart_debris_timer(void) {
    QsGame g=city();shoot(&g,0);
    for(int t=0;t<6;t++)qs_step(&g,idle());
    int left=g.city_crate_fx_frames;
    memset(g.shots,0,sizeof(g.shots));
    g.shots[0].active=1;g.shots[0].x=QS_CITY_CRATE_X-11;
    g.shots[0].y=197;g.shots[0].vx=5.5f;g.shots[0].frames=65;
    qs_step(&g,idle());
    CHECK(g.city_crate_fx_frames==left-1);
}
static void prototype_never_receives_fx(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    CHECK(!g.city_crate_active && !g.city_crate_fx_frames);
    g.city_crate_fx_frames=QS_CITY_CRATE_FX_TICKS;
    qs_step(&g,idle());
    CHECK(!g.city_crate_active);
    CHECK(g.city_crate_fx_frames==QS_CITY_CRATE_FX_TICKS);
}
static void deterministic_420_tick_fx_replay(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<420;t++) {
        int phase=t%70;
        QsInput in=idle();
        if(phase==2 && a.mode==QS_STAGE) {
            a.shots[0].active=b.shots[0].active=1;
            a.shots[0].x=b.shots[0].x=QS_CITY_CRATE_X-11;
            a.shots[0].y=b.shots[0].y=197;
            a.shots[0].vx=b.shots[0].vx=5.5f;
            a.shots[0].frames=b.shots[0].frames=65;
        }
        if(phase==17 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
            a.hero.grounded=b.hero.grounded=0;
        }
        if(phase==35 && a.mode==QS_STAGE)in.back=in.dash=1;
        if(phase==49 && a.mode==QS_STAGE)in.back=1;
        if(phase==52 && a.mode==QS_MAP)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.frames==b.frames);
        CHECK(a.city_crate_active==b.city_crate_active);
        CHECK(a.city_crate_fx_frames==b.city_crate_fx_frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.city_crate_fx_frames>=0 &&
              a.city_crate_fx_frames<=QS_CITY_CRATE_FX_TICKS);
    }
    CHECK(a.frames==420);
}
int main(void) {
    original_crate_has_no_debris();
    yellow_impact_starts_full_duration();
    red_impact_starts_same_feedback();
    exact_eighteen_frame_decay();
    particles_have_no_extra_collisions();
    particles_do_not_respawn_crate_or_award_points();
    pit_retry_does_not_reset_visual_countdown();
    quick_retry_clears_shards_and_rebuilds_box();
    map_exit_clears_shards();
    map_reentry_has_intact_box_without_shards();
    malformed_fx_is_clamped_not_wrapped();
    intact_box_rejects_forged_shards();
    one_hit_cannot_restart_debris_timer();
    prototype_never_receives_fx();
    deterministic_420_tick_fx_replay();
    puts("PASS: 15 crate debris cases and 420 deterministic ticks");
    return EXIT_SUCCESS;
}
