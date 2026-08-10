/**
 * @file matrix_display.h
 * @brief LED Matrix rendering for Battery Management System.
 */

#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include <Arduino_LED_Matrix.h>
#include <zephyr/kernel.h>

/**
 * @brief Initializes LED Matrix hardware.
 * @return void
 */
void init_matrix_display(void);

/**
 * @brief Clears the internal matrix frame buffer.
 * @return void
 */
void clear_matrix_buf(void);

/**
 * @brief Renders a single 7-segment digit onto the matrix buffer.
 * @param digit Value to display (0 to 9).
 * @param col_offset Starting column index on the grid.
 * @param brightness Pixel intensity level (0 to 7).
 * @return void
 */
void render_digit(int digit, int col_offset, uint8_t brightness = 7);

/**
 * @brief Flushes the frame buffer to the matrix display under mutex lock.
 * @return void
 */
void flush_matrix_locked(void);

/**
 * @brief Renders the State of Charge (SOC) percentage on the matrix.
 * @param soc The battery state of charge (0.0 to 100.0).
 * @return void
 */
void render_soc(float soc);

#endif /* MATRIX_DISPLAY_H */
