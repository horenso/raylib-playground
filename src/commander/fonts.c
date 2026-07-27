#include "fonts.h"
#include "types.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>

#include <string.h>

#define ASCII_FIRST 32
#define ASCII_GLYPH_COUNT 95
#define FONT_CACHE_CAPACITY 96

typedef struct {
    int pixel_size;
    Font font;
} CachedFont;

InterfaceFonts fonts = {0};
static FT_Library ft_library = NULL;
static FT_Face ft_face = NULL;
static CachedFont font_cache[FONT_CACHE_CAPACITY] = {0};
static int font_cache_count = 0;

float ScaledFontSize(float size, float scale) { return floorf(size * scale + 0.5f); }

static Font LoadFreeTypeFont(int pixel_size) {
    Font font = {0};
    if (!ft_face || FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)pixel_size) != 0) {
        return font;
    }

    GlyphInfo *glyphs = MemAlloc(ASCII_GLYPH_COUNT * sizeof(*glyphs));
    if (!glyphs) {
        return font;
    }
    memset(glyphs, 0, ASCII_GLYPH_COUNT * sizeof(*glyphs));

    int ascent = (int)((ft_face->size->metrics.ascender + 32) >> 6);
    int advance = (int)((ft_face->size->metrics.max_advance + 32) >> 6);
    if (FT_Load_Char(ft_face, 'M', FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) == 0) {
        advance = (int)((ft_face->glyph->advance.x + 32) >> 6);
    }

    bool complete = true;
    for (int i = 0; i < ASCII_GLYPH_COUNT; i++) {
        int codepoint = ASCII_FIRST + i;
        glyphs[i].value = codepoint;
        glyphs[i].advanceX = advance;

        if (FT_Load_Char(ft_face, (FT_ULong)codepoint, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) != 0 ||
            FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            complete = false;
            break;
        }

        FT_GlyphSlot slot = ft_face->glyph;
        FT_Bitmap *bitmap = &slot->bitmap;
        glyphs[i].offsetX = slot->bitmap_left;
        glyphs[i].offsetY = ascent - slot->bitmap_top;
        glyphs[i].image = (Image){
            .width = (int)bitmap->width,
            .height = (int)bitmap->rows,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
        };

        int byte_count = glyphs[i].image.width * glyphs[i].image.height;
        if (byte_count > 0) {
            unsigned char *pixels = MemAlloc(byte_count);
            if (!pixels) {
                complete = false;
                break;
            }
            glyphs[i].image.data = pixels;

            for (int y = 0; y < glyphs[i].image.height; y++) {
                const unsigned char *source = bitmap->pitch >= 0
                                                  ? bitmap->buffer + y * bitmap->pitch
                                                  : bitmap->buffer + (glyphs[i].image.height - 1 - y) * -bitmap->pitch;
                memcpy(pixels + y * glyphs[i].image.width, source, (size_t)glyphs[i].image.width);
            }
        }
    }

    if (!complete) {
        UnloadFontData(glyphs, ASCII_GLYPH_COUNT);
        return font;
    }

    Rectangle *recs = NULL;
    Image atlas = GenImageFontAtlas(glyphs, &recs, ASCII_GLYPH_COUNT, pixel_size, 1, 0);
    if (!atlas.data || !recs) {
        UnloadFontData(glyphs, ASCII_GLYPH_COUNT);
        if (atlas.data) {
            UnloadImage(atlas);
        }
        return font;
    }

    font = (Font){
        .baseSize = pixel_size,
        .glyphCount = ASCII_GLYPH_COUNT,
        .glyphPadding = 1,
        .texture = LoadTextureFromImage(atlas),
        .recs = recs,
        .glyphs = glyphs,
    };
    UnloadImage(atlas);

    // FreeType already produced the antialiased coverage at the exact target
    // size. Point sampling preserves those hinted pixels without another
    // filtering pass.
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    return font;
}

static Font FontForPixelSize(int pixel_size) {
    if (pixel_size < 1) {
        pixel_size = 1;
    }
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].pixel_size == pixel_size) {
            return font_cache[i].font;
        }
    }
    if (!ft_face || font_cache_count >= FONT_CACHE_CAPACITY) {
        return GetFontDefault();
    }

    Font font = LoadFreeTypeFont(pixel_size);
    if (font.texture.id == 0) {
        return GetFontDefault();
    }
    font_cache[font_cache_count++] = (CachedFont){.pixel_size = pixel_size, .font = font};
    return font;
}

void UpdateInterfaceFontScale(float application_scale, float canvas_scale) {
    int title_size = (int)ScaledFontSize(TITLE_TEXT_SIZE, canvas_scale);
    int body_size = (int)ScaledFontSize(BODY_TEXT_SIZE, application_scale);
    int small_size = (int)ScaledFontSize(11.0f, application_scale);
    int gui_size = (int)ScaledFontSize(GUI_TEXT_SIZE, application_scale);
    int node_body_size = (int)ScaledFontSize(BODY_TEXT_SIZE, canvas_scale);
    int node_small_size = (int)ScaledFontSize(BODY_TEXT_SIZE * 0.85f, canvas_scale);
    int node_gui_size = (int)ScaledFontSize(GUI_TEXT_SIZE, canvas_scale);

    if (title_size == fonts.title_size && body_size == fonts.body_size && small_size == fonts.small_size &&
        gui_size == fonts.gui_size && node_body_size == fonts.node_body_size &&
        node_small_size == fonts.node_small_size && node_gui_size == fonts.node_gui_size) {
        return;
    }

    fonts.title = FontForPixelSize(title_size);
    fonts.body = FontForPixelSize(body_size);
    fonts.small = FontForPixelSize(small_size);
    fonts.gui = FontForPixelSize(gui_size);
    fonts.node_body = FontForPixelSize(node_body_size);
    fonts.node_small = FontForPixelSize(node_small_size);
    fonts.node_gui = FontForPixelSize(node_gui_size);
    fonts.title_size = title_size;
    fonts.body_size = body_size;
    fonts.small_size = small_size;
    fonts.gui_size = gui_size;
    fonts.node_body_size = node_body_size;
    fonts.node_small_size = node_small_size;
    fonts.node_gui_size = node_gui_size;
    GuiSetFont(fonts.gui);
}

void LoadInterfaceFonts(void) {
    const char *candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
    };
    const char *path = NULL;
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (FileExists(candidates[i])) {
            path = candidates[i];
            break;
        }
    }

    if (path && FT_Init_FreeType(&ft_library) == 0 && FT_New_Face(ft_library, path, 0, &ft_face) == 0) {
        FT_Select_Charmap(ft_face, FT_ENCODING_UNICODE);
        fonts.custom_loaded = true;
    }

    UpdateInterfaceFontScale(UI_BASE_PIXEL_SIZE, UI_BASE_PIXEL_SIZE);
    GuiSetFont(fonts.gui);
}

void UnloadInterfaceFonts(void) {
    for (int i = 0; i < font_cache_count; i++) {
        UnloadFont(font_cache[i].font);
    }
    font_cache_count = 0;
    if (ft_face) {
        FT_Done_Face(ft_face);
        ft_face = NULL;
    }
    if (ft_library) {
        FT_Done_FreeType(ft_library);
        ft_library = NULL;
    }
}

void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color) {
    DrawTextEx(font, text, (Vector2){floorf(x + 0.5f), floorf(y + 0.5f)}, size, 0, color);
}

void SetGuiScale(float scale) {
    GuiSetFont(fonts.gui);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)ScaledFontSize(GUI_TEXT_SIZE, scale));
    GuiSetStyle(DEFAULT, TEXT_PADDING, (int)(4.0f * scale + 0.5f));
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, (int)(24.0f * scale + 0.5f));
    GuiSetStyle(LISTVIEW, SCROLLBAR_WIDTH, (int)(14.0f * scale + 0.5f));
    GuiSetIconScale(scale >= 1.5f ? 2 : 1);
}

void SetNodeGuiScale(float scale) {
    GuiSetFont(fonts.node_gui);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)ScaledFontSize(GUI_TEXT_SIZE, scale));
    GuiSetStyle(DEFAULT, TEXT_PADDING, (int)(4.0f * scale + 0.5f));
}
