/**
 * @file matrix_display.cpp
 * @brief Implementation of LED Matrix font rendering and animation displays.
 * @details Implements 7-segment digit drawing, Zephyr mutex locking, and Bridge providers.
 */

#include "matrix_display.h"

static Arduino_LED_Matrix matrix;
K_MUTEX_DEFINE(matrix_mtx);

static uint8_t matrix_buf[8][13];

/**
 * @brief 7-segment style pixel font definition (7 rows × 5 columns per digit).
 */
static const uint8_t DIGIT_FONT[10][7][5] = {
  /* 0 */
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  /* 1 */
  {{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,0,1,0,0},{0,1,1,1,0}},
  /* 2 */
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,0,1,1,0},{0,1,0,0,0},{1,0,0,0,0},{1,1,1,1,1}},
  /* 3 */
  {{0,1,1,1,0},{1,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0},{0,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  /* 4 */
  {{1,0,0,1,0},{1,0,0,1,0},{1,0,0,1,0},{1,1,1,1,1},{0,0,0,1,0},{0,0,0,1,0},{0,0,0,1,0}},
  /* 5 */
  {{1,1,1,1,1},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{0,0,0,0,1},{0,0,0,0,1},{1,1,1,1,0}},
  /* 6 */
  {{0,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  /* 7 */
  {{1,1,1,1,1},{0,0,0,0,1},{0,0,0,1,0},{0,0,1,0,0},{0,1,0,0,0},{0,1,0,0,0},{0,1,0,0,0}},
  /* 8 */
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,0}},
  /* 9 */
  {{0,1,1,1,0},{1,0,0,0,1},{1,0,0,0,1},{0,1,1,1,1},{0,0,0,0,1},{0,0,0,0,1},{0,1,1,1,0}},
};

/**
 * @brief 8x13 "X" Symbol glyph pattern for accident alerts.
 */
static const uint8_t X_SYMBOL[8][13] = {
  {0,7,7,0,0,0,0,0,0,0,7,7,0},
  {0,0,7,7,0,0,0,0,0,7,7,0,0},
  {0,0,0,7,7,0,0,0,7,7,0,0,0},
  {0,0,0,0,7,7,7,7,7,0,0,0,0},
  {0,0,0,0,7,7,7,7,7,0,0,0,0},
  {0,0,0,7,7,0,0,0,7,7,0,0,0},
  {0,0,7,7,0,0,0,0,0,7,7,0,0},
  {0,7,7,0,0,0,0,0,0,0,7,7,0}
};

/**
 * @brief Initializes matrix peripheral and registers Router Bridge providers.
 * @return void
 */
void init_matrix_display(void) {
  matrix.begin();
  matrix.setGrayscaleBits(3);
  matrix.clear();

  Bridge.provide("show_speed", show_speed);
  Bridge.provide("show_alert", show_alert);
}

/**
 * @brief Clears matrix frame buffer memory.
 * @return void
 */
void clear_matrix_buf(void) {
  memset(matrix_buf, 0, sizeof(matrix_buf));
}

/**
 * @brief Renders a single digit into matrix buffer.
 * @param digit Digit value (0-9).
 * @param col_offset Grid column starting position.
 * @param brightness Pixel intensity.
 * @return void
 */
void render_digit(int digit, int col_offset, uint8_t brightness) {
  if (digit < 0 || digit > 9) return;
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 5; c++) {
      int col = col_offset + c;
      if (col >= 0 && col < 13) {
        matrix_buf[r][col] = DIGIT_FONT[digit][r][c] ? brightness : 0;
      }
    }
  }
}

/**
 * @brief Flushes matrix buffer to screen.
 * @return void
 */
void flush_matrix_locked(void) {
  matrix.draw(reinterpret_cast<uint8_t*>(matrix_buf));
}

/**
 * @brief Bridge provider callback to display vehicle speed.
 * @param speed Speed magnitude integer.
 * @return void
 */
void show_speed(int speed) {
  k_mutex_lock(&matrix_mtx, K_FOREVER);
  clear_matrix_buf();

  if (speed < 0)  speed = 0;
  if (speed > 99) speed = 99;

  if (speed < 10) {
    render_digit(speed, 4);
  } else {
    render_digit(speed / 10, 1);
    render_digit(speed % 10, 7);
  }

  flush_matrix_locked();
  k_mutex_unlock(&matrix_mtx);
}

/**
 * @brief Bridge provider callback to display flashing accident alert symbol.
 * @return void
 */
void show_alert(void) {
  for (int flash = 0; flash < 5; flash++) {
    k_mutex_lock(&matrix_mtx, K_FOREVER);
    memcpy(matrix_buf, X_SYMBOL, sizeof(matrix_buf));
    flush_matrix_locked();
    k_mutex_unlock(&matrix_mtx);
    delay(250);

    k_mutex_lock(&matrix_mtx, K_FOREVER);
    clear_matrix_buf();
    flush_matrix_locked();
    k_mutex_unlock(&matrix_mtx);
    delay(250);
  }
}
