/*
 * libpad replacement: a DualShock-style controller in analog mode on port 1.
 * The receive buffers registered with PadInitDirect() are filled every
 * vblank, like the console's controller interrupt does.
 */
#include "psx.h"

#include <string.h>

static u_char *rx[2];
static int     started;

void PadInitDirect(unsigned char *a, unsigned char *b)
{
    rx[0] = a;
    rx[1] = b;
}

void PadStartCom(void) { started = 1; }
void PadStopCom(void)  { started = 0; }
void StopPAD(void)     { started = 0; }
void ChangeClearPAD(long v) { (void)v; }

int PadGetState(int port)
{
    if (port == 0 && started)
        return 2; /* PadStateFindCTP1: standard controller, no actuators */
    return 0;     /* PadStateDiscon */
}

int  PadInfoAct(int port, int act, int term) { (void)port; (void)act; (void)term; return 0; }
void PadSetAct(int port, unsigned char *data, int len) { (void)port; (void)data; (void)len; }
int  PadSetActAlign(int port, unsigned char *data) { (void)port; (void)data; return 1; }

/* analog stick axes (0..255, 128 = centre), filled by the platform layer */
uint8_t psx_pad_axes[2][4] = { { 128, 128, 128, 128 }, { 128, 128, 128, 128 } };

void psx_pad_vblank(void)
{
    int port;
    if (!started)
        return;
    for (port = 0; port < 2; port++) {
        u_char *b = rx[port];
        uint16_t buttons;
        if (!b)
            continue;
        if (port != 0) {
            b[0] = 0xFF; /* no controller */
            continue;
        }
        buttons = plat_pad_buttons(port);
        b[0] = 0x00;                 /* transfer ok */
        b[1] = 0x73;                 /* analog controller, 3 halfwords */
        b[2] = (u_char)~buttons;     /* active low */
        b[3] = (u_char)(~buttons >> 8);
        b[4] = psx_pad_axes[port][0]; /* right stick x */
        b[5] = psx_pad_axes[port][1]; /* right stick y */
        b[6] = psx_pad_axes[port][2]; /* left stick x */
        b[7] = psx_pad_axes[port][3]; /* left stick y */
    }
}
