/**
 * @file matrix_display.h
 * @brief LED Matrix rendering and animation display module header.
 * @details Declares matrix hardware routines, 7-segment font renderer,
 *          and Bridge callback functions for speed and accident alerts.
 */

#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include <Arduino_LED_Matrix.h>
#include <zephyr/kernel.h>
#include "Arduino_RouterBridge.h"

/**
 * @brief Initializes LED Matrix hardware and registers Bridge providers.
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
 * @param col_offset Starting column index on the 8x13 grid.
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
 * @brief Renders a dynamic water level animation based on tilt angle.
 * @param roll_angle The tilt angle of the device in radians.
 * @return void
 */
void render_water_level(float roll_angle);

/**
 * @brief Displays blinking "X" alert symbol on accident detection.
 * @return void
 */
void show_alert(void);

#endif /* MATRIX_DISPLAY_H */
