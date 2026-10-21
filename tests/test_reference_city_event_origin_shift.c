#include "qs_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage183: original-free event-origin time-translation invariant.
 *
 * Inserting N idle frames before an IDENTICAL synthetic city input
 * must shift all absolute event timestamps by N, but change neither
 * causal intervals nor world/camera contact coordinates.
 *
 * The authored ledge and wall are NOT original QuackShot level tiles.
 * Previously observed original event ticks are diagnostic only, not
 * zero-error assertions against this separate scene.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage183 event origin FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

enum {
    UPPER_EDGE_COL = 25,
    WALL_COL = 32,
    FORWARD_TICKS = 320,
    REVERSE_TICKS = 70,
    OBSERVED_ORIGINAL_FALL = 211,
    OBSERVED_ORIGINAL_LAND = 227,
    OBSERVED_ORIGINAL_WALL = 274,
    DELAY_VARIANTS = 5
};

static const int initial_delays[DELAY_VARIANTS] = {0, 1, 7, 17, 47};

typedef struct {
    int camera_right, fall, land, wall, reverse_camera;
    float fall_x, land_x, wall_x, reverse_x30, reverse_x70;
    float camera_at_wall, reverse_camera_x;
} Events;

static QsGame scene(void) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);

    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = '.';
    for (col = 0; col < QS_COLUMNS; ++col) {
        g.terrain[13][col] = '#';
        g.terrain[14][col] = '#';
        if (col < UPPER_EDGE_COL) {
            g.terrain[11][col] = '#';
            g.terrain[12][col] = '#';
        }
    }
    g.terrain[12][WALL_COL] = '#';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = 61.0f;
    g.hero.y = 156.0f;
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 1;
    g.hero.hearts = 3;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static void same_gameplay_state(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
    CHECK(a->hero.hearts == 3 && b->hero.hearts == 3);
}

static void step_both(QsGame *a, QsGame *b, QsInput input) {
    const float old_camera = a->camera_x;
    qs_step(a, input);
    qs_step(b, input);
    same_gameplay_state(a, b);
    CHECK(a->render_camera_x == old_camera);
}

static Events collect_events(int delay) {
    QsGame a = scene(), b = scene();
    Events e;
    int t;
    float wall_camera;
    memset(&e, 0, sizeof(e));
    e.camera_right = e.fall = e.land = e.wall = e.reverse_camera = -1;

    /* Only the clock origin changes. A grounded idle character must
     * remain entirely still while waiting for the first RIGHT input. */
    for (t = 0; t < delay; ++t) {
        QsInput idle = {0};
        step_both(&a, &b, idle);
        CHECK(a.hero.x == 61.0f);
        CHECK(a.hero.y == 156.0f && a.hero.grounded);
        CHECK(a.camera_x == 0.0f);
    }

    for (t = 0; t < FORWARD_TICKS; ++t) {
        QsInput in = {0};
        const int absolute = delay + t;
        in.right = 1;
        step_both(&a, &b, in);
        if (e.camera_right < 0 && a.camera_x > 0.0f)
            e.camera_right = absolute;
        if (e.fall < 0 && a.hero.y > 156.01f) {
            e.fall = absolute;
            e.fall_x = a.hero.x;
        }
        if (e.land < 0 && e.fall >= 0 && a.hero.grounded &&
            fabsf(a.hero.y - 188.0f) < 0.001f) {
            e.land = absolute;
            e.land_x = a.hero.x;
        }
        if (e.wall < 0 && e.land >= 0 &&
            fabsf(a.hero.vx) < 0.00001f && a.hero.x > 480.0f) {
            e.wall = absolute;
            e.wall_x = a.hero.x;
        }
    }

    CHECK(e.camera_right >= delay);
    CHECK(e.fall > e.camera_right);
    CHECK(e.land > e.fall);
    CHECK(e.wall > e.land && e.wall < delay + FORWARD_TICKS);
    CHECK(e.fall_x < e.land_x && e.land_x < e.wall_x);
    CHECK(a.hero.grounded && a.hero.y == 188.0f);
    CHECK(a.hero.vx == 0.0f);
    wall_camera = a.camera_x;
    e.camera_at_wall = wall_camera;

    for (t = 0; t < REVERSE_TICKS; ++t) {
        QsInput in = {0};
        const int absolute = delay + FORWARD_TICKS + t;
        in.left = 1;
        step_both(&a, &b, in);
        if (e.reverse_camera < 0 && a.camera_x < wall_camera - 0.001f) {
            e.reverse_camera = absolute;
            e.reverse_camera_x = a.camera_x;
        }
        if (t == 29) e.reverse_x30 = a.hero.x;
        if (t == 69) e.reverse_x70 = a.hero.x;
    }

    CHECK(e.reverse_camera > delay + FORWARD_TICKS);
    CHECK(e.reverse_camera < delay + FORWARD_TICKS + 25);
    CHECK(e.reverse_x30 < e.wall_x - 25.0f);
    CHECK(e.reverse_x70 <= e.reverse_x30 - 40.0f);
    CHECK(e.reverse_x70 >= 399.9f && e.reverse_x70 <= 410.0f);
    CHECK(a.camera_x < wall_camera);
    return e;
}

static void translation_invariant(const Events *base, const Events *test,
                                  int delay) {
    CHECK(test->camera_right - base->camera_right == delay);
    CHECK(test->fall - base->fall == delay);
    CHECK(test->land - base->land == delay);
    CHECK(test->wall - base->wall == delay);
    CHECK(test->reverse_camera - base->reverse_camera == delay);

    /* Identical event locations and hysteresis, not just equal
     * ordering, distinguish a clock-origin change from physics drift. */
    CHECK(test->fall_x == base->fall_x);
    CHECK(test->land_x == base->land_x);
    CHECK(test->wall_x == base->wall_x);
    CHECK(test->reverse_x30 == base->reverse_x30);
    CHECK(test->reverse_x70 == base->reverse_x70);
    CHECK(test->camera_at_wall == base->camera_at_wall);
    CHECK(test->reverse_camera_x == base->reverse_camera_x);

    CHECK(test->land - test->fall == base->land - base->fall);
    CHECK(test->wall - test->land == base->wall - base->land);
    CHECK(test->reverse_camera - (delay + FORWARD_TICKS) ==
          base->reverse_camera - FORWARD_TICKS);
}

int main(void) {
    Events origin = collect_events(initial_delays[0]);
    int i;
    for (i = 1; i < DELAY_VARIANTS; ++i) {
        Events shifted = collect_events(initial_delays[i]);
        translation_invariant(&origin, &shifted, initial_delays[i]);
    }
    printf("STAGE183_CITY_EVENT_ORIGIN_PASS variants=%d "
           "delays=0,1,7,17,47 camera=%d fall=%d land=%d wall=%d "
           "reverse_lag=%d original_relative_deltas=[%+d,%+d,%+d] "
           "rom_parity_asserted=no original_assets=none\n",
           DELAY_VARIANTS, origin.camera_right, origin.fall,
           origin.land, origin.wall, origin.reverse_camera - FORWARD_TICKS,
           origin.fall - OBSERVED_ORIGINAL_FALL,
           origin.land - OBSERVED_ORIGINAL_LAND,
           origin.wall - OBSERVED_ORIGINAL_WALL);
    return EXIT_SUCCESS;
}
