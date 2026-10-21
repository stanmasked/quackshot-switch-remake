/* Stage159: portable C99 regression of YELLOW bridge re-arming on ROOFTOP.
 * This is a native engine test, not new NeXium evidence. The live two-guest
 * opening/reset evidence is pinned separately at Stage158.
 * All scene transitions and shots below use qs_step() and actual collision
 * cells. Player position is a native test fixture, never guest injection.
 */
#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(ok) do { if (!(ok)) { \
    fprintf(stderr,"STAGE159_YELLOW_REARM_FAIL:%d %s\n",__LINE__,#ok); \
    exit(EXIT_FAILURE); } } while (0)

static QsInput neutral(void) { QsInput k={0}; return k; }
static void tick(QsGame *g, QsInput k) { qs_step(g,k); }
static int bridge_cells(const QsGame *g) {
    int cells=0;
    for(int col=QS_ROOF_BRIDGE_FIRST_COL;col<=QS_ROOF_BRIDGE_LAST_COL;++col)
        for(int row=13;row<=14;++row)
            cells+=qs_solid(g,col,row)!=0;
    return cells;
}
static void start_with_earned_yellow(QsGame *g) {
    qs_init(g);
    g->red_unlocked=1;
    g->weapon=QS_YELLOW;
    g->cleared[0]=1;
    g->ruins_relic_earned=1;
    QsInput down=neutral(); down.down=1;
    tick(g,down);tick(g,neutral());
    tick(g,down);tick(g,neutral());
    REQUIRE(g->mode==QS_MAP && g->selected_stage==2);
    QsInput select=neutral();select.select=1;
    tick(g,select);
    REQUIRE(g->mode==QS_STAGE && g->stage==2);
    REQUIRE(g->weapon==QS_YELLOW && g->red_unlocked);
    REQUIRE(bridge_cells(g)==0 && !g->rooftop_bridge_open);
}
static void shoot(QsGame *g,int expect_red,int expected_cells) {
    REQUIRE(g->mode==QS_STAGE && g->stage==2);
    REQUIRE(g->weapon==(expect_red?QS_RED:QS_YELLOW));
    /* Fixture ensures the projectile takes a real 640->704 engine path. */
    g->hero.x=640.0f;g->hero.y=188.0f;
    g->hero.vx=0;g->hero.vy=0;g->hero.grounded=1;g->hero.facing=1;
    QsInput fire=neutral();fire.fire=1;
    tick(g,fire);
    int seen=0;
    for(int i=0;i<QS_MAX_SHOTS;++i) if(g->shots[i].active) {
        REQUIRE(g->shots[i].red==expect_red);
        seen=1;
    }
    REQUIRE(seen);
    for(int frame=0;frame<25;++frame) tick(g,neutral());
    REQUIRE(bridge_cells(g)==expected_cells);
    REQUIRE(g->rooftop_bridge_open==(expected_cells==6));
}
static void to_map_and_reenter(QsGame *g,int old_cells) {
    REQUIRE(bridge_cells(g)==old_cells);
    QsInput back=neutral();back.back=1;
    tick(g,back);
    REQUIRE(g->mode==QS_MAP && g->selected_stage==2);
    REQUIRE(g->weapon==QS_YELLOW || g->weapon==QS_RED);
    REQUIRE(g->ruins_relic_earned && g->red_unlocked && g->cleared[0]);
    REQUIRE(g->rooftop_bridge_open==(old_cells==6));
    tick(g,neutral());
    QsInput select=neutral();select.select=1;
    tick(g,select);
    REQUIRE(g->mode==QS_STAGE && g->stage==2);
    REQUIRE(g->ruins_relic_earned && g->red_unlocked && g->cleared[0]);
    REQUIRE(!g->rooftop_bridge_open && bridge_cells(g)==0);
    for(int i=0;i<QS_MAX_SHOTS;++i)REQUIRE(!g->shots[i].active);
    tick(g,neutral()); /* keep future FIRE/CHANGE inputs independent */
}
static void change(QsGame *g,QsWeapon expected) {
    QsInput k=neutral();k.change=1;
    tick(g,k);
    REQUIRE(g->weapon==expected);
    tick(g,neutral());
}
static void yellow_can_reopen_after_third_visit(void) {
    QsGame g;
    start_with_earned_yellow(&g);
    shoot(&g,0,6);
    to_map_and_reenter(&g,6);
    REQUIRE(g.weapon==QS_YELLOW);
    shoot(&g,0,6); /* true engine re-arm, not merely bridge reset */
    REQUIRE(g.cleared[2]==0 && g.ruins_relic_earned==1);
}
static void red_is_still_negative_after_yellow_reset(void) {
    QsGame g;
    start_with_earned_yellow(&g);
    shoot(&g,0,6);
    to_map_and_reenter(&g,6);
    change(&g,QS_RED);
    shoot(&g,1,0);
    change(&g,QS_YELLOW);
    shoot(&g,0,6);
    REQUIRE(g.ruins_relic_earned && g.cleared[0]);
}
static void multiple_map_reentries_preserve_rearm_and_unlock(void) {
    QsGame g;
    start_with_earned_yellow(&g);
    for(int visit=0;visit<4;++visit) {
        REQUIRE(g.weapon==QS_YELLOW && bridge_cells(&g)==0);
        shoot(&g,0,6);
        to_map_and_reenter(&g,6);
    }
    REQUIRE(g.weapon==QS_YELLOW && g.red_unlocked);
    REQUIRE(g.ruins_relic_earned && g.cleared[0]);
    REQUIRE(g.cleared[2]==0);
}
static void held_fire_does_not_count_as_new_hit_after_reentry(void) {
    QsGame g;
    start_with_earned_yellow(&g);
    shoot(&g,0,6);
    to_map_and_reenter(&g,6);
    REQUIRE(g.weapon==QS_YELLOW);
    /* Long held FIRE is one rising edge; bridge still activates only on a
     * real launched YELLOW projectile, not by a held-input assertion. */
    g.hero.x=640.0f;g.hero.y=188.0f;
    g.hero.vx=0;g.hero.vy=0;g.hero.grounded=1;g.hero.facing=1;
    QsInput fire=neutral();fire.fire=1;
    tick(&g,fire);
    for(int frame=0;frame<25;++frame) tick(&g,fire);
    REQUIRE(bridge_cells(&g)==6);
    REQUIRE(g.rooftop_bridge_open);
}
int main(void) {
    yellow_can_reopen_after_third_visit();
    red_is_still_negative_after_yellow_reset();
    multiple_map_reentries_preserve_rearm_and_unlock();
    held_fire_does_not_count_as_new_hit_after_reentry();
    puts("STAGE159_YELLOW_REARM_NATIVE_PASS: 4 independent replay variants");
    return EXIT_SUCCESS;
}
