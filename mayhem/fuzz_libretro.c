// Fuzz harness: feed the input as a cartridge/ROM image to SkyEmu's libretro core API, emulate a few
// frames and unload. Compiled once per emulated system (FUZZ_CORE / FUZZ_EXT / FUZZ_MIN_SIZE / FUZZ_FRAMES).
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/libretro.h"

// A Game Boy cartridge image is at least 32 KiB and the loader copies that much unconditionally, but the
// fuzzing engine's default input cap is 4 KiB, so shorter inputs are taken as a zero-padded image prefix.
#ifndef FUZZ_PAD_SIZE
#define FUZZ_PAD_SIZE 0
#endif

#ifndef FUZZ_FRAMES
#define FUZZ_FRAMES 3
#endif

static uint32_t joy_state;

static void log_fn(enum retro_log_level level, const char* fmt, ...) { (void)level; (void)fmt; }
static void video_fn(const void* data, unsigned w, unsigned h, size_t pitch) { (void)data; (void)w; (void)h; (void)pitch; }
static size_t audio_batch_fn(const int16_t* data, size_t frames) { (void)data; return frames; }
static void input_poll_fn(void) {
  joy_state ^= joy_state << 13;
  joy_state ^= joy_state >> 17;
  joy_state ^= joy_state << 5;
}
static int16_t input_state_fn(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)port; (void)index;
  if (device == RETRO_DEVICE_JOYPAD) return (joy_state >> (id & 15)) & 1;
  if (device == RETRO_DEVICE_POINTER) return (int16_t)(joy_state >> (id & 7));
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
      var->value = strcmp(var->key, "system_core_override") == 0 ? FUZZ_CORE : "OFF";
      return true;
    }
    default: return true;
  }
}

int LLVMFuzzerInitialize(int* argc, char*** argv) {
  (void)argc; (void)argv;
  // The core prints load progress with printf; keep the fuzzer log readable.
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
  if (size < FUZZ_MIN_SIZE) return -1;

  uint32_t h = 2166136261u;
  for (size_t i = 0; i < size && i < 4096; ++i) h = (h ^ data[i]) * 16777619u;
  joy_state = h ? h : 1;

  static uint8_t padded[FUZZ_PAD_SIZE + 1];
  if (size < FUZZ_PAD_SIZE) {
    memset(padded, 0, FUZZ_PAD_SIZE);
    memcpy(padded, data, size);
    data = padded;
    size = FUZZ_PAD_SIZE;
  }

  struct retro_game_info game = {"fuzz" FUZZ_EXT, data, size, NULL};
  if (!retro_load_game(&game)) return -1;
  for (int i = 0; i < FUZZ_FRAMES; ++i) retro_run();
  retro_unload_game();
  return 0;
}
