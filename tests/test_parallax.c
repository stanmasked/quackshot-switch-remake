#include "qs_game.h"
#include "qs_parallax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENSURE(x) do { if (!(x)) { \
    fprintf(stderr,"parallax FAIL line %d: %s\\n", __LINE__, #x); \
    exit(1); } } while (0)

static QsGame test_game(QsPhysicsProfile profile) {
    QsGame g;
    qs_init(&g);
    g.physics_profile = profile;
    qs_begin_stage(&g, 0);
    return g;
}

static void actual_measured_2_to_1_samples(void) {
    QsGame g = test_game(QS_PHYSICS_REFERENCE_CITY);
    const int buffer[] = {0, 1, 2, 3, 90, 155, 285, 358, 359};
    size_t i;
    for (i = 0; i < sizeof buffer / sizeof buffer[0]; ++i) {
        QsParallax p;
        g.camera_x = (float)buffer[i];
        p = qs_parallax_offsets(&g);
        ENSURE(p.front_px == buffer[i]);
        ENSURE(p.distant_px == buffer[i] / 2);
        ENSURE(p.front_px == 2 * p.distant_px ||
               p.front_px == 2 * p.distant_px + 1);
    }
    g.camera_x = 155.9f;
    ENSURE(qs_parallax_offsets(&g).distant_px == 77);
    g.camera_x = 359.99f;
    ENSURE(qs_parallax_offsets(&g).distant_px == 179);
}

static void unchanged_default_one_third_layer(void) {
    QsGame g = test_game(QS_PHYSICS_PROTOTYPE);
    g.camera_x = 155.0f;
    ENSURE(!qs_parallax_is_reference_city(&g));
    ENSURE(qs_parallax_offsets(&g).front_px == 155);
    ENSURE(qs_parallax_offsets(&g).distant_px == 51);
    g.camera_x = 0.0f;
    ENSURE(qs_parallax_offsets(&g).distant_px == 0);
}

static void never_leak_to_unmeasured_stages_or_map(void) {
    QsGame g = test_game(QS_PHYSICS_REFERENCE_CITY);
    g.camera_x = 90.0f;
    qs_begin_stage(&g, 1);
    ENSURE(!qs_parallax_is_reference_city(&g));
    ENSURE(qs_parallax_offsets(&g).distant_px == 30);
    qs_init(&g);
    g.camera_x = 155;
    ENSURE(!qs_parallax_is_reference_city(&g));
    ENSURE(qs_parallax_offsets(&g).distant_px == 51);
    ENSURE(qs_parallax_offsets(NULL).front_px == 0);
}

static void reject_corrupted_camera_inputs(void) {
    QsGame g = test_game(QS_PHYSICS_REFERENCE_CITY);
    g.camera_x = -1.0f;
    ENSURE(qs_parallax_offsets(&g).front_px == 0);
    g.camera_x = 90000.0f;
    ENSURE(qs_parallax_offsets(&g).front_px == 0);
}

static void camera_reverse_keeps_hardware_ratio(void) {
    QsGame g = test_game(QS_PHYSICS_REFERENCE_CITY);
    QsInput right = {0}, left = {0};
    int i, shifted = 0;
    right.right = 1;
    left.left = 1;
    for (i = 0; i < 145; ++i) {
        QsParallax p;
        qs_step(&g, right);
        p = qs_parallax_offsets(&g);
        ENSURE(p.front_px / 2 == p.distant_px);
        if (p.front_px > 0) shifted = 1;
    }
    ENSURE(shifted);
    for (i = 0; i < 35; ++i) {
        QsParallax p;
        qs_step(&g, left);
        p = qs_parallax_offsets(&g);
        ENSURE(p.distant_px == p.front_px / 2);
    }
}

int main(void) {
    actual_measured_2_to_1_samples();
    unchanged_default_one_third_layer();
    never_leak_to_unmeasured_stages_or_map();
    reject_corrupted_camera_inputs();
    camera_reverse_keeps_hardware_ratio();
    puts("PASS: 5 numeric parallax tests; city A:B=2:1 and default preserved");
    return 0;
}
