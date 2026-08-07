#include "resize.h"

#include "raymath.h"

#include <math.h>

unsigned int ResizeEdgesAtPoint(Rectangle bounds, Vector2 point, float tolerance) {
    Rectangle hit_bounds = {bounds.x - tolerance, bounds.y - tolerance, bounds.width + tolerance * 2.0f,
                            bounds.height + tolerance * 2.0f};
    if (!CheckCollisionPointRec(point, hit_bounds)) {
        return RESIZE_EDGE_NONE;
    }

    unsigned int edges = RESIZE_EDGE_NONE;
    if (fabsf(point.x - bounds.x) <= tolerance) {
        edges |= RESIZE_EDGE_LEFT;
    } else if (fabsf(point.x - (bounds.x + bounds.width)) <= tolerance) {
        edges |= RESIZE_EDGE_RIGHT;
    }
    if (fabsf(point.y - bounds.y) <= tolerance) {
        edges |= RESIZE_EDGE_TOP;
    } else if (fabsf(point.y - (bounds.y + bounds.height)) <= tolerance) {
        edges |= RESIZE_EDGE_BOTTOM;
    }
    return edges;
}

int MouseCursorForResizeEdges(unsigned int edges) {
    bool horizontal = (edges & (RESIZE_EDGE_LEFT | RESIZE_EDGE_RIGHT)) != 0;
    bool vertical = (edges & (RESIZE_EDGE_TOP | RESIZE_EDGE_BOTTOM)) != 0;
    if (horizontal && vertical) {
        return MOUSE_CURSOR_RESIZE_ALL;
    }
    if (horizontal) {
        return MOUSE_CURSOR_RESIZE_EW;
    }
    if (vertical) {
        return MOUSE_CURSOR_RESIZE_NS;
    }
    return MOUSE_CURSOR_DEFAULT;
}

void BeginRectangleResize(ResizeState *state, Rectangle bounds, Vector2 mouse, unsigned int edges) {
    *state = (ResizeState){
        .active = edges != RESIZE_EDGE_NONE,
        .edges = edges,
        .start_mouse = mouse,
        .start_bounds = bounds,
    };
}

Rectangle RectangleResizeBounds(const ResizeState *state, Vector2 mouse, Vector2 minimum, Vector2 maximum) {
    Vector2 delta = {mouse.x - state->start_mouse.x, mouse.y - state->start_mouse.y};
    Rectangle start = state->start_bounds;
    Rectangle resized = start;
    float max_width = maximum.x > 0.0f ? maximum.x : INFINITY;
    float max_height = maximum.y > 0.0f ? maximum.y : INFINITY;

    if (state->edges & RESIZE_EDGE_LEFT) {
        float right = start.x + start.width;
        resized.width = Clamp(start.width - delta.x, minimum.x, max_width);
        resized.x = right - resized.width;
    } else if (state->edges & RESIZE_EDGE_RIGHT) {
        resized.width = Clamp(start.width + delta.x, minimum.x, max_width);
    }
    if (state->edges & RESIZE_EDGE_TOP) {
        float bottom = start.y + start.height;
        resized.height = Clamp(start.height - delta.y, minimum.y, max_height);
        resized.y = bottom - resized.height;
    } else if (state->edges & RESIZE_EDGE_BOTTOM) {
        resized.height = Clamp(start.height + delta.y, minimum.y, max_height);
    }
    return resized;
}
