// Fuzz harness: Action Replay / GameShark cheat codes. Byte 0 selects the emulated system, the rest is
// the cheat text handed to retro_cheat_set (text parsing + per-system cheat interpreter), applied for
// one frame on a blank cartridge.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/libretro.h"

static const char* const core_names[] = {"Game Boy", "Game Boy Advance", "Nintendo DS"};
static const char* const core_exts[] = {"fuzz.gb", "fuzz.gba", "fuzz.nds"};
static const size_t rom_sizes[] = {32 * 1024, 4096, 4096};
static int cur_core;

static void log_fn(enum retro_log_level level, const char* fmt, ...) { (void)level; (void)fmt; }
static void video_fn(const void* data, unsigned w, unsigned h, size_t pitch) { (void)data; (void)w; (void)h; (void)pitch; }
static size_t audio_batch_fn(const int16_t* data, size_t frames) { (void)data; return frames; }
static void input_poll_fn(void) {}
static int16_t input_state_fn(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)port; (void)device; (void)index; (void)id;
  return 0;
}

static bool env_fn(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback*)data)->log = log_fn; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = "/nonexistent"; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int*)data = 3; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* var = (struct retro_variable*)data;
      var->value = strcmp(var->key, "system_core_override") == 0 ? core_names[cur_core] : "OFF";
      return true;
    }
    default: return true;
  }
}

int LLVMFuzzerInitialize(int* argc, char*** argv) {
  (void)argc; (void)argv;
  static char sink[1 << 16];  // once full, further printf output just fails
  FILE* null_out = fmemopen(sink, sizeof sink, "w");
  if (null_out) { setvbuf(null_out, NULL, _IONBF, 0); stdout = null_out; }
  retro_set_environment(env_fn);
  retro_set_video_refresh(video_fn);
  retro_set_audio_sample_batch(audio_batch_fn);
  retro_set_input_poll(input_poll_fn);
  retro_set_input_state(input_state_fn);
  retro_init();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) return -1;
  cur_core = data[0] % 3;

  uint8_t* rom = (uint8_t*)calloc(rom_sizes[cur_core], 1);
  struct retro_game_info game = {core_exts[cur_core], rom, rom_sizes[cur_core], NULL};
  if (!retro_load_game(&game)) { free(rom); return -1; }

  char* text = (char*)malloc(size);
  memcpy(text, data + 1, size - 1);
  text[size - 1] = '\0';

  retro_cheat_reset();
  // one cheat slot per line
  unsigned slot = 0;
  for (char* line = text; line && slot < 4; ++slot) {
    char* nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    retro_cheat_set(slot, true, line);
    line = nl ? nl + 1 : NULL;
  }
  retro_run();

  retro_unload_game();
  free(text);
  free(rom);
  return 0;
}
