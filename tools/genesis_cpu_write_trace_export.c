/*
 * Stage278: bounded numeric-only 68000 access tracing for selected Work RAM.
 *
 * Appended to pinned libretro/libretro.c only in a separate HOOK_CPU=1
 * research build. The hook records frame index, instruction PC, CPU address,
 * access width, access kind and numeric value for a fixed set of small RAM windows.
 * It never exports instruction bytes, ROM bytes, RAM dumps, VRAM, or pixels.
 */
#include <stdint.h>
#include "../core/debug/cpuhook.h"
#include "../core/m68k/m68k.h"
#include "../core/vdp_ctrl.h"

#define QS_CPU_TRACE_MAX_EVENTS 65536U
#define QS_CPU_PC_TRACE_MAX_EVENTS 65536U
#define QS_CPU_TRACE_MAX_WINDOW 128U

typedef struct
{
    unsigned int frame;
    unsigned int pc;
    unsigned int address;
    unsigned int width;
    unsigned int value;
    unsigned int kind; /* 1 = selected read, 2 = selected write */
} quackshot_cpu_trace_event;

typedef struct
{
    unsigned int frame;
    unsigned int pc;
    unsigned int a0;
    unsigned int a1;
    unsigned int ccr;
} quackshot_cpu_pc_trace_event;

static quackshot_cpu_trace_event quackshot_cpu_trace_events[QS_CPU_TRACE_MAX_EVENTS];
static quackshot_cpu_pc_trace_event quackshot_cpu_pc_trace_events[QS_CPU_PC_TRACE_MAX_EVENTS];
static unsigned int quackshot_cpu_trace_count_value;
static unsigned int quackshot_cpu_trace_overflow_value;
static unsigned int quackshot_cpu_pc_trace_count_value;
static unsigned int quackshot_cpu_pc_trace_overflow_value;
static unsigned int quackshot_cpu_trace_current_frame;
static unsigned int quackshot_cpu_trace_current_pc;
static unsigned int quackshot_cpu_trace_first_frame;
static unsigned int quackshot_cpu_trace_last_frame;
static int quackshot_cpu_trace_active;

static int quackshot_cpu_trace_overlaps(unsigned int offset,
                                        unsigned int width,
                                        unsigned int start,
                                        unsigned int end_exclusive)
{
    unsigned int end = offset + width;
    return offset < end_exclusive && end > start;
}

static int quackshot_cpu_trace_watched(unsigned int address, int width)
{
    unsigned int offset;
    unsigned int w;

    /* Genesis Work RAM is mirrored in the 0xFFxxxx CPU address window. */
    if ((address & 0xFF0000U) != 0xFF0000U || width <= 0 || width > 4)
        return 0;

    offset = address & 0xFFFFU;
    w = (unsigned int)width;

    return
        quackshot_cpu_trace_overlaps(offset, w, 0x8F28U, 0x8F32U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0x8F52U, 0x8F5AU) ||
        quackshot_cpu_trace_overlaps(offset, w, 0x9025U, 0x9029U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0x904EU, 0x9052U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0x8F96U, 0x8F9AU) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xA2C0U, 0xA2F0U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE03U, 0xEE04U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE08U, 0xEE0BU) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE41U, 0xEE46U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE58U, 0xEE59U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE66U, 0xEE67U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xF374U, 0xF378U);
}


static int quackshot_cpu_trace_read_watched(unsigned int address, int width)
{
    unsigned int offset;
    unsigned int w;

    if ((address & 0xFF0000U) != 0xFF0000U || width <= 0 || width > 4)
        return 0;

    offset = address & 0xFFFFU;
    w = (unsigned int)width;

    return
        quackshot_cpu_trace_overlaps(offset, w, 0x8F94U, 0x8F9AU) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xA2C8U, 0xA2CEU) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xA2E4U, 0xA2E8U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE03U, 0xEE04U) ||
        quackshot_cpu_trace_overlaps(offset, w, 0xEE56U, 0xEE5AU);
}


static int quackshot_cpu_pc_trace_watched(unsigned int pc)
{
    pc &= 0xFFFFFFU;
    return
        quackshot_cpu_trace_overlaps(pc, 1, 0x009A26U, 0x009A40U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x009AC2U, 0x009ADCU) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x009B3CU, 0x009B4AU) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x00000206U, 0x00000208U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x005D20U, 0x005DE0U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x006100U, 0x006160U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x005E90U, 0x005F80U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x0064D0U, 0x006560U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x006A40U, 0x006B80U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x0094C0U, 0x0094E4U);
}

static void quackshot_cpu_trace_hook(hook_type_t type, int width,
                                     unsigned int address, unsigned int value)
{
    quackshot_cpu_trace_event *event;
    unsigned int kind;

    if (type == HOOK_M68K_E)
    {
        unsigned int pc = address & 0xFFFFFFU;
        quackshot_cpu_trace_current_pc = pc;

        if (quackshot_cpu_trace_active &&
            quackshot_cpu_trace_current_frame >= quackshot_cpu_trace_first_frame &&
            quackshot_cpu_trace_current_frame <= quackshot_cpu_trace_last_frame &&
            quackshot_cpu_pc_trace_watched(pc))
        {
            unsigned int a0 = m68k_get_reg(M68K_REG_A0) & 0xFFFFFFU;
            unsigned int a1 = m68k_get_reg(M68K_REG_A1) & 0xFFFFFFU;
            if (a0 == 0xFFA2C0U)
            {
                if (quackshot_cpu_pc_trace_count_value >= QS_CPU_PC_TRACE_MAX_EVENTS)
                {
                    quackshot_cpu_pc_trace_overflow_value++;
                }
                else
                {
                    quackshot_cpu_pc_trace_event *pc_event =
                        &quackshot_cpu_pc_trace_events[quackshot_cpu_pc_trace_count_value++];
                    pc_event->frame = quackshot_cpu_trace_current_frame;
                    pc_event->pc = pc;
                    pc_event->a0 = a0;
                    pc_event->a1 = a1;
                    pc_event->ccr = m68k_get_reg(M68K_REG_SR) & 0x1FU;
                }
            }
        }
        return;
    }

    if (!quackshot_cpu_trace_active ||
        quackshot_cpu_trace_current_frame < quackshot_cpu_trace_first_frame ||
        quackshot_cpu_trace_current_frame > quackshot_cpu_trace_last_frame)
        return;

    if (type == HOOK_M68K_W && quackshot_cpu_trace_watched(address, width))
        kind = 2U;
    else if (type == HOOK_M68K_R && quackshot_cpu_trace_read_watched(address, width))
        kind = 1U;
    else
        return;

    if (quackshot_cpu_trace_count_value >= QS_CPU_TRACE_MAX_EVENTS)
    {
        quackshot_cpu_trace_overflow_value++;
        return;
    }

    if (width == 1)
        value &= 0xFFU;
    else if (width == 2)
        value &= 0xFFFFU;

    event = &quackshot_cpu_trace_events[quackshot_cpu_trace_count_value++];
    event->frame = quackshot_cpu_trace_current_frame;
    event->pc = quackshot_cpu_trace_current_pc;
    event->address = address & 0xFFFFFFU;
    event->width = (unsigned int)width;
    event->value = value;
    event->kind = kind;
}

int quackshot_research_cpu_trace_reset(unsigned int first_frame,
                                       unsigned int last_frame)
{
    if (last_frame < first_frame ||
        last_frame - first_frame + 1U > QS_CPU_TRACE_MAX_WINDOW)
        return 0;

    quackshot_cpu_trace_count_value = 0;
    quackshot_cpu_trace_overflow_value = 0;
    quackshot_cpu_pc_trace_count_value = 0;
    quackshot_cpu_pc_trace_overflow_value = 0;
    quackshot_cpu_trace_current_frame = 0;
    quackshot_cpu_trace_current_pc = 0;
    quackshot_cpu_trace_first_frame = first_frame;
    quackshot_cpu_trace_last_frame = last_frame;
    quackshot_cpu_trace_active = 1;
    set_cpu_hook(quackshot_cpu_trace_hook);
    return 1;
}

void quackshot_research_cpu_trace_set_frame(unsigned int frame)
{
    quackshot_cpu_trace_current_frame = frame;
}

void quackshot_research_cpu_trace_stop(void)
{
    quackshot_cpu_trace_active = 0;
    set_cpu_hook(NULL);
}

unsigned int quackshot_research_cpu_trace_count(void)
{
    return quackshot_cpu_trace_count_value;
}

unsigned int quackshot_research_cpu_trace_overflow(void)
{
    return quackshot_cpu_trace_overflow_value;
}

int quackshot_research_cpu_trace_read(unsigned int index,
                                      unsigned int *frame,
                                      unsigned int *pc,
                                      unsigned int *address,
                                      unsigned int *width,
                                      unsigned int *value,
                                      unsigned int *kind)
{
    const quackshot_cpu_trace_event *event;

    if (index >= quackshot_cpu_trace_count_value ||
        !frame || !pc || !address || !width || !value || !kind)
        return 0;

    event = &quackshot_cpu_trace_events[index];
    *frame = event->frame;
    *pc = event->pc;
    *address = event->address;
    *width = event->width;
    *value = event->value;
    *kind = event->kind;
    return 1;
}

unsigned int quackshot_research_cpu_pc_trace_count(void)
{
    return quackshot_cpu_pc_trace_count_value;
}

unsigned int quackshot_research_cpu_pc_trace_overflow(void)
{
    return quackshot_cpu_pc_trace_overflow_value;
}

unsigned int quackshot_research_cpu_pc_trace_a1(unsigned int index)
{
    if (index >= quackshot_cpu_pc_trace_count_value)
        return 0;
    return quackshot_cpu_pc_trace_events[index].a1;
}

int quackshot_research_cpu_pc_trace_read(unsigned int index,
                                         unsigned int *frame,
                                         unsigned int *pc,
                                         unsigned int *a0,
                                         unsigned int *ccr)
{
    const quackshot_cpu_pc_trace_event *event;

    if (index >= quackshot_cpu_pc_trace_count_value ||
        !frame || !pc || !a0 || !ccr)
        return 0;

    event = &quackshot_cpu_pc_trace_events[index];
    *frame = event->frame;
    *pc = event->pc;
    *a0 = event->a0;
    *ccr = event->ccr;
    return 1;
}

/*
 * Stage223: parsed numeric VDP sprite-attribute-table observations.
 * This API exposes coordinates and decoded attributes only; it never exports
 * raw VRAM bytes or raw SAT words. The table is read after retro_run returns.
 */
#define QS_SAT_MAX_SPRITES 80U

static int quackshot_research_sat_entry(unsigned int requested,
                                        unsigned int *sprite_index,
                                        int *x_screen, int *y_screen,
                                        unsigned int *width,
                                        unsigned int *height,
                                        unsigned int *pattern,
                                        unsigned int *palette,
                                        unsigned int *priority,
                                        unsigned int *hflip,
                                        unsigned int *vflip,
                                        unsigned int *next_link)
{
    uint16 *sat_words = (uint16 *)&sat[0];
    uint16 *vram_words = (uint16 *)&vram[satb];
    unsigned char seen[QS_SAT_MAX_SPRITES] = {0};
    unsigned int sprite = 0;
    unsigned int entry;

    for (entry = 0; entry < QS_SAT_MAX_SPRITES; entry++)
    {
        unsigned int word_index;
        unsigned int size_link;
        unsigned int next;
        unsigned int attr;
        unsigned int y_raw;
        unsigned int x_raw;
        unsigned int size;

        if (sprite >= QS_SAT_MAX_SPRITES || seen[sprite])
            return 0;
        seen[sprite] = 1;
        word_index = sprite << 2;
        size_link = sat_words[word_index + 1U];
        next = size_link & 0x7FU;
        attr = vram_words[word_index + 2U];
        y_raw = sat_words[word_index] & 0x1FFU;
        x_raw = vram_words[word_index + 3U] & 0x1FFU;
        size = (size_link >> 8) & 0x0FU;

        if (entry == requested)
        {
            if (sprite_index) *sprite_index = sprite;
            if (x_screen) *x_screen = (int)x_raw - 0x80;
            if (y_screen) *y_screen = (int)y_raw - 0x80;
            if (width) *width = 8U + (((size >> 2) & 3U) << 3);
            if (height) *height = 8U + ((size & 3U) << 3);
            if (pattern) *pattern = attr & 0x7FFU;
            if (palette) *palette = (attr >> 13) & 3U;
            if (priority) *priority = (attr >> 15) & 1U;
            if (hflip) *hflip = (attr >> 11) & 1U;
            if (vflip) *vflip = (attr >> 12) & 1U;
            if (next_link) *next_link = next;
            return 1;
        }

        if (next == 0U || next >= QS_SAT_MAX_SPRITES || seen[next])
            return 0;
        sprite = next;
    }
    return 0;
}

unsigned int quackshot_research_sat_count(void)
{
    uint16 *sat_words = (uint16 *)&sat[0];
    unsigned char seen[QS_SAT_MAX_SPRITES] = {0};
    unsigned int sprite = 0;
    unsigned int count;

    for (count = 0; count < QS_SAT_MAX_SPRITES; count++)
    {
        unsigned int next;
        if (sprite >= QS_SAT_MAX_SPRITES || seen[sprite])
            break;
        seen[sprite] = 1;
        next = sat_words[(sprite << 2) + 1U] & 0x7FU;
        if (next == 0U || next >= QS_SAT_MAX_SPRITES || seen[next])
        {
            count++;
            break;
        }
        sprite = next;
    }
    return count;
}

int quackshot_research_sat_read(unsigned int index,
                                unsigned int *sprite_index,
                                int *x_screen, int *y_screen,
                                unsigned int *width, unsigned int *height,
                                unsigned int *pattern, unsigned int *palette,
                                unsigned int *priority, unsigned int *hflip,
                                unsigned int *vflip, unsigned int *next_link)
{
    if (index >= QS_SAT_MAX_SPRITES || !sprite_index || !x_screen ||
        !y_screen || !width || !height || !pattern || !palette ||
        !priority || !hflip || !vflip || !next_link)
        return 0;

    return quackshot_research_sat_entry(index, sprite_index, x_screen,
        y_screen, width, height, pattern, palette, priority, hflip, vflip,
        next_link);
}
