#pragma once

#include "raylib.h"

typedef struct {
    Font title;
    Font body;
    Font small;
    Font gui;
    Font node_body;
    Font node_small;
    Font node_gui;
    bool custom_loaded;
    int title_size;
    int body_size;
    int small_size;
    int gui_size;
    int node_body_size;
    int node_small_size;
    int node_gui_size;
} InterfaceFonts;

extern InterfaceFonts fonts;

void LoadInterfaceFonts(void);
void UpdateInterfaceFontScale(float application_scale, float canvas_scale);
void UnloadInterfaceFonts(void);
void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color);
float ScaledFontSize(float size, float scale);
void SetGuiScale(float scale);
void SetNodeGuiScale(float scale);
