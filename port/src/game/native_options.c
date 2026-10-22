/*
 * Native-only game options, applied every frame.
 * Compiled as game code (PsyQ headers).
 */
#include "mgstype.h"
#include "linkvar.h"
#include "game/game.h"

/* English text, subtitles and movie captions (GM_CONFIG_ENGLISH). The game
 * keeps the flag in its save data and the options menu can clear it, so it
 * is re-applied every frame. --japanese restores the original behaviour. */
int mgs_force_english = 1;

void mgs_native_frame(void)
{
    if (mgs_force_english)
        GM_Configuration |= GM_CONFIG_ENGLISH;
}
