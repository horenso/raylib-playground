#pragma once

#include "raylib.h"

enum {
    RESIZE_EDGE_NONE = 0,
    RESIZE_EDGE_LEFT = 1 << 0,
    RESIZE_EDGE_RIGHT = 1 << 1,
    RESIZE_EDGE_TOP = 1 << 2,
    RESIZE_EDGE_BOTTOM = 1 << 3,
};

typedef struct {
    bool active;
    unsigned int edges;
    Vector2 start_mouse;
    Rectangle start_bounds;
} ResizeState;

unsigned int ResizeEdgesAtPoint(Rectangle bounds, Vector2 point, float tolerance);
int MouseCursorForResizeEdges(unsigned int edges);
void BeginRectangleResize(ResizeState *state, Rectangle bounds, Vector2 mouse, unsigned int edges);
Rectangle RectangleResizeBounds(const ResizeState *state, Vector2 mouse, Vector2 minimum, Vector2 maximum);
