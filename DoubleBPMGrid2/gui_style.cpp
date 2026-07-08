#include "gui_style.h"

#include <cmath>
#include <vector>

#include "config2.h"

namespace gui_style {
namespace {

struct GuiTheme {
	COLORREF background;
	COLORREF text;
	COLORREF muted_text;
	COLORREF edit_background;
	COLORREF button_background;
	COLORREF button_hot;
	COLORREF button_pressed;
	COLORREF border;
	COLORREF focus_border;
	COLORREF disabled_text;
};

const GuiTheme k_fallback_theme = {
	RGB(0x20, 0x20, 0x20), // Background
	RGB(0xff, 0xff, 0xff), // Text
	RGB(0x90, 0x90, 0x90), // TextDisable
	RGB(0x20, 0x20, 0x20), // Background
	RGB(0x60, 0x60, 0x60), // ButtonBody
	RGB(0x80, 0x80, 0x80), // ButtonBodyHover
	RGB(0xa0, 0xa0, 0xa0), // ButtonBodyPress
	RGB(0x90, 0x90, 0x90), // Border
	RGB(0x80, 0x80, 0xe0), // BorderFocus
	RGB(0x90, 0x90, 0x90), // TextDisable
};

constexpr const wchar_t* k_fallback_font_family = L"Yu Gothic UI";
constexpr float k_fallback_control_font_size = 13.0f;

CONFIG_HANDLE* g_config = nullptr;
ControlIds g_ids = {};
GuiTheme g_theme = k_fallback_theme;
HBRUSH g_background_brush = nullptr;
HBRUSH g_edit_background_brush = nullptr;
HFONT g_control_font = nullptr;
WNDPROC g_rate_edit_base_proc = nullptr;
WNDPROC g_button_base_proc = nullptr;
HWND g_hot_button = nullptr;
bool g_style_loaded = false;

int clamp_min(int value, int min_value) {
	return value < min_value ? min_value : value;
}

COLORREF color_from_style_code(int code) {
	return RGB((code >> 16) & 0xff, (code >> 8) & 0xff, code & 0xff);
}

COLORREF get_style_color(const char* key, COLORREF fallback) {
	if (!g_config || !g_config->get_color_code) return fallback;

	const int code = g_config->get_color_code(g_config, key);
	return color_from_style_code(code);
}

std::wstring get_config_font_family() {
	std::wstring family = k_fallback_font_family;
	if (!g_config || !g_config->get_font_info) return family;

	FONT_INFO* default_family = g_config->get_font_info(g_config, "DefaultFamily");
	if (default_family && default_family->name && default_family->name[0]) {
		family = default_family->name;
	}

	FONT_INFO* control = g_config->get_font_info(g_config, "Control");
	if (control && control->name && control->name[0]) {
		family = control->name;
	}
	return family;
}

float get_config_control_font_size() {
	if (!g_config || !g_config->get_font_info) return k_fallback_control_font_size;

	FONT_INFO* control = g_config->get_font_info(g_config, "Control");
	if (!control || !std::isfinite(control->size) || control->size <= 0.0f) {
		return k_fallback_control_font_size;
	}
	return control->size;
}

HFONT create_control_font() {
	const std::wstring family = get_config_font_family();
	const float size = get_config_control_font_size();
	if (family.empty() || size <= 0.0f) {
		return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	}

	HFONT font = CreateFontW(
		-(int)std::lround(size),
		0, 0, 0,
		FW_NORMAL,
		FALSE, FALSE, FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE,
		family.c_str()
	);
	return font ? font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

void ensure_gdi_resources() {
	if (!g_background_brush) {
		g_background_brush = CreateSolidBrush(g_theme.background);
	}
	if (!g_edit_background_brush) {
		g_edit_background_brush = CreateSolidBrush(g_theme.edit_background);
	}
	if (!g_control_font) {
		g_control_font = create_control_font();
	}
}

void draw_rect_outline(HDC hdc, const RECT& rc, COLORREF color) {
	HPEN pen = CreatePen(PS_SOLID, 1, color);
	if (!pen) return;

	HGDIOBJ old_pen = SelectObject(hdc, pen);
	HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
	Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
	SelectObject(hdc, old_brush);
	SelectObject(hdc, old_pen);
	DeleteObject(pen);
}

bool is_button_id(UINT id) {
	return id == (UINT)g_ids.button_div_input
		|| id == (UINT)g_ids.button_mul_input
		|| id == (UINT)g_ids.button_mul2
		|| id == (UINT)g_ids.button_div2
		|| id == (UINT)g_ids.button_mul3
		|| id == (UINT)g_ids.button_div3
		|| id == (UINT)g_ids.button_reset
		|| id == (UINT)g_ids.button_measure
		|| id == (UINT)g_ids.button_basetime_minus
		|| id == (UINT)g_ids.button_basetime_plus
		|| id == (UINT)g_ids.button_add_bpm_grid;
}

bool is_static_id(UINT id) {
	return id == (UINT)g_ids.static_bpm_rate || id == (UINT)g_ids.static_basetime;
}

void invalidate_button(HWND hwnd) {
	if (hwnd) {
		InvalidateRect(hwnd, nullptr, FALSE);
		UpdateWindow(hwnd);
	}
}

void set_hot_button(HWND hwnd) {
	if (g_hot_button == hwnd) return;

	HWND old_hot = g_hot_button;
	g_hot_button = hwnd;
	invalidate_button(old_hot);
	invalidate_button(g_hot_button);
}

bool draw_owner_button(const DRAWITEMSTRUCT* item) {
	if (!item || item->CtlType != ODT_BUTTON || !is_button_id(item->CtlID)) {
		return false;
	}

	const bool disabled = (item->itemState & ODS_DISABLED) != 0;
	const bool pressed = (item->itemState & ODS_SELECTED) != 0;
	const bool hot = ((item->itemState & ODS_HOTLIGHT) != 0) || (item->hwndItem == g_hot_button);
	const bool focused = (item->itemState & ODS_FOCUS) != 0;

	COLORREF background = g_theme.button_background;
	if (pressed) {
		background = g_theme.button_pressed;
	}
	else if (hot) {
		background = g_theme.button_hot;
	}

	RECT rc = item->rcItem;
	HBRUSH brush = CreateSolidBrush(background);
	if (brush) {
		FillRect(item->hDC, &rc, brush);
		DeleteObject(brush);
	}

	draw_rect_outline(item->hDC, rc, focused ? g_theme.focus_border : g_theme.border);

	wchar_t text[256] = {};
	GetWindowTextW(item->hwndItem, text, _countof(text));

	RECT text_rc = rc;
	InflateRect(&text_rc, -6, -2);
	if (pressed) {
		OffsetRect(&text_rc, 1, 1);
	}

	int old_bk_mode = SetBkMode(item->hDC, TRANSPARENT);
	COLORREF old_text_color = SetTextColor(item->hDC, disabled ? g_theme.disabled_text : g_theme.text);
	DrawTextW(item->hDC, text, -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetTextColor(item->hDC, old_text_color);
	SetBkMode(item->hDC, old_bk_mode);

	return true;
}

bool draw_owner_static(const DRAWITEMSTRUCT* item) {
	if (!item || item->CtlType != ODT_STATIC || !is_static_id(item->CtlID)) {
		return false;
	}

	ensure_gdi_resources();

	RECT rc = item->rcItem;
	FillRect(item->hDC, &rc, g_background_brush);

	wchar_t text[256] = {};
	GetWindowTextW(item->hwndItem, text, _countof(text));

	RECT text_rc = rc;
	InflateRect(&text_rc, -2, -1);

	HFONT current_font = font();
	HGDIOBJ old_font = current_font ? SelectObject(item->hDC, current_font) : nullptr;
	int old_bk_mode = SetBkMode(item->hDC, TRANSPARENT);
	COLORREF old_text_color = SetTextColor(item->hDC, g_theme.text);
	const UINT align = (item->CtlID == (UINT)g_ids.static_bpm_rate) ? DT_CENTER : DT_LEFT;
	DrawTextW(item->hDC, text, -1, &text_rc, align | DT_VCENTER | DT_SINGLELINE);
	SetTextColor(item->hDC, old_text_color);
	SetBkMode(item->hDC, old_bk_mode);
	if (old_font) {
		SelectObject(item->hDC, old_font);
	}

	return true;
}

LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	if (!g_button_base_proc) {
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}

	switch (message) {
	case WM_MOUSEMOVE:
		if (IsWindowEnabled(hwnd)) {
			set_hot_button(hwnd);

			TRACKMOUSEEVENT track = {};
			track.cbSize = sizeof(track);
			track.dwFlags = TME_LEAVE;
			track.hwndTrack = hwnd;
			TrackMouseEvent(&track);
		}
		break;

	case WM_MOUSELEAVE:
		if (g_hot_button == hwnd) {
			set_hot_button(nullptr);
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_ENABLE:
		invalidate_button(hwnd);
		break;

	case WM_NCDESTROY:
		if (g_hot_button == hwnd) {
			g_hot_button = nullptr;
		}
		return CallWindowProcW(g_button_base_proc, hwnd, message, wparam, lparam);
	}

	return CallWindowProcW(g_button_base_proc, hwnd, message, wparam, lparam);
}

void draw_edit_border(HWND hwnd) {
	HDC hdc = GetDC(hwnd);
	if (!hdc) return;

	RECT rc;
	GetClientRect(hwnd, &rc);
	draw_rect_outline(hdc, rc, (GetFocus() == hwnd) ? g_theme.focus_border : g_theme.border);
	ReleaseDC(hwnd, hdc);
}

void update_rate_edit_format_rect(HWND hwnd) {
	if (!hwnd) return;

	RECT rc;
	GetClientRect(hwnd, &rc);

	HDC hdc = GetDC(hwnd);
	int text_h = 16;
	if (hdc) {
		HFONT current_font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
		HGDIOBJ old_font = current_font ? SelectObject(hdc, current_font) : nullptr;
		TEXTMETRICW tm = {};
		if (GetTextMetricsW(hdc, &tm)) {
			text_h = tm.tmHeight;
		}
		if (old_font) {
			SelectObject(hdc, old_font);
		}
		ReleaseDC(hwnd, hdc);
	}

	const int top = clamp_min((rc.bottom - rc.top - text_h) / 2, 1);
	rc.left += 6;
	rc.right -= 6;
	rc.top += top;
	rc.bottom -= top;
	SendMessageW(hwnd, EM_SETRECTNP, 0, (LPARAM)&rc);
}

LRESULT CALLBACK rate_edit_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	if (!g_rate_edit_base_proc) {
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}

	switch (message) {
	case WM_CHAR:
		if (wparam == L'\r' || wparam == L'\n') {
			return 0;
		}
		break;

	case WM_PAINT:
	{
		LRESULT result = CallWindowProcW(g_rate_edit_base_proc, hwnd, message, wparam, lparam);
		draw_edit_border(hwnd);
		return result;
	}

	case WM_SIZE:
	case WM_SETFONT:
	{
		LRESULT result = CallWindowProcW(g_rate_edit_base_proc, hwnd, message, wparam, lparam);
		update_rate_edit_format_rect(hwnd);
		return result;
	}

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		InvalidateRect(hwnd, nullptr, TRUE);
		break;

	case WM_NCDESTROY:
	{
		WNDPROC base_proc = g_rate_edit_base_proc;
		g_rate_edit_base_proc = nullptr;
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)base_proc);
		return CallWindowProcW(base_proc, hwnd, message, wparam, lparam);
	}
	}

	return CallWindowProcW(g_rate_edit_base_proc, hwnd, message, wparam, lparam);
}

} // namespace

void initialize(CONFIG_HANDLE* config, const ControlIds& ids) {
	if (g_style_loaded) return;
	g_style_loaded = true;
	g_config = config;
	g_ids = ids;

	g_theme.background = get_style_color("Background", k_fallback_theme.background);
	g_theme.text = get_style_color("Text", k_fallback_theme.text);
	g_theme.muted_text = get_style_color("TextDisable", k_fallback_theme.muted_text);
	g_theme.edit_background = get_style_color("Background", k_fallback_theme.edit_background);
	g_theme.button_background = get_style_color("ButtonBody", k_fallback_theme.button_background);
	g_theme.button_hot = get_style_color("ButtonBodyHover", k_fallback_theme.button_hot);
	g_theme.button_pressed = get_style_color("ButtonBodyPress", k_fallback_theme.button_pressed);
	g_theme.border = get_style_color("Border", k_fallback_theme.border);
	g_theme.focus_border = get_style_color("BorderFocus", k_fallback_theme.focus_border);
	g_theme.disabled_text = get_style_color("TextDisable", k_fallback_theme.disabled_text);

	ensure_gdi_resources();
}

HFONT font() {
	return g_control_font ? g_control_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

HBRUSH background_brush() {
	ensure_gdi_resources();
	return g_background_brush;
}

void apply_child(HWND hwnd) {
	if (!hwnd) return;
	SendMessageW(hwnd, WM_SETFONT, (WPARAM)font(), TRUE);
}

void apply_button(HWND hwnd) {
	if (!hwnd) return;

	SendMessageW(hwnd, WM_SETFONT, (WPARAM)font(), TRUE);
	WNDPROC base_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)button_proc);
	if (!g_button_base_proc) {
		g_button_base_proc = base_proc;
	}
}

void apply_edit(HWND hwnd) {
	if (!hwnd) return;

	SendMessageW(hwnd, WM_SETFONT, (WPARAM)font(), TRUE);
	SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
	update_rate_edit_format_rect(hwnd);
	g_rate_edit_base_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)rate_edit_proc);
}

bool set_window_text_if_changed(HWND hwnd, const std::wstring& text) {
	if (!hwnd) return false;

	const int len = GetWindowTextLengthW(hwnd);
	std::vector<wchar_t> current(len + 1);
	GetWindowTextW(hwnd, current.data(), (int)current.size());
	if (text == current.data()) {
		return false;
	}

	SetWindowTextW(hwnd, text.c_str());
	return true;
}

bool text_fits(HWND hwnd, const std::wstring& text, int horizontal_padding) {
	if (!hwnd || text.empty()) return true;

	RECT rc;
	GetClientRect(hwnd, &rc);
	const int available_width = (rc.right - rc.left) - horizontal_padding;
	if (available_width <= 0) return false;

	HDC hdc = GetDC(hwnd);
	if (!hdc) return true;

	HFONT current_font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
	if (!current_font) {
		current_font = font();
	}
	HGDIOBJ old_font = current_font ? SelectObject(hdc, current_font) : nullptr;

	SIZE size = {};
	const bool measured = GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size) != FALSE;

	if (old_font) {
		SelectObject(hdc, old_font);
	}
	ReleaseDC(hwnd, hdc);

	return !measured || size.cx <= available_width;
}

void redraw_child(HWND parent, int id) {
	HWND child = GetDlgItem(parent, id);
	if (child) {
		RedrawWindow(child, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
	}
}

void redraw_parent_background(HWND hwnd) {
	RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN | RDW_UPDATENOW);
}

LRESULT on_ctl_color_static(HDC hdc) {
	ensure_gdi_resources();
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, g_theme.text);
	return (LRESULT)g_background_brush;
}

LRESULT on_ctl_color_edit(HDC hdc) {
	ensure_gdi_resources();
	SetBkMode(hdc, OPAQUE);
	SetBkColor(hdc, g_theme.edit_background);
	SetTextColor(hdc, g_theme.text);
	return (LRESULT)g_edit_background_brush;
}

LRESULT on_ctl_color_button(HDC hdc) {
	ensure_gdi_resources();
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, g_theme.text);
	return (LRESULT)g_background_brush;
}

bool erase_background(HWND hwnd, HDC hdc) {
	ensure_gdi_resources();
	RECT rc;
	GetClientRect(hwnd, &rc);
	FillRect(hdc, &rc, g_background_brush);
	return true;
}

void paint_background(HWND hwnd) {
	ensure_gdi_resources();
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);
	FillRect(hdc, &ps.rcPaint, g_background_brush);
	EndPaint(hwnd, &ps);
}

bool draw_item(const DRAWITEMSTRUCT* item) {
	return draw_owner_static(item) || draw_owner_button(item);
}

} // namespace gui_style
