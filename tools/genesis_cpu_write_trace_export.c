/*
 * Stage219: bounded numeric-only 68000 write tracing for selected Work RAM.
 *
 * Appended to pinned libretro/libretro.c only in a separate HOOK_CPU=1
 * research build. The hook records frame index, instruction PC, CPU address,
 * access width and numeric write value for a fixed set of small RAM windows.
 * It never exports instruction bytes, ROM bytes, RAM dumps, VRAM, or pixels.
 */
#include <stdint.h>
#include "../core/debug/cpuhook.h"
#include "../core/m68k/m68k.h"

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
} quackshot_cpu_trace_event;

typedef struct
{
    unsigned int frame;
    unsigned int pc;
    unsigned int a0;
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


static int quackshot_cpu_pc_trace_watched(unsigned int pc)
{
    pc &= 0xFFFFFFU;
    return
        quackshot_cpu_trace_overlaps(pc, 1, 0x00000206U, 0x00000208U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x005D20U, 0x005DE0U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x006100U, 0x006160U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x005E90U, 0x005F80U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x006A40U, 0x006B80U) ||
        quackshot_cpu_trace_overlaps(pc, 1, 0x0094C0U, 0x0094E4U);
}

static void quackshot_cpu_trace_hook(hook_type_t type, int width,
                                     unsigned int address, unsigned int value)
{
    quackshot_cpu_trace_event *event;

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
                    pc_event->ccr = m68k_get_reg(M68K_REG_SR) & 0x1FU;
                }
            }
        }
        return;
    }

    if (!quackshot_cpu_trace_active || type != HOOK_M68K_W ||
        quackshot_cpu_trace_current_frame < quackshot_cpu_trace_first_frame ||
        quackshot_cpu_trace_current_frame > quackshot_cpu_trace_last_frame ||
        !quackshot_cpu_trace_watched(address, width))
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
                                      unsigned int *value)
{
    const quackshot_cpu_trace_event *event;

    if (index >= quackshot_cpu_trace_count_value ||
        !frame || !pc || !address || !width || !value)
        return 0;

    event = &quackshot_cpu_trace_events[index];
    *frame = event->frame;
    *pc = event->pc;
    *address = event->address;
    *width = event->width;
    *value = event->value;
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
