/*
 * Research-only numeric Genesis VDP horizontal-scroll exporter.
 * Included at the END of pinned upstream libretro/libretro.c by the
 * ROM-free GitHub workflow. This is NOT part of the Nintendo Switch NRO.
 *
 * The core's render_bg_m5() reads the hardware VRAM HScroll table at:
 *   hscb + ((line & hscroll_mask) << 2)
 * The current two plane words are returned only as NUMERIC registers;
 * never export VRAM content, pattern tables, graphics or game assets.
 */
int quackshot_research_vdp_hscroll(unsigned line, unsigned short *out, unsigned count)
{
    unsigned offset;
    if (!out || count < 6 || line > 1023)
        return 0;

    offset = (unsigned) hscb + ((line & (unsigned) hscroll_mask) << 2);
    if (offset + 3 >= sizeof(vram))
        return 0;

    out[0] = (unsigned short) reg[11]; /* horizontal scroll mode selection */
    out[1] = (unsigned short) reg[13]; /* HScroll VRAM base register */
    out[2] = (unsigned short) hscb;    /* decoded VRAM base address */
    out[3] = (unsigned short) hscroll_mask;
    out[4] = (unsigned short) ((unsigned)vram[offset] |
                                ((unsigned)vram[offset+1] << 8)); /* plane A */
    out[5] = (unsigned short) ((unsigned)vram[offset+2] |
                                ((unsigned)vram[offset+3] << 8)); /* plane B */
    return 1;
}
