#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr,"RUINS_GATE_TEST_FAIL line %d: %s\n",__LINE__,#expr); \
  exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput in={0};return in; }
static QsGame ruins(void) {
    QsGame g;qs_init(&g);qs_begin_stage(&g,1);
    CHECK(g.mode==QS_STAGE && g.stage==1);
    CHECK(g.physics_profile==QS_PHYSICS_PROTOTYPE);
    return g;
}
static int gate_tiles_closed(const QsGame *g) {
    int row;
    for(row=QS_RUINS_GATE_ROW_FIRST;row<=QS_RUINS_GATE_ROW_LAST;row++)
        if(!qs_solid(g,QS_RUINS_GATE_COL,row))return 0;
    return 1;
}
static int gate_tiles_open(const QsGame *g) {
    int row;
    for(row=QS_RUINS_GATE_ROW_FIRST;row<=QS_RUINS_GATE_ROW_LAST;row++)
        if(qs_solid(g,QS_RUINS_GATE_COL,row))return 0;
    return 1;
}
static void set_plate(QsGame *g) {
    g->hero.x=QS_RUINS_PLATE_X;
    g->hero.y=188;g->hero.grounded=1;
}
static void press_down(QsGame *g) {
    QsInput in=idle();in.down=1;qs_step(g,in);
}
static void grounded_plate_opens_physical_wall(void) {
    QsGame g=ruins();
    CHECK(gate_tiles_closed(&g) && g.ruins_gate_open==0);
    set_plate(&g);press_down(&g);
    CHECK(g.ruins_gate_open==1 && gate_tiles_open(&g));
    qs_step(&g,idle());
    CHECK(g.ruins_gate_open==1 && gate_tiles_open(&g));
}
static void switch_requires_new_down_edge_near_safe_floor(void) {
    QsGame g=ruins();
    g.hero.x=QS_RUINS_PLATE_X-40;g.hero.y=188;g.hero.grounded=1;
    press_down(&g);
    CHECK(!g.ruins_gate_open && gate_tiles_closed(&g));
    set_plate(&g);press_down(&g);
    CHECK(!g.ruins_gate_open);
    qs_step(&g,idle());set_plate(&g);press_down(&g);
    CHECK(g.ruins_gate_open==1 && gate_tiles_open(&g));
}
static void airborne_and_corrupt_coordinates_cannot_open(void) {
    QsGame g=ruins();set_plate(&g);
    g.hero.grounded=0;press_down(&g);
    CHECK(!g.ruins_gate_open);
    qs_step(&g,idle());set_plate(&g);
    g.hero.x=NAN;press_down(&g);
    CHECK(!g.ruins_gate_open && gate_tiles_closed(&g));
    qs_begin_stage(&g,1);set_plate(&g);
    g.hero.y=160;press_down(&g);
    CHECK(!g.ruins_gate_open && gate_tiles_closed(&g));
}
static void opening_changes_real_horizontal_collision(void) {
    QsGame g=ruins();QsInput in=idle();
    const float wall_left=QS_RUINS_GATE_COL*QS_TILE;
    g.hero.x=wall_left-12;g.hero.y=188;g.hero.grounded=1;
    in.right=1;qs_step(&g,in);
    CHECK(fabsf(g.hero.x-(wall_left-12))<0.001f);
    set_plate(&g);press_down(&g);
    CHECK(gate_tiles_open(&g));
    g.hero.x=wall_left-12;g.hero.y=188;g.hero.grounded=1;
    qs_step(&g,in);
    CHECK(g.hero.x>wall_left-12);
}
static void back_aborts_before_switch_interaction(void) {
    QsGame g=ruins();QsInput in=idle();set_plate(&g);
    in.down=1;in.back=1;qs_step(&g,in);
    CHECK(g.mode==QS_MAP && !g.ruins_gate_open);
    CHECK(gate_tiles_closed(&g));
}
static void scene_reentry_restores_wall_without_losing_progress(void) {
    QsGame g=ruins();set_plate(&g);press_down(&g);
    CHECK(gate_tiles_open(&g));
    g.cleared[0]=1;g.red_unlocked=1;
    qs_begin_stage(&g,2);
    CHECK(!g.ruins_gate_open && g.cleared[0] && g.red_unlocked);
    qs_begin_stage(&g,1);
    CHECK(!g.ruins_gate_open && gate_tiles_closed(&g));
    CHECK(g.cleared[0] && g.red_unlocked);
}
static void other_stages_are_isolated(void) {
    QsGame g;qs_init(&g);
    g.physics_profile=QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);set_plate(&g);press_down(&g);
    CHECK(g.stage==0 && !g.ruins_gate_open);
    qs_begin_stage(&g,2);set_plate(&g);press_down(&g);
    CHECK(g.stage==2 && !g.ruins_gate_open);
}
int main(void) {
    grounded_plate_opens_physical_wall();
    switch_requires_new_down_edge_near_safe_floor();
    airborne_and_corrupt_coordinates_cannot_open();
    opening_changes_real_horizontal_collision();
    back_aborts_before_switch_interaction();
    scene_reentry_restores_wall_without_losing_progress();
    other_stages_are_isolated();
    puts("RUINS_GATE_TEST_PASS: seven interaction, collision, reset and isolation cases");
    return EXIT_SUCCESS;
}
