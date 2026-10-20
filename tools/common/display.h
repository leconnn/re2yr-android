#pragma once

// Puts a tool's window on a particular monitor.
//
// The monitor is chosen by size: RA_DISPLAY="1600x900" (the default) picks the display
// with that resolution, RA_DISPLAY="1" picks display number 1 (counting from 0). The
// window is sized to fit inside it. Does nothing if no display matches.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace tools {

inline void place_window_on_display(SDL_Window* window)
{
	const char* env = std::getenv("RA_DISPLAY");
	const std::string spec = env ? env : "1600x900";
	int count = 0;
	SDL_DisplayID* displays = SDL_GetDisplays(&count);
	if (!displays)
		return;
	SDL_DisplayID chosen = 0;
	int w = 0, h = 0;
	if (std::sscanf(spec.c_str(), "%dx%d", &w, &h) == 2) {
		for (int i = 0; i < count && !chosen; ++i) {
			SDL_Rect bounds;
			if (SDL_GetDisplayBounds(displays[i], &bounds) && bounds.w == w && bounds.h == h)
				chosen = displays[i];
		}
	} else {
		const int index = std::atoi(spec.c_str());
		if (index >= 0 && index < count)
			chosen = displays[index];
	}
	SDL_free(displays);
	if (!chosen)
		return;
	SDL_Rect usable;
	if (!SDL_GetDisplayUsableBounds(chosen, &usable))
		return;
	// Leave room for the title bar and borders (their size is only known once the window exists;
	// a normal Windows title bar is about 31 pixels).
	int top = 31, left = 8, bottom = 8, right = 8;
	SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);
	if (top <= 0)
		top = 31;
	SDL_SetWindowSize(window, usable.w - left - right, usable.h - top - bottom);
	SDL_SetWindowPosition(window, usable.x + left, usable.y + top);
}

} // namespace tools
