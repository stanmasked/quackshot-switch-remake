#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Stage180: documented precision limits of a float approximation to
 * 16.16 source-state numbers. This is an analytical, ROM-free test:
 * test coordinates are public numerical milestones, not asset bytes.
 *
 * It is NOT a claim that all original QuackShot position samples lose
 * precision: the observed walk increments can be exactly dyadic.
 */
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "Stage180 precision FAIL line %d: %s\n", \
            __LINE__, #test); \
    exit(EXIT_FAILURE); \
} } while (0)

#define RAW_UNIT INT64_C(65536)
#define RAW_WALK_ACCEL INT64_C(10240)
#define RAW_MAX_SPEED INT64_C(106496)
#define FLOAT_ACCEL (5.0f / 32.0f)
#define FLOAT_MAX_SPEED (13.0f / 8.0f)

typedef struct {
    int world_x;
    int expected_ulp_16ths;
} PrecisionLandmark;

static const PrecisionLandmark landmarks[] = {
    {61, 1},   /* 0.25 of a 16.16 unit */
    {142, 4},  /* one 16.16 unit */
    {396, 8},  /* two 16.16 units */
    {423, 8},
    {498, 8},
    {640, 16}, /* four units */
    {1200, 32} /* eight units */
};

static int64_t clamp_i64(int64_t v, int64_t a, int64_t b) {
    return v < a ? a : (v > b ? b : v);
}

static void audit_float_ulp(void) {
    size_t i;
    for (i = 0; i < sizeof(landmarks)/sizeof(landmarks[0]); ++i) {
        float value = (float)landmarks[i].world_x;
        float next = nextafterf(value, INFINITY);
        /* Expected ULP is measured in quarter-16.16-steps so
         * x=61 (ULP=1/262144) is representable as an integer. */
        float scaled = (next - value) * (4.0f * RAW_UNIT);
        CHECK(scaled == landmarks[i].expected_ulp_16ths);
        CHECK(next > value);
        printf("STAGE180_ULP world_x=%d ulp_quarter_16_16=%d\n",
               landmarks[i].world_x, landmarks[i].expected_ulp_16ths);
    }
    {
        const float at_142 = (float)142.0;
        const float at_396 = (float)396.0;
        const float one_tick = 1.0f / 65536.0f;
        CHECK(at_142 + one_tick > at_142);
        /* At X=396 one 16.16 tick is only half a float ULP;
         * rounding-to-nearest can erase that exact input state. */
        CHECK(at_396 + one_tick == at_396);
        CHECK(nextafterf(at_396, INFINITY) ==
              at_396 + 2.0f * one_tick);
    }
}

static void audit_observed_walk_grid(int start_x) {
    int64_t x = (int64_t)start_x * RAW_UNIT, vx = 0;
    float float_x = (float)start_x, float_vx = 0.0f;
    int frame;
    for (frame = 0; frame < 720; ++frame) {
        /* Independent bidirectional and neutral six-phase walk.
         * Public measured acceleration and maximum speed only;
         * no ROM input trace, map, graphics or game instructions. */
        int phase = frame / 120;
        int direction =
            phase == 0 || phase == 4 ? 1 :
            phase == 2 || phase == 5 ? -1 : 0;
        int64_t requested = (int64_t)direction * RAW_MAX_SPEED;
        float float_requested = direction * FLOAT_MAX_SPEED;

        if (vx < requested)
            vx = clamp_i64(vx + RAW_WALK_ACCEL, -RAW_MAX_SPEED,
                           requested);
        else if (vx > requested)
            vx = clamp_i64(vx - RAW_WALK_ACCEL, requested,
                           RAW_MAX_SPEED);

        if (float_vx < float_requested)
            float_vx = fminf(float_vx + FLOAT_ACCEL, float_requested);
        else if (float_vx > float_requested)
            float_vx = fmaxf(float_vx - FLOAT_ACCEL, float_requested);

        x += vx;
        float_x += float_vx;
        /* Observed walk accel and speed have denominator 32, so
         * this path remains exactly representable even when generic
         * 16.16 sample positions at the same world X do not. */
        CHECK((double)float_x == (double)x / (double)RAW_UNIT);
        CHECK((double)float_vx == (double)vx / (double)RAW_UNIT);
    }
    printf("STAGE180_WALK_GRID_PASS start=%d frames=720\n", start_x);
}

int main(void) {
    audit_float_ulp();
    audit_observed_walk_grid(61);
    audit_observed_walk_grid(396);
    puts("STAGE180_CITY_NUMERIC_PRECISION_PASS measured_positions=7 walk_traces=2 original_assets=none");
    return EXIT_SUCCESS;
}
