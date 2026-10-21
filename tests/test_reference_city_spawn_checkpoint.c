#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
  fprintf(stderr,"spawn-checkpoint FAIL line %d: %s\n",__LINE__,#x); \
  exit(EXIT_FAILURE); } } while (0)

typedef struct {
    QsGame g;
    int pit_launch, roof_launch, pit_crossing, ledge_crossing;
    int checkpoint_tick, enemy_contact, respawns;
    float furthest_x;
} QsRoute;

static QsInput idle(void){QsInput in={0};return in;}
static QsGame fresh_city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    return g;
}

/* Stage 86: real movement from x=40, with authored level enemies,
 * initial item positions, a missing floor and the original-free wall.
 * The steering policy knows obstacle positions, but it never changes
 * the hero position, velocity, level terrain or enemy slots. */
static QsRoute traverse_to_checkpoint(void) {
    QsRoute route={0};
    QsGame *g=&route.g;
    *g=fresh_city();
    int pit_hold=0, roof_hold=0;
    float prev_x=g->hero.x;
    CHECK(g->hero.x==40 && g->hero.y==188);
    CHECK(g->enemies[0].active && g->enemies[1].active &&
          g->enemies[2].active);
    CHECK(g->terrain[13][20]=='.' && g->terrain[13][22]=='.');
    CHECK(g->terrain[12][35]=='#' && g->terrain[9][27]=='#');
    route.furthest_x=40;
    for(int tick=1;tick<850 && !g->city_checkpoint_active;tick++) {
        QsInput in=idle();
        in.right=1;
        /* First running jump clears the 3-tile pit. */
        if(!route.pit_launch && g->hero.x>=278 &&
           g->hero.x<320 && g->hero.grounded) {
            route.pit_launch=tick;
            pit_hold=15;
        }
        /* Second full jump climbs the higher platform and clears
         * the wall beneath its far right edge. */
        if(route.pit_launch && !route.roof_launch &&
           g->hero.x>=394 && g->hero.x<=425 &&
           g->hero.grounded) {
            route.roof_launch=tick;
            roof_hold=15;
        }
        if(pit_hold>0){in.jump=1;--pit_hold;}
        else if(roof_hold>0){in.jump=1;--roof_hold;}
        int before_hearts=g->hero.hearts;
        float before_x=g->hero.x;
        qs_step(g,in);
        if(g->hero.x>route.furthest_x)
            route.furthest_x=g->hero.x;
        if(!route.pit_crossing && g->hero.x>=368)
            route.pit_crossing=tick;
        if(!route.ledge_crossing && g->hero.x>=560)
            route.ledge_crossing=tick;
        if(g->hero.hearts<before_hearts) ++route.enemy_contact;
        if(g->hero.x+4<before_x) ++route.respawns;
        prev_x=g->hero.x;
        CHECK(g->mode==QS_STAGE);
        CHECK(g->city_attempt_frames==tick);
        CHECK(g->hero.hearts>=1 && g->hero.hearts<=3);
    }
    (void)prev_x;
    route.checkpoint_tick=g->city_checkpoint_split_frames;
    if(!g->city_checkpoint_active) {
        fprintf(stderr,
           "SPAWN_ROUTE_INCOMPLETE x=%.4f y=%.4f vx=%.4f vy=%.4f max=%.4f pit=%d roof=%d pit_cross=%d wall_cross=%d respawns=%d frames=%d\n",
           g->hero.x,g->hero.y,g->hero.vx,g->hero.vy,
           route.furthest_x,route.pit_launch,route.roof_launch,
           route.pit_crossing,route.ledge_crossing,
           route.respawns,g->city_attempt_frames);
        exit(EXIT_FAILURE);
    }
    CHECK(route.pit_launch>0 && route.roof_launch>route.pit_launch);
    CHECK(route.pit_crossing>route.pit_launch);
    CHECK(route.ledge_crossing>route.roof_launch);
    CHECK(route.checkpoint_tick>=route.ledge_crossing);
    CHECK(g->hero.x>=QS_CITY_CHECKPOINT_X-12);
    CHECK(g->city_best_checkpoint_frames==route.checkpoint_tick);
    CHECK(g->mode==QS_STAGE && g->cleared[0]==0);
    return route;
}
static void real_spawn_to_checkpoint(void) {
    QsRoute r=traverse_to_checkpoint();
    CHECK(r.checkpoint_tick<850 && r.furthest_x>=QS_CITY_CHECKPOINT_X-12);
}
static void deterministic_gamepad_route_replay(void) {
    QsRoute a=traverse_to_checkpoint(),b=traverse_to_checkpoint();
    CHECK(a.pit_launch==b.pit_launch && a.roof_launch==b.roof_launch);
    CHECK(a.pit_crossing==b.pit_crossing);
    CHECK(a.ledge_crossing==b.ledge_crossing);
    CHECK(a.checkpoint_tick==b.checkpoint_tick);
    CHECK(a.g.hero.x==b.g.hero.x && a.g.hero.y==b.g.hero.y);
    CHECK(a.g.city_attempt_frames==b.g.city_attempt_frames);
    CHECK(a.g.city_bonus_mask==b.g.city_bonus_mask);
    CHECK(a.g.camera_x==b.g.camera_x);
}
static void earned_checkpoint_respawn(void) {
    QsRoute r=traverse_to_checkpoint();
    QsGame *g=&r.g;
    int before=g->city_attempt_frames;
    g->hero.x=NAN;
    qs_step(g,idle());
    CHECK(g->hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(g->hero.y==QS_CITY_CHECKPOINT_Y);
    CHECK(g->city_checkpoint_active==1);
    CHECK(g->city_checkpoint_split_frames==r.checkpoint_tick);
    CHECK(g->city_attempt_frames==before+1);
}
static void manual_back_from_checkpoint_does_not_clear(void) {
    QsRoute r=traverse_to_checkpoint();
    QsGame *g=&r.g;
    QsInput in=idle();in.back=1;
    qs_step(g,in);
    CHECK(g->mode==QS_MAP && g->cleared[0]==0);
    CHECK(g->city_checkpoint_active==0);
    CHECK(g->city_attempt_frames==0);
}
static void normal_prototype_has_no_reference_checkpoint(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    g.hero.x=QS_CITY_CHECKPOINT_X;
    g.hero.y=QS_CITY_CHECKPOINT_Y;
    g.hero.vx=g.hero.vy=0;
    qs_step(&g,idle());
    CHECK(!g.city_checkpoint_active);
}
int main(void) {
    real_spawn_to_checkpoint();
    deterministic_gamepad_route_replay();
    earned_checkpoint_respawn();
    manual_back_from_checkpoint_does_not_clear();
    normal_prototype_has_no_reference_checkpoint();
    QsRoute r=traverse_to_checkpoint();
    printf("PASS: 5 CITY spawn-to-checkpoint scenarios, pit=%d roof=%d checkpoint=%d\n",
           r.pit_launch,r.roof_launch,r.checkpoint_tick);
    return EXIT_SUCCESS;
}
