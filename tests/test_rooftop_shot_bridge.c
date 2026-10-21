#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if(!(expr)) { \
    fprintf(stderr,"ROOFTOP_BRIDGE_FAIL line %d: %s\n",__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while(0)

static QsInput idle(void) { QsInput in={0};return in; }

static QsGame roof(void) {
    QsGame g;
    qs_init(&g);
    qs_begin_stage(&g,2);
    CHECK(g.mode==QS_STAGE && g.stage==2);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    return g;
}

static int all_bridge_tiles(const QsGame *g,int expected_solid) {
    int col;
    for(col=QS_ROOF_BRIDGE_FIRST_COL;col<=QS_ROOF_BRIDGE_LAST_COL;++col)
        if(qs_solid(g,col,13)!=expected_solid ||
           qs_solid(g,col,14)!=expected_solid)
            return 0;
    return 1;
}

static void arm_yellow_and_wait(QsGame *g) {
    QsInput fire=idle();
    int ticks;
    g->hero.x=640;
    g->hero.y=188;
    g->hero.facing=1;
    g->hero.grounded=1;
    fire.fire=1;
    qs_step(g,fire);
    CHECK(g->rooftop_bridge_open==0);
    for(ticks=0;ticks<22 && g->rooftop_bridge_open==0;++ticks)
        qs_step(g,idle());
}

static void actual_projectile_creates_all_bridge_cells(void) {
    QsGame g=roof();
    CHECK(all_bridge_tiles(&g,0) && g.rooftop_bridge_open==0);
    arm_yellow_and_wait(&g);
    CHECK(g.rooftop_bridge_open==1 && all_bridge_tiles(&g,1));
    CHECK(g.mode==QS_STAGE && g.cleared[2]==0);
}

static void walking_on_new_floor_is_real_collision(void) {
    QsGame g=roof();
    QsInput in=idle();
    int tick;
    arm_yellow_and_wait(&g);
    CHECK(all_bridge_tiles(&g,1));
    /* Isolate floor traversal from enemy patrols: test the terrain,
     * not invulnerability or contact logic from a different module. */
    for(tick=0;tick<QS_MAX_ENEMIES;++tick)
        g.enemies[tick].active=0;
    g.hero.x=804;g.hero.y=188;g.hero.grounded=1;
    in.right=1;
    for(tick=0;tick<70;++tick)
        qs_step(&g,in);
    CHECK(g.hero.x>900.0f && g.hero.x<QS_COLUMNS*QS_TILE);
    CHECK(g.hero.grounded==1 && g.hero.y>=187.0f &&
          g.hero.y<=189.0f);
    CHECK(all_bridge_tiles(&g,1));
}

static void projectile_is_not_instant_switch_toggle(void) {
    QsGame g=roof();
    QsInput in=idle();
    g.hero.x=640;g.hero.y=188;g.hero.grounded=1;
    in.fire=1;
    qs_step(&g,in);
    CHECK(g.rooftop_bridge_open==0 && all_bridge_tiles(&g,0));
    in=idle();
    qs_step(&g,in);
    CHECK(g.rooftop_bridge_open==0 && all_bridge_tiles(&g,0));
    for(int tick=0;tick<20 && !g.rooftop_bridge_open;++tick)
        qs_step(&g,in);
    CHECK(g.rooftop_bridge_open==1);
}

static void red_shot_cannot_activate_yellow_switch(void) {
    QsGame g=roof();QsInput fire=idle();
    int tick;
    g.red_unlocked=1;g.weapon=QS_RED;
    g.hero.x=640;g.hero.y=188;g.hero.facing=1;
    fire.fire=1;qs_step(&g,fire);
    for(tick=0;tick<26;++tick)qs_step(&g,idle());
    CHECK(g.rooftop_bridge_open==0 && all_bridge_tiles(&g,0));
}

static void hero_contact_cannot_forge_projectile_hit(void) {
    QsGame g=roof();
    g.hero.x=QS_ROOF_SWITCH_X;
    g.hero.y=188;g.hero.grounded=1;
    qs_step(&g,idle());
    CHECK(g.rooftop_bridge_open==0 && all_bridge_tiles(&g,0));
}

static void stage_reentry_restores_pit_and_other_progress(void) {
    QsGame g=roof();arm_yellow_and_wait(&g);
    CHECK(all_bridge_tiles(&g,1));
    g.ruins_relic_earned=1;g.cleared[0]=1;
    qs_begin_stage(&g,1);
    CHECK(g.rooftop_bridge_open==0 && g.cleared[0]==1 &&
          g.ruins_relic_earned==1);
    qs_begin_stage(&g,2);
    CHECK(g.rooftop_bridge_open==0 && all_bridge_tiles(&g,0));
    CHECK(g.cleared[0]==1 && g.ruins_relic_earned==1);
}

static void stage_isolation_keeps_other_levels_intact(void) {
    QsGame g;
    qs_init(&g);qs_begin_stage(&g,1);
    arm_yellow_and_wait(&g);
    CHECK(g.stage==1 && g.rooftop_bridge_open==0);
    qs_begin_stage(&g,0);
    arm_yellow_and_wait(&g);
    CHECK(g.stage==0 && g.rooftop_bridge_open==0);
}

int main(void) {
    actual_projectile_creates_all_bridge_cells();
    walking_on_new_floor_is_real_collision();
    projectile_is_not_instant_switch_toggle();
    red_shot_cannot_activate_yellow_switch();
    hero_contact_cannot_forge_projectile_hit();
    stage_reentry_restores_pit_and_other_progress();
    stage_isolation_keeps_other_levels_intact();
    puts("ROOFTOP_BRIDGE_TEST_PASS: seven projectile, collision and reset cases");
    return EXIT_SUCCESS;
}
