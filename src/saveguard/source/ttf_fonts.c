#include <proto-include.h>
#include <SDL2/SDL.h>
#include <stdint.h>

#include "types.h"
#include "ttf_render.h"
#include "menu.h"

/******************************************************************************************************************************************************/
/* TTF functions to load and convert fonts                                                                                                             */
/******************************************************************************************************************************************************/

static int ttf_inited = 0;

/* The renderer draws UTF-8 glyphs one by one.  Arabic therefore needs a
 * small presentation-form shaper before it reaches FreeType. */
typedef struct arabic_form {
	uint32_t base, isolated, final, initial, medial;
	int join_left, join_right;
} arabic_form_t;

static const arabic_form_t arabic_forms[] = {
	{0x0627,0xFE8D,0xFE8E,0,0,0,1},{0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,1,1},
	{0x0629,0xFE93,0xFE94,0,0,0,1},{0x062A,0xFE95,0xFE96,0xFE97,0xFE98,1,1},
	{0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,1,1},{0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,1,1},
	{0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,1,1},{0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,1,1},
	{0x062F,0xFEA9,0xFEAA,0,0,0,1},{0x0630,0xFEAB,0xFEAC,0,0,0,1},
	{0x0631,0xFEAD,0xFEAE,0,0,0,1},{0x0632,0xFEAF,0xFEB0,0,0,0,1},
	{0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,1,1},{0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,1,1},
	{0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,1,1},{0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,1,1},
	{0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,1,1},{0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,1,1},
	{0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,1,1},{0x063A,0xFECD,0xFECE,0xFECF,0xFED0,1,1},
	{0x0641,0xFED1,0xFED2,0xFED3,0xFED4,1,1},{0x0642,0xFED5,0xFED6,0xFED7,0xFED8,1,1},
	{0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,1,1},{0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,1,1},
	{0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,1,1},{0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,1,1},
	{0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,1,1},{0x0648,0xFEED,0xFEEE,0,0,0,1},
	{0x0649,0xFEEF,0xFEF0,0,0,0,1},{0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,1,1},
	{0x0671,0xFB50,0xFB51,0,0,0,1},{0x067E,0xFB56,0xFB57,0xFB58,0xFB59,1,1},
	{0x0686,0xFB7A,0xFB7B,0xFB7C,0xFB7D,1,1},{0x06A9,0xFEDA,0xFED9,0xFEE0,0,1,1},
	{0x06AF,0xFB92,0xFB93,0xFB94,0xFB95,1,1},{0x06CC,0xFBFC,0xFBFD,0xFBFE,0xFBFF,1,1},
};

static const arabic_form_t *arabic_form(uint32_t c)
{
	for (unsigned i = 0; i < sizeof(arabic_forms) / sizeof(arabic_forms[0]); i++)
		if (arabic_forms[i].base == c) return &arabic_forms[i];
	return NULL;
}

static int arabic_is_mark(uint32_t c)
{
	return (c >= 0x064B && c <= 0x065F) || (c >= 0x0670 && c <= 0x0670);
}

static int arabic_decode(const char **src, uint32_t *out)
{
	const unsigned char *p = (const unsigned char *)*src;
	if (!*p) return 0;
	if (*p < 0x80) { *out = *p++; *src = (const char *)p; return 1; }
	if ((*p & 0xE0) == 0xC0) { *out = (*p++ & 0x1F) << 6; *out |= *p++ & 0x3F; }
	else if ((*p & 0xF0) == 0xE0) { *out = (*p++ & 0x0F) << 12; *out |= (*p++ & 0x3F) << 6; *out |= *p++ & 0x3F; }
	else { *out = (*p++ & 7) << 18; *out |= (*p++ & 0x3F) << 12; *out |= (*p++ & 0x3F) << 6; *out |= *p++ & 0x3F; }
	*src = (const char *)p;
	return 1;
}

static int arabic_encode(char *out, int at, int cap, uint32_t c)
{
	if (c < 0x80) { if (at + 1 >= cap) return at; out[at++] = (char)c; }
	else if (c < 0x800) { if (at + 2 >= cap) return at; out[at++] = (char)(0xC0 | (c >> 6)); out[at++] = (char)(0x80 | (c & 63)); }
	else if (c < 0x10000) { if (at + 3 >= cap) return at; out[at++] = (char)(0xE0 | (c >> 12)); out[at++] = (char)(0x80 | ((c >> 6) & 63)); out[at++] = (char)(0x80 | (c & 63)); }
	return at;
}

static int arabic_prepare(const char *input, char *output, int cap)
{
	uint32_t cp[768], shaped[768];
	int n = 0, at = 0;
	const char *p = input;
	while (*p && n < 767) arabic_decode(&p, &cp[n++]);
	for (int i = 0; i < n;) {
		int begin = i, end = i, has_ar = 0;
		while (end < n && (arabic_form(cp[end]) || arabic_is_mark(cp[end]) || cp[end] == ' ' || cp[end] == '\t')) {
			if (arabic_form(cp[end])) has_ar = 1;
			end++;
		}
		if (!has_ar) { at = arabic_encode(output, at, cap, cp[i++]); continue; }
		for (int j = begin; j < end; j++) {
			const arabic_form_t *f = arabic_form(cp[j]);
			if (!f) { shaped[j] = cp[j]; continue; }
			int prev = j - 1, next = j + 1;
			while (prev >= begin && arabic_is_mark(cp[prev])) prev--;
			while (next < end && arabic_is_mark(cp[next])) next++;
			const arabic_form_t *pf = (prev >= begin) ? arabic_form(cp[prev]) : NULL;
			const arabic_form_t *nf = (next < end) ? arabic_form(cp[next]) : NULL;
			int join_prev = pf && pf->join_left && f->join_right;
			int join_next = nf && f->join_left && nf->join_right;
			if (join_prev && join_next && f->medial) shaped[j] = f->medial;
			else if (join_prev) shaped[j] = f->final;
			else if (join_next) shaped[j] = f->initial;
			else shaped[j] = f->isolated;
		}
		for (int j = end - 1; j >= begin; j--) at = arabic_encode(output, at, cap, shaped[j]);
		i = end;
	}
	output[at < cap ? at : cap - 1] = 0;
	return at;
}

static FT_Library freetype;
static FT_Face face[4];
static int f_face[4] = {0, 0, 0, 0};

int TTFLoadFont(int set, const char * path, void * from_memory, int size_from_memory)
{

    if(!ttf_inited)
        FT_Init_FreeType(&freetype);
    ttf_inited = 1;

    f_face[set] = 0;

    if(path) {
        if(FT_New_Face(freetype, path, 0, &face[set])<0) return -1;
    } else {
        if(FT_New_Memory_Face(freetype, from_memory, size_from_memory, 0, &face[set])) return -1;
        }

    f_face[set] = 1;

    return 0;
}

/* release all */

void TTFUnloadFont()
{
   if(!ttf_inited) return;
   FT_Done_FreeType(freetype);
   ttf_inited = 0;
}

/* function to render the character
chr : character from 0 to 255
bitmap: u8 bitmap passed to render the character character (max 256 x 256 x 1 (8 bits Alpha))
*w : w is the bitmap width as input and the width of the character (used to increase X) as output
*h : h is the bitmap height as input and the height of the character (used to Y correction combined with y_correction) as output
y_correction : the Y correction to display the character correctly in the screen

*/
/*
void TTF_to_Bitmap(uint8_t chr, uint8_t * bitmap, short *w, short *h, short *y_correction)
{
    if(f_face[0]) FT_Set_Pixel_Sizes(face[0], (*w), (*h));
    if(f_face[1]) FT_Set_Pixel_Sizes(face[1], (*w), (*h));
    if(f_face[2]) FT_Set_Pixel_Sizes(face[2], (*w), (*h));
    if(f_face[3]) FT_Set_Pixel_Sizes(face[3], (*w), (*h));

    FT_GlyphSlot slot;

    memset(bitmap, 0, (*w) * (*h));

    FT_UInt index;

    if(f_face[0] && (index = FT_Get_Char_Index(face[0], (char) chr))!=0
        && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
    else if(f_face[1] && (index = FT_Get_Char_Index(face[1], (char) chr))!=0
        && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
    else if(f_face[2] && (index = FT_Get_Char_Index(face[2], (char) chr))!=0
        && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
    else if(f_face[3] && (index = FT_Get_Char_Index(face[3], (char) chr))!=0
        && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
    else {(*w) = 0; return;}

    int n, m, ww;

    *y_correction = (*h) - 1 - slot->bitmap_top;

    ww = 0;

    for(n = 0; n < slot->bitmap.rows; n++) {
        for (m = 0; m < slot->bitmap.width; m++) {

            if(m >= (*w) || n >= (*h)) continue;

            bitmap[m] = (u8) slot->bitmap.buffer[ww + m];
        }

    bitmap += *w;

    ww += slot->bitmap.width;
    }

    *w = ((slot->advance.x + 31) >> 6) + ((slot->bitmap_left < 0) ? -slot->bitmap_left : 0);
    *h = slot->bitmap.rows;
}

int Render_String_UTF8(u16 * bitmap, int w, int h, u8 *string, int sw, int sh)
{
    int posx = 0;
    int n, m, ww, ww2;
    u8 color;
    u32 ttf_char;

    if(f_face[0]) FT_Set_Pixel_Sizes(face[0], sw, sh);
    if(f_face[1]) FT_Set_Pixel_Sizes(face[1], sw, sh);
    if(f_face[2]) FT_Set_Pixel_Sizes(face[2], sw, sh);
    if(f_face[3]) FT_Set_Pixel_Sizes(face[3], sw, sh);

    //FT_Set_Pixel_Sizes(face, sw, sh);
    FT_GlyphSlot slot = NULL;

    memset(bitmap, 0, w * h * 2);

    while(*string) {

        if(*string == 32 || *string == 9) {posx += sw>>1; string++; continue;}

        if(*string & 128) {
            m = 1;

            if((*string & 0xf8)==0xf0) { // 4 bytes
                ttf_char = (u32) (*(string++) & 3);
                m = 3;
            } else if((*string & 0xE0)==0xE0) { // 3 bytes
                ttf_char = (u32) (*(string++) & 0xf);
                m = 2;
            } else if((*string & 0xE0)==0xC0) { // 2 bytes
                ttf_char = (u32) (*(string++) & 0x1f);
                m = 1;
            } else {string++;continue;} // error!

             for(n = 0; n < m; n++) {
                if(!*string) break; // error!
                    if((*string & 0xc0) != 0x80) break; // error!
                    ttf_char = (ttf_char <<6) |((u32) (*(string++) & 63));
             }

            if((n != m) && !*string) break;

        } else ttf_char = (u32) *(string++);

        if(ttf_char == 13 || ttf_char == 10) ttf_char='/';

        FT_UInt index;

        if(f_face[0] && (index = FT_Get_Char_Index(face[0], ttf_char))!=0
            && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
        else if(f_face[1] && (index = FT_Get_Char_Index(face[1], ttf_char))!=0
            && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
        else if(f_face[2] && (index = FT_Get_Char_Index(face[2], ttf_char))!=0
            && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
        else if(f_face[3] && (index = FT_Get_Char_Index(face[3], ttf_char))!=0
            && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
        else ttf_char = 0;

        if(ttf_char!=0 && slot->bitmap.buffer) {
            ww = ww2 = 0;

            int y_correction = sh - 1 - slot->bitmap_top;
            if(y_correction < 0) y_correction = 0;
            ww2 = y_correction * w;

            for(n = 0; n < slot->bitmap.rows; n++) {
                if(n + y_correction >= h) break;
                for (m = 0; m < slot->bitmap.width; m++) {

                    if(m + posx >= w) continue;

                    color = (u8) slot->bitmap.buffer[ww + m];

                    if(color) bitmap[posx + m + ww2] = (color<<8) | 0xfff;
                }

            ww2 += w;

            ww += slot->bitmap.width;
            }

        }

        if(slot) posx+= slot->bitmap.width;
    }
    return posx;
}
*/
// constructor dinamico de fuentes 32 x 32

typedef struct ttf_dyn {
    u32 ttf;
    SDL_Texture *text[2];
    u32 r_use;
    u16 y_start;
    u16 width;
    u16 height;
    u16 flags;

} ttf_dyn;

#define MAX_CHARS 1600
#define TEX_SZ 64

static ttf_dyn ttf_font_datas[MAX_CHARS];

static u32 r_use= 0;

float Y_ttf = 0.0f;
float Z_ttf = 0.0f;

static int Win_X_ttf = 0;
static int Win_Y_ttf = 0;
static int Win_W_ttf = SCREEN_WIDTH;
static int Win_H_ttf = SCREEN_HEIGHT;


static u32 Win_flag = 0;

void set_ttf_window(int x, int y, int width, int height, u32 mode)
{
    Win_X_ttf = x;
    Win_Y_ttf = y;
    Win_W_ttf = width;
    Win_H_ttf = height;
    Win_flag = mode;
    Y_ttf = 0.0f;
    Z_ttf = 0.0f;

}

u16 * init_ttf_table(u8 *texture)
{
    int n;

    r_use= 0;
    for(n= 0; n <  MAX_CHARS; n++) {
        memset(&ttf_font_datas[n], 0, sizeof(ttf_dyn));
    }

    return (u16*)texture;

}

void reset_ttf_frame(void)
{
    int n;

    for(n = 0; n < MAX_CHARS; n++) {

        ttf_font_datas[n].flags &= 1;

    }

    r_use++;

}
static void DrawBox_ttf(float x, float y, float z, float w, float h, u32 rgba)
{
    SDL_FRect rect = {
        .x = x,
        .y = y,
        .w = w,
        .h = h,
    };

    SDL_SetRenderDrawColor(renderer, RGBA_R(rgba), RGBA_G(rgba), RGBA_B(rgba), RGBA_A(rgba));
    SDL_RenderFillRectF(renderer, &rect);
/*
    tiny3d_SetPolygon(TINY3D_QUADS);


    tiny3d_VertexPos(x    , y    , z);
    tiny3d_VertexColor(rgba);

    tiny3d_VertexPos(x + w, y    , z);

    tiny3d_VertexPos(x + w, y + h, z);

    tiny3d_VertexPos(x    , y + h, z);

    tiny3d_End();
*/
}

static SDL_Texture* create_texture(const u8* bitmap, u32 rgba)
{
    u32 buf[TEX_SZ * TEX_SZ];

    for (int i = 0; i < TEX_SZ*TEX_SZ; i++)
        buf[i] = bitmap[i] ? ((rgba & 0xFFFFFF00) | bitmap[i]) : 0;

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom((void*) buf, TEX_SZ, TEX_SZ, 32, 4 * TEX_SZ, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
    SDL_Texture* sdl_tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_SetTextureBlendMode(sdl_tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(surface);

    return (sdl_tex);
}

static void DrawTextBox_ttf(SDL_Texture* bitmap, float x, float y, float z, float w, float h, u32 rgba, float tx, float ty)
{
    SDL_FRect dest = {
        .x = x,
        .y = y,
        .w = w,
        .h = h,
    };

	SDL_SetTextureAlphaMod(bitmap, RGBA_A(rgba));
    SDL_RenderCopyF(renderer, bitmap, NULL, &dest);
}

#define TTF_UX 30 * (TEX_SZ /32)
#define TTF_UY 24 * (TEX_SZ /32)


int display_ttf_string(int posx, int posy, const char *string, u32 color, u32 bkcolor, int sw, int sh, int (*DrawIcon_cb)(int, int, char))
{
    int l,n, m, ww, ww2;
    u8 colorc;
    u32 ttf_char;
    char arabic_buffer[3072];
    arabic_prepare(string, arabic_buffer, sizeof(arabic_buffer));
    u8 *ustring = (u8 *) arabic_buffer;

    int lenx = 0;

    while(*ustring) {

        if(posy >= Win_H_ttf) break;

        if(*ustring == ' ') {posx += sw>>1; ustring++; continue;}

        if(*ustring & 128) {
            m = 1;

            if((*ustring & 0xf8)==0xf0) { // 4 bytes
                ttf_char = (u32) (*(ustring++) & 3);
                m = 3;
            } else if((*ustring & 0xE0)==0xE0) { // 3 bytes
                ttf_char = (u32) (*(ustring++) & 0xf);
                m = 2;
            } else if((*ustring & 0xE0)==0xC0) { // 2 bytes
                ttf_char = (u32) (*(ustring++) & 0x1f);
                m = 1;
            } else {ustring++;continue;} // error!

             for(n = 0; n < m; n++) {
                if(!*ustring || (*ustring & 0xc0) != 0x80) { // error!
                    ttf_char = ' ';
                    break;
                }
                ttf_char = (ttf_char <<6) |((u32) (*(ustring++) & 63));
             }

            if((n != m) && !*ustring) break;

        } else ttf_char = (u32) *(ustring++);


        if(Win_flag & WIN_SKIP_LF) {
            if(ttf_char == '\r' || ttf_char == '\n') ttf_char=' ';
        } else {
            if(Win_flag & WIN_DOUBLE_LF) {
                if(ttf_char == '\r') {if(posx > lenx) lenx = posx; posx = 0;continue;}
                if(ttf_char == '\n') {posy += sh;continue;}
            } else {
                if(ttf_char == '\n') {if(posx > lenx) lenx = posx; posx = 0;posy += sh;continue;}
            }
        }

        if ((ttf_char < 32) && DrawIcon_cb) {
            int resx = DrawIcon_cb(posx, posy, (char) ttf_char);
            if (resx > 0) {
                posx += resx;
                continue;
            }
            else
                ttf_char='?';
        }

        // search ttf_char
        if(ttf_char < 128) n= ttf_char;
        else {
            m= 0;
            int rel=0;

            for(n= 128; n < MAX_CHARS; n++) {
                if(!(ttf_font_datas[n].flags & 1)) m= n;

                if((ttf_font_datas[n].flags & 3)==1) {
                    int trel= r_use - ttf_font_datas[n].r_use;
                    if(m==0) {m= n;rel = trel;}
                    else if(rel > trel) {m= n;rel = trel;}

                }
                if(ttf_font_datas[n].ttf == ttf_char) break;
            }

            if(m==0) m = 128;

        }

        if(n >= MAX_CHARS) {ttf_font_datas[m].flags = 0; l= m;} else l=n;

        // building the character
        if(!(ttf_font_datas[l].flags & 1)) {

            if(f_face[0]) FT_Set_Pixel_Sizes(face[0], TTF_UX, TTF_UY);
            if(f_face[1]) FT_Set_Pixel_Sizes(face[1], TTF_UX, TTF_UY);
            if(f_face[2]) FT_Set_Pixel_Sizes(face[2], TTF_UX, TTF_UY);
            if(f_face[3]) FT_Set_Pixel_Sizes(face[3], TTF_UX, TTF_UY);

            FT_GlyphSlot slot = NULL;

            u8 bitmap[TEX_SZ * TEX_SZ];
            memset(bitmap, 0, TEX_SZ * TEX_SZ);

            ///////////

            FT_UInt index;

            if(f_face[0] && (index = FT_Get_Char_Index(face[0], ttf_char))!=0
                && !FT_Load_Glyph(face[0], index, FT_LOAD_RENDER )) slot = face[0]->glyph;
            else if(f_face[1] && (index = FT_Get_Char_Index(face[1], ttf_char))!=0
                && !FT_Load_Glyph(face[1], index, FT_LOAD_RENDER )) slot = face[1]->glyph;
            else if(f_face[2] && (index = FT_Get_Char_Index(face[2], ttf_char))!=0
                && !FT_Load_Glyph(face[2], index, FT_LOAD_RENDER )) slot = face[2]->glyph;
            else if(f_face[3] && (index = FT_Get_Char_Index(face[3], ttf_char))!=0
                && !FT_Load_Glyph(face[3], index, FT_LOAD_RENDER )) slot = face[3]->glyph;
            else ttf_char = 0;

            if(ttf_char!=0) {
                ww = ww2 = 0;

                int y_correction = TTF_UY - 1 - slot->bitmap_top;
                if(y_correction < 0) y_correction = 0;

                ttf_font_datas[l].flags = 1;
                ttf_font_datas[l].y_start = y_correction;
                ttf_font_datas[l].height = slot->bitmap.rows;
                ttf_font_datas[l].width = slot->bitmap.width;
                ttf_font_datas[l].ttf = ttf_char;


                for(n = 0; n < slot->bitmap.rows; n++) {
                    if(n >= TEX_SZ) break;
                    for (m = 0; m < slot->bitmap.width; m++) {

                        if(m >= TEX_SZ) continue;

                        colorc = (u8) slot->bitmap.buffer[ww + m];

                        if(colorc) bitmap[m + ww2] = colorc; //(colorc<<8) | 0xfff;
                    }

                ww2 += TEX_SZ;

                ww += slot->bitmap.width;
                }
                ttf_font_datas[l].text[0] = create_texture(bitmap, 0x000000FF);
                ttf_font_datas[l].text[1] = create_texture(bitmap, 0xFFFFFFFF);
            }
            else continue;
        }

        // displaying the character
        ttf_font_datas[l].flags |= 2; // in use
        ttf_font_datas[l].r_use = r_use;

        if((Win_flag & WIN_AUTO_LF) && (posx + (ttf_font_datas[l].width * sw / TEX_SZ) + 1) > Win_W_ttf) {
            posx = 0;
            posy += sh;
        }

        u32 ccolor = color;
        u32 cx =(ttf_font_datas[l].width * sw / TEX_SZ) + 1;

        // skip if out of window
        if((posx + cx) > Win_W_ttf || (posy + sh) > Win_H_ttf ) ccolor = 0;

        if(ccolor) {
//            tiny3d_SetTextureWrap(0, tiny3d_TextureOffset(bitmap), 32, 32, 32 * 2,
//                TINY3D_TEX_FORMAT_A4R4G4B4, TEXTWRAP_CLAMP, TEXTWRAP_CLAMP, TEXTURE_LINEAR);

            if (bkcolor != 0) DrawBox_ttf((float) (Win_X_ttf + posx), (float) (Win_Y_ttf + posy) + ((float) ttf_font_datas[l].y_start * sh) * 0.03125f,
            Z_ttf, (float) sw, (float) sh, bkcolor);
            DrawTextBox_ttf(ttf_font_datas[l].text[((color & 0xFFFFFF00) != 0)], (float) (Win_X_ttf + posx), (float) (Win_Y_ttf + posy) + ((float) ttf_font_datas[l].y_start * sh) * 0.015625f,
            Z_ttf, (float) sw, (float) sh, color, 0.99f, 0.99f);
        }

        posx+= cx;
    }

    Y_ttf = (float) posy + sh;

    if(posx < lenx) posx = lenx;
    return posx;
}

int width_ttf_string(const char *string, int sw, int sh)
{
    return (display_ttf_string(0, 0, string, 0, 0, sw, sh, NULL));
}
