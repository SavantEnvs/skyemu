// Functional known-answer oracle for mayhem/test.sh. Built with the project's normal flags (no
// sanitizers). It drives the libretro core API with hand-assembled ROMs whose programs compute
// 10+9+...+1 on the emulated CPU and store the result to RAM, then checks the emulated RAM.
// An emulator that is neutered, or whose CPU/memory behaviour is broken, cannot print the PASS lines.
#include "../../src/libretro.c"

static int cur_core;
static const char* const core_names[] = {"Game Boy", "Game Boy Advance", "Nintendo DS"};
static const char* const core_exts[] = {".gb", ".gba", ".nds"};

static void log_fn(enum retro_log_level level, const char* fmt, ...) { (void)level; (void)fmt; }
static void video_fn(const void* d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static size_t audio_fn(const int16_t* d, size_t n) { (void)d; return n; }
static void poll_fn(void) {}
static int16_t input_fn(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }

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

static uint8_t* read_file(const char* path, size_t* size) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* data = malloc(*size);
  if (fread(data, 1, *size, f) != *size) { free(data); data = NULL; }
  fclose(f);
  return data;
}

static void report(const char* name, int ok, const char* detail) {
  printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

static void run_rom(int core, const char* path, const char* name) {
  char detail[128];
  size_t size = 0;
  uint8_t* data = read_file(path, &size);
  if (!data) { report(name, 0, "cannot read rom"); return; }
  cur_core = core;
  char rom_name[32];
  snprintf(rom_name, sizeof rom_name, "oracle%s", core_exts[core]);
  struct retro_game_info game = {rom_name, data, size, NULL};
  if (!retro_load_game(&game)) { report(name, 0, "retro_load_game failed"); free(data); return; }
  for (int i = 0; i < 6; ++i) retro_run();

  uint32_t value = 0;
  switch (core) {
    case 0: value = cores.gb.mem.data[0xC000]; break;
    case 1: value = cores.gba.mem.wram0[0] | (cores.gba.mem.wram0[1] << 8); break;
    case 2: value = cores.nds.mem.ram[0x1000] | (cores.nds.mem.ram[0x1001] << 8); break;
  }
  snprintf(detail, sizeof detail, "sum=%u expected=55", value);
  report(name, value == 55, detail);
  retro_unload_game();
}

static void check_cheat(const char* name, const char* text, size_t want_size, uint32_t w0, uint32_t w1) {
  char detail[128];
  se_reset_cheats();
  se_convert_cheat_code(text, 0);
  int ok = cheats[0].size == want_size && cheats[0].buffer[0] == w0 && (want_size < 2 || cheats[0].buffer[1] == w1);
  snprintf(detail, sizeof detail, "size=%d w0=%08x w1=%08x", (int)cheats[0].size, cheats[0].buffer[0], cheats[0].buffer[1]);
  report(name, ok, detail);
}

int main(int argc, char** argv) {
  if (argc != 4) { fprintf(stderr, "usage: %s oracle.gb oracle.gba oracle.nds\n", argv[0]); return 2; }
  retro_set_environment(env_fn);
  retro_set_video_refresh(video_fn);
  retro_set_audio_sample_batch(audio_fn);
  retro_set_input_poll(poll_fn);
  retro_set_input_state(input_fn);
  retro_init();

  run_rom(0, argv[1], "gb-cpu-sum-loop");
  run_rom(1, argv[2], "gba-arm-sum-loop");
  run_rom(2, argv[3], "nds-arm9-sum-loop");
  check_cheat("cheat-parse-hex", "0123ABCD FEDCBA98", 2, 0x0123ABCD, 0xFEDCBA98);
  check_cheat("cheat-parse-skips-nonhex", "01-23 ab:cd 12345678zz", 2, 0x0123ABCD, 0x12345678);
  return 0;
}
