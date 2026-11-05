// SDL2 windowed preview. UNVERIFIED: written against the SDL2 API without a
// compiler or SDL2 headers available (the cloud build container has no SDL2).
// First build on a machine with SDL2 may need small fixes.
//
// Keys: SPACE pause, RIGHT step one tick (while paused), F hold = fast-forward x8,
//       1..6 force mood 0..5, M release forced mood, W cycle weather, N toggle night,
//       D doorbell, S new random seed, R restart same seed, P write PPM screenshot,
//       ESC quit. The HUD line is in the window title.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "preview_common.h"

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t nextRandom() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return g_rng;
}

int main(int argc, char* argv[]) {
  PreviewOptions o;
  previewParseArgs(argc, argv, o);
  if (o.sweep) o.mood = o.sweep_a;   // sweep is a headless feature; start value only

  g_rng ^= (uint64_t)time(nullptr) * 0x2545F4914F6CDD1Dull;
  if (!g_rng) g_rng = 1;

  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("ss_window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     SS_WIDTH, SS_HEIGHT, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
  SDL_Texture* tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                             SS_WIDTH, SS_HEIGHT)
                         : nullptr;
  if (!tex) {
    fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  SDL_RenderSetLogicalSize(ren, SS_WIDTH, SS_HEIGHT);
  SDL_RenderSetIntegerScale(ren, SDL_TRUE);

  PreviewState st;
  if (!previewInit(st, o.seed, previewInputsFromOptions(o))) {
    fprintf(stderr, "init failed\n");
    return 1;
  }
  st.doorbell_at = o.doorbell_at;

  bool paused = false;
  int step_ticks = 0;          // RIGHT while paused
  int frame_no = 0;
  bool quit = false;
  Uint32 t0 = SDL_GetTicks();
  char hud[256];

  while (!quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        quit = true;
      } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) {
          quit = true;
        } else if (k == SDLK_SPACE) {
          paused = !paused;
        } else if (k == SDLK_RIGHT) {
          if (paused) step_ticks++;
        } else if (k >= SDLK_1 && k <= SDLK_6) {
          previewSetMood(st, (float)(k - SDLK_1));
        } else if (k == SDLK_m) {
          previewSetMood(st, o.mood);
        } else if (k == SDLK_w) {
          previewSetWeatherCode(st, (st.inputs.weather_code + 1) % 7);
        } else if (k == SDLK_n) {
          previewSetNight(st, !st.inputs.night_mode);
        } else if (k == SDLK_d) {
          previewDoorbell(st);
        } else if (k == SDLK_s) {
          previewSetSeed(st, nextRandom());
        } else if (k == SDLK_r) {
          previewRestart(st);
        } else if (k == SDLK_p) {
          char name[128];
          snprintf(name, sizeof name, "shot_seed%llu_tick%u.ppm", (unsigned long long)st.seed,
                   (unsigned)st.tick);
          if (previewWritePPM(st, name))
            printf("wrote %s\n", name);
          else
            fprintf(stderr, "cannot write %s\n", name);
        }
      }
    }

    // Frame pacing: frame N is due at t0 + N * 1000 / fps.
    Uint32 now = SDL_GetTicks();
    Uint32 due = t0 + (Uint32)((long long)frame_no * 1000 / o.fps);
    if (now < due) {
      SDL_Delay(1);
      continue;
    }
    if (now - due > 250) {   // fell far behind (window drag, breakpoint): resync
      t0 = now;
      frame_no = 0;
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    st.speed = keys[SDL_SCANCODE_F] ? 8 : 1;
    int n = paused ? step_ticks : previewTicksForFrame(o.fps, frame_no) * st.speed;
    step_ticks = 0;
    previewStep(st, n);
    frame_no++;

    SDL_UpdateTexture(tex, nullptr, st.fb, SS_WIDTH * (int)sizeof(uint16_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);

    if (frame_no % 5 == 0) {
      previewHudLine(st, hud, sizeof hud);
      char title[320];
      snprintf(title, sizeof title, "%s%s", hud, paused ? " [paused]" : "");
      SDL_SetWindowTitle(win, title);
    }
  }

  previewShutdown(st);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
