#ifndef AUDIOCOMMANDER_MINIMAL_FLUIDSYNTH_H
#define AUDIOCOMMANDER_MINIMAL_FLUIDSYNTH_H

#ifdef HAS_FLUIDSYNTH

typedef struct _fluid_settings_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;
typedef struct _fluid_player_t fluid_player_t;

fluid_settings_t *new_fluid_settings(void);
void delete_fluid_settings(fluid_settings_t *settings);

fluid_synth_t *new_fluid_synth(fluid_settings_t *settings);
void delete_fluid_synth(fluid_synth_t *synth);
int fluid_synth_sfload(fluid_synth_t *synth, const char *filename, int reset_presets);
void fluid_synth_set_gain(fluid_synth_t *synth, float gain);
int fluid_synth_write_s16(fluid_synth_t *synth, int len,
                          void *lout, int loff, int lincr,
                          void *rout, int roff, int rincr);
int fluid_synth_sfunload(fluid_synth_t *synth, int id, int reset_presets);

fluid_player_t *new_fluid_player(fluid_synth_t *synth);
void delete_fluid_player(fluid_player_t *player);
int fluid_player_add(fluid_player_t *player, const char *midifile);
int fluid_player_play(fluid_player_t *player);
int fluid_player_stop(fluid_player_t *player);
int fluid_player_join(fluid_player_t *player);
int fluid_player_get_status(fluid_player_t *player);
int fluid_player_get_current_tick(fluid_player_t *player);
int fluid_player_get_total_ticks(fluid_player_t *player);
int fluid_player_seek(fluid_player_t *player, int ticks);
int fluid_player_set_loop(fluid_player_t *player, int loop);

enum {
    FLUID_PLAYER_READY = 0,
    FLUID_PLAYER_PLAYING = 1,
    FLUID_PLAYER_DONE = 3
};

#define FLUID_OK 0
#define FLUID_FAILED -1

#endif
#endif
