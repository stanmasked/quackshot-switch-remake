#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"frame arbitration FAIL %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput q={0}; return q; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    memset(g.enemies,0,sizeof(g.enemies));
    memset(g.shots,0,sizeof(g.shots));
    memset(g.pickups,0,sizeof(g.pickups));
    g.hero.x=100;g.hero.y=188;
    g.hero.vx=g.hero.vy=0;g.hero.grounded=1;
    return g;
}
static void pickup(QsGame *g,int id,float x,float y,int kind) {
    QsPickup *p=&g->pickups[id];
    memset(p,0,sizeof(*p));
    p->active=1;p->x=x;p->y=y;p->kind=kind;
}
static void foe(QsGame *g,int id,float x) {
    QsEnemy *e=&g->enemies[id];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;
    e->min_x=e->max_x=x;e->dir=1;
}
static void numeric_retry_does_not_collect_spawn_red_on_old_tick(void) {
    QsGame g=city();
    pickup(&g,0,36,189,QS_PICKUP_RED);
    g.hero.x=NAN;
    qs_step(&g,idle());
    CHECK(g.mode==QS_STAGE && g.hero.x==40 && g.hero.y==188);
    CHECK(g.hero.hurt_frames==89);
    CHECK(g.pickups[0].active && !g.red_unlocked && g.weapon==QS_YELLOW);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.red_unlocked && g.weapon==QS_RED);
    CHECK(g.hero.hurt_frames==88);
}
static void numeric_retry_does_not_consume_old_position_item(void) {
    QsGame g=city();
    pickup(&g,0,100,189,QS_PICKUP_RED);
    g.hero.y=INFINITY;
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.y==188);
    CHECK(g.pickups[0].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(g.pickups[0].active && !g.red_unlocked);
}
static void numeric_retry_defers_spawn_heart(void) {
    QsGame g=city();
    pickup(&g,0,36,189,QS_PICKUP_HEART);
    g.hero.x=-INFINITY;
    qs_step(&g,idle());
    CHECK(g.pickups[0].active && g.hero.hearts==3);
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.hero.hearts==3);
}
static void invalid_pickup_removed_but_valid_spawn_pickup_deferred(void) {
    QsGame g=city();
    pickup(&g,0,NAN,189,QS_PICKUP_RED);
    pickup(&g,1,36,189,QS_PICKUP_RED);
    g.hero.x=INFINITY;
    qs_step(&g,idle());
    CHECK(!g.pickups[0].active && g.pickups[0].x==0);
    CHECK(g.pickups[1].active && !g.red_unlocked);
    qs_step(&g,idle());
    CHECK(!g.pickups[1].active && g.red_unlocked);
}
static void nonfatal_foe_and_heart_in_same_tick_restore_health(void) {
    QsGame g=city();
    g.hero.hearts=2;
    foe(&g,0,100);
    pickup(&g,0,100,189,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.hero.x==100 && g.hero.hearts==2);
    CHECK(g.hero.hurt_frames==69);
    CHECK(!g.pickups[0].active);
}
static void fatal_foe_cannot_claim_old_or_spawn_pickup(void) {
    QsGame g=city();
    g.hero.hearts=1;
    foe(&g,0,100);
    pickup(&g,0,100,189,QS_PICKUP_RED);
    pickup(&g,1,36,189,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.hero.hearts==3 && g.hero.hurt_frames==89);
    CHECK(!g.red_unlocked && g.pickups[0].active && g.pickups[1].active);
    qs_step(&g,idle());
    CHECK(g.pickups[0].active && !g.pickups[1].active);
}
static void projectile_stuns_foe_before_contact_and_allows_pickup(void) {
    QsGame g=city();
    QsShot *s=&g.shots[0];
    foe(&g,0,100);
    pickup(&g,0,100,189,QS_PICKUP_RED);
    s->active=1;s->x=94;s->y=195;s->vx=1;s->frames=65;
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==179);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    CHECK(g.red_unlocked && !g.pickups[0].active);
}
static void last_stun_tick_still_allows_same_tick_heart(void) {
    QsGame g=city();
    g.hero.hearts=2;
    foe(&g,0,100);
    g.enemies[0].stunned_frames=1;
    pickup(&g,0,100,189,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.enemies[0].stunned_frames==0);
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==0);
    CHECK(!g.pickups[0].active);
}
static void multiple_contacts_damage_once_and_award_bounded_items(void) {
    QsGame g=city();
    g.hero.hearts=2;
    foe(&g,0,100);foe(&g,1,100);foe(&g,2,100);
    pickup(&g,0,100,189,QS_PICKUP_HEART);
    pickup(&g,1,100,189,QS_PICKUP_HEART);
    pickup(&g,2,100,189,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.hero.hearts==3 && g.hero.hurt_frames==69);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active && !g.pickups[1].active && !g.pickups[2].active);
}
static void crossing_exit_awards_pickups_before_enemy_damage(void) {
    QsGame g=city();
    QsInput in=idle();
    const float goal=(float)((QS_COLUMNS-4)*QS_TILE);
    g.hero.x=goal-0.1f;g.hero.hearts=1;
    foe(&g,0,goal+0.5f);
    pickup(&g,0,goal+1,189,QS_PICKUP_RED);
    pickup(&g,1,goal+2,189,QS_PICKUP_HEART);
    in.right=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(g.hero.hearts==2 && g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active && !g.pickups[1].active);
    CHECK(g.camera_x==0 && g.render_camera_x==0);
}
static void already_at_goal_does_not_replay_pickup(void) {
    QsGame g=city();
    const float goal=(float)((QS_COLUMNS-4)*QS_TILE);
    g.hero.x=goal+1;g.hero.hearts=1;
    pickup(&g,0,goal+1,189,QS_PICKUP_HEART);
    qs_step(&g,idle());
    CHECK(g.mode==QS_MAP && g.cleared[0]);
    CHECK(g.hero.hearts==1 && g.pickups[0].active);
}
static void prototype_keeps_legacy_respawn_pickup_timing(void) {
    QsGame g=city();
    g.physics_profile=QS_PHYSICS_PROTOTYPE;
    g.hero.hearts=1;
    foe(&g,0,100);
    pickup(&g,0,36,189,QS_PICKUP_RED);
    qs_step(&g,idle());
    CHECK(g.hero.x==40 && g.red_unlocked && !g.pickups[0].active);
}
static void deterministic_600_tick_mixed_events(void) {
    QsGame a=city(),b;
    int t,i;
    b=a;
    for(t=0;t<600;t++) {
        QsInput in=idle();
        if(t%41==0) {
            a.hero.x=b.hero.x=NAN;
            pickup(&a,0,36,189,QS_PICKUP_RED);
            pickup(&b,0,36,189,QS_PICKUP_RED);
        }
        if(t%67==9) {
            pickup(&a,1,40,189,QS_PICKUP_HEART);
            pickup(&b,1,40,189,QS_PICKUP_HEART);
        }
        if(t%83==19) {
            pickup(&a,2,INFINITY,189,QS_PICKUP_RED);
            pickup(&b,2,INFINITY,189,QS_PICKUP_RED);
        }
        if(t%113==31) {
            foe(&a,0,40);foe(&b,0,40);
            a.hero.hearts=b.hero.hearts=2;
            a.hero.hurt_frames=b.hero.hurt_frames=0;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.frames==b.frames && a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.red_unlocked==b.red_unlocked && a.weapon==b.weapon);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(i=0;i<QS_MAX_PICKUPS;i++) {
            CHECK(a.pickups[i].active==b.pickups[i].active);
            CHECK(a.pickups[i].x==b.pickups[i].x);
            CHECK(a.pickups[i].y==b.pickups[i].y);
        }
    }
    CHECK(a.frames==600 && b.frames==600 && a.red_unlocked);
}
int main(void) {
    numeric_retry_does_not_collect_spawn_red_on_old_tick();
    numeric_retry_does_not_consume_old_position_item();
    numeric_retry_defers_spawn_heart();
    invalid_pickup_removed_but_valid_spawn_pickup_deferred();
    nonfatal_foe_and_heart_in_same_tick_restore_health();
    fatal_foe_cannot_claim_old_or_spawn_pickup();
    projectile_stuns_foe_before_contact_and_allows_pickup();
    last_stun_tick_still_allows_same_tick_heart();
    multiple_contacts_damage_once_and_award_bounded_items();
    crossing_exit_awards_pickups_before_enemy_damage();
    already_at_goal_does_not_replay_pickup();
    prototype_keeps_legacy_respawn_pickup_timing();
    deterministic_600_tick_mixed_events();
    puts("PASS: 13 same-frame contact/reward scenarios and 600 deterministic ticks");
    return EXIT_SUCCESS;
}
