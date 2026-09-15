// Originally SquareLine-Studio-generated with ~35 declarations covering
// every widget event uaDASH's rusEFI-era screens used (bench-test IGN/INJ
// outputs, engine-config trigger/cam/display settings, warning steppers).
// Trimmed 2026-09-14: every one of those had already been deleted from
// events.cpp (see its own comments for when/why - bench/engine-config
// screens have no EV equivalent, the warning steppers were replaced by the
// hand-written settings screen), leaving ~33 dangling declarations for
// functions that no longer existed anywhere - confirmed via a project-wide
// grep before removing, not assumed. Only these two ever had a real
// definition.

#ifndef _UI_EVENTS_H
#define _UI_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

void upBrightness(lv_event_t * e);
void downBribrightness(lv_event_t * e);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
