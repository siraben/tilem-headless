#ifndef TILEM_SDL_ICONS_H
#define TILEM_SDL_ICONS_H

#include <SDL.h>

typedef struct {
	SDL_Texture *texture;
	int width;
	int height;
} TilemSdlIcon;

typedef enum {
	TILEM_SDL_MENU_ICON_NONE = 0,
	TILEM_SDL_MENU_ICON_OPEN,
	TILEM_SDL_MENU_ICON_SAVE,
	TILEM_SDL_MENU_ICON_SAVE_AS,
	TILEM_SDL_MENU_ICON_REVERT,
	TILEM_SDL_MENU_ICON_CLEAR,
	TILEM_SDL_MENU_ICON_RECORD,
	TILEM_SDL_MENU_ICON_STOP,
	TILEM_SDL_MENU_ICON_PLAY,
	TILEM_SDL_MENU_ICON_PREFERENCES,
	TILEM_SDL_MENU_ICON_ABOUT,
	TILEM_SDL_MENU_ICON_QUIT,
	TILEM_SDL_MENU_ICON_CLOSE,
	TILEM_SDL_MENU_ICON_COUNT
} TilemSdlMenuIconId;

typedef struct {
	SDL_Surface *app_surface;
	TilemSdlIcon disasm_pc;
	TilemSdlIcon disasm_break;
	TilemSdlIcon disasm_break_pc;
	TilemSdlIcon db_step;
	TilemSdlIcon db_step_over;
	TilemSdlIcon db_finish;
	TilemSdlIcon db_run;
	TilemSdlIcon db_pause;
	TilemSdlIcon menu[TILEM_SDL_MENU_ICON_COUNT];
} TilemSdlIcons;

TilemSdlIcons *tilem_sdl_icons_load(SDL_Renderer *renderer);
void tilem_sdl_icons_free(TilemSdlIcons *icons);

#endif
