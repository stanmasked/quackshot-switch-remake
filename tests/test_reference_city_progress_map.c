#include "qs_game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"progress map FAIL line %d: %s\n",__LINE__,#x); exit(1); \
} } while (0)

static QsInput idle(void) { QsInput in = {0}; return in; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g,0);
    g.red_unlocked = 1;
    g.weapon = QS_RED;
    g.cleared[2] = 1;
    return g;
}
static void transient(QsGame *g) {
    QsShot *s = &g->shots[0];
    s->active = s->attached = s->red = 1;
    s->frames = 230; s->x = 600; s->y = 180;
    g->pending_reference_shot = g->pending_reference_red = 1;
    g->reference_jump_steps = 13;
    g->camera_x = 740;
    g->render_camera_x = 720;
}
static void clean_map(const QsGame *g) {
    int i;
    CHECK(g->mode == QS_MAP);
    CHECK(g->camera_x == 0 && g->render_camera_x == 0);
    CHECK(!g->pending_reference_shot && !g->pending_reference_red);
    CHECK(!g->reference_jump_steps);
    for (i = 0; i < QS_MAX_SHOTS; ++i) {
        CHECK(!g->shots[i].active);
        CHECK(!g->shots[i].attached);
    }
}
static void completion_cleans_city_and_preserves_progress(void) {
    QsGame g = city();
    transient(&g);
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.25f;
    g.hero.y = 188;
    qs_step(&g,idle());
    clean_map(&g);
    CHECK(g.cleared[0] && g.cleared[2]);
    CHECK(g.red_unlocked && g.weapon == QS_RED);
    CHECK(g.selected_stage == 0);
}
static void manual_exit_cleans_camera_and_queued_ammo(void) {
    QsGame g = city();
    QsInput back = idle(); back.back=1;
    transient(&g);
    qs_step(&g,back);
    clean_map(&g);
    CHECK(!g.cleared[0] && g.cleared[2]);
    CHECK(g.red_unlocked && g.weapon == QS_RED);
}
static void reenter_completed_city_resets_only_transient_state(void) {
    QsGame g = city();
    QsInput in = idle();
    transient(&g);
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.25f;
    qs_step(&g,idle());
    CHECK(g.mode == QS_MAP && g.cleared[0]);
    in.select=1;in.dash=1;
    qs_step(&g,in);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.hero.x == 40 && g.hero.y == 188);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    CHECK(g.red_unlocked && g.weapon == QS_RED);
    CHECK(!g.pickups[0].active);
    CHECK(g.cleared[0] && g.cleared[2]);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
}
static void map_hold_select_does_not_automatically_reenter(void) {
    QsGame g = city();
    QsInput in = idle();
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 0.5f;
    in.select = 1;
    qs_step(&g,in);
    CHECK(g.mode == QS_MAP && g.cleared[0]);
    qs_step(&g,in);
    CHECK(g.mode == QS_MAP);
    in.select = 0;qs_step(&g,in);
    in.select = 1;in.dash=1;qs_step(&g,in);
    CHECK(g.mode == QS_STAGE && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
}
static void alternate_stage_progress_survives_return_to_city(void) {
    QsGame g = city();
    QsInput in = idle();
    in.back = 1;qs_step(&g,in);
    CHECK(g.mode == QS_MAP);
    in = idle();in.right=1;qs_step(&g,in);
    CHECK(g.selected_stage == 1);
    in = idle();in.select=1;qs_step(&g,in);
    CHECK(g.stage == 1 && g.physics_profile == QS_PHYSICS_PROTOTYPE);
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 1;
    in=idle();qs_step(&g,in);
    CHECK(g.mode == QS_MAP && g.cleared[1] && g.cleared[2]);
    in.left=1;qs_step(&g,in);
    CHECK(g.selected_stage == 0);
    in=idle();in.select=1;in.dash=1;qs_step(&g,in);
    CHECK(g.stage == 0 && g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.cleared[1] && g.cleared[2] && g.red_unlocked);
}
static void ordinary_prototype_retains_its_completion_camera(void) {
    QsGame g;
    qs_init(&g);
    qs_begin_stage(&g,0);
    transient(&g);
    g.hero.x = (QS_COLUMNS - 4) * QS_TILE + 1;
    qs_step(&g,idle());
    CHECK(g.mode == QS_MAP && g.cleared[0]);
    CHECK(g.shots[0].active && g.shots[0].attached);
    CHECK(g.camera_x > 0);
}
static void repeated_completion_replay_is_deterministic(void) {
    QsGame a=city(),b;
    int i;
    a.cleared[1]=1;
    transient(&a);
    a.hero.x = (QS_COLUMNS - 4) * QS_TILE + 1;
    b=a;
    for (i=0;i<90;i++) {
        QsInput in=idle();
        if(i==1 || i==45){in.select=1;in.dash=1;}
        if(i==40 || i==80){in.back=1;}
        if(a.mode==QS_STAGE && i==50) a.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        if(b.mode==QS_STAGE && i==50) b.hero.x=(QS_COLUMNS-4)*QS_TILE+1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode && a.selected_stage==b.selected_stage);
        CHECK(a.cleared[0]==b.cleared[0] && a.cleared[1]==b.cleared[1]);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
    }
    CHECK(a.cleared[0] && a.cleared[1] && a.cleared[2]);
}
int main(void) {
    completion_cleans_city_and_preserves_progress();
    manual_exit_cleans_camera_and_queued_ammo();
    reenter_completed_city_resets_only_transient_state();
    map_hold_select_does_not_automatically_reenter();
    alternate_stage_progress_survives_return_to_city();
    ordinary_prototype_retains_its_completion_camera();
    repeated_completion_replay_is_deterministic();
    puts("PASS: 7 map completion/progress cases, 90-step replay and SDL companion");
    return 0;
}
