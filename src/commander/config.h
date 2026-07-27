#pragma once

#include "raylib.h"

#define TOOLBAR_HEIGHT 52.0f
#define STATUS_HEIGHT 28.0f
#define NODE_HEADER_HEIGHT 42.0f
#define NODE_CONNECTOR_HEIGHT 58.0f
#define NODE_CONNECTOR_ROW_HEIGHT 22.0f
#define PORT_RADIUS 7.0f

#define TITLE_TEXT_SIZE 15
#define BODY_TEXT_SIZE 14
#define PORT_TEXT_SIZE 14
#define GUI_TEXT_SIZE 16

#define NODE_ZOOM_MIN 0.70f
#define NODE_ZOOM_MAX 2.00f
#define NODE_ZOOM_STEP 0.15f

#define APPLICATION_SCALE_MIN 1.00f
#define APPLICATION_SCALE_MAX 2.00f
#define APPLICATION_SCALE_STEP 0.25f

static const Color COLOR_CANVAS = {18, 21, 28, 255};
static const Color COLOR_GRID_MINOR = {31, 36, 46, 255};
static const Color COLOR_GRID_MAJOR = {43, 49, 62, 255};
static const Color COLOR_NODE = {35, 40, 51, 255};
static const Color COLOR_NODE_HEADER = {47, 54, 68, 255};
static const Color COLOR_NODE_SELECTED = {92, 170, 255, 255};
static const Color COLOR_STRING = {242, 178, 74, 255};
static const Color COLOR_STRING_LIST = {91, 207, 151, 255};
static const Color COLOR_TEXT = {225, 230, 239, 255};
static const Color COLOR_MUTED = {142, 151, 168, 255};
