/*
 * pad_probe — what does each pad actually produce, through the real dsp.so,
 * on the device, with the host's own API?
 *
 * The loadtest answers "does every pad sound" as a yes/no. When a module is
 * reported silent in the chain but passes that, the next question is how LOUD
 * each pad is in absolute terms — a lane that is technically non-zero and
 * 40 dB down is indistinguishable from silence on a speaker, and a pass/fail
 * cannot tell you which one you have.
 *
 * GPL-3.0.
 */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "plugin_api_v1.h"

static int   g_sr = 44100;
static float host_bpm(void) { return 120.0f; }
static double host_beat(void) { return -1.0; }
static int host_clock(void) { return MOVE_CLOCK_STATUS_STOPPED; }
static void host_log(const char *m) { (void)m; }

int main(int argc, char **argv)
{
    const char *so = argc > 1 ? argv[1] : "./dsp.so";
    void *h = dlopen(so, RTLD_NOW);
    if(!h) { printf("dlopen: %s\n", dlerror()); return 2; }
    plugin_api_v2_t *(*init)(const host_api_v1_t *) =
        (plugin_api_v2_t *(*)(const host_api_v1_t *))dlsym(h, "move_plugin_init_v2");
    if(!init) { printf("no move_plugin_init_v2\n"); return 2; }

    static host_api_v1_t host;
    memset(&host, 0, sizeof host);
    host.sample_rate = g_sr;
    host.get_bpm = host_bpm;
    host.get_beat_position = host_beat;
    host.get_clock_status = host_clock;
    host.log = host_log;

    plugin_api_v2_t *api = init(&host);
    void *inst = api->create_instance(".", NULL);
    if(!inst) { printf("create_instance failed\n"); return 2; }

    /* the left 4x4 pad block, and the drum-rack notes for the same lanes */
    const int pads[16] = { 68,69,70,71, 76,77,78,79, 84,85,86,87, 92,93,94,95 };
    printf("%-4s %-6s %-6s %10s %9s\n", "pad", "note", "rack", "peak", "dBFS");
    for(int i = 0; i < 16; ++i)
    {
        for(int pass = 0; pass < 2; ++pass)
        {
            const int note = pass ? (36 + i) : pads[i];
            /* fresh instance so tails cannot bleed between pads */
            api->destroy_instance(inst);
            inst = api->create_instance(".", NULL);
            uint8_t on[3] = { 0x90, (uint8_t)note, 110 };
            api->on_midi(inst, on, 3, 0);
            int16_t buf[256];
            double pk = 0.0;
            for(int b = 0; b < (int)(g_sr * 2.0 / 128); ++b)
            {
                api->render_block(inst, buf, 128);
                for(int k = 0; k < 256; ++k)
                { const double a = fabs(buf[k] / 32768.0); if(a > pk) pk = a; }
            }
            if(pass == 0)
                printf("%-4d %-6d %-6s %10.5f %+9.2f", i + 1, note, "-", pk,
                       20.0 * log10(pk + 1e-12));
            else
                printf("   |  rack %d -> %10.5f %+9.2f\n", note, pk,
                       20.0 * log10(pk + 1e-12));
        }
    }
    api->destroy_instance(inst);
    return 0;
}
