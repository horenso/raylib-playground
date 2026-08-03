#include "fonts.h"
#include "types.h"

#include "raylib.h"

// Correction applied to raygui's vertical text centering (TEXT_VALIGN_PIXEL_OFFSET).
// Shifts GuiButton/GuiWindowBox etc. text so capitals are optically centred.
// Updated by SetGuiScale() whenever the GUI font or size changes.
static int g_gui_valign_offset = 0;
#define TEXT_VALIGN_PIXEL_OFFSET(h) (g_gui_valign_offset + (int)(h) % 2)

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>

#include <string.h>

#define ASCII_FIRST 32
#define ASCII_GLYPH_COUNT 95
#define LATIN1_FIRST 160
#define LATIN1_GLYPH_COUNT 96
#define FONT_GLYPH_COUNT (ASCII_GLYPH_COUNT + LATIN1_GLYPH_COUNT)
#define FONT_CACHE_CAPACITY 128

typedef enum {
    FONT_FAMILY_UI,
    FONT_FAMILY_MONO,
} FontFamily;

typedef struct {
    FontFamily family;
    int pixel_size;
    int ascender;
    int cap_height;
    Font font;
} CachedFont;

InterfaceFonts fonts = {0};
static FT_Library ft_library = NULL;
static FT_Face ft_ui_face = NULL;
static FT_Face ft_mono_face = NULL;
static CachedFont font_cache[FONT_CACHE_CAPACITY] = {0};
static int font_cache_count = 0;

float ScaledFontSize(float size, float scale) { return floorf(size * scale + 0.5f); }

static Font LoadFreeTypeFont(FT_Face face, int pixel_size) {
    Font font = {0};
    if (!face || FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size) != 0) {
        return font;
    }

    GlyphInfo *glyphs = MemAlloc(FONT_GLYPH_COUNT * sizeof(*glyphs));
    if (!glyphs) {
        return font;
    }
    memset(glyphs, 0, FONT_GLYPH_COUNT * sizeof(*glyphs));

    int ascent = (int)((face->size->metrics.ascender + 32) >> 6);

    bool complete = true;
    for (int i = 0; i < FONT_GLYPH_COUNT; i++) {
        int codepoint = i < ASCII_GLYPH_COUNT ? ASCII_FIRST + i : LATIN1_FIRST + i - ASCII_GLYPH_COUNT;
        glyphs[i].value = codepoint;

        if (FT_Load_Char(face, (FT_ULong)codepoint, FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT) != 0 ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            complete = false;
            break;
        }

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap *bitmap = &slot->bitmap;
        glyphs[i].advanceX = (int)((slot->advance.x + 32) >> 6);
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
        UnloadFontData(glyphs, FONT_GLYPH_COUNT);
        return font;
    }

    Rectangle *recs = NULL;
    Image atlas = GenImageFontAtlas(glyphs, &recs, FONT_GLYPH_COUNT, pixel_size, 2, 0);
    if (!atlas.data || !recs) {
        UnloadFontData(glyphs, FONT_GLYPH_COUNT);
        if (atlas.data) {
            UnloadImage(atlas);
        }
        return font;
    }

    font = (Font){
        .baseSize = pixel_size,
        .glyphCount = FONT_GLYPH_COUNT,
        .glyphPadding = 2,
        .texture = LoadTextureFromImage(atlas),
        .recs = recs,
        .glyphs = glyphs,
    };
    UnloadImage(atlas);

    // Linear sampling keeps glyph edges stable when DPI or canvas zoom causes
    // a fractional texture-to-screen ratio.
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

static Font FontForPixelSize(FontFamily family, int pixel_size) {
    if (pixel_size < 1) {
        pixel_size = 1;
    }
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].family == family && font_cache[i].pixel_size == pixel_size) {
            return font_cache[i].font;
        }
    }
    FT_Face face = family == FONT_FAMILY_MONO ? ft_mono_face : ft_ui_face;
    if (!face) {
        face = family == FONT_FAMILY_MONO ? ft_ui_face : ft_mono_face;
    }
    if (!face || font_cache_count >= FONT_CACHE_CAPACITY) {
        return GetFontDefault();
    }

    Font font = LoadFreeTypeFont(face, pixel_size);
    if (font.texture.id == 0) {
        return GetFontDefault();
    }
    // face is still set to pixel_size; read metrics for optical centering.
    int ascender = (int)((face->size->metrics.ascender + 32) >> 6);
    int cap_height = ascender;
    if (FT_Load_Char(face, (FT_ULong)'H', FT_LOAD_DEFAULT) == 0) {
        int ch = (int)((face->glyph->metrics.horiBearingY + 32) >> 6);
        if (ch > 0) {
            cap_height = ch;
        }
    }
    font_cache[font_cache_count++] = (CachedFont){
        .family = family, .pixel_size = pixel_size, .ascender = ascender, .cap_height = cap_height, .font = font};
    return font;
}

// Returns the y offset to optically centre uppercase text inside a box.
// Usage: DrawText(font, text, x, box_y + FontTextCenterOffset(font, box_height), size, color);
float FontTextCenterOffset(Font font, float box_height) {
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].font.texture.id == font.texture.id) {
            // Capitals occupy [y + (ascender - cap_height), y + ascender].
            // Centre that range within box_height:
            //   visual_center = y + ascender - cap_height/2  =  box_height/2
            //   => y = (box_height - 2*ascender + cap_height) / 2
            float ascender = (float)font_cache[i].ascender;
            float cap_h = (float)font_cache[i].cap_height;
            return (box_height - 2.0f * ascender + cap_h) * 0.5f;
        }
    }
    return (box_height - (float)font.baseSize) * 0.5f;
}

void UpdateInterfaceFontScale(float application_scale, float canvas_scale) {
    Vector2 dpi = GetWindowScaleDPI();
    float dpi_scale = fmaxf(dpi.x, dpi.y);
    if (dpi_scale < 1.0f) {
        dpi_scale = 1.0f;
    }
    int title_size = (int)ScaledFontSize(TITLE_TEXT_SIZE, canvas_scale);
    int body_size = (int)ScaledFontSize(BODY_TEXT_SIZE, application_scale);
    int small_size = (int)ScaledFontSize(11.0f, application_scale);
    int mono_size = body_size;
    int gui_size = (int)ScaledFontSize(GUI_TEXT_SIZE, application_scale);
    int node_body_size = (int)ScaledFontSize(BODY_TEXT_SIZE, canvas_scale);
    int node_small_size = (int)ScaledFontSize(BODY_TEXT_SIZE * 0.85f, canvas_scale);
    int node_mono_size = node_body_size;
    int node_gui_size = (int)ScaledFontSize(GUI_TEXT_SIZE, canvas_scale);

    if (title_size == fonts.title_size && body_size == fonts.body_size && small_size == fonts.small_size &&
        mono_size == fonts.mono_size && gui_size == fonts.gui_size && node_body_size == fonts.node_body_size &&
        node_small_size == fonts.node_small_size && node_mono_size == fonts.node_mono_size &&
        node_gui_size == fonts.node_gui_size && fabsf(dpi_scale - fonts.dpi_scale) < 0.01f) {
        return;
    }

    fonts.title = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(title_size, dpi_scale));
    fonts.body = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(body_size, dpi_scale));
    fonts.small = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(small_size, dpi_scale));
    fonts.mono = FontForPixelSize(FONT_FAMILY_MONO, (int)ScaledFontSize(mono_size, dpi_scale));
    fonts.gui = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(gui_size, dpi_scale));
    fonts.node_body = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(node_body_size, dpi_scale));
    fonts.node_small = FontForPixelSize(FONT_FAMILY_UI, (int)ScaledFontSize(node_small_size, dpi_scale));
    fonts.node_mono = FontForPixelSize(FONT_FAMILY_MONO, (int)ScaledFontSize(node_mono_size, dpi_scale));
    fonts.node_gui = FontForPixelSize(FONT_FAMILY_MONO, (int)ScaledFontSize(node_gui_size, dpi_scale));
    fonts.title_size = title_size;
    fonts.body_size = body_size;
    fonts.small_size = small_size;
    fonts.mono_size = mono_size;
    fonts.gui_size = gui_size;
    fonts.node_body_size = node_body_size;
    fonts.node_small_size = node_small_size;
    fonts.node_mono_size = node_mono_size;
    fonts.node_gui_size = node_gui_size;
    fonts.dpi_scale = dpi_scale;
    GuiSetFont(fonts.gui);
}

void LoadInterfaceFonts(void) {
    const char *ui_candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuSans[wdth,wght].ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    };
    const char *mono_candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/noto/NotoMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
    };
    const char *ui_path = NULL;
    const char *mono_path = NULL;
    for (int i = 0; i < (int)(sizeof(ui_candidates) / sizeof(ui_candidates[0])); i++) {
        if (FileExists(ui_candidates[i])) {
            ui_path = ui_candidates[i];
            break;
        }
    }
    for (int i = 0; i < (int)(sizeof(mono_candidates) / sizeof(mono_candidates[0])); i++) {
        if (FileExists(mono_candidates[i])) {
            mono_path = mono_candidates[i];
            break;
        }
    }

    if (FT_Init_FreeType(&ft_library) == 0) {
        if (ui_path && FT_New_Face(ft_library, ui_path, 0, &ft_ui_face) == 0) {
            FT_Select_Charmap(ft_ui_face, FT_ENCODING_UNICODE);
        }
        if (mono_path && FT_New_Face(ft_library, mono_path, 0, &ft_mono_face) == 0) {
            FT_Select_Charmap(ft_mono_face, FT_ENCODING_UNICODE);
        }
        fonts.custom_loaded = ft_ui_face || ft_mono_face;
    }

    UpdateInterfaceFontScale(UI_BASE_PIXEL_SIZE, UI_BASE_PIXEL_SIZE);
    GuiSetFont(fonts.gui);
}

void UnloadInterfaceFonts(void) {
    for (int i = 0; i < font_cache_count; i++) {
        UnloadFont(font_cache[i].font);
    }
    font_cache_count = 0;
    if (ft_ui_face) {
        FT_Done_Face(ft_ui_face);
        ft_ui_face = NULL;
    }
    if (ft_mono_face) {
        FT_Done_Face(ft_mono_face);
        ft_mono_face = NULL;
    }
    if (ft_library) {
        FT_Done_FreeType(ft_library);
        ft_library = NULL;
    }
}

void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color) {
    DrawTextEx(font, text, (Vector2){floorf(x + 0.5f), floorf(y + 0.5f)}, size, 0, color);
}

UiTextStyle GetUiTextStyle(TextRole role, bool canvas) {
    UiTextStyle style = {0};
    switch (role) {
    case TEXT_ROLE_TITLE:
        style.font = fonts.title;
        style.size = (float)fonts.title_size;
        break;
    case TEXT_ROLE_CODE:
        style.font = canvas ? fonts.node_mono : fonts.mono;
        style.size = (float)(canvas ? fonts.node_mono_size : fonts.mono_size);
        break;
    case TEXT_ROLE_LABEL:
    case TEXT_ROLE_CAPTION:
        style.font = canvas ? fonts.node_small : fonts.small;
        style.size = (float)(canvas ? fonts.node_small_size : fonts.small_size);
        break;
    case TEXT_ROLE_BODY:
    default:
        style.font = canvas ? fonts.node_body : fonts.body;
        style.size = (float)(canvas ? fonts.node_body_size : fonts.body_size);
        break;
    }
    style.line_height = floorf(style.size * 1.4f + 0.5f);
    return style;
}

void DrawUiText(TextRole role, bool canvas, const char *text, float x, float y, Color color) {
    UiTextStyle style = GetUiTextStyle(role, canvas);
    DrawInterfaceText(style.font, text, x, y, style.size, color);
}

Vector2 MeasureUiText(TextRole role, bool canvas, const char *text) {
    UiTextStyle style = GetUiTextStyle(role, canvas);
    Vector2 measured = MeasureTextEx(style.font, text, style.size, 0);
    measured.y = style.line_height;
    return measured;
}

void SetGuiScale(float scale) {
    GuiSetFont(fonts.gui);
    int text_size = (int)ScaledFontSize(GUI_TEXT_SIZE, scale);
    GuiSetStyle(DEFAULT, TEXT_SIZE, text_size);
    g_gui_valign_offset = (int)FontTextCenterOffset(fonts.gui, (float)text_size);
    GuiSetStyle(DEFAULT, TEXT_PADDING, (int)(4.0f * scale + 0.5f));
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, (int)(24.0f * scale + 0.5f));
    GuiSetStyle(LISTVIEW, SCROLLBAR_WIDTH, (int)(14.0f * scale + 0.5f));
    GuiSetIconScale(scale >= 1.5f ? 2 : 1);
}

void SetCodeGuiScale(float scale) {
    SetGuiScale(scale);
    GuiSetFont(fonts.mono);
    g_gui_valign_offset = (int)FontTextCenterOffset(fonts.mono, (float)GuiGetStyle(DEFAULT, TEXT_SIZE));
}

void SetNodeGuiScale(float scale) {
    GuiSetFont(fonts.node_gui);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)ScaledFontSize(GUI_TEXT_SIZE, scale));
    GuiSetStyle(DEFAULT, TEXT_PADDING, (int)(4.0f * scale + 0.5f));
}
