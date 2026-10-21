#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "exit interactions FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

/* Only independently created CITY TEST positions and objects. */
static QsInput none(void) { QsInput in={0};return in; }
static QsGame city(void) {
    QsGame g;qs_init(&g);g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    return g;
}
static void crossing(QsGame *g) {
    g->hero.x=(QS_COLUMNS-4)*QS_TILE-0.1f;
    g->hero.y=188;
    g->hero.vx=g->hero.vy=0;
    g->hero.grounded=1;
}
static void foe(QsGame *g,int slot,float x) {
    QsEnemy *e=&g->enemies[slot];
    memset(e,0,sizeof(*e));
    e->active=1;e->x=x;e->y=188;
    e->dir=1;e->min_x=e->max_x=x;
}
static void pickup(QsGame *g,int slot,float x,int kind) {
    QsPickup *p=&g->pickups[slot];
    memset(p,0,sizeof(*p));
    p->active=1;p->x=x;p->y=189;p->kind=kind;
}
static void clean_finish(const QsGame *g) {
    int i;
    CHECK(g->mode==QS_MAP && g->cleared[0]);
    CHECK(g->camera_x==0 && g->render_camera_x==0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    for(i=0;i<QS_MAX_SHOTS;i++)
        CHECK(!g->shots[i].active && !g->shots[i].attached);
}
static void lethal_enemy_does_not_erase_newly_crossed_goal(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    g.hero.hearts=1;
    foe(&g,0,(QS_COLUMNS-4)*QS_TILE+0.5f);
    in.right=1;qs_step(&g,in);
    clean_finish(&g);
    CHECK(g.hero.hearts==1);
    CHECK(g.hero.x>=(QS_COLUMNS-4)*QS_TILE);
}
static void red_pickup_at_goal_is_persistent(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    pickup(&g,0,(QS_COLUMNS-4)*QS_TILE+1,QS_PICKUP_RED);
    in.right=1;qs_step(&g,in);
    clean_finish(&g);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active);
    in=none();qs_step(&g,in);
    in.select=1;in.dash=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active);
}
static void heart_pickup_at_exit_is_applied(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    g.hero.hearts=1;
    pickup(&g,1,(QS_COLUMNS-4)*QS_TILE+1,QS_PICKUP_HEART);
    in.right=1;qs_step(&g,in);
    clean_finish(&g);
    CHECK(g.hero.hearts==2);
    CHECK(!g.pickups[1].active);
}
static void pickup_and_lethal_enemy_on_same_goal_step(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    g.hero.hearts=1;
    foe(&g,0,(QS_COLUMNS-4)*QS_TILE+0.5f);
    pickup(&g,0,(QS_COLUMNS-4)*QS_TILE+1,QS_PICKUP_RED);
    pickup(&g,1,(QS_COLUMNS-4)*QS_TILE+2,QS_PICKUP_HEART);
    in.right=1;in.fire=1;in.change=1;
    qs_step(&g,in);
    clean_finish(&g);
    CHECK(g.hero.hearts==2);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active && !g.pickups[1].active);
    CHECK(g.enemies[0].stunned_frames==0);
}
static void queued_shot_is_cleared_instead_of_firing_after_finish(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    g.pending_reference_shot=1;
    g.pending_reference_red=1;
    g.shots[0].active=g.shots[0].attached=1;
    g.shots[0].frames=230;
    in.right=1;qs_step(&g,in);
    clean_finish(&g);
}
static void already_at_exit_still_finishes_before_pickup_scan(void) {
    QsGame g=city();QsInput in=none();
    g.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
    g.hero.y=188;g.hero.hearts=1;
    pickup(&g,0,g.hero.x,QS_PICKUP_HEART);
    in.back=1;qs_step(&g,in);
    clean_finish(&g);
    /* Stage 36 established a zero-simulation boundary handoff. */
    CHECK(g.hero.hearts==1 && g.pickups[0].active);
}
static void back_before_crossing_does_not_award_goal_or_pickup(void) {
    QsGame g=city();QsInput in=none();
    crossing(&g);
    g.hero.hearts=1;
    pickup(&g,0,(QS_COLUMNS-4)*QS_TILE+1,QS_PICKUP_RED);
    in.right=1;in.back=1;
    qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.cleared[0]);
    CHECK(!g.red_unlocked && g.pickups[0].active);
}
static void legacy_prototype_keeps_enemy_before_finish(void) {
    QsGame g;QsInput in=none();
    qs_init(&g);qs_begin_stage(&g,0);
    crossing(&g);
    g.hero.hearts=1;
    foe(&g,0,(QS_COLUMNS-4)*QS_TILE+0.5f);
    in.right=1;qs_step(&g,in);
    CHECK(g.mode==QS_STAGE && !g.cleared[0]);
    CHECK(g.hero.x==40 && g.hero.hearts==3);
}
static void repeated_goal_contact_replay_is_deterministic(void) {
    QsGame a=city(),b;int t,k;
    crossing(&a);a.hero.hearts=1;
    foe(&a,0,(QS_COLUMNS-4)*QS_TILE+0.5f);
    pickup(&a,0,(QS_COLUMNS-4)*QS_TILE+1,QS_PICKUP_RED);
    b=a;
    for(t=0;t<450;t++) {
        QsInput in=none();
        if(t==0)in.right=1;
        if(t%75==2){in.select=1;in.dash=1;}
        if(t%75==11){in.back=1;}
        if(t>0 && t%75==0) {
            if(a.mode==QS_STAGE)crossing(&a);
            if(b.mode==QS_STAGE)crossing(&b);
            in.right=1;
        }
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.stage==b.stage);
        CHECK(a.cleared[0]==b.cleared[0] && a.red_unlocked==b.red_unlocked);
        CHECK(a.weapon==b.weapon && a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        for(k=0;k<QS_MAX_SHOTS;k++)
            CHECK(a.shots[k].active==b.shots[k].active);
    }
    CHECK(a.cleared[0] && a.red_unlocked);
}
int main(void) {
    lethal_enemy_does_not_erase_newly_crossed_goal();
    red_pickup_at_goal_is_persistent();
    heart_pickup_at_exit_is_applied();
    pickup_and_lethal_enemy_on_same_goal_step();
    queued_shot_is_cleared_instead_of_firing_after_finish();
    already_at_exit_still_finishes_before_pickup_scan();
    back_before_crossing_does_not_award_goal_or_pickup();
    legacy_prototype_keeps_enemy_before_finish();
    repeated_goal_contact_replay_is_deterministic();
    puts("PASS: 9 exit-enemy-pickup scenarios, 450-tick replay and legacy isolation");
    return EXIT_SUCCESS;
}
