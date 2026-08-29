// Pre-optimization C snapshot for differential tests; not compiled into firmware.
static void render_fluid_settings_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_done == NULL) {
        return;
    }

    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = s_lcd_stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = rgb565_for_sh8601(fluid_settings_pixel(x, y));
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, s_lcd_stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fluid settings transfer failed: %s", esp_err_to_name(err));
            return;
        }
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "Fluid settings DMA completion timed out");
            return;
        }
    }
    s_settings_dirty = false;
}
static void render_fluid_colour_picker_frame(void)
{
    if (s_lcd_panel == NULL || s_lcd_stripe == NULL || s_lcd_done == NULL) {
        return;
    }

    for (int stripe_y = 0; stripe_y < LCD_HEIGHT; stripe_y += LCD_STRIPE_LINES) {
        int stripe_end = clamp_int(stripe_y + LCD_STRIPE_LINES, 0, LCD_HEIGHT);
        for (int y = stripe_y; y < stripe_end; ++y) {
            uint16_t *line = s_lcd_stripe + (size_t)(y - stripe_y) * LCD_WIDTH;
            for (int x = 0; x < LCD_WIDTH; ++x) {
                line[x] = rgb565_for_sh8601(fluid_colour_picker_pixel(x, y));
            }
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, stripe_y,
                                                   LCD_WIDTH, stripe_end, s_lcd_stripe);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Colour palette transfer failed: %s", esp_err_to_name(err));
            return;
        }
        if (xSemaphoreTake(s_lcd_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            ESP_LOGE(TAG, "Colour palette DMA completion timed out");
            return;
        }
    }
    s_colour_picker_dirty = false;
}

static uint16_t fluid_settings_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }

    uint16_t colour = rgb565(0, 0, 0);
    const uint16_t primary = rgb565(10, 132, 255);
    const uint16_t text = rgb565(245, 245, 247);
    const uint16_t muted = rgb565(142, 142, 147);
    const uint16_t selected = rgb565(16, 78, 139);
    const uint16_t panel = rgb565(28, 28, 30);
    const uint16_t border = rgb565(58, 58, 60);

    if (y == 96 && x >= 70 && x <= LCD_WIDTH - 71) {
        colour = border;
    }

    const int button_top[] = {112, 182, 252};
    for (int index = 0; index < 3; ++index) {
        const int top = button_top[index];
        const bool active = (int)s_fluid_shape == index;
        if (home_in_round_rect(x, y, 70, top, LCD_WIDTH - 71, top + 59, 16)) {
            bool button_border = !home_in_round_rect(x, y, 73, top + 3,
                                                      LCD_WIDTH - 74, top + 56, 13);
            colour = button_border ? (active ? primary : panel) :
                     (active ? selected : panel);
        }

        /* Three self-explanatory, text-free mode glyphs: liquid / blocks /
         * matrix.  The wide cards remain easy to tap on the round display. */
        const int glyph_x = center_x;
        const int glyph_y = top + 29;
        const int glyph_dx = x - glyph_x;
        const int glyph_dy = y - glyph_y;
        const uint16_t glyph_colour = active ? text : muted;
        if (index == FLUID_SHAPE_SIMPLE) {
            const int droplet_distance2 = glyph_dx * glyph_dx + (glyph_dy + 2) * (glyph_dy + 2);
            const bool droplet_tip = glyph_dy >= -15 && glyph_dy <= -5 &&
                                     (glyph_dx < 0 ? -glyph_dx : glyph_dx) <= glyph_dy + 15;
            if (droplet_distance2 <= 12 * 12 || droplet_tip) {
                colour = glyph_colour;
            }
        } else if (index == FLUID_SHAPE_BLOCKS) {
            const bool block = ((glyph_dx >= -17 && glyph_dx <= -5 && glyph_dy >= -10 && glyph_dy <= 2) ||
                                (glyph_dx >= 4 && glyph_dx <= 16 && glyph_dy >= -10 && glyph_dy <= 2) ||
                                (glyph_dx >= -6 && glyph_dx <= 6 && glyph_dy >= 7 && glyph_dy <= 19));
            if (block) {
                colour = glyph_colour;
            }
        } else {
            const int grid_x = glyph_dx + 14;
            const int grid_y = glyph_dy + 14;
            if (grid_x >= 0 && grid_x < 30 && grid_y >= 0 && grid_y < 30 &&
                (grid_x % 10) < 6 && (grid_y % 10) < 6) {
                colour = glyph_colour;
            }
        }
    }

    const int colour_top = 342;
    if (home_in_round_rect(x, y, 70, colour_top, LCD_WIDTH - 71, 424, 20)) {
        const bool button_border = !home_in_round_rect(x, y, 73, colour_top + 3,
                                                        LCD_WIDTH - 74, 421, 17);
        colour = button_border ? panel : panel;
        const int preview_dx = x - center_x;
        const int preview_dy = y - 383;
        const int preview_distance2 = preview_dx * preview_dx + preview_dy * preview_dy;
        if (preview_distance2 <= 28 * 28) {
            const liquid_palette_t *palette = active_palette();
            colour = rgb565(palette->body[0], palette->body[1], palette->body[2]);
        } else if (preview_distance2 <= 32 * 32) {
            colour = text;
        }
        /* Navigation chevron without an accompanying word label. */
        if (x >= 350 && x <= 374 && y >= 371 && y <= 395 &&
            ((x >= 366 && ((y - 383) < 3 && (y - 383) > -3)) ||
             (x >= 354 && x <= 366 && ((y - 383) == (x - 354) / 2 ||
                                        (y - 383) == -(x - 354) / 2)))) {
            colour = text;
        }
    }

    /* Circular icon-only navigation is the watchOS convention.  The larger
     * invisible y<92 hit area remains unchanged for reliable touch. */
    if (home_in_circle(x, y, 86, 73, 24)) {
        colour = rgb565(28, 28, 30);
    }
    const bool back_arrow = (x >= 78 && x <= 102 && y >= 65 && y <= 81 &&
                             ((x <= 84 && ((y - 73) < 3 && (y - 73) > -3)) ||
                              (x >= 84 && x <= 100 && y >= 71 && y <= 75)));
    if (back_arrow) {
        colour = text;
    }
    uint8_t alpha = 0;
    alpha = home_font_alpha(x, y, 187, 61, &lv_font_montserrat_28, "FLUID");
    if (alpha != 0) {
        return rgb565_blend(colour, text, alpha);
    }
    alpha = home_font_alpha(x, y, 74, 91, &lv_font_montserrat_18, "MODE");
    if (alpha != 0) {
        return rgb565_blend(colour, muted, alpha);
    }
    alpha = home_font_alpha(x, y, 70, 330, &lv_font_montserrat_18, "COLOR");
    if (alpha != 0) {
        return rgb565_blend(colour, muted, alpha);
    }
    return colour;
}

static uint16_t fluid_colour_picker_pixel(int x, int y)
{
    const int center_x = LCD_WIDTH / 2;
    const int center_y = LCD_HEIGHT / 2;
    if (!home_in_circle(x, y, center_x, center_y, 230)) {
        return 0;
    }

    uint16_t colour = rgb565(0, 0, 0);
    const uint16_t text = rgb565(245, 245, 247);
    const int wheel_x = center_x;
    const int wheel_y = 270;
    const int wheel_radius = 150;
    const int dx = x - wheel_x;
    const int dy = y - wheel_y;
    const int distance2 = dx * dx + dy * dy;

    if (y == 106 && x >= 70 && x <= LCD_WIDTH - 71) {
        colour = rgb565(58, 58, 60);
    }

    if (distance2 <= wheel_radius * wheel_radius) {
        const float two_pi = 6.283185307f;
        float hue = atan2f((float)dy, (float)dx) / two_pi;
        if (hue < 0.0f) {
            hue += 1.0f;
        }
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        hsv_to_rgb(hue, sqrtf((float)distance2) / (float)wheel_radius,
                   0.96f, &red, &green, &blue);
        colour = rgb565(red, green, blue);

        const int marker_x = wheel_x + (int)(cosf(s_colour_hue * two_pi) *
                                               s_colour_saturation *
                                               (float)(wheel_radius - 6));
        const int marker_y = wheel_y + (int)(sinf(s_colour_hue * two_pi) *
                                               s_colour_saturation *
                                               (float)(wheel_radius - 6));
        const int marker_dx = x - marker_x;
        const int marker_dy = y - marker_y;
        const int marker_distance2 = marker_dx * marker_dx + marker_dy * marker_dy;
        if (marker_distance2 <= 36 && marker_distance2 >= 16) {
            colour = rgb565(246, 252, 255);
        } else if (marker_distance2 < 16) {
            colour = rgb565(2, 9, 20);
        }
    }

    if (home_in_circle(x, y, 86, 73, 24)) {
        colour = rgb565(28, 28, 30);
    }
    /* A compact icon-only chevron preserves most of the wheel area. */
    const bool back_arrow = (x >= 75 && x <= 101 && y >= 65 && y <= 81 &&
                             ((x <= 82 && ((y - 73) < 3 && (y - 73) > -3)) ||
                              (x >= 82 && x <= 99 && y >= 71 && y <= 75)));
    if (back_arrow) {
        colour = text;
    }
    uint8_t alpha = home_font_alpha(x, y, 195, 61, &lv_font_montserrat_28, "COLOR");
    if (alpha != 0) {
        return rgb565_blend(colour, text, alpha);
    }
    return colour;
}
