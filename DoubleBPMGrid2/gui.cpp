#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cmath>

#include "config2.h"
#include "gui.h"
#include "main.h"
#include <string>
#include <vector>

extern CONFIG_HANDLE* config;

// コントロールID
#define IDC_STATIC_BPM_RATE       1001
#define IDC_BUTTON_DIV_INPUT      1002
#define IDC_EDIT_RATE_INPUT       1003
#define IDC_BUTTON_MUL_INPUT      1004
#define IDC_BUTTON_MUL2           1005
#define IDC_BUTTON_DIV2           1006
#define IDC_BUTTON_MUL3           1007
#define IDC_BUTTON_DIV3           1008
#define IDC_BUTTON_RESET          1009
#define IDC_BUTTON_MEASURE        1010
#define IDC_STATIC_BASETIME       1011
#define IDC_BUTTON_BASETIME_MINUS 1012
#define IDC_BUTTON_BASETIME_PLUS  1013

// 定数群（UI高さ計算）
constexpr int w_margin_10 = 10;
constexpr int w_space_5 = 5;
constexpr int h_label = 20;
constexpr int h_btn = 30;
constexpr int h_btn_measure = 50;
constexpr int y_space_5 = 5;
constexpr int y_space_10 = 10;
constexpr int w_btn_basetime = 40;

// 行座標の定義
constexpr int y_1st_row = 10;
constexpr int y_2nd_row = y_1st_row + h_label + y_space_5;
constexpr int y_3rd_row = y_2nd_row + h_btn + y_space_10;
constexpr int y_4th_row = y_3rd_row + h_btn + y_space_10;
constexpr int y_5th_row = y_4th_row + h_label + y_space_10;
constexpr int y_6th_row = y_5th_row + h_btn + y_space_5;

constexpr int w_ui = 260;
constexpr int h_ui = y_6th_row + h_btn_measure + y_space_10;
constexpr UINT_PTR timer_id_mouse_watch = 1;
constexpr UINT timer_interval_mouse_watch = 50;

// グローバル変数
static HWND g_hwnd = nullptr;
static int g_scroll_pos_y = 0;
static std::wstring g_bpm_full_text;
static std::wstring g_bpm_short_text;
static std::wstring g_measure_button_text;
static std::wstring g_offset_full_text;
static std::wstring g_offset_short_text;
static bool g_mouse_inside_window = false;
static bool g_key_down[256] = {};

// メニュー名のコンテナ
static std::vector<std::wstring> g_registered_menu_names;

/// マウスカーソルがプラグインウィンドウ内にあるかを返す
static bool is_cursor_in_plugin_window() {
	if (!g_hwnd) return false;

	POINT pt;
	if (!GetCursorPos(&pt)) return false;

	HWND hwnd_under_cursor = WindowFromPoint(pt);
	return hwnd_under_cursor == g_hwnd || IsChild(g_hwnd, hwnd_under_cursor);
}

/// マウスがプラグインウィンドウへ入った時だけ表示を更新する
static void update_gui_on_mouse_enter() {
	if (g_mouse_inside_window) return;

	g_mouse_inside_window = true;
	ZeroMemory(g_key_down, sizeof(g_key_down));
	SetTimer(g_hwnd, timer_id_mouse_watch, timer_interval_mouse_watch, nullptr);
	update_gui();
}

/// マウスがプラグインウィンドウから出たことを検知する
static void check_mouse_leave() {
	if (!g_mouse_inside_window || is_cursor_in_plugin_window()) return;

	g_mouse_inside_window = false;
	KillTimer(g_hwnd, timer_id_mouse_watch);
}

/// キャッシュ済みの文字列を現在のウィンドウ幅に合わせて反映する
static void apply_cached_gui_texts() {
	if (!g_hwnd) return;

	RECT rc;
	GetClientRect(g_hwnd, &rc);
	int w = rc.right;

	SetWindowTextW(GetDlgItem(g_hwnd, IDC_BUTTON_MEASURE), g_measure_button_text.c_str());
	SetWindowTextW(GetDlgItem(g_hwnd, IDC_STATIC_BPM_RATE), (w < 180) ? g_bpm_short_text.c_str() : g_bpm_full_text.c_str());

	int label_max_w = w - (2 * w_margin_10) - (2 * w_btn_basetime) - (2 * w_space_5);
	SetWindowTextW(GetDlgItem(g_hwnd, IDC_STATIC_BASETIME), (label_max_w < 90) ? g_offset_short_text.c_str() : g_offset_full_text.c_str());
}


/// グローバル変数のg_hwndを返す
HWND get_hwnd() {
	return g_hwnd;
}


/// 倍率入力欄の値を返す
static float get_gui_rate() {
	HWND hEdit = GetDlgItem(g_hwnd, IDC_EDIT_RATE_INPUT);

	wchar_t buffer[64];
	GetWindowText(hEdit, buffer, 64);
	float rate = (float)_wtof(buffer);
	if (rate <= 0.0f || isinf(rate) || isnan(rate)) return 1.0f;
	return rate;
}


/// GUIの各種値を最新の状態にする
void update_gui(EDIT_INFO* info) {
    if (!g_hwnd) return;

    // 呼び出し元から編集情報が渡されていない場合は、現在の編集情報を取得する
    EDIT_INFO local_info;
    if (info == nullptr) {
        EDIT_HANDLE* edit = get_edit_handle();
        edit->get_edit_info(&local_info, sizeof(EDIT_INFO));
        info = &local_info;
    }

	// --- BPMラベル・BPM測定ボタンの描画 ---
	wchar_t bpm_full[128], bpm_short[64];
	const wchar_t* measure_btn_text = config->translate(config, L"BPMを測定");

	if (is_measuring()) {
		float m_bpm = get_measuring_bpm();
		_snwprintf_s(bpm_full, _countof(bpm_full), _TRUNCATE,
			m_bpm > 0 ? L"BPM：%.0f?" : L"BPM：????", m_bpm
		);
		wcscpy_s(bpm_short, bpm_full);
		measure_btn_text = config->translate(config, L"タップし続けて測定");
	}
	else {
		float tempo = get_nearest_tempo(info);
		float rate = get_rate(info);
		float current = tempo * rate;

		_snwprintf_s(bpm_full, _countof(bpm_full), _TRUNCATE, L"BPM：%.2f × %.2f = %.2f", tempo, rate, current);
		_snwprintf_s(bpm_short, _countof(bpm_short), _TRUNCATE,
			rate != 1.0f ? L"BPM：%.1f *" : L"BPM：%.1f", current
		);
	}

	g_measure_button_text = measure_btn_text;
	g_bpm_full_text = bpm_full;
	g_bpm_short_text = bpm_short;

	// --- 基準時間ラベルの描画 ---
	wchar_t off_full[128], off_short[64];
	float current_offset = get_nearest_offset(info);
	int current_f = offset_to_frame(current_offset, info);

	_snwprintf_s(off_full, _countof(off_full), _TRUNCATE, config->translate(config, L"基準：%+dF (%.3fs)"), current_f, current_offset);
	_snwprintf_s(off_short, _countof(off_short), _TRUNCATE, L"%+dF", current_f);

	g_offset_full_text = off_full;
	g_offset_short_text = off_short;

	apply_cached_gui_texts();
}


/// ウィンドウプロシージャ ※ここから
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	switch (message) {
	case WM_COMMAND:
		// ボタンと編集メニューから飛んでくる操作を実行する
		switch (LOWORD(wparam)) {
		case IDC_BUTTON_MUL_INPUT: multiply_bpm(get_gui_rate()); return TRUE;
		case IDC_BUTTON_DIV_INPUT: { float v = get_gui_rate(); if (v != 0) multiply_bpm(1.0f / v); } return TRUE;
		case IDC_BUTTON_MUL2: multiply_bpm(2.0f); return 0;
		case IDC_BUTTON_DIV2: multiply_bpm(0.5f); return 0;
		case IDC_BUTTON_MUL3: multiply_bpm(3.0f); return 0;
		case IDC_BUTTON_DIV3: multiply_bpm(1.0f / 3.0f); return 0;
		case IDC_BUTTON_BASETIME_MINUS: shift_grid(-1); return 0;
		case IDC_BUTTON_BASETIME_PLUS: shift_grid(1); return 0;
		case IDC_BUTTON_RESET: reset_bpm(); return 0;
		case IDC_BUTTON_MEASURE: measure_bpm(); return 0;
		}
		break;

	case WM_MOUSEWHEEL:
	{
		const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
		RECT rc;
		GetClientRect(hwnd, &rc);

		if (rc.bottom < h_ui) {
			g_scroll_pos_y = max(0, min(g_scroll_pos_y - (delta / WHEEL_DELTA) * 30, h_ui - rc.bottom));
			SendMessage(hwnd, WM_SIZE, 1, MAKELPARAM(rc.right, rc.bottom));
		}
		return 0;
	}

	case WM_SETCURSOR:
		update_gui_on_mouse_enter();
		break;

	case WM_TIMER:
		if (wparam == timer_id_mouse_watch) {
			check_mouse_leave();
			if (g_mouse_inside_window) {
				bool released = false;
				for (int vk = 0; vk < 256; vk++) {
					bool is_down = (GetAsyncKeyState(vk) & 0x8000) != 0;
					if (!is_down && g_key_down[vk]) {
						released = true;
					}
					g_key_down[vk] = is_down;
				}
				if (released) update_gui();
			}
			return 0;
		}
		break;

	case WM_SIZE:
	{
		// ウィンドウサイズに合わせて各コントロールの位置を再配置する
		const int w_window = LOWORD(lparam);
		const int h_window = HIWORD(lparam);
		g_scroll_pos_y = (h_window >= h_ui) ? 0 : min(g_scroll_pos_y, h_ui - h_window);

		HDWP hdwp = BeginDeferWindowPos(13);
		auto DeferPos = [&](int id, int x, int y, int w, int h) {
			hdwp = DeferWindowPos(hdwp, GetDlgItem(hwnd, id), NULL, x, y - g_scroll_pos_y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
			};

		// --- BPMラベルの描画 ---
		const int w_full = w_window - 2 * w_margin_10;
		DeferPos(IDC_STATIC_BPM_RATE, w_margin_10, y_1st_row, w_full, h_label);

		// --- 倍率変更ボタンの描画 ---
		constexpr int w_edit_rate_input = 80;
		const int w_btn_input = (w_window - 2 * w_margin_10 - 2 * w_space_5 - w_edit_rate_input) / 2;
		DeferPos(IDC_BUTTON_MUL_INPUT, w_margin_10, y_2nd_row, w_btn_input, h_btn);
		DeferPos(IDC_EDIT_RATE_INPUT, w_margin_10 + w_btn_input + w_space_5, y_2nd_row, 80, h_btn);
		DeferPos(IDC_BUTTON_DIV_INPUT, w_window - w_margin_10 - w_btn_input, y_2nd_row, w_btn_input, h_btn);

		// --- 倍率プリセットボタンの描画 ---
		const int w_btn_n = (w_window - 2 * w_margin_10 - 3 * w_space_5) / 4;
		hdwp = DeferWindowPos(hdwp, GetDlgItem(hwnd, IDC_BUTTON_MUL3), NULL, w_margin_10, y_3rd_row - g_scroll_pos_y, w_btn_n, h_btn, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, GetDlgItem(hwnd, IDC_BUTTON_MUL2), NULL, w_margin_10 + w_btn_n + w_space_5, y_3rd_row - g_scroll_pos_y, w_btn_n, h_btn, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, GetDlgItem(hwnd, IDC_BUTTON_DIV2), NULL, w_margin_10 + 2 * (w_btn_n + w_space_5), y_3rd_row - g_scroll_pos_y, w_btn_n, h_btn, SWP_NOZORDER | SWP_NOACTIVATE);
		hdwp = DeferWindowPos(hdwp, GetDlgItem(hwnd, IDC_BUTTON_DIV3), NULL, w_margin_10 + 3 * (w_btn_n + w_space_5), y_3rd_row - g_scroll_pos_y, w_btn_n, h_btn, SWP_NOZORDER | SWP_NOACTIVATE);

		// --- 基準時間変更ボタンの描画 ---
		const int w_label_basetime = w_window - 2 * w_margin_10 - 2 * w_btn_basetime - 2 * w_space_5;
		DeferPos(IDC_STATIC_BASETIME, w_margin_10, y_4th_row, w_label_basetime, h_label);
		DeferPos(IDC_BUTTON_BASETIME_MINUS, w_window - w_margin_10 - w_btn_basetime * 2 - w_space_5, y_4th_row, 40, h_label);
		DeferPos(IDC_BUTTON_BASETIME_PLUS, w_window - w_margin_10 - w_btn_basetime, y_4th_row, 40, h_label);

		// --- BPMリセットボタン、BPM測定ボタンの描画 ---
		DeferPos(IDC_BUTTON_RESET, w_margin_10, y_5th_row, w_full, h_btn);
		DeferPos(IDC_BUTTON_MEASURE, w_margin_10, y_6th_row, w_full, h_btn_measure);

		// GUIの更新・再描画
		EndDeferWindowPos(hdwp);
		apply_cached_gui_texts();

		// スクロール位置が変わった場合は再描画
		if (wparam != 1) {
			InvalidateRect(hwnd, NULL, FALSE);
			UpdateWindow(hwnd);
		}
		return 0;
	}
	}

	// 残りのウィンドウメッセージはAviUtl2に送る
	return DefWindowProc(hwnd, message, wparam, lparam);
	}

	/// プラグインウィンドウ作成
	void create_plugin_window(HOST_APP_TABLE* host, HINSTANCE hInstance) {
		std::wstring Plugin_Name = config->translate(config, PLUGIN_NAME);

		WNDCLASSEXW wcex = {};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.lpszClassName = Plugin_Name.c_str();
		wcex.lpfnWndProc = wnd_proc;
		wcex.hInstance = hInstance;
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		if (!RegisterClassEx(&wcex)) return;

		g_hwnd = CreateWindowEx(
			0, Plugin_Name.c_str(), Plugin_Name.c_str(), WS_POPUP,
			CW_USEDEFAULT, CW_USEDEFAULT, w_ui, h_ui,
			nullptr, nullptr, hInstance, nullptr);

		auto CreateChild = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id) {
			CreateWindowEx(0, cls, txt, WS_VISIBLE | WS_CHILD | style, 0, 0, 0, 0, g_hwnd, (HMENU)(INT_PTR)id, hInstance, nullptr);
			};

		// コントロール作成処理（とりあえず作成だけして、WM_SIZE内で再調整）
		CreateChild(WC_STATICW, L"", SS_CENTER, IDC_STATIC_BPM_RATE);
		CreateChild(WC_BUTTONW, L"×", BS_PUSHBUTTON, IDC_BUTTON_MUL_INPUT);
		CreateChild(WC_EDITW, L"2.00", WS_BORDER | ES_CENTER, IDC_EDIT_RATE_INPUT);
		CreateChild(WC_BUTTONW, L"÷", BS_PUSHBUTTON, IDC_BUTTON_DIV_INPUT);
		CreateChild(WC_BUTTONW, L"×3", BS_PUSHBUTTON, IDC_BUTTON_MUL3);
		CreateChild(WC_BUTTONW, L"×2", BS_PUSHBUTTON, IDC_BUTTON_MUL2);
		CreateChild(WC_BUTTONW, L"÷2", BS_PUSHBUTTON, IDC_BUTTON_DIV2);
		CreateChild(WC_BUTTONW, L"÷3", BS_PUSHBUTTON, IDC_BUTTON_DIV3);
		CreateChild(WC_STATICW, L"", SS_LEFT, IDC_STATIC_BASETIME);
		CreateChild(WC_BUTTONW, L"<", BS_PUSHBUTTON, IDC_BUTTON_BASETIME_MINUS);
		CreateChild(WC_BUTTONW, L">", BS_PUSHBUTTON, IDC_BUTTON_BASETIME_PLUS);
		CreateChild(WC_BUTTONW, config->translate(config, L"▲ BPMを元に戻す"), BS_PUSHBUTTON, IDC_BUTTON_RESET);
		CreateChild(WC_BUTTONW, config->translate(config, L"BPMを測定"), BS_PUSHBUTTON, IDC_BUTTON_MEASURE);

		host->register_window_client(Plugin_Name.c_str(), g_hwnd);

		// 編集メニュー追加処理
		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"BPMを2倍する"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_MUL2, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"BPMを半分にする"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_DIV2, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"BPMを3倍する"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_MUL3, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"BPMを1/3にする"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_DIV3, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"グリッドの基準時間を戻す (-1F)"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_BASETIME_MINUS, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"グリッドの基準時間を進める (+1F)"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_BASETIME_PLUS, 0), 0); });

		g_registered_menu_names.push_back(Plugin_Name + L"\\" + config->translate(config, L"BPMを元に戻す"));
		host->register_edit_menu(g_registered_menu_names.back().c_str(), [](EDIT_SECTION* edit) {
			PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(IDC_BUTTON_RESET, 0), 0); });

	}
