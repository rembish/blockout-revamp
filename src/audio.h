/* PC-speaker effects of the original, re-synthesised. */
#ifndef AUDIO_H
#define AUDIO_H

void audio_init(void);
void audio_resume(void);     /* browsers start audio only after a user gesture */
void audio_play(int snd);    /* enum bo_sound */
void audio_click(void);      /* menu feedback (not in the original) */
float audio_length(int snd); /* seconds */
void audio_set_volume(float v);

#endif
