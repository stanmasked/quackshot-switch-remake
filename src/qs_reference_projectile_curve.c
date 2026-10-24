#include "qs_reference_projectile_curve.h"

/*
 * Fitted first-city screen-space displacement; no original ROM material.
 * Cumulative subpixel motion for the measured initial interval:
 * (127*n-n*n)/16 pixels. Negative directions use floor(-distance), not
 * C99's truncation-toward-zero. This is not verified collision physics.
 */
int qs_reference_projectile_offset(QsReferenceProjectileAxis axis,
                                   int facing, int age, int *dx, int *dy) {
    int numerator, positive_floor, positive_ceil;
    int x = 0, y = 0, limit;
    if (!dx || !dy || dx == dy || (facing != 1 && facing != -1))
        return 0;
    if (axis == QS_REFERENCE_PROJECTILE_UPWARD)
        limit = QS_REFERENCE_UPWARD_OBSERVED_FRAMES;
    else if (axis == QS_REFERENCE_PROJECTILE_HORIZONTAL)
        limit = facing == 1 ? QS_REFERENCE_HORIZONTAL_OBSERVED_FRAMES
                            : QS_REFERENCE_LEFT_HORIZONTAL_OBSERVED_FRAMES;
    else
        return 0;
    if (age < 0 || age >= limit)
        return 0;
    if (age <= 24) {
        numerator = 127 * age - age * age;
        positive_floor = numerator / 16;
        positive_ceil = (numerator + 15) / 16;
    } else {
        /* Measured horizontal-right tail: five px per frame after n=24. */
        positive_floor = 154 + 5 * (age - 24);
        positive_ceil = 155 + 5 * (age - 24);
    }
    if (axis == QS_REFERENCE_PROJECTILE_UPWARD)
        y = -positive_ceil;
    else
        x = facing == 1 ? positive_floor : -positive_ceil;
    *dx = x;
    *dy = y;
    return 1;
}
