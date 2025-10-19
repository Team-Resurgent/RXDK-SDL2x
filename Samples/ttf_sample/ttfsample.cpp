// Minimal SDL2 + SDL_ttf overlay demo for Original Xbox (RXDK) For support of UTF / Symbols set "/utf-8" in c++ > command line > additional options .

#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_test_common.h"

#define WINDOW_WIDTH      640
#define WINDOW_HEIGHT     480
#define NUM_STARS         200
#define NUM_VERTICES      8
#define SINE_TABLE_SIZE   360

/* Font paths */
#define FONT_PATH_LATIN   "D:\\media\\DejaVuSans.ttf"        /* Latin + symbols */
#define FONT_PATH_JP      "D:\\media\\NotoSansJP-Regular.ttf"/* Japanese */
#define FONT_PATH_TC      "D:\\media\\NotoSansTC-Regular.ttf"/* Traditional Chinese */
#define FONT_PATH_KR      "D:\\media\\NotoSansKR-Regular.ttf"/* Korean */
#define PRIMARY_RENDERER_INDEX 0

/* layout for the text grid (tighter) */
#define COL_L_X   12
#define COL_R_X   300
#define TOP_Y     10
#define LINE_GAP  2      /* smaller vertical spacing */
#define WRAP_W    260    /* left column wrap width */
#define R_WRAP_W  (WINDOW_WIDTH - COL_R_X - 8)  /* right column wrap width */

static SDLTest_CommonState* state;
static int done = 0;

/* LUT trig (0..359) */
static float sineTable[SINE_TABLE_SIZE];
static float cosTable[SINE_TABLE_SIZE];

/* ---------- Starfield ---------- */
typedef struct { float x, y, z; Uint8 r, g, b; } Star;
static Star stars[NUM_STARS];

/* ---------- Text helpers ---------- */
typedef struct {
    SDL_Texture* tex;
    int w, h;
} TextTexture;

static void DestroyText(TextTexture* tt) {
    if (tt && tt->tex) { SDL_DestroyTexture(tt->tex); tt->tex = NULL; tt->w = tt->h = 0; }
}

static TextTexture RenderTextTexture(SDL_Renderer* r, TTF_Font* font,
    const char* utf8, SDL_Color fg, SDL_Color bg,
    int mode_solid_shaded_blended, int wrap_w)
{
    SDL_Surface* s = NULL;
    TextTexture out; out.tex = NULL; out.w = out.h = 0;

    if (wrap_w > 0) {
        s = TTF_RenderUTF8_Blended_Wrapped(font, utf8, fg, (Uint32)wrap_w);
    }
    else {
        switch (mode_solid_shaded_blended) {
        case 0: s = TTF_RenderUTF8_Solid(font, utf8, fg);       break;
        case 1: s = TTF_RenderUTF8_Shaded(font, utf8, fg, bg);  break;
        default:s = TTF_RenderUTF8_Blended(font, utf8, fg);     break;
        }
    }
    if (!s) return out;

    /* Convert once to ARGB8888 for correct alpha on the Xbox path */
    SDL_Surface* s32 = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!s32) return out;

    out.w = s32->w; out.h = s32->h;
    out.tex = SDL_CreateTextureFromSurface(r, s32);
    SDL_FreeSurface(s32);
    if (!out.tex) { out.w = out.h = 0; return out; }

    SDL_SetTextureBlendMode(out.tex, SDL_BLENDMODE_BLEND);
    return out;
}

static void DrawText(SDL_Renderer* r, const TextTexture* tt, int x, int y) {
    if (!tt || !tt->tex) return;
    SDL_Rect dst; dst.x = x; dst.y = y; dst.w = tt->w; dst.h = tt->h;
    SDL_RenderCopy(r, tt->tex, NULL, &dst);
}

/* ---------- Fonts ---------- */
static TTF_Font* g_font12 = NULL;           /* 12pt small (Latin) */
static TTF_Font* g_font16 = NULL;           /* 16pt medium (Latin) */
static TTF_Font* g_font16_bold = NULL;      /* bold              */
static TTF_Font* g_font16_italic = NULL;    /* italic            */
static TTF_Font* g_font16_ul = NULL;        /* underline         */
static TTF_Font* g_font16_strike = NULL;    /* strikethrough     */
static TTF_Font* g_font16_outline1 = NULL;  /* outline variant   */
static TTF_Font* g_font16_outline3 = NULL;  /* outline variant   */

/* CJK fonts (16pt) */
static TTF_Font* g_font16_jp = NULL;
static TTF_Font* g_font16_tc = NULL;
static TTF_Font* g_font16_kr = NULL;

/* same face but different hinting (Latin 12pt) */
static TTF_Font* g_font12_hint_none = NULL;
static TTF_Font* g_font12_hint_mono = NULL;
static TTF_Font* g_font12_hint_light = NULL;
static TTF_Font* g_font12_hint_normal = NULL;

/* ---------- Cached text items (drawn each frame) ---------- */
typedef struct { TextTexture tex; int x, y; } TextItem;
#define MAX_TEXT_ITEMS 64
static TextItem g_items[MAX_TEXT_ITEMS];
static int g_itemCount = 0;
static TextTexture g_tFPS;           /* dynamic @ 4 Hz */
static Uint32 g_lastFPSTexMS = 0;
static float  g_fpsEMA = 0.0f;

/* ---------- Math / visuals init ---------- */
static void InitTrigTables(void) {
    int i; for (i = 0; i < SINE_TABLE_SIZE; ++i) {
        float rad = i * (float)M_PI / 180.0f;
        sineTable[i] = sinf(rad);
        cosTable[i] = cosf(rad);
    }
}
static float GetSine(int a) { return sineTable[a % SINE_TABLE_SIZE]; }
static float GetCos(int a) { return cosTable[a % SINE_TABLE_SIZE]; }

static void InitStars(void) {
    int i; for (i = 0; i < NUM_STARS; ++i) {
        stars[i].x = (float)(rand() % WINDOW_WIDTH - WINDOW_WIDTH / 2);
        stars[i].y = (float)(rand() % WINDOW_HEIGHT - WINDOW_HEIGHT / 2);
        stars[i].z = (float)(rand() % 200 + 1);
        stars[i].r = (Uint8)(rand() % 256);
        stars[i].g = (Uint8)(rand() % 256);
        stars[i].b = (Uint8)(rand() % 256);
    }
}
static void UpdateStars(void) {
    int i; for (i = 0; i < NUM_STARS; ++i) {
        stars[i].z -= 2.0f;
        if (stars[i].z <= 0.0f) {
            stars[i].x = (float)(rand() % WINDOW_WIDTH - WINDOW_WIDTH / 2);
            stars[i].y = (float)(rand() % WINDOW_HEIGHT - WINDOW_HEIGHT / 2);
            stars[i].z = 200.0f;
        }
    }
}

/* original colorful stars */
static void DrawStars(SDL_Renderer* r) {
    int i; for (i = 0; i < NUM_STARS; ++i) {
        int sx = (int)((stars[i].x / stars[i].z) * 100.0f + WINDOW_WIDTH / 2);
        int sy = (int)((stars[i].y / stars[i].z) * 100.0f + WINDOW_HEIGHT / 2);
        int size = (int)((1.0f - stars[i].z / 200.0f) * 3.0f);
        SDL_Rect rect;
        if (size < 1) size = 1;
        SDL_SetRenderDrawColor(r, stars[i].r, stars[i].g, stars[i].b, 255);
        rect.x = sx - size / 2; rect.y = sy - size / 2; rect.w = size; rect.h = size;
        SDL_RenderFillRect(r, &rect);
    }
}

/* sine wave (original colorful) */
static void DrawSineWave(SDL_Renderer* r, Uint32 tms) {
    int amp = 100, freq = 6, thick = 3;
    int x, t;
    for (x = 0; x < WINDOW_WIDTH; x++) {
        int y = (int)(WINDOW_HEIGHT / 2 + amp * GetSine((x * freq + tms / 5) % SINE_TABLE_SIZE));
        Uint8 rr = (Uint8)((GetSine((x + tms / 10) % SINE_TABLE_SIZE) + 1.0f) * 127.0f);
        Uint8 gg = (Uint8)((GetSine((x + tms / 15) % SINE_TABLE_SIZE) + 1.0f) * 127.0f);
        Uint8 bb = (Uint8)((GetSine((x + tms / 20) % SINE_TABLE_SIZE) + 1.0f) * 127.0f);
        SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
        for (t = -thick / 2; t <= thick / 2; t++) SDL_RenderDrawPoint(r, x, y + t);
    }
}

/* cube using LUT trig */
static void Draw3DCube(SDL_Renderer* r, Uint32 tms) {
    static const float v[NUM_VERTICES][3] = {
        {-50,-50,-50},{50,-50,-50},{50,50,-50},{-50,50,-50},
        {-50,-50, 50},{50,-50, 50},{50,50, 50},{-50,50, 50}
    };
    static const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    int deg = (int)((tms / 10) % 360);
    float ca = GetCos(deg), sa = GetSine(deg);
    int i;
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (i = 0; i < 12; ++i) {
        int v0 = e[i][0], v1 = e[i][1];
        float x0 = v[v0][0], y0 = v[v0][1], z0 = v[v0][2];
        float x1 = v[v1][0], y1 = v[v1][1], z1 = v[v1][2];
        float nx0 = x0 * ca - z0 * sa, nz0 = x0 * sa + z0 * ca;
        float nx1 = x1 * ca - z1 * sa, nz1 = x1 * sa + z1 * ca;
        int sx0 = (int)((nx0 / (nz0 + 200.0f)) * 300.0f + WINDOW_WIDTH / 2);
        int sy0 = (int)((y0 / (nz0 + 200.0f)) * 300.0f + WINDOW_HEIGHT / 2);
        int sx1 = (int)((nx1 / (nz1 + 200.0f)) * 300.0f + WINDOW_WIDTH / 2);
        int sy1 = (int)((y1 / (nz1 + 200.0f)) * 300.0f + WINDOW_HEIGHT / 2);
        SDL_RenderDrawLine(r, sx0, sy0, sx1, sy1);
    }
}

/* ---------- FPS ---------- */
static float CalcFPS(Uint32 dt_ms) { return dt_ms ? (1000.0f / (float)dt_ms) : 0.0f; }

/* ---------- Text grid management ---------- */
static void AddItem(SDL_Renderer* r, TTF_Font* font, const char* text,
    SDL_Color fg, SDL_Color bg, int mode, int wrap_w,
    int x, int* py /* in/out cursor y */)
{
    if (g_itemCount >= MAX_TEXT_ITEMS) return;
    g_items[g_itemCount].tex = RenderTextTexture(r, font, text, fg, bg, mode, wrap_w);
    g_items[g_itemCount].x = x;
    g_items[g_itemCount].y = *py;
    *py += g_items[g_itemCount].tex.h + LINE_GAP;
    g_itemCount++;
}

/* two-pass outlined text (outline first, then fill on top) */
static void AddOutlined(SDL_Renderer* r, TTF_Font* font,
    const char* text, int outline_px,
    SDL_Color outlineCol, SDL_Color fillCol, SDL_Color bgCol,
    int x, int* py)
{
    TextTexture tOutline, tFill;

    /* Pass 1: outline */
    TTF_SetFontOutline(font, outline_px);
    tOutline = RenderTextTexture(r, font, text, outlineCol, bgCol, 2, 0);

    /* Pass 2: fill (no outline) */
    TTF_SetFontOutline(font, 0);
    tFill = RenderTextTexture(r, font, text, fillCol, bgCol, 2, 0);

    if (g_itemCount < MAX_TEXT_ITEMS) {
        g_items[g_itemCount].tex = tOutline; g_items[g_itemCount].x = x; g_items[g_itemCount].y = *py; g_itemCount++;
    }
    if (g_itemCount < MAX_TEXT_ITEMS) {
        g_items[g_itemCount].tex = tFill;    g_items[g_itemCount].x = x; g_items[g_itemCount].y = *py; g_itemCount++;
    }
    *py += tFill.h + LINE_GAP;
}

/* ---------- Main draw loop ---------- */
static void loop(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) SDLTest_CommonEvent(state, &e, &done);

    static Uint32 prev = 0;
    Uint32 now = SDL_GetTicks();
    Uint32 dt = now - prev; prev = now;

    /* FPS EMA for smooth readout */
    {
        float inst = CalcFPS(dt);
        if (g_fpsEMA <= 0.0f) g_fpsEMA = inst;
        else g_fpsEMA = g_fpsEMA * 0.85f + inst * 0.15f;
    }

    /* draw */
    {
        int i;
        for (i = 0; i < state->num_windows; ++i) {
            SDL_Renderer* r = state->renderers[i];
            if (!r) continue;

            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);

            UpdateStars();
            DrawStars(r);
            DrawSineWave(r, now);
            Draw3DCube(r, now);

            if (i == PRIMARY_RENDERER_INDEX) {
                /* update FPS texture at most every 250ms */
                if (now - g_lastFPSTexMS >= 250 || g_tFPS.tex == NULL) {
                    char fpsText[64];
                    SDL_Color white = { 255,255,255,255 }, black = { 0,0,0,255 };
                    _snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", g_fpsEMA);
                    g_lastFPSTexMS = now;
                    DestroyText(&g_tFPS);
                    g_tFPS = RenderTextTexture(r, g_font12, fpsText, white, black, 2, 0);
                }

                /* draw all cached test items */
                {
                    int k; for (k = 0; k < g_itemCount; ++k)
                        DrawText(r, &g_items[k].tex, g_items[k].x, g_items[k].y);
                }
                DrawText(r, &g_tFPS, COL_L_X, 0);
            }

            SDL_RenderPresent(r);
        }
    }
}

/* ---------- Entry ---------- */
int main(int argc, char* argv[]) {
    int yL = TOP_Y + 18;   /* leave space under FPS */
    int yR = TOP_Y;
    SDL_Color white = { 255,255,255,255 };
    SDL_Color black = { 0,0,0,255 };
    SDL_Color yellow = { 255,220,0,255 };
    SDL_Color gray = { 220,220,220,255 };
    SDL_Color green = { 40,220,120,255 };
    SDL_Color magenta = { 255,0,200,255 };
    SDL_Color halfwhite = { 255,255,255,128 }; /* alpha test */

    srand((unsigned int)time(NULL));

    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

    state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
    if (!state) return 1;
    if (!SDLTest_CommonInit(state)) return 2;

    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        return 3;
    }

    /* Smaller loads:
       g_font12 -> 12pt, g_font16 -> 16pt (Latin) */
    g_font12 = TTF_OpenFont(FONT_PATH_LATIN, 12);
    g_font16 = TTF_OpenFont(FONT_PATH_LATIN, 16);

    /* style variants (@16pt) */
    g_font16_bold = TTF_OpenFont(FONT_PATH_LATIN, 16);
    g_font16_italic = TTF_OpenFont(FONT_PATH_LATIN, 16);
    g_font16_ul = TTF_OpenFont(FONT_PATH_LATIN, 16);
    g_font16_strike = TTF_OpenFont(FONT_PATH_LATIN, 16);
    g_font16_outline1 = TTF_OpenFont(FONT_PATH_LATIN, 16);
    g_font16_outline3 = TTF_OpenFont(FONT_PATH_LATIN, 16);

    /* CJK faces (@16pt) */
    g_font16_jp = TTF_OpenFont(FONT_PATH_JP, 16);
    g_font16_tc = TTF_OpenFont(FONT_PATH_TC, 16);
    g_font16_kr = TTF_OpenFont(FONT_PATH_KR, 16);

    /* hinting variants (@12pt, Latin) */
    g_font12_hint_none = TTF_OpenFont(FONT_PATH_LATIN, 12);
    g_font12_hint_mono = TTF_OpenFont(FONT_PATH_LATIN, 12);
    g_font12_hint_light = TTF_OpenFont(FONT_PATH_LATIN, 12);
    g_font12_hint_normal = TTF_OpenFont(FONT_PATH_LATIN, 12);

    if (!g_font12 || !g_font16 || !g_font16_bold || !g_font16_italic ||
        !g_font16_ul || !g_font16_strike || !g_font16_outline1 || !g_font16_outline3 ||
        !g_font12_hint_none || !g_font12_hint_mono || !g_font12_hint_light || !g_font12_hint_normal) {
        SDL_Log("TTF_OpenFont failed (Latin): %s", TTF_GetError());
        goto quit_fail;
    }

    /* configure styles */
    TTF_SetFontStyle(g_font16_bold, TTF_STYLE_BOLD);
    TTF_SetFontStyle(g_font16_italic, TTF_STYLE_ITALIC);
    TTF_SetFontStyle(g_font16_ul, TTF_STYLE_UNDERLINE);
    TTF_SetFontStyle(g_font16_strike, TTF_STYLE_STRIKETHROUGH);

    /* hinting variants */
    TTF_SetFontHinting(g_font12_hint_none, TTF_HINTING_NONE);
    TTF_SetFontHinting(g_font12_hint_mono, TTF_HINTING_MONO);
    TTF_SetFontHinting(g_font12_hint_light, TTF_HINTING_LIGHT);
    TTF_SetFontHinting(g_font12_hint_normal, TTF_HINTING_NORMAL);

    /* kerning ON (base) and OFF (clone) */
    {
        TTF_Font* g_font12_kern_off = TTF_OpenFont(FONT_PATH_LATIN, 12);
        if (!g_font12_kern_off) { SDL_Log("TTF_OpenFont (kern_off) failed"); goto quit_fail; }
        TTF_SetFontKerning(g_font12, 1);
        TTF_SetFontKerning(g_font12_kern_off, 0);

        /* Build cached text on primary renderer */
        if (state->num_windows > 0 && state->renderers[PRIMARY_RENDERER_INDEX]) {
            SDL_Renderer* r0 = state->renderers[PRIMARY_RENDERER_INDEX];

            /* LEFT COLUMN -------------------------------------------------- */
            AddItem(r0, g_font16, "SDL_ttf: SOLID", yellow, black, 0, 0, COL_L_X, &yL);
            AddItem(r0, g_font12, "Shaded text sample", black, gray, 1, 0, COL_L_X, &yL);
            AddItem(r0, g_font16, "Blended text sample", white, black, 2, 0, COL_L_X, &yL);

            AddItem(r0, g_font12,
                "Wrapped text test — this paragraph should wrap inside "
                "260px. This lets us check line breaks.",
                white, black, 2, WRAP_W, COL_L_X, &yL);

            AddItem(r0, g_font12, "Kerning ON:  AV WA To Ty Ta Te Yo VA", white, black, 2, 0, COL_L_X, &yL);
            AddItem(r0, g_font12_kern_off, "Kerning OFF: AV WA To Ty Ta Te Yo VA", white, black, 2, 0, COL_L_X, &yL);

            AddItem(r0, g_font12_hint_none, "Hinting NONE", white, black, 2, 0, COL_L_X, &yL);
            AddItem(r0, g_font12_hint_mono, "Hinting MONO", white, black, 2, 0, COL_L_X, &yL);
            AddItem(r0, g_font12_hint_light, "Hinting LIGHT", white, black, 2, 0, COL_L_X, &yL);
            AddItem(r0, g_font12_hint_normal, "Hinting NORMAL", white, black, 2, 0, COL_L_X, &yL);

            /* RIGHT COLUMN ------------------------------------------------- */
            {
                SDL_Color outlineMagenta = { 255,0,200,255 };
                SDL_Color outlineGreen = { 40,220,120,255 };
                SDL_Color fillWhite = { 255,255,255,255 };
                SDL_Color bgBlack = { 0,0,0,255 };

                AddOutlined(r0, g_font16_outline1, "Outlined (1px)", 1,
                    outlineMagenta, fillWhite, bgBlack, COL_R_X, &yR);

                AddOutlined(r0, g_font16_outline3, "Outlined (2px)", 2,
                    outlineGreen, fillWhite, bgBlack, COL_R_X, &yR);
            }

            AddItem(r0, g_font16_bold, "Bold", white, black, 2, 0, COL_R_X, &yR);
            AddItem(r0, g_font16_italic, "Italic", white, black, 2, 0, COL_R_X, &yR);
            AddItem(r0, g_font16_ul, "Underline", white, black, 2, 0, COL_R_X, &yR);
            AddItem(r0, g_font16_strike, "Strikethrough", white, black, 2, 0, COL_R_X, &yR);

            AddItem(r0, g_font16, "Alpha 50% (blended)", halfwhite, black, 2, 0, COL_R_X, &yR);

            /* RIGHT-COLUMN LONG LINES: use R_WRAP_W so they don't get cut off */
            AddItem(r0, g_font16,
                "UTF-8: café • naïve • fiancée — en–dash — em—dash",
                white, black, 2, R_WRAP_W, COL_R_X, &yR);

            /* CJK: render each script with its own face (fallback to Latin if open failed) */
            AddItem(r0, (g_font16_tc ? g_font16_tc : g_font16),
                "CJK (TC): 你好，世界", white, black, 2, R_WRAP_W, COL_R_X, &yR);
            AddItem(r0, (g_font16_jp ? g_font16_jp : g_font16),
                "CJK (JP): こんにちは、世界", white, black, 2, R_WRAP_W, COL_R_X, &yR);
            AddItem(r0, (g_font16_kr ? g_font16_kr : g_font16),
                "CJK (KR): 안녕하세요, 세계", white, black, 2, R_WRAP_W, COL_R_X, &yR);

            AddItem(r0, g_font16,
                "Symbols: ✓ ✗ ★ ☆ © ® ™ → ← ↑ ↓",
                white, black, 2, R_WRAP_W, COL_R_X, &yR);
        }

        /* not needed after building textures */
        TTF_CloseFont(g_font12_kern_off);
    }

    /* visuals */
    InitTrigTables();
    InitStars();

    /* main loop */
    done = 0;
    while (!done) loop();

    /* teardown */
    {
        int i;
        for (i = 0; i < g_itemCount; ++i) DestroyText(&g_items[i].tex);
        DestroyText(&g_tFPS);

        if (g_font12) TTF_CloseFont(g_font12);
        if (g_font16) TTF_CloseFont(g_font16);
        if (g_font16_bold) TTF_CloseFont(g_font16_bold);
        if (g_font16_italic) TTF_CloseFont(g_font16_italic);
        if (g_font16_ul) TTF_CloseFont(g_font16_ul);
        if (g_font16_strike) TTF_CloseFont(g_font16_strike);
        if (g_font16_outline1) TTF_CloseFont(g_font16_outline1);
        if (g_font16_outline3) TTF_CloseFont(g_font16_outline3);

        if (g_font16_jp) TTF_CloseFont(g_font16_jp);
        if (g_font16_tc) TTF_CloseFont(g_font16_tc);
        if (g_font16_kr) TTF_CloseFont(g_font16_kr);

        if (g_font12_hint_none)   TTF_CloseFont(g_font12_hint_none);
        if (g_font12_hint_mono)   TTF_CloseFont(g_font12_hint_mono);
        if (g_font12_hint_light)  TTF_CloseFont(g_font12_hint_light);
        if (g_font12_hint_normal) TTF_CloseFont(g_font12_hint_normal);

        TTF_Quit();
        SDLTest_CommonQuit(state);
    }
    return 0;

quit_fail:
    if (g_font12) TTF_CloseFont(g_font12);
    if (g_font16) TTF_CloseFont(g_font16);
    if (g_font16_bold) TTF_CloseFont(g_font16_bold);
    if (g_font16_italic) TTF_CloseFont(g_font16_italic);
    if (g_font16_ul) TTF_CloseFont(g_font16_ul);
    if (g_font16_strike) TTF_CloseFont(g_font16_strike);
    if (g_font16_outline1) TTF_CloseFont(g_font16_outline1);
    if (g_font16_outline3) TTF_CloseFont(g_font16_outline3);

    if (g_font16_jp) TTF_CloseFont(g_font16_jp);
    if (g_font16_tc) TTF_CloseFont(g_font16_tc);
    if (g_font16_kr) TTF_CloseFont(g_font16_kr);

    if (g_font12_hint_none)   TTF_CloseFont(g_font12_hint_none);
    if (g_font12_hint_mono)   TTF_CloseFont(g_font12_hint_mono);
    if (g_font12_hint_light)  TTF_CloseFont(g_font12_hint_light);
    if (g_font12_hint_normal) TTF_CloseFont(g_font12_hint_normal);
    TTF_Quit();
    SDLTest_CommonQuit(state);
    return 4;
}
