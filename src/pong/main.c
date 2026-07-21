#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "raylib.h"

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 450;
const float BALL_RADIUS = 12.0f;
const float PADDLE_WIDTH = 12.0f;
const float PADDLE_HEIGHT = 100.0f;
const float SCORE_POSITION_Y = 30.0f;
const float SCORE_FONT_SIZE = 40.0f;
const int MIDDLE_LINE_DASH_SIZE = 6;
const int MIDDLE_LINE_DASH_SPACE = 6;

const float PADDLE_SPEED = 10.0f;
const float BALL_SPEED = 10.0f;

typedef struct GameState {
    float ball_x;
    float ball_y;
    float ball_dx;
    float ball_dy;
    float paddle_left;
    float paddle_right;
    int score_left;
    int score_right;
    bool is_paused;
    bool is_next_turn_player_left;
} GameState;

typedef struct GameInput {
    bool move_paddle_left_down;
    bool move_paddle_left_up;
    bool move_paddle_right_down;
    bool move_paddle_right_up;
    bool start_game;
} GameInput;

GameState initGameState();
GameInput input();
void input_ai(const GameState *state, GameInput *input);
void reset_after_score(GameState *state, bool player_left_scored);
void update(GameInput input, GameState *state);
void draw(GameState state);

int main(void) {
    GameState state = initGameState();

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Pong");

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        GameInput in = input(&state);
        input_ai(&state, &in);
        update(in, &state);
        draw(state);
    }

    CloseWindow();

    return 0;
}

GameState initGameState() {
    GameState state = {
        .ball_x = (float)(SCREEN_WIDTH / 2),
        .ball_y = (float)(SCREEN_HEIGHT / 2),
        .ball_dx = 0.0f,
        .ball_dy = 0.0f,
        .paddle_left = 0.0f,
        .paddle_right = 0.0f,
        .score_left = 0,
        .score_right = 0,
        .is_paused = true,
        .is_next_turn_player_left = true,
    };
    return state;
}

GameInput input() {
    GameInput input = {0};

    if (IsKeyDown(KEY_W)) {
        input.move_paddle_left_up = true;
    }
    if (IsKeyDown(KEY_S)) {
        input.move_paddle_left_down = true;
    }
    if (IsKeyDown(KEY_SPACE)) {
        input.start_game = true;
    }
    if (IsKeyReleased(KEY_ONE)) {
        ToggleFullscreen();
    }

    return input;
}

void input_ai(const GameState *state, GameInput *input) {
    const float paddle_center = state->paddle_right + PADDLE_HEIGHT / 2;
    const float paddle_ball_diff = state->ball_y - paddle_center;
    const float threshold = PADDLE_SPEED * 4;
    if (paddle_ball_diff > threshold) {
        input->move_paddle_right_down = true;
    } else if (paddle_ball_diff < -threshold) {
        input->move_paddle_right_up = true;
    }
}

void reset_after_score(GameState *state, bool player_left_scored) {
    state->is_paused = true;
    if (player_left_scored) {
        state->score_left += 1;
    } else {
        state->score_right += 1;
    }
    state->ball_x = (float)(SCREEN_WIDTH / 2.0f);
    state->ball_y = (float)(SCREEN_HEIGHT / 2.0f);
    state->ball_dx = 0.0f;
    state->ball_dy = 0.0f;
}

void update(GameInput input, GameState *state) {
    if (input.start_game && state->is_paused) {
        if (state->is_next_turn_player_left) {
            state->ball_dx = -BALL_SPEED;
        } else {
            state->ball_dx = BALL_SPEED;
        }
        state->ball_dy = 0.0f;
        state->is_next_turn_player_left = !state->is_next_turn_player_left;
        state->is_paused = false;
    }

    const float paddle_max_down = SCREEN_HEIGHT - PADDLE_HEIGHT;

    if (input.move_paddle_left_down) {
        state->paddle_left =
            fminf(paddle_max_down, state->paddle_left + PADDLE_SPEED);
    }
    if (input.move_paddle_left_up) {
        state->paddle_left = fmaxf(0.0f, state->paddle_left - PADDLE_SPEED);
    }
    if (input.move_paddle_right_down) {
        state->paddle_right =
            fminf(paddle_max_down, state->paddle_right + PADDLE_SPEED);
    }
    if (input.move_paddle_right_up) {
        state->paddle_right = fmaxf(0.0f, state->paddle_right - PADDLE_SPEED);
    }

    state->ball_x += state->ball_dx;
    state->ball_y += state->ball_dy;

    const float ball_left = state->ball_x - BALL_RADIUS;
    const float ball_right = state->ball_x + BALL_RADIUS;
    const float ball_top = state->ball_y - BALL_RADIUS;
    const float ball_bottom = state->ball_y + BALL_RADIUS;

    const bool is_ball_touching_paddle_left =
        (ball_left >= 0.0f && ball_left <= PADDLE_WIDTH) &&
        (ball_bottom >= state->paddle_left &&
         ball_top <= state->paddle_left + PADDLE_HEIGHT);

    const bool is_ball_touching_paddle_right =
        (ball_right >= SCREEN_WIDTH - PADDLE_WIDTH &&
         ball_right <= SCREEN_WIDTH) &&
        (ball_bottom >= state->paddle_right &&
         ball_top <= state->paddle_right + PADDLE_HEIGHT);

    if (is_ball_touching_paddle_left || is_ball_touching_paddle_right) {
        const float speed = sqrtf(state->ball_dx * state->ball_dx +
                                  state->ball_dy * state->ball_dy);
        const float paddle_y = is_ball_touching_paddle_left
                                   ? state->paddle_left
                                   : state->paddle_right;
        const float paddle_center_y = paddle_y + (PADDLE_HEIGHT / 2.0f);
        const float hit_factor =
            (state->ball_y - paddle_center_y) / (PADDLE_HEIGHT / 2.0f);

        const float max_angle = 60.0f * DEG2RAD;
        const float bounce_angle = hit_factor * max_angle;

        state->ball_dy = speed * sinf(bounce_angle);

        state->ball_dx = -state->ball_dx;
        if (is_ball_touching_paddle_left) {
            state->ball_x = PADDLE_WIDTH + BALL_RADIUS;
        } else {
            state->ball_x = SCREEN_WIDTH - PADDLE_WIDTH - BALL_RADIUS;
        }
    }

    const bool is_score_right = state->ball_x - BALL_RADIUS < 0.0f;
    if (is_score_right) {
        reset_after_score(state, false);
    }

    const bool is_score_left = state->ball_x + BALL_RADIUS > SCREEN_WIDTH;
    if (is_score_left) {
        reset_after_score(state, true);
    }

    const bool is_hitting_top = state->ball_y - BALL_RADIUS <= 0.0f;
    if (is_hitting_top) {
        state->ball_y = BALL_RADIUS;
        state->ball_dy = -state->ball_dy;
    }

    const bool is_hitting_bottom = state->ball_y + BALL_RADIUS >= SCREEN_HEIGHT;
    if (is_hitting_bottom) {
        state->ball_y = SCREEN_HEIGHT - BALL_RADIUS;
        state->ball_dy = -state->ball_dy;
    }
}

void draw(GameState state) {
    BeginDrawing();

    ClearBackground(BLACK);

    Vector2 start = {SCREEN_WIDTH / 2.0f, 0};
    Vector2 end = {SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT};
    DrawLineDashed(start, end, MIDDLE_LINE_DASH_SIZE, MIDDLE_LINE_DASH_SPACE,
                   WHITE);

    const char *score =
        TextFormat("%d   %d", state.score_left, state.score_right);
    int score_text_size = MeasureText(score, SCORE_FONT_SIZE);
    DrawText(score, (int)((SCREEN_WIDTH - score_text_size) / 2),
             (int)SCORE_POSITION_Y, SCORE_FONT_SIZE, WHITE);

    Vector2 ball_position = {
        .x = state.ball_x,
        .y = state.ball_y,
    };
    DrawCircleV(ball_position, BALL_RADIUS, YELLOW);

    Rectangle paddle_rect = {
        .x = 0,
        .y = state.paddle_left,
        .width = PADDLE_WIDTH,
        .height = PADDLE_HEIGHT,
    };
    DrawRectangleRec(paddle_rect, WHITE);
    paddle_rect.x = SCREEN_WIDTH - PADDLE_WIDTH;
    paddle_rect.y = state.paddle_right;
    DrawRectangleRec(paddle_rect, WHITE);

    EndDrawing();
}