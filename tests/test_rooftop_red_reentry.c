#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * Stage155: portable/native transition regression on authored test scenes.
 * Stage154 independently proved the REAL NeXium pickup and red impact.
 * This test starts from an already-earned red unlock and exercises qs_step:
 * two ROOFTOP visits with red shots, then an intentional yellow activation.
 * No original ROM, host-injected guest state, or proprietary assets.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "STAGE155_RED_REENTRY_FAIL:%d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput none(void) { QsInput in={0}; return in; }
static void step(QsGame *g, QsInput in) { qs_step(g,in); }

static int bridge_is(const QsGame *g,int solid) {
    int col, row;
    for(col=QS_ROOF_BRIDGE_FIRST_COL; col<=QS_ROOF_BRIDGE_LAST_COL; ++col)
        for(row=13;row<=14;++row)
            if(qs_solid(g,col,row)!=solid) return 0;
    return 1;
}

static void map_down(QsGame *g) {
    QsInput in=none();
    CHECK(g->mode==QS_MAP);
    in.down=1; step(g,in);
    step(g,none()); /* release for a new edge */
}
static void map_confirm(QsGame *g) {
    QsInput in=none();
    CHECK(g->mode==QS_MAP);
    in.select=1; step(g,in);
    CHECK(g->mode==QS_STAGE);
}
static void start_roof(QsGame *g) {
    qs_init(g);
    g->red_unlocked=1;g->weapon=QS_RED;
    g->ruins_relic_earned=1;g->cleared[0]=1;
    map_down(g);map_down(g);
    CHECK(g->selected_stage==2);
    map_confirm(g);
    CHECK(g->stage==2 && g->physics_profile==QS_PHYSICS_PROTOTYPE);
    CHECK(g->red_unlocked==1 && g->weapon==QS_RED);
    CHECK(g->ruins_relic_earned==1 && g->cleared[0]==1);
    CHECK(!g->rooftop_bridge_open && bridge_is(g,0));
}
static void leave_and_reenter_roof(QsGame *g) {
    QsInput in=none();
    int i;
    CHECK(g->mode==QS_STAGE && g->stage==2);
    in.back=1;step(g,in);
    CHECK(g->mode==QS_MAP && g->selected_stage==2);
    CHECK(g->red_unlocked==1 && g->ruins_relic_earned==1);
    CHECK(g->cleared[0]==1);
    step(g,none());
    map_confirm(g);
    CHECK(g->stage==2 && g->mode==QS_STAGE);
    CHECK(g->red_unlocked && g->ruins_relic_earned && g->cleared[0]);
    CHECK(!g->rooftop_bridge_open && bridge_is(g,0));
    for(i=0;i<QS_MAX_SHOTS;++i)CHECK(!g->shots[i].active);
}
static void fire_from_switch_approach(QsGame *g, int red) {
    QsInput in=none();
    int i,t,observed=0;
    CHECK(g->stage==2 && g->mode==QS_STAGE);
    CHECK(g->weapon==(red ? QS_RED : QS_YELLOW));
    g->hero.x=640.0f;g->hero.y=188.0f;
    g->hero.vx=0;g->hero.vy=0;
    g->hero.grounded=1;g->hero.facing=1;
    in.fire=1;
    step(g,in);
    for(i=0;i<QS_MAX_SHOTS;++i) {
        if(g->shots[i].active) {
            CHECK(g->shots[i].red==red);
            observed=1;
        }
    }
    CHECK(observed);
    for(t=0;t<23;++t) {
        step(g,none());
        if(red) CHECK(!g->rooftop_bridge_open && bridge_is(g,0));
        for(i=0;i<QS_MAX_SHOTS;++i)
            if(g->shots[i].active) CHECK(g->shots[i].red==red);
    }
    CHECK(red ? (!g->rooftop_bridge_open && bridge_is(g,0))
              : (g->rooftop_bridge_open && bridge_is(g,1)));
}
static void change_weapon(QsGame *g, QsWeapon expected) {
    QsInput in=none();
    in.change=1;step(g,in);
    CHECK(g->weapon==expected);
    step(g,none());
}

static void red_shot_stays_negative_after_map_reentry(void) {
    QsGame g;
    start_roof(&g);
    fire_from_switch_approach(&g,1);
    leave_and_reenter_roof(&g);
    CHECK(g.weapon==QS_RED);
    fire_from_switch_approach(&g,1);
    CHECK(g.cleared[2]==0 && g.ruins_relic_earned==1);
}
static void yellow_switch_remains_usable_after_two_red_failures(void) {
    QsGame g;
    start_roof(&g);
    fire_from_switch_approach(&g,1);
    leave_and_reenter_roof(&g);
    fire_from_switch_approach(&g,1);
    change_weapon(&g,QS_YELLOW);
    fire_from_switch_approach(&g,0);
    CHECK(g.rooftop_bridge_open && bridge_is(&g,1));
    leave_and_reenter_roof(&g);
    CHECK(g.weapon==QS_YELLOW);
    CHECK(!g.rooftop_bridge_open && bridge_is(&g,0));
    change_weapon(&g,QS_RED);
    fire_from_switch_approach(&g,1);
}
static void red_unlock_never_regenerates_city_pickup(void) {
    QsGame g;
    start_roof(&g);
    leave_and_reenter_roof(&g);
    QsInput in=none();
    in.back=1;step(&g,in);
    CHECK(g.mode==QS_MAP && g.selected_stage==2);
    step(&g,none());
    map_down(&g); /* 2 -> 0 */
    CHECK(g.selected_stage==0);
    map_confirm(&g);
    CHECK(g.mode==QS_STAGE && g.stage==0);
    CHECK(g.red_unlocked && g.weapon==QS_RED);
    CHECK(!g.pickups[0].active);
    CHECK(g.ruins_relic_earned && g.cleared[0]);
}
static void repeated_back_does_not_reenter_or_clear_unlock(void) {
    QsGame g;
    start_roof(&g);
    QsInput back=none();back.back=1;
    step(&g,back);
    CHECK(g.mode==QS_MAP);
    int frame=g.frames;
    for(int i=0;i<45;++i) {
        step(&g,back);
        CHECK(g.mode==QS_MAP && g.selected_stage==2);
        CHECK(g.red_unlocked && g.weapon==QS_RED);
        CHECK(g.ruins_relic_earned);
    }
    CHECK(g.frames==frame+45);
    step(&g,none());
    map_confirm(&g);
    CHECK(g.stage==2 && g.mode==QS_STAGE);
    CHECK(g.weapon==QS_RED && bridge_is(&g,0));
}

int main(void) {
    red_shot_stays_negative_after_map_reentry();
    yellow_switch_remains_usable_after_two_red_failures();
    red_unlock_never_regenerates_city_pickup();
    repeated_back_does_not_reenter_or_clear_unlock();
    puts("STAGE155_RED_REENTRY_NATIVE_PASS: 4 red/transition/color/reset cases");
    return EXIT_SUCCESS;
}
