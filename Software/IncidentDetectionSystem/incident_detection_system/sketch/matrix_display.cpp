/**
 * @file matrix_display.cpp
 * @brief Implementation of LED Matrix font rendering and animation displays.
 * @details Implements 7-segment digit drawing, Zephyr mutex locking, and Bridge providers.
 */

#include "matrix_display.h"
#include <math.h>

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
 * @brief Initializes matrix peripheral and registers Router Bridge providers.
 * @return void
 */
void init_matrix_display(void) {
  matrix.begin();
  matrix.setGrayscaleBits(3);
  matrix.clear();

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
 * @brief Renders an animated water level based on physical device tilt.
 * @param roll_angle Device tilt angle in radians.
 * @return void
 */
void render_water_level(float roll_angle) {
  k_mutex_lock(&matrix_mtx, K_FOREVER);
  clear_matrix_buf();

  // Draw bucket container borders (left, right, bottom)
  for (int y = 0; y < 8; y++) {
    matrix_buf[y][0] = 7;   // Left border
    matrix_buf[y][12] = 7;  // Right border (shifted to cell 12)
  }
  for (int x = 0; x < 13; x++) {
    matrix_buf[7][x] = 7;   // Bottom border
  }

  // Matrix geometry: 8 rows (0-7), interior columns (1-11)
  // We want the surface to pivot around the center (13 cols -> center is 6.0).
  // Shift cy lower (e.g. 4.5) to simulate ~40% full
  float cx = 6.0f;
  float cy = 4.5f;

  for (int x = 1; x < 12; x++) {
    // Calculate the row index of the water surface for this column
    // Adding 0.5 for rounding to nearest integer pixel
    int surface_y = (int)(cy + tan(roll_angle) * (x - cx) + 0.5f);
    
    // Fill water below the surface line, stopping before bottom border
    for (int y = 0; y < 7; y++) {
      if (y >= surface_y) {
        matrix_buf[y][x] = 4; // Use slightly dimmer brightness (4) for water to contrast with border
      }
    }
  }

  flush_matrix_locked();
  k_mutex_unlock(&matrix_mtx);
}



/**
 * @brief Bridge provider callback to display flashing accident alert symbol.
 * @return void
 */
void show_alert(void) {
  // Deliberately empty, accident symbol removed from matrix
}
