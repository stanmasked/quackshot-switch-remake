#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr,"retry transitions FAIL line %d: %s\n",__LINE__,#x); \
  exit(EXIT_FAILURE); } } while (0)

static QsInput idle(void) { QsInput x = {0}; return x; }
static QsGame city(void) {
    QsGame g;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    g.red_unlocked = 1;
    g.weapon = QS_RED;
    g.cleared[2] = 1;
    return g;
}
static void red_platform(QsGame *g) {
    QsShot *s = &g->shots[0];
    memset(s,0,sizeof(*s));
    s->active = 1; s->attached = 1; s->red = 1;
    s->frames = 230; s->x = 150; s->y = 165;
}
static void yellow_flying(QsGame *g) {
    QsShot *s = &g->shots[1];
    memset(s,0,sizeof(*s));
    s->active = 1; s->attached = 0; s->red = 0;
    s->frames = 60; s->x = 300; s->y = 155; s->vx = 0;
}
static void no_live_shots(const QsGame *g) {
    int i;
    for (i=0; i<QS_MAX_SHOTS; i++) {
        CHECK(!g->shots[i].active);
        CHECK(!g->shots[i].attached);
    }
}
static void pit_retry_clears_old_platforms_and_render_camera(void) {
    QsGame g = city();
    g.hero.x = 21 * QS_TILE; /* authored city pit at columns 20..22 */
    g.hero.y = 261;
    g.hero.vy = 7.5f;
    g.hero.vx = 0;
    g.hero.grounded = 0;
    g.hero.facing = -1;
    g.hero.hearts = 2;
    g.pending_reference_shot = 1;
    g.pending_reference_red = 1;
    g.reference_jump_steps = 12;
    g.camera_x = 192; g.render_camera_x = 180;
    red_platform(&g); yellow_flying(&g);
    qs_step(&g, idle());
    CHECK(g.hero.x == 40 && g.hero.y == 188);
    CHECK(g.hero.vx == 0 && g.hero.vy == 0);
    CHECK(g.hero.facing == 1 && !g.hero.grounded);
    CHECK(g.hero.hearts == 3 && g.hero.hurt_frames == 89);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    CHECK(g.reference_jump_steps == 0);
    CHECK(g.pending_reference_shot == 0 && g.pending_reference_red == 0);
    no_live_shots(&g);
    CHECK(g.red_unlocked && g.weapon == QS_RED && g.cleared[2]);
}
static void natural_pit_fall_retries_once_and_stabilizes(void) {
    QsGame g = city();
    int k, respawns = 0;
    g.hero.x = 21 * QS_TILE;
    g.hero.y = 188;
    red_platform(&g);
    for (k=0; k<110; ++k) {
        float prev_x = g.hero.x;
        qs_step(&g, idle());
        if (prev_x != 40 && g.hero.x == 40) {
            ++respawns;
            CHECK(g.hero.y == 188);
            no_live_shots(&g);
        }
    }
    CHECK(respawns == 1);
    CHECK(g.hero.x == 40 && g.hero.y == 188);
    CHECK(g.hero.grounded);
}
static void lethal_enemy_respawn_retires_previous_attempt(void) {
    QsGame g = city();
    g.hero.x = 50; g.hero.y = 188;
    /* The queued shot must fly AWAY from this overlapping enemy;
     * otherwise the shot correctly stuns it before contact and the
     * intended fatal-respawn branch is not reached. */
    g.hero.facing = -1;
    g.hero.hearts = 1; g.hero.hurt_frames = 0;
    g.enemies[0].active = 1;
    g.enemies[0].x = 50; g.enemies[0].y = 188;
    g.enemies[0].min_x = 50; g.enemies[0].max_x = 50;
    g.enemies[0].stunned_frames = 0;
    g.pending_reference_shot = 1; g.pending_reference_red = 1;
    g.camera_x = 200;
    red_platform(&g);
    qs_step(&g,idle());
    CHECK(g.hero.x == 40 && g.hero.hearts == 3);
    CHECK(g.hero.hurt_frames == 89);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    no_live_shots(&g);
}
static void map_exit_erases_queued_and_attached_shots(void) {
    QsGame g = city();
    QsInput back = idle();
    red_platform(&g); yellow_flying(&g);
    g.pending_reference_shot = 1;
    g.pending_reference_red = 1;
    g.reference_jump_steps = 8;
    back.back = 1;
    qs_step(&g,back);
    CHECK(g.mode == QS_MAP);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    CHECK(!g.reference_jump_steps);
    no_live_shots(&g);
    CHECK(g.red_unlocked && g.cleared[2]);
}
static void map_to_unmeasured_stage_and_back_to_city(void) {
    QsGame g = city();
    QsInput in = idle();
    red_platform(&g);
    in.back = 1; qs_step(&g,in);
    CHECK(g.mode == QS_MAP);
    in = idle(); in.right = 1; qs_step(&g,in);
    CHECK(g.selected_stage == 1);
    in = idle(); in.select = 1; qs_step(&g,in);
    CHECK(g.mode == QS_STAGE && g.stage == 1);
    CHECK(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    no_live_shots(&g);
    CHECK(g.cleared[2] && g.red_unlocked);
    in = idle(); in.back = 1; qs_step(&g,in);
    in = idle(); in.left = 1; qs_step(&g,in);
    CHECK(g.mode == QS_MAP && g.selected_stage == 0);
    in = idle(); in.select = 1; in.dash = 1; qs_step(&g,in);
    CHECK(g.mode == QS_STAGE && g.stage == 0);
    CHECK(g.physics_profile == QS_PHYSICS_REFERENCE_CITY);
    CHECK(g.cleared[2] && g.red_unlocked);
    CHECK(!g.pending_reference_shot && !g.pending_reference_red);
    no_live_shots(&g);
}
static void regular_prototype_keeps_projectiles_during_respawn(void) {
    QsGame g;QsInput none=idle();
    qs_init(&g);qs_begin_stage(&g,0);
    red_platform(&g);
    g.hero.x = 21 * QS_TILE;
    g.hero.y = 261;
    g.hero.vy = 7.5f;
    qs_step(&g,none);
    CHECK(g.hero.x == 40 && g.hero.hearts == 3);
    /* Intentional historical behavior of ordinary prototype. */
    CHECK(g.shots[0].active && g.shots[0].attached);
}
static void repeated_retry_state_is_deterministic(void) {
    QsGame a = city(), b;
    int k;
    a.hero.x = 21 * QS_TILE;
    a.hero.y = 190;
    red_platform(&a);
    b = a;
    for(k=0;k<220;k++) {
        QsInput in=idle();
        if(k>70 && k<130) in.right = 1;
        qs_step(&a,in);qs_step(&b,in);
        CHECK(a.mode==b.mode);
        CHECK(a.hero.x==b.hero.x && a.hero.y==b.hero.y);
        CHECK(a.hero.hearts==b.hero.hearts);
        CHECK(a.hero.hurt_frames==b.hero.hurt_frames);
        CHECK(a.pending_reference_shot==b.pending_reference_shot);
        CHECK(a.pending_reference_red==b.pending_reference_red);
        CHECK(a.reference_jump_steps==b.reference_jump_steps);
        CHECK(a.camera_x==b.camera_x && a.render_camera_x==b.render_camera_x);
        for(int i=0;i<QS_MAX_SHOTS;i++) {
            CHECK(a.shots[i].active==b.shots[i].active);
            CHECK(a.shots[i].attached==b.shots[i].attached);
        }
    }
}
int main(void) {
    pit_retry_clears_old_platforms_and_render_camera();
    natural_pit_fall_retries_once_and_stabilizes();
    lethal_enemy_respawn_retires_previous_attempt();
    map_exit_erases_queued_and_attached_shots();
    map_to_unmeasured_stage_and_back_to_city();
    regular_prototype_keeps_projectiles_during_respawn();
    repeated_retry_state_is_deterministic();
    puts("PASS: 7 city retry/stage transitions including 220-step deterministic replay");
    return EXIT_SUCCESS;
}
