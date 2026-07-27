#pragma once

#include "raylib.h"

typedef struct {
    Font title;
    Font body;
    Font gui;
    bool custom_loaded;
} InterfaceFonts;

extern InterfaceFonts fonts;

void LoadInterfaceFonts(void);
void UnloadInterfaceFonts(void);
void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color);
