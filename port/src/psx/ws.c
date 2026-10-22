/*
 * Widescreen support.
 *
 * In widescreen mode the GTE squeezes the 3D projection horizontally by 3/4
 * and the picture is shown stretched to 16:9. 2D elements (HUD, menus, text)
 * are not projected by the GTE and would come out stretched, so the GPU
 * squeezes them as well - around the centre of the drawing area.
 *
 * To tell the two apart, every screen position the GTE produces is recorded
 * in a cache (the idea behind PGXP's vertex cache). A polygon or line whose
 * vertices come from the cache is 3D; anything else is 2D.
 *
 * Movies are shown pillarboxed in 4:3 instead.
 */
#include "psx.h"

#include <stdlib.h>
#include <string.h>

int psx_gte_widescreen;      /* squeeze the 3D projection */
int psx_ws_hud;              /* keep 2D elements in 4:3 proportions */

/* stamp per screen position (-1024..1023 on both axes) */
static uint16_t *cache;
static uint16_t  generation = 1;
static uint32_t  movie_vblank;
static int       movie_seen;

void psx_ws_init(int widescreen, int fit_hud)
{
    psx_gte_widescreen = widescreen;
    psx_ws_hud = widescreen && fit_hud;
    movie_seen = 0;
    free(cache);
    cache = NULL;
    if (psx_ws_hud) {
        cache = (uint16_t *)calloc(2048 * 2048, sizeof(uint16_t));
        if (!cache)
            psx_ws_hud = 0;
    }
}

/* called by the GTE for every projected vertex */
void psx_ws_mark(int x, int y)
{
    if (cache)
        cache[((y + 1024) & 2047) * 2048 + ((x + 1024) & 2047)] = generation;
}

/* was (x, y) produced by the GTE for the frame being drawn or the one before? */
int psx_ws_is_projected(int x, int y)
{
    uint16_t g;
    if (!cache)
        return 0;
    g = cache[((y + 1024) & 2047) * 2048 + ((x + 1024) & 2047)];
    return g != 0 && (uint16_t)(generation - g) <= 2;
}

/* a new ordering table is drawn: positions older than two frames expire */
void psx_ws_new_frame(void)
{
    if (++generation == 0)
        generation = 1;
}

/* the MDEC decodes a movie frame */
void psx_ws_movie_frame(void)
{
    movie_vblank = psx_vblank_count();
    movie_seen = 1;
}

/* a movie is playing: show the picture in 4:3, leave 2D elements alone */
int psx_ws_movie_active(void)
{
    return movie_seen && psx_vblank_count() - movie_vblank < 30;
}
