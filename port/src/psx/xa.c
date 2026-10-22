/*
 * XA-ADPCM decoding (CD-ROM XA audio sectors, used for movie sound).
 * Decoded PCM goes to the SPU's CD audio input (psx_spu_xa_queue).
 */
#include "psx.h"

void psx_spu_xa_queue(const int16_t *lr, int frames, int rate);

static const int pos_tab[4] = { 0, 60, 115, 98 };
static const int neg_tab[4] = { 0, 0, -52, -55 };

static int32_t hist[2][2]; /* per channel: older, old */

static int16_t clamp16(int32_t v)
{
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* decode 28 samples of one sound unit */
static void decode_unit(const uint8_t *group, int unit, int bits8, int ch, int16_t *out)
{
    uint8_t hdr = group[4 + unit];
    int range = hdr & 0x0F, filter = (hdr >> 4) & 3, i;
    int32_t *h = hist[ch];

    if (range > 12)
        range = 9;
    for (i = 0; i < 28; i++) {
        int32_t t, s;
        if (bits8) {
            t = (int32_t)(int8_t)group[16 + unit + i * 4] << 8;
        } else {
            int nib = (group[16 + (unit >> 1) + i * 4] >> ((unit & 1) * 4)) & 0xF;
            t = (int32_t)(int16_t)(nib << 12);
        }
        s = (t >> range) + ((h[1] * pos_tab[filter] + h[0] * neg_tab[filter] + 32) >> 6);
        s = clamp16(s);
        h[0] = h[1];
        h[1] = s;
        out[i] = (int16_t)s;
    }
}

void psx_xa_reset(void)
{
    hist[0][0] = hist[0][1] = hist[1][0] = hist[1][1] = 0;
}

/* subheader: file, channel, submode, coding info; data: 18 sound groups */
void psx_xa_decode_sector(const uint8_t *subheader, const uint8_t *data)
{
    static int16_t pcm[18 * 8 * 28 * 2];
    uint8_t coding = subheader[3];
    int stereo = (coding & 3) == 1;
    int rate = ((coding >> 2) & 3) ? 18900 : 37800;
    int bits8 = ((coding >> 4) & 3) == 1;
    int units = bits8 ? 4 : 8;
    int frames = 0, g, u, i;
    int16_t tmp[28], tmp2[28];

    for (g = 0; g < 18; g++) {
        const uint8_t *group = data + g * 128;
        if (stereo) {
            for (u = 0; u < units; u += 2) {
                decode_unit(group, u, bits8, 0, tmp);
                decode_unit(group, u + 1, bits8, 1, tmp2);
                for (i = 0; i < 28; i++) {
                    pcm[frames * 2] = tmp[i];
                    pcm[frames * 2 + 1] = tmp2[i];
                    frames++;
                }
            }
        } else {
            for (u = 0; u < units; u++) {
                decode_unit(group, u, bits8, 0, tmp);
                for (i = 0; i < 28; i++) {
                    pcm[frames * 2] = pcm[frames * 2 + 1] = tmp[i];
                    frames++;
                }
            }
        }
    }
    psx_spu_xa_queue(pcm, frames, rate);
}
