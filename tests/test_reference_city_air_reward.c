#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"air reward FAIL line %d: %s\n",__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=QS_CITY_SPRING_X+6;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void collect_on_bounce(QsGame *g) {
    int t;
    qs_step(g,idle());
    CHECK(g->hero.vy==QS_CITY_SPRING_BOUNCE);
    for(t=0;t<42 && !g->city_air_reward_collected;t++)
        qs_step(g,idle());
    CHECK(g->city_air_reward_collected==1);
    CHECK(t>0 && t<42);
}
static void starts_uncollected_with_separate_bonus_score(void) {
    QsGame g=city();
    CHECK(!g.city_air_reward_collected);
    CHECK(!g.city_bonus_mask);
    CHECK(qs_city_bonus_score(&g)==0);
}
static void spring_arc_reaches_reward_without_steering(void) {
    QsGame g=city();
    g.hero.hearts=2;
    collect_on_bounce(&g);
    CHECK(g.hero.hearts==3);
    CHECK(g.city_bonus_mask==0 && qs_city_bonus_score(&g)==0);
}
static void ordinary_jump_stays_below_reward(void) {
    QsGame g=city();
    QsInput in=idle();
    in.jump=1;
    qs_step(&g,in);
    CHECK(g.hero.vy!=QS_CITY_SPRING_BOUNCE);
    for(int t=0;t<30;t++) {
        qs_step(&g,in);
        CHECK(!g.city_air_reward_collected);
        CHECK(g.hero.y > QS_CITY_AIR_REWARD_Y+QS_CITY_AIR_REWARD_SIZE);
    }
}
static void ground_level_walk_cannot_claim(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_AIR_REWARD_X;
    for(int t=0;t<8;t++)qs_step(&g,idle());
    CHECK(!g.city_air_reward_collected);
}
static void falling_from_above_claims_only_at_contact(void) {
    QsGame g=city();
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y-30;
    g.hero.vy=0;g.hero.grounded=0;g.hero.hearts=2;
    for(int t=0;t<22 && !g.city_air_reward_collected;t++)
        qs_step(&g,idle());
    CHECK(g.city_air_reward_collected==1 && g.hero.hearts==3);
}
static void full_health_is_capped_and_reward_consumed(void) {
    QsGame g=city();
    collect_on_bounce(&g);
    CHECK(g.hero.hearts==3);
    g.hero.hearts=2;
    for(int t=0;t<85;t++)qs_step(&g,idle());
    CHECK(g.hero.hearts==2 && g.city_air_reward_collected==1);
}
static void pause_freezes_air_reward_and_arc(void) {
    QsGame g=city();
    qs_step(&g,idle());
    for(int t=0;t<5;t++)qs_step(&g,idle());
    QsInput in=idle();in.back=in.change=1;
    qs_step(&g,in);
    CHECK(g.city_paused==1);
    float y=g.hero.y, vy=g.hero.vy;
    for(int t=0;t<70;t++)qs_step(&g,idle());
    CHECK(!g.city_air_reward_collected && g.hero.y==y && g.hero.vy==vy);
    qs_step(&g,in);
    CHECK(g.city_paused==0);
    for(int t=0;t<42 && !g.city_air_reward_collected;t++)
        qs_step(&g,idle());
    CHECK(g.city_air_reward_collected==1);
}
static void invalid_position_cannot_collect_on_respawn_tick(void) {
    QsGame g=city();
    g.hero.x=NAN;g.hero.y=QS_CITY_AIR_REWARD_Y;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.city_air_reward_collected==0);
}
static void claimed_reward_survives_checkpoint_respawn(void) {
    QsGame g=city();
    collect_on_bounce(&g);
    g.hero.y=NAN;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.city_air_reward_collected==1);
}
static void retry_resets_reward(void) {
    QsGame g=city();
    collect_on_bounce(&g);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.city_air_reward_collected==0);
    CHECK(g.hero.hearts==3 && g.hero.x==40);
}
static void map_reentry_resets_reward(void) {
    QsGame g=city();
    collect_on_bounce(&g);
    QsInput in=idle();in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.city_air_reward_collected);
    qs_step(&g,idle());
    in=idle();in.select=in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && !g.city_air_reward_collected);
}
static void malformed_flag_fails_closed(void) {
    const int invalid[]={-1,2,INT_MIN,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_air_reward_collected=invalid[i];
        qs_step(&g,idle());
        CHECK(g.city_air_reward_collected==1);
    }
}
static void legacy_prototype_ignores_air_reward(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vy=0;g.hero.hearts=2;g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.city_air_reward_collected==0);
    CHECK(g.hero.hearts==2);
}
static void deterministic_two_copy_arc_and_retries(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<420;t++) {
        QsInput in=idle();
        if(t%84==0) {
            a.hero.x=b.hero.x=QS_CITY_SPRING_X+6;
            a.hero.y=b.hero.y=188;
            a.hero.vx=b.hero.vx=0;
            a.hero.vy=b.hero.vy=0;
            a.hero.grounded=b.hero.grounded=1;
        }
        if(t%84==70){in.back=in.dash=1;}
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.city_air_reward_collected==b.city_air_reward_collected);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.city_attempt_frames==b.city_attempt_frames);
    }
    CHECK(a.frames==420 && b.frames==420);
}

int main(void) {
    starts_uncollected_with_separate_bonus_score();
    spring_arc_reaches_reward_without_steering();
    ordinary_jump_stays_below_reward();
    ground_level_walk_cannot_claim();
    falling_from_above_claims_only_at_contact();
    full_health_is_capped_and_reward_consumed();
    pause_freezes_air_reward_and_arc();
    invalid_position_cannot_collect_on_respawn_tick();
    claimed_reward_survives_checkpoint_respawn();
    retry_resets_reward();
    map_reentry_resets_reward();
    malformed_flag_fails_closed();
    legacy_prototype_ignores_air_reward();
    deterministic_two_copy_arc_and_retries();
    puts("PASS: 14 airborne-heart scenarios and 420 deterministic frames");
    return EXIT_SUCCESS;
}
