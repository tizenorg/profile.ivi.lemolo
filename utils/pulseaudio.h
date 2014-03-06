#ifndef _PULSEAUDIO_H__
#define _PULSEAUDIO_H__

Eina_Bool pa_init(void);

Eina_Bool pa_play_ringtone(void);

void pa_stop_ringtone(void);

void pa_shutdown(void);

#endif
