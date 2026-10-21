#include "qs_game.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stage187 — independent integer 16.16 free-fall oracle and
 * PRE-STEP versus POST-STEP event sampling.
 *
 * A timestamp convention is not a physics parameter: reading the
 * same event before qs_step rather than after qs_step labels it one
 * iteration later. This authored flat floor is NOT a ROM stage.
 */
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "Stage187 drop sampling FAIL line %d: %s\n", \
            __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

#define SCALE INT64_C(65536)
#define G_Q16 INT64_C(12800)
#define FLOOR_Y_Q16 (INT64_C(188) * SCALE)
#define MAX_V_Q16 INT64_C(491520)
enum { TRACE_TICKS = 40, HEIGHT_CASES = 4, X_CASES = 4 };
static const int heights[HEIGHT_CASES] = {16,32,48,64};
static const int expected_post_intervals[HEIGHT_CASES] = {12,17,21,25};
static const int x_positions[X_CASES] = {61,396,640,900};

typedef struct {
    int gt_001, gt_025, gt_1px, landed;
} SampleEvents;

typedef struct {
    int64_t y, vy;
    int grounded;
} IntegerFall;

static QsGame authored_air_start(int x, int height) {
    QsGame g;
    int row, col;
    qs_init(&g);
    g.physics_profile = QS_PHYSICS_REFERENCE_CITY;
    qs_begin_stage(&g, 0);
    CHECK(g.mode == QS_STAGE && g.stage == 0);

    for (row = 0; row < QS_ROWS; ++row)
        for (col = 0; col < QS_COLUMNS; ++col)
            g.terrain[row][col] = row >= 13 ? '#' : '.';
    memset(g.enemies, 0, sizeof(g.enemies));
    memset(g.shots, 0, sizeof(g.shots));
    memset(g.pickups, 0, sizeof(g.pickups));
    g.city_crate_active = 0;
    g.hero.x = (float)x;
    g.hero.y = (float)(188-height);
    g.hero.vx = g.hero.vy = 0.0f;
    g.hero.grounded = 0;
    g.hero.hearts = 3;
    g.reference_jump_steps = 16;
    g.camera_x = g.render_camera_x = 0.0f;
    memset(&g.previous, 0, sizeof(g.previous));
    return g;
}

static SampleEvents missing_events(void) {
    SampleEvents e = {-1,-1,-1,-1};
    return e;
}

static void record_sample(SampleEvents *e, const QsGame *g,
                          int tick, float start_y) {
    const float delta = g->hero.y - start_y;
    if (e->gt_001 < 0 && delta > 0.01f) e->gt_001 = tick;
    if (e->gt_025 < 0 && delta > 0.25f) e->gt_025 = tick;
    if (e->gt_1px < 0 && delta > 1.0f) e->gt_1px = tick;
    if (e->landed < 0 && g->hero.grounded &&
        g->hero.y == 188.0f) e->landed = tick;
}

static void integer_step(IntegerFall *o) {
    if (o->grounded) return;
    o->vy += G_Q16;
    if (o->vy > MAX_V_Q16) o->vy = MAX_V_Q16;
    o->y += o->vy;
    if (o->y > FLOOR_Y_Q16) {
        o->y = FLOOR_Y_Q16;
        o->vy = 0;
        o->grounded = 1;
    }
}

static void assert_repeat(const QsGame *a, const QsGame *b) {
    CHECK(a->mode == QS_STAGE && b->mode == QS_STAGE);
    CHECK(a->hero.x == b->hero.x && a->hero.y == b->hero.y);
    CHECK(a->hero.vx == b->hero.vx && a->hero.vy == b->hero.vy);
    CHECK(a->hero.grounded == b->hero.grounded);
    CHECK(a->hero.hearts == 3 && b->hero.hearts == 3);
    CHECK(a->camera_x == b->camera_x);
    CHECK(a->render_camera_x == b->render_camera_x);
}

static void run_case(int x, int height, int expected_interval) {
    QsGame a = authored_air_start(x,height);
    QsGame b = authored_air_start(x,height);
    SampleEvents pre = missing_events(), post = missing_events();
    IntegerFall oracle = {(int64_t)(188-height)*SCALE,0,0};
    const float start_y = (float)(188-height);
    int tick;

    for (tick = 0; tick < TRACE_TICKS; ++tick) {
        QsInput idle = {0};
        const float old_camera = a.camera_x;

        /* Reading the state at this location labels it with the
         * NEXT loop index, not the index of the step that created it. */
        record_sample(&pre, &a, tick, start_y);
        CHECK(a.hero.x == (float)x);
        qs_step(&a,idle);
        qs_step(&b,idle);
        integer_step(&oracle);
        assert_repeat(&a,&b);
        record_sample(&post, &a, tick, start_y);
        CHECK(a.render_camera_x == old_camera);
        CHECK(a.hero.x == (float)x && a.hero.vx == 0.0f);
        CHECK(a.hero.y >= start_y && a.hero.y <= 188.0f);
        CHECK(a.hero.grounded == oracle.grounded);
        CHECK(fabsf(a.hero.y -
                    (float)((double)oracle.y / (double)SCALE)) < 0.0625f);
        CHECK(fabsf(a.hero.vy -
                    (float)((double)oracle.vy / (double)SCALE)) < 0.0005f);
    }

    CHECK(post.gt_001 == 0);
    CHECK(post.gt_025 > post.gt_001);
    CHECK(post.gt_1px > post.gt_025);
    CHECK(post.landed > post.gt_1px && post.landed < TRACE_TICKS-1);
    CHECK(post.landed - post.gt_001 == expected_interval);
    CHECK(pre.gt_001 == post.gt_001+1);
    CHECK(pre.gt_025 == post.gt_025+1);
    CHECK(pre.gt_1px == post.gt_1px+1);
    CHECK(pre.landed == post.landed+1);

    /* Consistent sampling phase preserves the causal interval,
     * while mixing phases creates an artificial +/-1-frame gap. */
    CHECK(pre.landed-pre.gt_001 == post.landed-post.gt_001);
    CHECK(post.landed-pre.gt_001 == expected_interval-1);
    CHECK(pre.landed-post.gt_001 == expected_interval+1);
    if (height == 32) {
        CHECK(post.landed-post.gt_025 == 16);
        CHECK(post.landed-post.gt_1px == 15);
    }
    CHECK(a.hero.grounded && a.hero.y == 188.0f && a.hero.vy == 0.0f);
    CHECK(oracle.grounded && oracle.y == FLOOR_Y_Q16);

    printf("STAGE187_PHASE_ROW height=%d world_x=%d post_start=%d "
           "post_land=%d pre_start=%d pre_land=%d "
           "post_interval=%d mixed_pre_start_post_land=%d "
           "mixed_post_start_pre_land=%d\n",
           height,x,post.gt_001,post.landed,pre.gt_001,pre.landed,
           post.landed-post.gt_001,post.landed-pre.gt_001,
           pre.landed-post.gt_001);
}

int main(void) {
    int i,j,total=0;
    for(i=0;i<HEIGHT_CASES;++i)
        for(j=0;j<X_CASES;++j) {
            run_case(x_positions[j],heights[i],expected_post_intervals[i]);
            ++total;
        }
    CHECK(total == 16);
    puts("STAGE187_DROP_SAMPLING_PHASE_PASS cases=16 "
         "same_phase_intervals_unchanged=yes mixed_phase_off_by_one=yes "
         "oracle=independent_q16 original_assets=none");
    return EXIT_SUCCESS;
}
