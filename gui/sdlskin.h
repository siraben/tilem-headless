#ifndef TILEM_SDL_SKIN_H
#define TILEM_SDL_SKIN_H

#include <SDL.h>
#include <glib.h>
#include <stdint.h>

#define LCD_LOW_WHITE 0xcfe0cc
#define LCD_LOW_BLACK 0x222e31

#define SKIN_KEYS 80

typedef struct {
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;
} RECT;

typedef struct {
	int width;
	int height;
	double sx;
	double sy;
	uint32_t lcd_black;
	uint32_t lcd_white;
	RECT lcd_pos;
	RECT keys_pos[SKIN_KEYS];
	SDL_Surface *surface;
} TilemSdlSkin;

TilemSdlSkin *tilem_sdl_skin_load(const char *filename, GError **err);
void tilem_sdl_skin_free(TilemSdlSkin *skin);

#endif
