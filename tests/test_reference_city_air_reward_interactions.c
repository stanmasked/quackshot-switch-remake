#include "qs_game.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"air interactions FAIL line %d: %s\n",__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput i={0};return i; }
static QsGame city(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vx=g.hero.vy=0;
    g.hero.grounded=0;
    return g;
}
static void foe(QsGame *g,int stun) {
    QsEnemy *e=&g->enemies[0];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=QS_CITY_AIR_REWARD_X;
    e->y=QS_CITY_AIR_REWARD_Y;
    e->min_x=e->max_x=e->x;e->dir=1;
    e->stunned_frames=stun;
}
static void collect(QsGame *g) {
    qs_step(g,idle());
    CHECK(g->city_air_reward_collected==1);
    CHECK(g->city_air_reward_fx_frames==QS_CITY_AIR_REWARD_FX_TICKS);
}
static void fatal_enemy_blocks_air_reward_on_same_tick(void) {
    QsGame g=city();
    g.hero.hearts=1;
    foe(&g,0);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3);
    CHECK(g.city_air_reward_collected==0);
    CHECK(g.city_air_reward_fx_frames==0);
}
static void nonfatal_enemy_damage_then_heal(void) {
    QsGame g=city();
    g.hero.hearts=2;
    foe(&g,0);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==2);
    CHECK(g.hero.hurt_frames==69);
    CHECK(g.city_air_reward_collected==1);
    CHECK(g.city_air_reward_fx_frames==QS_CITY_AIR_REWARD_FX_TICKS);
}
static void stunning_enemy_prevents_contact_damage(void) {
    QsGame g=city();
    g.hero.hearts=2;
    foe(&g,1);
    collect(&g);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void projectile_stuns_enemy_before_reward_arbitration(void) {
    QsGame g=city();
    QsShot *s=&g.shots[0];
    g.hero.hearts=2;
    foe(&g,0);
    s->active=1;s->x=QS_CITY_AIR_REWARD_X-8;
    s->y=QS_CITY_AIR_REWARD_Y+8;s->vx=3.0f;s->frames=65;
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    CHECK(g.city_air_reward_collected==1 && g.enemies[0].stunned_frames>0);
}
static void checkpoint_respawn_preserves_claim_but_retires_flash(void) {
    QsGame g=city();
    collect(&g);
    g.city_checkpoint_active=1;g.city_checkpoint_split_frames=1;
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.hero.x==QS_CITY_CHECKPOINT_X);
    CHECK(g.city_air_reward_collected==1);
    CHECK(g.city_air_reward_fx_frames==0);
    CHECK(g.camera_x==g.render_camera_x);
}
static void camera_phase_does_not_change_pickup_collision(void) {
    QsGame a=city(),b=a;
    const float max=(float)(QS_COLUMNS*QS_TILE-QS_VIEW_W);
    a.camera_x=a.render_camera_x=0;
    b.camera_x=b.render_camera_x=max;
    a.hero.hearts=b.hero.hearts=2;
    collect(&a);
    collect(&b);
    CHECK(a.hero.hearts==b.hero.hearts && a.hero.x==b.hero.x);
    CHECK(a.city_air_reward_collected==b.city_air_reward_collected);
    CHECK(a.city_air_reward_fx_frames==b.city_air_reward_fx_frames);
    CHECK(a.render_camera_x==0 && b.render_camera_x==max);
    CHECK(a.camera_x==max && b.camera_x==max);
}
static void flash_full_tick_then_exact_countdown(void) {
    QsGame g=city();
    collect(&g);
    g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
    for(int tick=1;tick<=QS_CITY_AIR_REWARD_FX_TICKS;tick++) {
        qs_step(&g,idle());
        CHECK(g.city_air_reward_fx_frames==QS_CITY_AIR_REWARD_FX_TICKS-tick);
    }
    for(int i=0;i<20;i++) {
        qs_step(&g,idle());
        CHECK(g.city_air_reward_fx_frames==0);
    }
    CHECK(g.city_air_reward_collected==1);
}
static void pause_freezes_flash_and_hero_camera(void) {
    QsGame g=city();
    collect(&g);
    QsInput in=idle();in.back=in.change=1;
    qs_step(&g,in);
    CHECK(g.city_paused==1);
    int fx=g.city_air_reward_fx_frames;
    float camera=g.camera_x;
    float y=g.hero.y;
    for(int i=0;i<35;i++)qs_step(&g,idle());
    CHECK(g.city_air_reward_fx_frames==fx);
    CHECK(g.camera_x==camera && g.hero.y==y);
    qs_step(&g,in);
    CHECK(g.city_paused==0 && g.city_air_reward_fx_frames==fx);
    qs_step(&g,idle());
    CHECK(g.city_air_reward_fx_frames==fx-1);
}
static void bad_flash_is_safely_normalized(void) {
    const int bad[]={-1,INT_MIN,QS_CITY_AIR_REWARD_FX_TICKS+1,INT_MAX};
    for(int i=0;i<4;i++) {
        QsGame g=city();
        g.city_air_reward_collected=1;
        g.city_air_reward_fx_frames=bad[i];
        g.hero.x=100;g.hero.y=188;g.hero.grounded=1;
        qs_step(&g,idle());
        CHECK(g.city_air_reward_collected==1 && g.city_air_reward_fx_frames==0);
    }
}
static void new_attempt_clears_both_flag_and_flash(void) {
    QsGame g=city();
    collect(&g);
    QsInput in=idle();in.back=in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_STAGE);
    CHECK(g.city_air_reward_collected==0 && g.city_air_reward_fx_frames==0);
}
static void map_exit_clears_both_flag_and_flash(void) {
    QsGame g=city();
    collect(&g);
    QsInput in=idle();in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP);
    CHECK(g.city_air_reward_collected==0 && g.city_air_reward_fx_frames==0);
}
static void ordinary_prototype_cannot_earn_flash(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    g.hero.x=QS_CITY_AIR_REWARD_X;
    g.hero.y=QS_CITY_AIR_REWARD_Y;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=0;
    qs_step(&g,idle());
    CHECK(g.city_air_reward_collected==0 && g.city_air_reward_fx_frames==0);
}
static void replay_with_stun_camera_and_pause_is_deterministic(void) {
    QsGame a=city(),b=a;
    for(int t=0;t<360;t++) {
        QsInput in=idle();
        if(t%90==0) {
            a.hero.x=b.hero.x=QS_CITY_AIR_REWARD_X;
            a.hero.y=b.hero.y=QS_CITY_AIR_REWARD_Y;
            a.hero.vx=b.hero.vx=a.hero.vy=b.hero.vy=0;
        }
        if(t%90==10 || t%90==19){in.back=in.change=1;}
        if(t%90==75){in.back=in.dash=1;}
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.city_air_reward_collected==b.city_air_reward_collected);
        CHECK(a.city_air_reward_fx_frames==b.city_air_reward_fx_frames);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
    }
    CHECK(a.frames==360 && b.frames==360);
}
int main(void) {
    fatal_enemy_blocks_air_reward_on_same_tick();
    nonfatal_enemy_damage_then_heal();
    stunning_enemy_prevents_contact_damage();
    projectile_stuns_enemy_before_reward_arbitration();
    checkpoint_respawn_preserves_claim_but_retires_flash();
    camera_phase_does_not_change_pickup_collision();
    flash_full_tick_then_exact_countdown();
    pause_freezes_flash_and_hero_camera();
    bad_flash_is_safely_normalized();
    new_attempt_clears_both_flag_and_flash();
    map_exit_clears_both_flag_and_flash();
    ordinary_prototype_cannot_earn_flash();
    replay_with_stun_camera_and_pause_is_deterministic();
    puts("PASS: 13 air-reward interaction scenarios and 360 replay frames");
    return EXIT_SUCCESS;
}
