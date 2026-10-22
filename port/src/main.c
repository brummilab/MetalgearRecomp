/*
 * Native entry point: set up the PlayStation runtime, then run the game's
 * own main() (renamed to mgs_game_main by the build).
 */
#include "psx/psx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  mgs_game_main(void);
void psx_card_init(const char *dir);

static void usage(const char *argv0)
{
    printf("usage: %s --iso <disc image> [options]\n"
           "  --iso PATH       disc 1 of Metal Gear Solid Integral (SLPM-86247), .bin or .iso\n"
           "  --save-dir DIR   directory for memcard1.mcd (default: current directory)\n"
           "  --scale N        window scale (default 3)\n"
           "  --fullscreen     start in fullscreen (F11 toggles)\n"
           "\n"
           "Keys: arrows = d-pad, X = cross, C = circle, Z = square, V = triangle,\n"
           "      A/S = L1/R1, Q/W = L2/R2, Enter = start, Backspace = select, Esc = quit\n",
           argv0);
}

int main(int argc, char **argv)
{
    const char *iso = NULL, *save_dir = NULL;
    int scale = 3, fullscreen = 0, i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--iso") && i + 1 < argc)
            iso = argv[++i];
        else if (!strcmp(argv[i], "--save-dir") && i + 1 < argc)
            save_dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen"))
            fullscreen = 1;
        else {
            usage(argv[0]);
            return strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    if (scale < 1)
        scale = 1;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (psx_mem_init() != 0)
        return 1;
    if (plat_init(scale, fullscreen) != 0)
        return 1;
    psx_kernel_init();
    psx_gpu_init();
    psx_spu_init();
    psx_cd_init(iso);
    psx_card_init(save_dir);

    mgs_game_main();
    psx_log("game main returned\n");
    plat_shutdown();
    return 0;
}
