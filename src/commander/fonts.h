#pragma once

#include "raylib.h"

typedef enum {
    TEXT_ROLE_TITLE,
    TEXT_ROLE_BODY,
    TEXT_ROLE_LABEL,
    TEXT_ROLE_CODE,
    TEXT_ROLE_CAPTION,
} TextRole;

typedef struct {
    Font font;
    float size;
    float line_height;
} UiTextStyle;

typedef struct {
    Font title;
    Font body;
    Font small;
    Font mono;
    Font gui;
    Font node_body;
    Font node_small;
    Font node_mono;
    Font node_gui;
    bool custom_loaded;
    int title_size;
    int body_size;
    int small_size;
    int mono_size;
    int gui_size;
    int node_body_size;
    int node_small_size;
    int node_mono_size;
    int node_gui_size;
    float dpi_scale;
} InterfaceFonts;

extern InterfaceFonts fonts;

void LoadInterfaceFonts(void);
void UpdateInterfaceFontScale(float application_scale, float canvas_scale);
void UnloadInterfaceFonts(void);
void DrawInterfaceText(Font font, const char *text, float x, float y, float size, Color color);
UiTextStyle GetUiTextStyle(TextRole role, bool canvas);
void DrawUiText(TextRole role, bool canvas, const char *text, float x, float y, Color color);
Vector2 MeasureUiText(TextRole role, bool canvas, const char *text);
float ScaledFontSize(float size, float scale);
float FontTextCenterOffset(Font font, float box_height);
void SetGuiScale(float scale);
void SetCodeGuiScale(float scale);
void SetNodeGuiScale(float scale);
