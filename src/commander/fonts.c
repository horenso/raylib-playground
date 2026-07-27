#include "fonts.h"
#include "raylib.h"
#include "types.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

InterfaceFonts fonts = {0};

void LoadInterfaceFonts(void) {
    const char *candidates[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    };
    const char *path = NULL;
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        if (FileExists(candidates[i])) {
            path = candidates[i];
            break;
        }
    }

    if (path) {
        fonts.title = LoadFontEx(path, TITLE_TEXT_SIZE, NULL, 0);
        fonts.body = LoadFontEx(path, BODY_TEXT_SIZE, NULL, 0);
        fonts.gui = LoadFontEx(path, GUI_TEXT_SIZE, NULL, 0);
        fonts.custom_loaded = fonts.title.texture.id > 0 && fonts.body.texture.id > 0 && fonts.gui.texture.id > 0;
    }
    if (!fonts.custom_loaded) {
        fonts.title = GetFontDefault();
        fonts.body = GetFontDefault();
        fonts.gui = GetFontDefault();
    }
    GuiSetFont(fonts.gui);
}

void UnloadInterfaceFonts(void) {
    if (fonts.custom_loaded) {
        UnloadFont(fonts.title);
        UnloadFont(fonts.body);
        UnloadFont(fonts.gui);
    }
}

void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color) {
    DrawTextEx(font, text, (Vector2){x, y}, size, 0, color);
}
