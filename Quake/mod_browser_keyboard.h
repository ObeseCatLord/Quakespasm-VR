#ifndef MOD_BROWSER_KEYBOARD_H
#define MOD_BROWSER_KEYBOARD_H

/* Key geometry/navigation reused from QuakeSpasm OpenVR's
 * mod_browser_layout.h, reference 51b452c0. Drawing and input share one
 * logical 320x200 canvas; the native browser/list layout remains unchanged. */
#define MOD_BROWSER_KEY_COUNT 43
#define MOD_BROWSER_KEY_COLS 10

typedef struct { int x, y, w, h; } mod_browser_rect_t;

static inline int ModBrowser_Contains (mod_browser_rect_t r, float x, float y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static inline mod_browser_rect_t ModBrowser_KeyRect (int key)
{
	mod_browser_rect_t r = {10 + (key % MOD_BROWSER_KEY_COLS) * 30,
		52 + (key / MOD_BROWSER_KEY_COLS) * 24, 28, 22};
	if (key >= 40)
	{
		r.x = 10 + (key - 40) * 100;
		r.y = 152;
		r.w = 96;
	}
	return r;
}

static inline int ModBrowser_KeyVertical (int key, int direction)
{
	mod_browser_rect_t current = ModBrowser_KeyRect (key);
	int row = (key / MOD_BROWSER_KEY_COLS + 5 + direction) % 5;
	int first = row * MOD_BROWSER_KEY_COLS;
	int best = first, distance = 1000, i;
	for (i = first; i < first + MOD_BROWSER_KEY_COLS && i < MOD_BROWSER_KEY_COUNT; i++)
	{
		mod_browser_rect_t candidate = ModBrowser_KeyRect (i);
		int dx = candidate.x + candidate.w / 2 - current.x - current.w / 2;
		if (dx < 0)
			dx = -dx;
		if (dx < distance)
		{
			distance = dx;
			best = i;
		}
	}
	return best;
}
#endif
