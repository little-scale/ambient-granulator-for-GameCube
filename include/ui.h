#ifndef GC_GRANULATOR_UI_H
#define GC_GRANULATOR_UI_H

#include <stdbool.h>

#include <gccore.h>

#define UI_LOGICAL_HEIGHT 240

typedef struct {
    GXRModeObj *mode;
    u32 *framebuffers[2];
    u32 *draw_buffer;
    int draw_index;
    int width;
    int height;
    int logical_width;
} Ui;

bool ui_init(Ui *ui);
void ui_wait_vsync(void);
void ui_begin(Ui *ui, int logical_width, bool white_background);
void ui_present(Ui *ui);
void ui_shutdown(Ui *ui);
void ui_draw_rect(Ui *ui, int x, int y, int width, int height,
                  bool white);
void ui_draw_text(Ui *ui, int column, int row, const char *text,
                  bool inverted);
void ui_draw_light_text(Ui *ui, int column, int row, const char *text);
void ui_draw_rule(Ui *ui, int x, int y, int width, bool white);
void ui_draw_section_header(Ui *ui, int column, int row,
                            const char *text);

#endif
