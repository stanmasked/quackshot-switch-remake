#include "qs_game.h"
#include "qs_parallax.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "camera phase FAIL line %d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)

static QsGame stage(QsPhysicsProfile profile) {
    QsGame g;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    return g;
}

static void city_latch_follows_previous_simulation_tick(void) {
    QsGame g = stage(QS_PHYSICS_REFERENCE_CITY);
    QsInput right = {0}, left = {0};
    unsigned i, positive = 0, negative = 0;
    float previous;
    right.right = 1;
    left.left = 1;
    for (i = 0; i < 140; ++i) {
        QsParallax p;
        previous = g.camera_x;
        qs_step(&g, right);
        CHECK(g.render_camera_x == previous);
        p = qs_parallax_offsets(&g);
        CHECK(p.front_px == (int)g.render_camera_x);
        CHECK(p.distant_px == p.front_px / 2);
        if ((int)g.camera_x > (int)g.render_camera_x) ++positive;
    }
    CHECK(positive > 10 && g.camera_x > 20);
    for (i = 0; i < 85; ++i) {
        QsParallax p;
        previous = g.camera_x;
        qs_step(&g, left);
        CHECK(g.render_camera_x == previous);
        p = qs_parallax_offsets(&g);
        CHECK(p.front_px == (int)g.render_camera_x);
        CHECK(p.distant_px == p.front_px / 2);
        if ((int)g.camera_x < (int)g.render_camera_x) ++negative;
    }
    CHECK(negative > 10);
}

static void prototype_has_no_visual_delay(void) {
    QsGame g = stage(QS_PHYSICS_PROTOTYPE);
    QsInput right = {0};
    unsigned i;
    right.right = 1;
    for (i = 0; i < 145; ++i) {
        QsParallax p;
        qs_step(&g, right);
        CHECK(g.render_camera_x == g.camera_x);
        p = qs_parallax_offsets(&g);
        CHECK(p.front_px == (int)g.camera_x);
        CHECK(p.distant_px == p.front_px / 3);
    }
}

static void stage_restart_resets_visual_state(void) {
    QsGame g = stage(QS_PHYSICS_REFERENCE_CITY);
    QsInput right = {0};
    unsigned i;
    right.right = 1;
    for (i = 0; i < 130; ++i) qs_step(&g, right);
    CHECK(g.render_camera_x > 0);
    qs_begin_stage(&g, 0);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    CHECK(qs_parallax_offsets(&g).front_px == 0);
    qs_begin_stage(&g, 1);
    CHECK(g.physics_profile == QS_PHYSICS_PROTOTYPE);
    CHECK(g.camera_x == 0 && g.render_camera_x == 0);
    g.camera_x = 90;
    CHECK(qs_parallax_offsets(&g).front_px == 90);
    CHECK(qs_parallax_offsets(&g).distant_px == 30);
}

static void not_a_free_running_clock(void) {
    QsGame g = stage(QS_PHYSICS_REFERENCE_CITY);
    unsigned i;
    QsInput right = {0}, idle = {0};
    right.right = 1;
    for (i = 0; i < 95; ++i) qs_step(&g, right);
    for (i = 0; i < 40; ++i) qs_step(&g, idle);
    CHECK(g.camera_x == g.render_camera_x);
}

int main(void) {
    city_latch_follows_previous_simulation_tick();
    prototype_has_no_visual_delay();
    stage_restart_resets_visual_state();
    not_a_free_running_clock();
    puts("PASS: latched city render camera, bidirectional phase, mode isolation and stage reset");
    return 0;
}
