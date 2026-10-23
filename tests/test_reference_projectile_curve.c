#include "qs_reference_projectile_curve.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define VERIFY(x) do { if (!(x)) { \
    fprintf(stderr, "projectile curve test FAIL line %d: %s\n", __LINE__, #x); \
    exit(EXIT_FAILURE); \
} } while (0)

static void test_curve(void) {
    int axis, face, age, dx, dy, previous, step, limit;
    for (axis = 0; axis < 2; ++axis) {
        for (face = -1; face <= 1; face += 2) {
            previous = 0;
            limit = axis == QS_REFERENCE_PROJECTILE_UPWARD ? 20
                  : face == 1 ? 47 : 12;
            for (age = 0; age < limit; ++age) {
                VERIFY(qs_reference_projectile_offset(
                    (QsReferenceProjectileAxis)axis, face, age, &dx, &dy));
                VERIFY((axis == QS_REFERENCE_PROJECTILE_UPWARD && dx == 0) ||
                       (axis == QS_REFERENCE_PROJECTILE_HORIZONTAL && dy == 0));
                step = axis == QS_REFERENCE_PROJECTILE_UPWARD ? -dy : face * dx;
                VERIFY(step >= previous);
                if (age)
                    VERIFY(step - previous >= 5 && step - previous <= 8);
                if (axis == QS_REFERENCE_PROJECTILE_HORIZONTAL && age > 24)
                    VERIFY(step - previous == 5);
                previous = step;
            }
        }
    }
    VERIFY(qs_reference_projectile_offset(
        QS_REFERENCE_PROJECTILE_UPWARD, 1, 19, &dx, &dy) &&
        dx == 0 && dy == -129);
    VERIFY(qs_reference_projectile_offset(
        QS_REFERENCE_PROJECTILE_HORIZONTAL, 1, 46, &dx, &dy) &&
        dx == 264 && dy == 0);
}

static void test_invalid_no_clobber(void) {
    int a = 123, b = 456;
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            1, -1, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            1, 20, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_HORIZONTAL,
                                            1, 47, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_HORIZONTAL,
                                            -1, 12, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            0, 1, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            1, INT_MAX, &a, &b));
    VERIFY(!qs_reference_projectile_offset((QsReferenceProjectileAxis)99,
                                            1, 0, &a, &b));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            1, 0, &a, &a));
    VERIFY(!qs_reference_projectile_offset(QS_REFERENCE_PROJECTILE_UPWARD,
                                            1, 0, NULL, &b));
    VERIFY(a == 123 && b == 456);
}
int main(void) {
    test_curve();
    test_invalid_no_clobber();
    puts("PASS: bounded ROM-free projectile presentation curve");
    return EXIT_SUCCESS;
}
