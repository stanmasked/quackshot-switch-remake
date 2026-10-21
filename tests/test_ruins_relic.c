#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr,"RUINS_RELIC_FAIL line %d: %s\n",__LINE__,#x); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0}; return in; }
static QsGame ruins(void) {
    QsGame g; qs_init(&g); qs_begin_stage(&g,1);
    REQUIRE(g.mode==QS_STAGE && g.physics_profile==QS_PHYSICS_PROTOTYPE);
    return g;
}
static void plate(QsGame *g) {
    QsInput in=idle();
    g->hero.x=QS_RUINS_PLATE_X;g->hero.y=188;g->hero.grounded=1;
    in.down=1;qs_step(g,in);
    REQUIRE(g->ruins_gate_open==1);
}
static void at_relic(QsGame *g) {
    g->hero.x=QS_RUINS_RELIC_X;
    g->hero.y=QS_RUINS_RELIC_Y;g->hero.grounded=1;
}
static void closed_gate_does_not_award_forged_position(void) {
    QsGame g=ruins();
    at_relic(&g);qs_step(&g,idle());
    REQUIRE(g.ruins_gate_open==0 && !g.ruins_relic_collected &&
            !g.ruins_relic_earned);
}
static void real_open_gate_and_walk_through_to_relic(void) {
    QsGame g=ruins();QsInput in=idle();
    int tick;
    plate(&g);
    /* Place at safe near-wall ground as a test fixture, then cross
     * continuously through the actual unblocked corridor. */
    g.hero.x=QS_RUINS_GATE_COL*QS_TILE-12.0f;
    g.hero.y=188;g.hero.grounded=1;
    in.right=1;
    for(tick=0;tick<41;tick++)qs_step(&g,in);
    REQUIRE(g.hero.x>QS_RUINS_RELIC_X);
    REQUIRE(g.ruins_relic_collected==1 && g.ruins_relic_earned==1);
    REQUIRE(g.mode==QS_STAGE && g.cleared[1]==0);
    for(tick=0;tick<30;tick++)qs_step(&g,in);
    REQUIRE(g.ruins_relic_collected==1 && g.ruins_relic_earned==1);
}
static void one_shot_reward_requires_live_stage_and_gate(void) {
    QsGame g=ruins();
    plate(&g);
    at_relic(&g);qs_step(&g,idle());
    REQUIRE(g.ruins_relic_collected==1 && g.ruins_relic_earned==1);
    /* The consumed stage objective cannot become available twice. */
    g.hero.x=40;qs_step(&g,idle());
    at_relic(&g);qs_step(&g,idle());
    REQUIRE(g.ruins_relic_collected==1 && g.ruins_relic_earned==1);
}
static void earned_map_badge_survives_back_and_reentry(void) {
    QsGame g=ruins();QsInput back=idle();
    plate(&g);at_relic(&g);qs_step(&g,idle());
    back.back=1;qs_step(&g,back);
    REQUIRE(g.mode==QS_MAP && g.ruins_relic_earned==1);
    REQUIRE(g.cleared[1]==0);
    qs_begin_stage(&g,1);
    REQUIRE(g.ruins_gate_open==0 && g.ruins_relic_collected==0);
    REQUIRE(g.ruins_relic_earned==1);
}
static void other_scenes_do_not_grant_relic(void) {
    QsGame g;qs_init(&g);
    qs_begin_stage(&g,2);g.ruins_gate_open=1;at_relic(&g);
    qs_step(&g,idle());
    REQUIRE(g.ruins_relic_collected==0 && g.ruins_relic_earned==0);
    qs_begin_stage(&g,0);g.ruins_gate_open=1;at_relic(&g);
    qs_step(&g,idle());
    REQUIRE(g.ruins_relic_collected==0 && g.ruins_relic_earned==0);
}
static void collection_does_not_grant_health_or_red_ammo(void) {
    QsGame g=ruins();plate(&g);
    g.hero.hearts=2;g.red_unlocked=0;g.weapon=QS_YELLOW;
    at_relic(&g);qs_step(&g,idle());
    REQUIRE(g.ruins_relic_collected==1);
    REQUIRE(g.hero.hearts==2 && g.red_unlocked==0 && g.weapon==QS_YELLOW);
}
int main(void) {
    closed_gate_does_not_award_forged_position();
    real_open_gate_and_walk_through_to_relic();
    one_shot_reward_requires_live_stage_and_gate();
    earned_map_badge_survives_back_and_reentry();
    other_scenes_do_not_grant_relic();
    collection_does_not_grant_health_or_red_ammo();
    puts("RUINS_RELIC_TEST_PASS: six objective, collision, persistence and isolation cases");
    return EXIT_SUCCESS;
}
