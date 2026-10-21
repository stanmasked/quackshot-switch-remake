#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"checkpoint FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame scene(QsPhysicsProfile p) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=p;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.pickups,0,sizeof(g.pickups));
    memset(g.shots,0,sizeof(g.shots));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void foe(QsGame *g,float x) {
    QsEnemy *e=&g->enemies[0];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;
    e->min_x=e->max_x=x;e->dir=1;
}
static void reach(QsGame *g) {
    g->hero.x=QS_CITY_CHECKPOINT_X-5;
    g->hero.y=188;
    g->hero.vx=g->hero.vy=0;g->hero.grounded=1;
    qs_step(g,idle());
    CHECK(g->city_checkpoint_active==1 && g->mode==QS_STAGE);
}
static void initial_stage_has_no_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    CHECK(!g.city_checkpoint_active);
    CHECK(g.hero.x==100);
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
}
static void geometric_contact_activates_once(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=QS_CITY_CHECKPOINT_X-12.5f;
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
    reach(&g);
    for(int i=0;i<5;i++)qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    CHECK(g.hero.hearts==3);
}
static void fatal_contact_respawns_at_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    reach(&g);
    g.red_unlocked=1;g.weapon=QS_RED;
    g.hero.hearts=1;g.hero.hurt_frames=0;
    g.hero.facing=-1;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X && g.hero.y==QS_CITY_CHECKPOINT_Y);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.hero.facing==1 && !g.hero.grounded);
    CHECK(g.camera_x==QS_CITY_CHECKPOINT_X-141);
    CHECK(g.render_camera_x==g.camera_x);
    CHECK(g.red_unlocked==1 && g.weapon==QS_RED);
}
static void fatal_contact_on_marker_does_not_unlock_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    g.hero.hearts=1;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.hero.hurt_frames==89);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void nonfatal_contact_allows_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    g.hero.hearts=2;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==69);
}
static void pit_retry_uses_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    reach(&g);
    g.hero.x=21*QS_TILE;g.hero.y=261;g.hero.vy=7.5f;
    g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.city_checkpoint_active==1);
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X && g.hero.y==QS_CITY_CHECKPOINT_Y);
    CHECK(g.hero.hurt_frames==89);
    CHECK(g.camera_x==QS_CITY_CHECKPOINT_X-141);
}
static void numeric_retry_uses_checkpoint_without_old_pickup(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    reach(&g);
    g.pickups[0].active=1;g.pickups[0].kind=QS_PICKUP_RED;
    g.pickups[0].x=QS_CITY_CHECKPOINT_X;g.pickups[0].y=189;
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X && g.hero.hurt_frames==89);
    CHECK(g.pickups[0].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.red_unlocked && g.weapon==QS_RED);
}
static void fatal_retry_defers_checkpoint_spawn_pickup(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    reach(&g);
    g.pickups[0].active=1;g.pickups[0].kind=QS_PICKUP_RED;
    g.pickups[0].x=QS_CITY_CHECKPOINT_X;g.pickups[0].y=189;
    g.hero.hearts=1;g.hero.hurt_frames=0;
    foe(&g,g.hero.x);
    qs_step(&g,idle());
    CHECK(g.pickups[0].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.red_unlocked);
}
static void unactivated_death_still_uses_original_city_spawn(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.hero.hearts=1;foe(&g,100);
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
    CHECK(g.hero.x==40 && g.hero.hurt_frames==89);
}
static void back_clears_checkpoint_and_reentry_starts_fresh(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=idle();
    reach(&g);
    in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_checkpoint_active);
    in=idle();qs_step(&g,in);
    in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(!g.city_checkpoint_active && g.hero.x==40);
}
static void finish_clears_checkpoint_but_retains_progress(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    reach(&g);
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(!g.city_checkpoint_active);
}
static void corrupted_active_flag_is_not_an_earned_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_REFERENCE_CITY);
    g.city_checkpoint_active=-1;
    g.hero.hearts=1;foe(&g,100);
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active && g.hero.x==40);
}
static void prototype_does_not_activate_or_use_checkpoint(void) {
    QsGame g=scene(QS_PHYSICS_PROTOTYPE);
    g.hero.x=QS_CITY_CHECKPOINT_X-5;
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
    g.city_checkpoint_active=1;
    g.hero.x=100;g.hero.hearts=1;foe(&g,100);
    qs_step(&g,idle());
    CHECK(g.hero.x==40);
}
static void deterministic_600_ticks_and_retry_transitions(void) {
    QsGame a=scene(QS_PHYSICS_REFERENCE_CITY),b=a;
    for(int t=0;t<600;t++) {
        QsInput in=idle();
        int phase=t%97;
        if(phase==0 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=QS_CITY_CHECKPOINT_X-5;
            a.hero.y=b.hero.y=188;
            a.hero.hearts=b.hero.hearts=3;
            a.hero.vx=b.hero.vx=0;
            a.hero.vy=b.hero.vy=0;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
        }
        if(phase==1 && a.mode==QS_STAGE) {
            a.hero.hearts=b.hero.hearts=1;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
            foe(&a,a.hero.x);foe(&b,b.hero.x);
        }
        if(phase==30 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=21*QS_TILE;
            a.hero.y=b.hero.y=261;
            a.hero.vy=b.hero.vy=7.5f;
        }
        if(phase==40 && a.mode==QS_STAGE) {
            a.hero.x=b.hero.x=NAN;
        }
        if(phase==60)in.back=1;
        if(phase==62)in.select=in.dash=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.city_checkpoint_active==b.city_checkpoint_active);
        CHECK(a.city_checkpoint_active==0 || a.city_checkpoint_active==1);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.red_unlocked==b.red_unlocked && a.weapon==b.weapon);
        CHECK(isfinite(a.hero.x) && isfinite(a.hero.y));
    }
    CHECK(a.frames==600 && b.frames==600);
}
int main(void) {
    initial_stage_has_no_checkpoint();
    geometric_contact_activates_once();
    fatal_contact_respawns_at_checkpoint();
    fatal_contact_on_marker_does_not_unlock_checkpoint();
    nonfatal_contact_allows_checkpoint();
    pit_retry_uses_checkpoint();
    numeric_retry_uses_checkpoint_without_old_pickup();
    fatal_retry_defers_checkpoint_spawn_pickup();
    unactivated_death_still_uses_original_city_spawn();
    back_clears_checkpoint_and_reentry_starts_fresh();
    finish_clears_checkpoint_but_retains_progress();
    corrupted_active_flag_is_not_an_earned_checkpoint();
    prototype_does_not_activate_or_use_checkpoint();
    deterministic_600_ticks_and_retry_transitions();
    puts("PASS: 14 city checkpoint gameplay scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
