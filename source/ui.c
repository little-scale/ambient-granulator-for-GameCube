#include "ui.h"

#include <ogcsys.h>

#include <stdint.h>
#include <string.h>

#define CELL_WIDTH 8
#define CELL_HEIGHT 8
#define LUMA_BLACK 16
#define LUMA_WHITE 235

static const uint8_t digit_font[12][7] = {
    { 14, 17, 19, 21, 25, 17, 14 },
    {  4, 12,  4,  4,  4,  4, 14 },
    { 14, 17,  1,  2,  4,  8, 31 },
    { 30,  1,  1, 14,  1,  1, 30 },
    {  2,  6, 10, 18, 31,  2,  2 },
    { 31, 16, 16, 30,  1,  1, 30 },
    { 14, 16, 16, 30, 17, 17, 14 },
    { 31,  1,  2,  4,  8,  8,  8 },
    { 14, 17, 17, 14, 17, 17, 14 },
    { 14, 17, 17, 15,  1,  1, 14 },
    {  0,  4,  4, 31,  4,  4,  0 },
    {  0,  0,  0, 31,  0,  0,  0 },
};

static const uint8_t alphabet_font[26][7] = {
    { 14, 17, 17, 31, 17, 17, 17 }, { 30, 17, 17, 30, 17, 17, 30 },
    { 14, 17, 16, 16, 16, 17, 14 }, { 30, 17, 17, 17, 17, 17, 30 },
    { 31, 16, 16, 30, 16, 16, 31 }, { 31, 16, 16, 30, 16, 16, 16 },
    { 14, 17, 16, 23, 17, 17, 15 }, { 17, 17, 17, 31, 17, 17, 17 },
    { 14,  4,  4,  4,  4,  4, 14 }, {  7,  2,  2,  2, 18, 18, 12 },
    { 17, 18, 20, 24, 20, 18, 17 }, { 16, 16, 16, 16, 16, 16, 31 },
    { 17, 27, 21, 21, 17, 17, 17 }, { 17, 25, 21, 19, 17, 17, 17 },
    { 14, 17, 17, 17, 17, 17, 14 }, { 30, 17, 17, 30, 16, 16, 16 },
    { 14, 17, 17, 17, 21, 18, 13 }, { 30, 17, 17, 30, 20, 18, 17 },
    { 15, 16, 16, 14,  1,  1, 30 }, { 31,  4,  4,  4,  4,  4,  4 },
    { 17, 17, 17, 17, 17, 17, 14 }, { 17, 17, 17, 17, 10, 10,  4 },
    { 17, 17, 17, 21, 21, 21, 10 }, { 17, 17, 10,  4, 10, 17, 17 },
    { 17, 17, 10,  4,  4,  4,  4 }, { 31,  1,  2,  4,  8, 16, 31 },
};

static const uint8_t percent_glyph[7] = { 17, 2, 4, 4, 8, 16, 17 };
static const uint8_t slash_glyph[7] = { 1, 2, 2, 4, 8, 8, 16 };
static const uint8_t colon_glyph[7] = { 0, 12, 12, 0, 12, 12, 0 };
static const uint8_t dot_glyph[7] = { 0, 0, 0, 0, 0, 12, 12 };

static const uint8_t *font_glyph(char character)
{
    if (character >= '0' && character <= '9')
        return digit_font[character - '0'];
    if (character >= 'A' && character <= 'Z')
        return alphabet_font[character - 'A'];
    if (character >= 'a' && character <= 'z')
        return alphabet_font[character - 'a'];
    if (character == '+')
        return digit_font[10];
    if (character == '-')
        return digit_font[11];
    if (character == '%')
        return percent_glyph;
    if (character == '/')
        return slash_glyph;
    if (character == ':')
        return colon_glyph;
    if (character == '.')
        return dot_glyph;
    return NULL;
}

static u32 grayscale_pair(bool white)
{
    return white ? COLOR_WHITE : COLOR_BLACK;
}

bool ui_init(Ui *ui)
{
    memset(ui, 0, sizeof(*ui));
    VIDEO_Init();
    PAD_Init();
    ui->mode = VIDEO_GetPreferredMode(NULL);
    if (ui->mode == NULL)
        return false;
    ui->width = ui->mode->fbWidth;
    ui->height = ui->mode->xfbHeight;
    for (int index = 0; index < 2; index++) {
        void *framebuffer = SYS_AllocateFramebuffer(ui->mode);
        if (framebuffer == NULL)
            return false;
        ui->framebuffers[index] = MEM_K0_TO_K1(framebuffer);
        VIDEO_ClearFrameBuffer(ui->mode, ui->framebuffers[index],
                               COLOR_BLACK);
    }
    VIDEO_Configure(ui->mode);
    VIDEO_SetNextFramebuffer(ui->framebuffers[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (ui->mode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
    ui->draw_index = 1;
    return true;
}

void ui_wait_vsync(void)
{
    VIDEO_WaitVSync();
}

void ui_begin(Ui *ui, int logical_width, bool white_background)
{
    ui->logical_width = logical_width;
    ui->draw_buffer = ui->framebuffers[ui->draw_index];
    u32 pair = grayscale_pair(white_background);
    for (int index = 0; index < ui->width * ui->height / 2; index++)
        ui->draw_buffer[index] = pair;
}

void ui_present(Ui *ui)
{
    VIDEO_SetNextFramebuffer(ui->draw_buffer);
    VIDEO_Flush();
    ui->draw_index ^= 1;
}

void ui_shutdown(Ui *ui)
{
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    memset(ui, 0, sizeof(*ui));
}

void ui_draw_rect(Ui *ui, int x, int y, int width, int height,
                  bool white)
{
    if (width <= 0 || height <= 0 || ui->draw_buffer == NULL)
        return;
    int x0 = x * ui->width / ui->logical_width;
    int x1 = (x + width) * ui->width / ui->logical_width;
    int y0 = y * ui->height / UI_LOGICAL_HEIGHT;
    int y1 = (y + height) * ui->height / UI_LOGICAL_HEIGHT;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > ui->width)
        x1 = ui->width;
    if (y1 > ui->height)
        y1 = ui->height;
    if (x0 >= x1 || y0 >= y1)
        return;
    uint32_t luma = white ? LUMA_WHITE : LUMA_BLACK;
    for (int physical_y = y0; physical_y < y1; physical_y++) {
        u32 *row = ui->draw_buffer + physical_y * ui->width / 2;
        for (int physical_x = x0; physical_x < x1; physical_x++) {
            int pair_x = physical_x / 2;
            u32 pair = row[pair_x];
            if ((physical_x & 1) == 0)
                pair = (pair & UINT32_C(0x0000FFFF))
                     | (luma << 24) | UINT32_C(0x00800000);
            else
                pair = (pair & UINT32_C(0xFFFF0000))
                     | (luma << 8) | UINT32_C(0x00000080);
            row[pair_x] = pair;
        }
    }
}

static void draw_text_ink(Ui *ui, int column, int row, const char *text,
                          bool white_ink, bool background)
{
    for (int index = 0; text[index] != '\0'; index++) {
        int origin_x = (column + index) * CELL_WIDTH;
        int origin_y = row * CELL_HEIGHT;
        if (background)
            ui_draw_rect(ui, origin_x, origin_y,
                         CELL_WIDTH, CELL_HEIGHT, !white_ink);
        const uint8_t *glyph = font_glyph(text[index]);
        if (glyph == NULL)
            continue;
        for (int glyph_y = 0; glyph_y < 7; glyph_y++) {
            for (int glyph_x = 0; glyph_x < 5; glyph_x++) {
                if (glyph[glyph_y] & (1U << (4 - glyph_x)))
                    ui_draw_rect(ui, origin_x + 1 + glyph_x,
                                 origin_y + glyph_y, 1, 1, white_ink);
            }
        }
    }
}

void ui_draw_text(Ui *ui, int column, int row, const char *text,
                  bool inverted)
{
    draw_text_ink(ui, column, row, text, inverted, inverted);
}

void ui_draw_light_text(Ui *ui, int column, int row, const char *text)
{
    draw_text_ink(ui, column, row, text, true, false);
}

void ui_draw_rule(Ui *ui, int x, int y, int width, bool white)
{
    ui_draw_rect(ui, x, y, width, 1, white);
}

void ui_draw_section_header(Ui *ui, int column, int row,
                            const char *text)
{
    ui_draw_text(ui, column, row, text, false);
    int start = (column + (int)strlen(text) + 1) * CELL_WIDTH;
    int end = (column + 18) * CELL_WIDTH;
    if (end > start)
        ui_draw_rule(ui, start, row * CELL_HEIGHT + 7,
                     end - start, false);
}
