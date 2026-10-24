#ifndef QS_REFERENCE_PROJECTILE_CURVE_H
#define QS_REFERENCE_PROJECTILE_CURVE_H

typedef enum { QS_REFERENCE_PROJECTILE_HORIZONTAL = 0, QS_REFERENCE_PROJECTILE_UPWARD = 1 } QsReferenceProjectileAxis;
#define QS_REFERENCE_HORIZONTAL_OBSERVED_FRAMES 47
#define QS_REFERENCE_LEFT_HORIZONTAL_OBSERVED_FRAMES 12
#define QS_REFERENCE_UPWARD_OBSERVED_FRAMES 20
int qs_reference_projectile_offset(QsReferenceProjectileAxis axis, int facing, int age, int *dx, int *dy);

#endif
