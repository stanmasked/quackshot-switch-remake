#ifndef QS_ROUTE_PROBE_H
#define QS_ROUTE_PROBE_H
#include <stdio.h>
/* Stage 88: deterministic CITY TEST simulation run inside the NRO.
 * Never claims that physical player/controller input completed the map.
 * Does not mutate live gameplay; stream is owned by caller. */
int qs_route_probe_write(FILE *stream);
#endif
