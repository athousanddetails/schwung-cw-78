/*
 * cr78_engine.h — CW-78 voice orchestration.
 *
 * The DSP is the set of circuit models under cr78_*_circuit.h, built from the
 * CR-78 service notes. This layer is everything a drum machine needs that a
 * set of voice circuits does not have: pot mapping, per-voice drive and
 * distortion, velocity, per-lane mutes, the send buses and the master stage.
 *
 * Realtime contract: every entry point here runs on the SPI callback. Nothing
 * below allocates, opens a file or takes a lock after cr78_create().
 *
 * GPL-3.0.
 */
#ifndef CR78_ENGINE_H
#define CR78_ENGINE_H

#include <stddef.h>

/*
 * Lane order. This is the pad order, the state blob order and the mute-bit
 * order, and changing it breaks saved patches.
 *
 * FOURTEEN voices, which is what a CR-78 has. Move's left 4x4 pad block holds
 * sixteen, so unlike 8W8 there is room for a MASTER PAD and one spare:
 *
 *     row 3 (92-95)   GU  MB  MST --
 *     row 2 (84-87)   LB  LC  CB  TB
 *     row 1 (76-79)   CY  MA  CL  HB
 *     row 0 (68-71)   BD  SD  RS  HH
 *
 * There is no high conga and no mid conga: the machine has two bongos and ONE
 * conga, and inventing the other two to fill the block would be making up a
 * drum machine rather than modelling this one. There is no open hat either —
 * one hi-hat, which is also why this module has no choke control.
 */
typedef enum {
    CR78_BD = 0, CR78_SD, CR78_RS, CR78_HH,
    CR78_CY,     CR78_MA, CR78_CL, CR78_HB,
    CR78_LB,     CR78_LC, CR78_CB, CR78_TB,
    CR78_GU,     CR78_MB,
    CR78_NUM_VOICES
} cr78_voice_t;

/* The pad index Master sits on. Fourteen drums, so pad 15 is free for it and
 * the on-device editor's focus range runs 0..CR78_MASTER_PAD. */
#define CR78_MASTER_PAD CR78_NUM_VOICES

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cr78_engine cr78_engine_t;

cr78_engine_t *cr78_create(float sample_rate);
void  cr78_destroy(cr78_engine_t *e);

/* velocity 0 is treated as a note-off and ignored (drums are one-shots). */
void  cr78_trigger(cr78_engine_t *e, int voice, int velocity);

/* Mono float render. The plugin wrapper does the int16 stereo interleave. */
void  cr78_render(cr78_engine_t *e, float *out, int frames);

/* Both return 1 on a recognised key, 0 otherwise. */
int   cr78_set_param(cr78_engine_t *e, const char *key, const char *val);
int   cr78_get_param(cr78_engine_t *e, const char *key, char *buf, int len);

/* Bit n = lane n muted. Muted lanes swallow triggers and stop ringing. */
void  cr78_set_mutes(cr78_engine_t *e, unsigned mask);
unsigned cr78_get_mutes(const cr78_engine_t *e);

/* State blob for the host's get_param("state") / set_param("state") cycle. */
int   cr78_serialize(const cr78_engine_t *e, char *buf, int len);
void  cr78_deserialize(cr78_engine_t *e, const char *json);

const char *cr78_voice_id(int voice);

#ifdef __cplusplus
}
#endif
#endif /* CR78_ENGINE_H */
