#ifndef MOD_BROWSER_LAYOUT_H
#define MOD_BROWSER_LAYOUT_H

/* One logical 320x200 layout for drawing, mouse and VR-ray selection. */
#define MOD_BROWSER_ROWS 4
#define MOD_BROWSER_LIST_Y 58
#define MOD_BROWSER_ROW_H 20
#define MOD_BROWSER_KEY_COUNT 43
#define MOD_BROWSER_KEY_COLS 10

typedef struct { int x, y, w, h; } mod_browser_rect_t;
typedef enum {
  MOD_BROWSER_INSTALLED, MOD_BROWSER_CATALOGUE, MOD_BROWSER_SEARCH,
  MOD_BROWSER_CLEAR, MOD_BROWSER_BACK, MOD_BROWSER_PREVIOUS,
  MOD_BROWSER_NEXT, MOD_BROWSER_REFRESH, MOD_BROWSER_ACTIVATE,
  MOD_BROWSER_CONTROL_COUNT
} mod_browser_control_t;

static const mod_browser_rect_t mod_browser_controls[MOD_BROWSER_CONTROL_COUNT] = {
  {8, 14, 148, 20}, {164, 14, 148, 20},
  {8, 38, 248, 18}, {260, 38, 52, 18},
  {8, 154, 64, 22}, {76, 154, 40, 22}, {120, 154, 40, 22},
  {164, 154, 64, 22}, {232, 154, 80, 22}
};

static inline int ModBrowser_Contains(mod_browser_rect_t r, float x, float y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static inline mod_browser_rect_t ModBrowser_RowRect(int row) {
  mod_browser_rect_t r = {8, MOD_BROWSER_LIST_Y + row * MOD_BROWSER_ROW_H, 304, 18};
  return r;
}

static inline mod_browser_rect_t ModBrowser_KeyRect(int key) {
  mod_browser_rect_t r = {10 + (key % MOD_BROWSER_KEY_COLS) * 30,
                         52 + (key / MOD_BROWSER_KEY_COLS) * 24, 28, 22};
  if (key >= 40) {
    r.x = 10 + (key - 40) * 100;
    r.y = 152;
    r.w = 96;
  }
  return r;
}

static inline int ModBrowser_KeyVertical(int key, int direction) {
  mod_browser_rect_t current = ModBrowser_KeyRect(key);
  int row = (key / MOD_BROWSER_KEY_COLS + 5 + direction) % 5;
  int first = row * MOD_BROWSER_KEY_COLS;
  int best = first, distance = 1000, i;
  for (i = first; i < first + MOD_BROWSER_KEY_COLS && i < MOD_BROWSER_KEY_COUNT; i++) {
    mod_browser_rect_t candidate = ModBrowser_KeyRect(i);
    int dx = candidate.x + candidate.w / 2 - current.x - current.w / 2;
    if (dx < 0) dx = -dx;
    if (dx < distance) { distance = dx; best = i; }
  }
  return best;
}
#endif
