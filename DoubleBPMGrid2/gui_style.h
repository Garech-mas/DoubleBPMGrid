#pragma once

#include <windows.h>
#include <string>

struct CONFIG_HANDLE;

namespace gui_style {

struct ControlIds {
	int static_bpm_rate;
	int static_basetime;
	int button_div_input;
	int button_mul_input;
	int button_mul2;
	int button_div2;
	int button_mul3;
	int button_div3;
	int button_reset;
	int button_measure;
	int button_basetime_minus;
	int button_basetime_plus;
	int button_add_bpm_grid;
};

void initialize(CONFIG_HANDLE* config, const ControlIds& ids);
HFONT font();
HBRUSH background_brush();

void apply_child(HWND hwnd);
void apply_button(HWND hwnd);
void apply_edit(HWND hwnd);

bool set_window_text_if_changed(HWND hwnd, const std::wstring& text);
bool text_fits(HWND hwnd, const std::wstring& text, int horizontal_padding = 4);
void redraw_child(HWND parent, int id);
void redraw_parent_background(HWND hwnd);

LRESULT on_ctl_color_static(HDC hdc);
LRESULT on_ctl_color_edit(HDC hdc);
LRESULT on_ctl_color_button(HDC hdc);
bool erase_background(HWND hwnd, HDC hdc);
void paint_background(HWND hwnd);
bool draw_item(const DRAWITEMSTRUCT* item);

} // namespace gui_style
