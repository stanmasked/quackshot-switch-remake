#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No original ROM data, sprite assets or copied game geometry. */
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"combat transition FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)
static QsInput none(void) { QsInput in = {0}; return in; }
static QsGame scenario(QsPhysicsProfile profile) {
    QsGame g;
    int r,c,i;
    qs_init(&g);g.physics_profile=profile;qs_begin_stage(&g,0);
    for(r=0;r<QS_ROWS;r++)for(c=0;c<QS_COLUMNS;c++)g.terrain[r][c]='.';
    for(c=0;c<QS_COLUMNS;c++)g.terrain[13][c]=g.terrain[14][c]='#';
    for(i=0;i<QS_MAX_ENEMIES;i++)g.enemies[i].active=0;
    for(i=0;i<QS_MAX_PICKUPS;i++)g.pickups[i].active=0;
    g.hero.x=100;g.hero.y=188;g.hero.vx=g.hero.vy=0;
    g.hero.grounded=1;
    g.red_unlocked=1;
    g.weapon=QS_YELLOW;
    return g;
}
static void enemy(QsGame *g,int slot,float x) {
    QsEnemy *e=&g->enemies[slot];memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=192;e->dir=1;
    e->min_x=x;e->max_x=x;
}
static void projectile(QsGame *g,int slot,float x,float vx,int red) {
    QsShot *s=&g->shots[slot];memset(s,0,sizeof(*s));
    s->active=1;s->frames=65;s->x=x;s->y=198;
    s->vx=vx;s->red=red;
}
static void deferred_yellow_not_recolored_to_red(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=none();
    in.fire=1;qs_step(&g,in);
    CHECK(g.pending_reference_shot==1 && g.pending_reference_red==0);
    CHECK(!g.shots[0].active);
    in.fire=0;in.change=1;
    qs_step(&g,in);
    CHECK(g.weapon==QS_RED);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(g.shots[0].active && !g.shots[0].red);
}
static void deferred_red_not_recolored_to_yellow(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=none();
    g.weapon=QS_RED;
    in.fire=1;qs_step(&g,in);
    CHECK(g.pending_reference_shot==1 && g.pending_reference_red==1);
    in.fire=0;in.change=1;
    qs_step(&g,in);
    CHECK(g.weapon==QS_YELLOW);
    CHECK(g.shots[0].active && g.shots[0].red);
}
static void fire_and_toggle_same_tick_uses_current_selection(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=none();
    in.fire=1;in.change=1;qs_step(&g,in);
    CHECK(g.weapon==QS_RED && g.pending_reference_red==1);
    in.fire=0;in.change=0;qs_step(&g,in);
    CHECK(g.shots[0].active && g.shots[0].red);
}
static void stage_change_cancels_pending_color(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    QsInput in=none();
    g.weapon=QS_RED;
    in.fire=1;qs_step(&g,in);
    CHECK(g.pending_reference_shot && g.pending_reference_red);
    qs_begin_stage(&g,0);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(!g.shots[0].active);
}
static void ordinary_mode_still_fires_immediately(void) {
    QsGame g=scenario(QS_PHYSICS_PROTOTYPE);
    QsInput in=none();
    in.fire=1;qs_step(&g,in);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(g.shots[0].active && !g.shots[0].red);
}
static void simultaneous_stun_prevents_hero_contact(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    int i;
    enemy(&g,0,100);
    projectile(&g,0,94,1,0);
    qs_step(&g,none());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.hero.hearts==3 && !g.hero.hurt_frames);
    CHECK(!g.shots[0].active);
    for(i=0;i<179;i++) {
        qs_step(&g,none());
        CHECK(g.hero.hearts==3);
    }
    CHECK(g.enemies[0].stunned_frames==0);
    qs_step(&g,none());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
}
static void one_stunned_enemy_other_active_hurts_once(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    enemy(&g,0,100);enemy(&g,1,104);
    projectile(&g,0,94,1,1);
    qs_step(&g,none());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==0);
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    qs_step(&g,none());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==68);
}
static void double_stun_shields_player_in_one_step(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    enemy(&g,0,100);enemy(&g,1,124);
    g.hero.x=111;
    projectile(&g,0,94,1,0);projectile(&g,1,131,-1,1);
    qs_step(&g,none());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.enemies[1].stunned_frames==179);
    CHECK(g.hero.hearts==3);
}
static void invulnerability_expires_on_exact_frame(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    int k;
    enemy(&g,0,100);
    qs_step(&g,none());
    CHECK(g.hero.hearts==2 && g.hero.hurt_frames==69);
    for(k=0;k<69;k++) {
        qs_step(&g,none());
        CHECK(g.hero.hearts==2);
    }
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,none());
    CHECK(g.hero.hearts==1 && g.hero.hurt_frames==69);
}
static void losing_third_heart_respawns_with_immunity(void) {
    QsGame g=scenario(QS_PHYSICS_REFERENCE_CITY);
    int k;
    enemy(&g,0,40);
    g.hero.x=40;
    qs_step(&g,none());
    CHECK(g.hero.hearts==2);
    for(k=0;k<70;k++)qs_step(&g,none());
    CHECK(g.hero.hearts==1);
    for(k=0;k<70;k++)qs_step(&g,none());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(g.hero.x==40 && g.hero.y==188);
    for(k=0;k<89;k++) {
        qs_step(&g,none());
        CHECK(g.hero.hearts==3);
    }
    CHECK(g.hero.hurt_frames==0);
    qs_step(&g,none());
    CHECK(g.hero.hearts==2);
}
static void long_stun_hit_cycle_replays_exactly(void) {
    QsGame a=scenario(QS_PHYSICS_REFERENCE_CITY),b;
    int k,t;
    enemy(&a,0,105);enemy(&a,1,121);
    b=a;
    for(t=0;t<720;t++) {
        QsInput in=none();
        in.fire=(t%71)==0;
        in.change=(t%153)==20;
        in.jump=(t%59)<3;
        if((t%140)>=70 && (t%140)<105)in.right=1;
        if((t%140)>=110)in.left=1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.weapon==b.weapon);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        CHECK(a.pending_reference_red==b.pending_reference_red);
        CHECK(a.camera_x==b.camera_x);
        CHECK(a.render_camera_x==b.render_camera_x);
        for(k=0;k<QS_MAX_ENEMIES;k++) {
            CHECK(a.enemies[k].x==b.enemies[k].x);
            CHECK(a.enemies[k].stunned_frames==b.enemies[k].stunned_frames);
        }
        for(k=0;k<QS_MAX_SHOTS;k++) {
            CHECK(a.shots[k].active==b.shots[k].active);
            CHECK(a.shots[k].red==b.shots[k].red);
            CHECK(a.shots[k].attached==b.shots[k].attached);
        }
    }
    CHECK(a.frames==720);
}
int main(void) {
    deferred_yellow_not_recolored_to_red();
    deferred_red_not_recolored_to_yellow();
    fire_and_toggle_same_tick_uses_current_selection();
    stage_change_cancels_pending_color();
    ordinary_mode_still_fires_immediately();
    simultaneous_stun_prevents_hero_contact();
    one_stunned_enemy_other_active_hurts_once();
    double_stun_shields_player_in_one_step();
    invulnerability_expires_on_exact_frame();
    losing_third_heart_respawns_with_immunity();
    long_stun_hit_cycle_replays_exactly();
    puts("PASS: 11 original-free combat transitions, one-tick fire snapshots and 720-frame replay");
    return 0;
}
