/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#include "ws2812.h"

#define IS_RGBW false

void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

void neopixel_init(uint8_t pin) {
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    ws2812_program_init(pio, sm, offset, pin, 800000, IS_RGBW);
}
