#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <stdio.h>
#include <mini18n.h>
#include <SDL2/SDL.h>

#define STBI_ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "types.h"
#include "libfont.h"
#include "menu.h"
#include "saves.h"

#include <dbglogger.h>
#define LOG dbglogger_log

#define JAR_COLUMNS 7


int LoadMenuTexture(const char* path, int idx)
{
	int d;

	LOG("Loading '%s'", path);
	if (menu_textures[idx].texture)
		SDL_DestroyTexture(menu_textures[idx].texture);

	menu_textures[idx].size = 0;
	menu_textures[idx].texture = NULL;
	menu_textures[idx].buffer = (uint32_t*) stbi_load(path, &menu_textures[idx].width, &menu_textures[idx].height, &d, STBI_rgb_alpha);

	if (!menu_textures[idx].buffer)
	{
		LOG("Error Loading texture (%s)!", path);
		return 0;
	}

	// GLES2 supports RGBA8888 only
	for (d = 0; d < menu_textures[idx].width * menu_textures[idx].height; d++)
		menu_textures[idx].buffer[d] = ES32(menu_textures[idx].buffer[d]);

	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(menu_textures[idx].buffer, menu_textures[idx].width, menu_textures[idx].height, 32, 4 * menu_textures[idx].width,
												0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);

	menu_textures[idx].texture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_FreeSurface(surface);
	stbi_image_free(menu_textures[idx].buffer);

	menu_textures[idx].size = menu_textures[idx].width * menu_textures[idx].height * 4;
	menu_textures[idx].buffer = NULL;
	return 1;
}

void LoadVmcTexture(int width, int height, uint8_t* icon)
{
	if (menu_textures[icon_png_file_index].texture)
		SDL_DestroyTexture(menu_textures[icon_png_file_index].texture);

	menu_textures[icon_png_file_index].width = width;
	menu_textures[icon_png_file_index].height = height;
	menu_textures[icon_png_file_index].size = width * height * 4;
	menu_textures[icon_png_file_index].buffer = NULL;

	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(icon, width, height, 32, 4 * width,
												0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);

	menu_textures[icon_png_file_index].texture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_FreeSurface(surface);
	free(icon);
}

// draw one background color in virtual 2D coordinates
void DrawBackground2D(u32 rgba)
{
	SDL_Rect rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

	SDL_SetRenderDrawColor(renderer, RGBA_R(rgba), RGBA_G(rgba), RGBA_B(rgba), RGBA_A(rgba));
	SDL_RenderFillRect(renderer, &rect);
}

void _drawListBackground(int off, int icon)
{
	switch (icon)
	{
		case cat_db_png_index:
		case cat_usb_png_index:
		case cat_hdd_png_index:
		case cat_opt_png_index:
		case cat_bup_png_index:
		case cat_warning_png_index:
			DrawTexture(&menu_textures[help_png_index], help_png_x, help_png_y, 0, help_png_w, help_png_h, 0xFFFFFF00 | 0xFF);
			break;

		case cat_sav_png_index:
			DrawTexture(&menu_textures[help_png_index], help_png_x, help_png_y, 0, help_png_w, help_png_h, 0xFFFFFF00 | 0xFF);

			if (menu_textures[icon_png_file_index].size)
			{
				DrawTexture(&menu_textures[help_png_index], SCREEN_WIDTH - 404, help_png_y + 4, 0, menu_textures[icon_png_file_index].width + 8, menu_textures[icon_png_file_index].height + 8, 0xFFFFFF00 | 0xFF);
				DrawTexture(&menu_textures[icon_png_file_index], SCREEN_WIDTH - 400, help_png_y + 8, 0, menu_textures[icon_png_file_index].width, menu_textures[icon_png_file_index].height, 0xFFFFFF00 | 0xFF);
			}
			break;

		case cat_cheats_png_index:
			DrawTexture(&menu_textures[help_png_index], off + MENU_ICON_OFF, help_png_y, 0, (SCREEN_WIDTH - 75) - off - MENU_ICON_OFF, help_png_h, 0xFFFFFF00 | 0xFF);
			break;

		case cat_about_png_index:
			break;

		default:
			break;
	}
}

void DrawHeader_Ani(int icon, const char * hdrTitle, const char * headerSubTitle, u32 rgba, u32 bgrgba, int ani, int div)
{
	u8 icon_a = (u8)(((ani * 2) > 0xFF) ? 0xFF : (ani * 2));
	char headerTitle[44];
	snprintf(headerTitle, sizeof(headerTitle), "%.40s%s", hdrTitle, (strlen(hdrTitle) > 40 ? "..." : ""));

	//------------ Backgrounds

	//Background
	DrawBackgroundTexture(0, (u8)bgrgba);

	_drawListBackground(0, icon);
	//------------- Menu Bar
/*
	int cnt, cntMax = ((ani * div) > (SCREEN_WIDTH - 75)) ? (SCREEN_WIDTH - 75) : (ani * div);
	for (cnt = MENU_ICON_OFF; cnt < cntMax; cnt++)
		DrawTexture(&menu_textures[header_line_png_index], cnt, 40, 0, menu_textures[header_line_png_index].width, menu_textures[header_line_png_index].height / 2, 0xffffffff);

	DrawTexture(&menu_textures[header_dot_png_index], cnt - 4, 40, 0, menu_textures[header_dot_png_index].width / 2, menu_textures[header_dot_png_index].height / 2, 0xffffff00 | icon_a);
*/

	//header mini icon
	DrawTextureCenteredX(&menu_textures[icon], MENU_ICON_OFF - 20, 32, 0, 96, 96, 0xffffff00 | icon_a);

	//header title string
	SetFontColor(rgba | icon_a, 0);
	SetFontSize(APP_FONT_SIZE_TITLE);
	DrawString(MENU_ICON_OFF + 40, 31, headerTitle);

	//header sub title string
	if (headerSubTitle)
	{
		int width = (SCREEN_WIDTH - 75) - (MENU_ICON_OFF + MENU_TITLE_OFF + WidthFromStr(headerTitle)) - 30;
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		char * tName = strdup(headerSubTitle);
		while (WidthFromStr(tName) > width)
		{
			tName[strlen(tName) - 1] = 0;
		}
		SetFontAlign(FONT_ALIGN_RIGHT);
		DrawString(SCREEN_WIDTH - 75, 35, tName);
		free(tName);
		SetFontAlign(FONT_ALIGN_LEFT);
	}
}

void DrawHeader(int icon, int xOff, const char * hdrTitle, const char * headerSubTitle, u32 rgba, u32 bgrgba, int mode)
{
	char headerTitle[44];
	snprintf(headerTitle, sizeof(headerTitle), "%.40s%s", hdrTitle, (strlen(hdrTitle) > 40 ? "..." : ""));

	//Background
	DrawBackgroundTexture(xOff, (u8)bgrgba);

	_drawListBackground(xOff, icon);
	//------------ Menu Bar
/*
	int cnt = 0;
	for (cnt = xOff + MENU_ICON_OFF; cnt < (SCREEN_WIDTH - 75); cnt++)
		DrawTexture(&menu_textures[header_line_png_index], cnt, 55, 0, menu_textures[header_line_png_index].width, menu_textures[header_line_png_index].height / 2, 0xffffffff);

	DrawTexture(&menu_textures[header_dot_png_index], cnt - 4, 55, 0, menu_textures[header_dot_png_index].width / 2, menu_textures[header_dot_png_index].height / 2, 0xffffffff);
*/

	//header mini icon
	//header title string
	SetFontColor(rgba, 0);
	if (mode)
	{
		DrawTextureCenteredX(&menu_textures[icon], xOff + MENU_ICON_OFF - 12, 40, 0, 64, 64, 0xffffffff);
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		DrawString(xOff + MENU_ICON_OFF + 40, 35, headerTitle);
	}
	else
	{
		DrawTextureCenteredX(&menu_textures[icon], xOff + MENU_ICON_OFF - 20, 32, 0, 96, 96, 0xffffffff);
		SetFontSize(APP_FONT_SIZE_TITLE);
		DrawString(xOff + MENU_ICON_OFF + 40, 31, headerTitle);
	}

	//header sub title string
	if (headerSubTitle)
	{
		int width = (SCREEN_WIDTH - 75) - (MENU_ICON_OFF + MENU_TITLE_OFF + WidthFromStr(headerTitle)) - 30;
		SetFontSize(APP_FONT_SIZE_SUBTITLE);
		char * tName = strdup(headerSubTitle);
		while (WidthFromStr(tName) > width)
		{
			tName[strlen(tName) - 1] = 0;
		}
		SetFontAlign(FONT_ALIGN_RIGHT);
		DrawString(SCREEN_WIDTH - 75, 35, tName);
		free(tName);
		SetFontAlign(FONT_ALIGN_LEFT);
	}
}

void DrawBackgroundTexture(int x, u8 alpha)
{
	/* SaveGuard uses a clean dark canvas; the old Apollo background image is removed. */
	(void)x;
	(void)alpha;
	DrawBackground2D(0x08111FFF);
}
static void drawTelegramWatermarks(void)
{
	/* Ten small, 50%-transparent Telegram labels travel bottom-left to top-right. */
	static const char *label = "Telegram  @h_y_88";
	const int span = SCREEN_WIDTH + 420;
	const int speed = (int)(SDL_GetTicks() / 14);
	SetFontAlign(FONT_ALIGN_CENTER);
	SetFontSize(APP_FONT_SIZE_DESCRIPTION);
	SetFontColor(0xFFFFFF80, 0);
	for (int i = 0; i < 10; i++)
	{
		int along = (speed + (i * (span / 10))) % span;
		int x = along - 210;
		int y = SCREEN_HEIGHT + 30 - ((along * (SCREEN_HEIGHT + 100)) / span);
		DrawString(x, y, label);
	}
	SetFontAlign(FONT_ALIGN_LEFT);
}

void DrawTexture(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	SDL_Rect dest = {
		.x = x,
		.y = y,
		.w = w,
		.h = h,
	};

	SDL_SetTextureAlphaMod(tex->texture, RGBA_A(rgba));
	SDL_RenderCopy(renderer, tex->texture, NULL, &dest);
}

void DrawTextureCentered(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	x -= w / 2;
	y -= h / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureCenteredX(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	x -= w / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureCenteredY(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba)
{
	y -= h / 2;

	DrawTexture(tex, x, y, z, w, h, rgba);
}

void DrawTextureRotated(png_texture* tex, int x, int y, int z, int w, int h, u32 rgba, float angle)
{
	SDL_Rect dest = {
		.x = x - (w / 2),
		.y = y - (h / 2),
		.w = w,
		.h = h,
	};

	SDL_RenderCopyEx(renderer, tex->texture, NULL, &dest, angle, NULL, SDL_FLIP_NONE);
}

static uint8_t menuIconForJar(uint8_t idx)
{
	switch (idx)
	{
		case jar_trophy_png_index: return ui_trophy_png_index;
		case jar_usb_png_index: return ui_usb_png_index;
		case jar_hdd_png_index: return ui_hdd_png_index;
		case jar_db_png_index: return ui_db_png_index;
		case jar_bup_png_index: return ui_bup_png_index;
		case jar_opt_png_index: return ui_opt_png_index;
		default: return ui_about_png_index;
	}
}

static void drawJar(uint8_t idx, uint8_t label_idx, int pos_x, int pos_y, uint8_t alpha, int slot)
{
	const int card_w = 250, card_h = 334;
	const int icon_size = 192;
	uint8_t active = (menu_sel == slot);
	uint8_t frame = active ? tile_frame_active_png_index : tile_frame_png_index;
	if (active)
	{
		/* A soft pulse makes the focus follow the selected card, including Sahl. */
		Uint32 tick = SDL_GetTicks() % 1000;
		uint8_t pulse = (uint8_t)(70 + ((tick < 500 ? tick : 1000 - tick) * 90 / 500));
		DrawTexture(&menu_textures[tile_frame_active_png_index], pos_x - 14, pos_y - 16, 0,
			card_w + 28, card_h + 32, 0xFF2020A0 | pulse);
	}
	DrawTexture(&menu_textures[frame], pos_x - (active ? 6 : 0), pos_y - (active ? 8 : 0), 0,
		card_w + (active ? 12 : 0), card_h + (active ? 16 : 0), 0xffffff00 | alpha);
	uint8_t icon = (idx == 0xFE) ? ui_sahl_png_index : menuIconForJar(idx);
	DrawTexture(&menu_textures[icon], pos_x + (card_w - icon_size) / 2, pos_y + 35, 0,
		icon_size, icon_size, 0xffffff00 | alpha);
	if (idx == 0xFE)
	{
		SetFontAlign(FONT_ALIGN_CENTER);
		SetFontSize(APP_FONT_SIZE_JARS);
		SetFontColor(APP_FONT_COLOR | alpha, 0);
		DrawString(pos_x + card_w / 2, pos_y + 282, _("السهل"));
		SetFontAlign(FONT_ALIGN_LEFT);
	}
	else if (alpha)
		DrawTexture(&menu_textures[label_idx], pos_x, pos_y + 264, 0, card_w, 60, 0xffffff00 | alpha);
}
static void drawJars(uint8_t alpha)
{
const int xs[4] = {25, 510, 995, 1480};
const int top[2] = {350, 700};
drawJar(jar_trophy_png_index, label_trophy_png_index, xs[0], top[0], alpha, 0);
drawJar(jar_usb_png_index, label_usb_png_index, xs[1], top[0], alpha, 1);
drawJar(jar_hdd_png_index, label_hdd_png_index, xs[2], top[0], alpha, 2);
drawJar(jar_db_png_index, apollo_config.online_opt ? label_ftp_png_index : label_online_png_index, xs[3], top[0], alpha, 3);
drawJar(jar_bup_png_index, label_user_png_index, xs[0], top[1], alpha, 4);
drawJar(0xFE, 0, xs[1], top[1], alpha, 5);
drawJar(jar_opt_png_index, label_settings_png_index, xs[2], top[1], alpha, 6);
drawJar(jar_about_png_index, label_about_png_index, xs[3], top[1], alpha, 7);
}
void drawIntroVideo(void)
{
	const int frame_count = 180;
	const int fps = 15;
	Uint32 start_ticks = SDL_GetTicks();
	char path[256];
	for (int frame = 0; frame < frame_count;)
	{
		// Keep playback tied to elapsed time; skip frames if decoding is slower than real time.
		Uint32 elapsed = SDL_GetTicks() - start_ticks;
		int scheduled_frame = (int)((elapsed * fps + 500) / 1000);
		if (scheduled_frame > frame)
			frame = scheduled_frame;
		if (frame >= frame_count)
			break;

		snprintf(path, sizeof(path), APOLLO_APP_PATH "images/intro_%03d.jpg", frame);
		if (LoadMenuTexture(path, icon_png_file_index))
		{
			SDL_RenderClear(renderer);
			DrawBackground2D(0x000000FF);
			DrawTextureCentered(&menu_textures[icon_png_file_index], SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
				0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xFFFFFFFF);
			SDL_RenderPresent(renderer);
			SDL_DestroyTexture(menu_textures[icon_png_file_index].texture);
			menu_textures[icon_png_file_index].texture = NULL;
		}
		frame++;
		Uint32 target_ticks = start_ticks + (Uint32)((frame * 1000) / fps);
		Uint32 now = SDL_GetTicks();
		if ((int32_t)(target_ticks - now) > 0)
			SDL_Delay(target_ticks - now);
	}
}
void drawEndLogo(void)
{
	SDL_Rect rect = {
		.x = 0,
		.w = SCREEN_WIDTH,
	};

	for (rect.h = 0; rect.h <= SCREEN_HEIGHT/2; rect.h += 3)
	{
		// clear the current display buffer
		SDL_RenderClear(renderer);
		DrawBackground2D(0xFFFFFFFF);

		//App description
		DrawTextureCentered(&menu_textures[logo_png_index], SCREEN_WIDTH/2, SCREEN_HEIGHT /2, 0, menu_textures[logo_png_index].width *3/4, menu_textures[logo_png_index].height *3/4, 0xFFFFFF00 | 0xFF);

		rect.y = 0;
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
		SDL_RenderFillRect(renderer, &rect);

		rect.y = SCREEN_HEIGHT - rect.h;
		SDL_RenderFillRect(renderer, &rect);

		//flush and flip
		SDL_RenderPresent(renderer);
	}
}

static void drawGTXMainBackground(uint8_t alpha)
{
	if (menu_textures[bg_gtx_jpg_index].texture)
		DrawTexture(&menu_textures[bg_gtx_jpg_index], 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0xffffff00 | alpha);
	else
		DrawBackground2D(0x08111FFF);
}

static void _draw_MainMenu(uint8_t alpha)
{
	drawGTXMainBackground(0xFF);
	DrawTextureCenteredX(&menu_textures[logo_text_png_index], SCREEN_WIDTH / 2, 82, 0, 760, 124, 0xFFFFFF00 | 0xFF);
	drawJars(alpha);
}

void Draw_MainMenu_Ani(void)
{
	int max = MENU_ANI_MAX, ani = 0;
	for (ani = 0; ani < max; ani++)
	{
		SDL_RenderClear(renderer);
		DrawBackground2D(0xFFFFFFFF);

		//------------ Backgrounds
		u8 bg_a = (u8)(ani * 2);
		if (bg_a < 0x20)
			bg_a = 0x20;
		int logo_a_t = ((ani < 0x30) ? 0 : ((ani - 0x20)*3));
		if (logo_a_t > 0xFF)
			logo_a_t = 0xFF;
		u8 logo_a = (u8)logo_a_t;

			// Fade in the GTX backdrop and wordmark before showing the seven menu cards.
			drawGTXMainBackground(bg_a);
			DrawTextureCenteredX(&menu_textures[logo_text_png_index], SCREEN_WIDTH / 2, 82, 0, 760, 124, 0xFFFFFF00 | logo_a);

		SDL_RenderPresent(renderer);
	}

	max = MENU_ANI_MAX / 2;
	int rate = (0x100 / max);
	for (ani = 0; ani < max; ani++)
	{
		SDL_RenderClear(renderer);
		DrawBackground2D(0xFFFFFFFF);

		u8 icon_a = (u8)(((ani * rate) > 0xFF) ? 0xFF : (ani * rate));

		_draw_MainMenu(icon_a);

		SDL_RenderPresent(renderer);

		if (icon_a == 32)
			break;
	}
}

void Draw_MainMenu(void)
{
	_draw_MainMenu(0xFF);
}
