#include "qs_parallax.h"

int qs_parallax_is_reference_city(const QsGame *game) {
    return game && game->mode == QS_STAGE &&
           game->stage == 0 &&
           game->physics_profile == QS_PHYSICS_REFERENCE_CITY;
}

QsParallax qs_parallax_offsets(const QsGame *game) {
    QsParallax p = {0, 0};
    /* Only the opt-in city uses a one-tick-latched presentation position.
     * Other scenes retain their original camera-to-pixel mapping. */
    float camera;
    if (!game) return p;
    camera = qs_parallax_is_reference_city(game)
                 ? game->render_camera_x : game->camera_x;
    if (!(camera >= 0.0f) || camera > 65535.0f) return p;
    p.front_px = (int)camera;
    if (qs_parallax_is_reference_city(game)) {
        /* Original VDP: HScroll A=-S, B=-floor(S/2) for this city.
         * S here is our own nonnegative synthetic camera offset. */
        p.distant_px = p.front_px / 2;
    } else {
        /* Preserve legacy synthetic 1/3-speed decorative backdrop. */
        p.distant_px = p.front_px / 3;
    }
    return p;
}
