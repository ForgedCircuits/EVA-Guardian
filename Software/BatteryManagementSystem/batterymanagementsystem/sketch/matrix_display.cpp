/**
 * @file matrix_display.cpp
 * @brief Implementation of LED Matrix rendering for SOC display.
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

void init_matrix_display(void) {
  matrix.begin();
  matrix.setGrayscaleBits(3);
  matrix.clear();
}

void clear_matrix_buf(void) {
  memset(matrix_buf, 0, sizeof(matrix_buf));
}

void render_digit(int digit, int col_offset, uint8_t brightness) {
  if (digit < 0 || digit > 9) return;
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 5; c++) {
      int col = col_offset + c;
      if (col >= 0 && col < 13) {
        // Default orientation (0-degree rotation relative to base)
        int flipped_r = r;
        int flipped_col = col;
        matrix_buf[flipped_r][flipped_col] = DIGIT_FONT[digit][r][c] ? brightness : 0;
      }
    }
  }
}

void flush_matrix_locked(void) {
  matrix.draw(reinterpret_cast<uint8_t*>(matrix_buf));
}

void render_soc(float soc) {
  int soc_int = (int)(soc + 0.5f);
  if (soc_int > 99) soc_int = 99;
  if (soc_int < 0) soc_int = 0;

  k_mutex_lock(&matrix_mtx, K_FOREVER);
  clear_matrix_buf();

  if (soc_int >= 10) {
    int tens = soc_int / 10;
    int units = soc_int % 10;
    render_digit(tens, 1, 7);  // Use columns 1-5
    render_digit(units, 7, 7); // Use columns 7-11
  } else {
    // Center a single digit
    render_digit(soc_int, 4, 7);
  }

  flush_matrix_locked();
  k_mutex_unlock(&matrix_mtx);
}
