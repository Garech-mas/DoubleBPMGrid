#include <windows.h>
#include <cmath>
#include <chrono>

#include "plugin2.h"
#include "logger2.h"

#include "main.h"
#include "gui.h"

// グローバル変数
static float g_tempo = 120.0f; // 現在のBPM
static float g_rate = 1.0f;    // 現在の倍率
static float g_offset = 0.0f;  // 現在の基準位置

// BPM測定用
static std::chrono::steady_clock::time_point last_tap_time;
static double bpm_sum = 0;
static int bpm_count = -1; // -1: 未開始、 0: 計測モード開始、1+: 計測中
static UINT_PTR bpm_timer_id = 0;

EDIT_HANDLE* edit_handle = nullptr;
LOG_HANDLE* logger = nullptr;

// 定数群
static constexpr float EPSILON_ROUNDING = 1e-6; // 丸め誤差補正値
static constexpr double EPSILON_FRAME = 1e-12;

// アクセサ
float get_tempo() { return g_tempo; }
float get_rate() { return g_rate; }
float get_offset() { return g_offset; }
bool is_measuring() { return bpm_count >= 0; }
float get_measuring_bpm() { return (bpm_count > 1) ? (float)(bpm_sum / (bpm_count - 1)) : 0.0f; }

EDIT_HANDLE* get_edit_handle() { return edit_handle; }
LOG_HANDLE* get_logger() { return logger; }


/// 基準時間(秒)をフレーム値に変換
///  - 値が整数に十分近ければその整数を返す (例: 15.0 -> 15)
///  - それ以外は切り上げ(ceil)する (例: 13.99998 -> 14, 14.00001 -> 15)
int offset_to_frame(float offset_sec, EDIT_INFO* info) {
	if (!info || info->rate <= 0 || info->scale <= 0) return 0;
	const double fps = static_cast<double>(info->rate) / static_cast<double>(info->scale);

	// 秒 → フレーム変換
	const double frames = offset_sec * fps;

	// 浮動小数誤差で「ほぼ整数」になった場合は丸める
	const double tol = 1e-5;
	const double nearest = std::round(frames);
	if (std::abs(frames - nearest) <= tol) {
		return static_cast<int>(nearest);
	}

	// 境界誤差対策として微小値を引いた上で切り上げ
	return static_cast<int>(std::ceil(frames - EPSILON_FRAME));
}

/// BPMを設定する
static bool set_bpm(float new_tempo) {
	if (new_tempo < 1.0f || 1000.0f < new_tempo) {
		wchar_t buf[256];
		std::swprintf(buf, 256, L"不正なBPM(%.2f)が入力されました。(設定可能範囲: 1-1000）", new_tempo);
		logger->warn(logger, buf);
		return false;
	}

	edit_handle->call_edit_section_param(
		(void*)&new_tempo,
		[](void* new_tempo, EDIT_SECTION* edit) {
			edit->set_grid_bpm(*(float*)new_tempo, edit->info->grid_bpm_beat, edit->info->grid_bpm_offset);
		}
	);
	return true;
}

/// グリッド設定の内部値を最新の状態に同期する
void sync_bpm() {
	if (edit_handle == nullptr) return;

	EDIT_INFO info;
	edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));

	float actual_bpm = info.grid_bpm_tempo;
	float calculated_bpm = g_tempo * g_rate;

	if (fabs(actual_bpm - calculated_bpm) > EPSILON_ROUNDING) {
		g_tempo = actual_bpm;
		g_offset = (float)info.grid_bpm_offset;
		g_rate = 1.0f;
	}
	else if (fabs((float)info.grid_bpm_offset - g_offset) > EPSILON_ROUNDING) {
		g_offset = (float)info.grid_bpm_offset;
	}
}

/// BPMを倍にする
void multiply_bpm(float new_rate) {
	sync_bpm();
	float before_rate = g_rate;
	g_rate *= new_rate;

	if (!set_bpm(g_tempo * g_rate)) {
		g_rate = before_rate;
	}
}


/// グリッドを左右に動かす (-1 / 1)
void shift_grid(int dir) {
	sync_bpm();

	edit_handle->call_edit_section_param(&dir, [](void* p_dir, EDIT_SECTION* edit) {
		int dir = *(int*)p_dir;
		EDIT_INFO* info = edit->info;

		// 一旦フレーム数に変換後、±1したフレーム値の秒数を求める
		int current_f = offset_to_frame((float)edit->info->grid_bpm_offset, info);
		int next_f = current_f + dir;

		// フレーム番号をそのまま秒に変換
		g_offset = (float)next_f * info->scale / info->rate;

		edit->set_grid_bpm(info->grid_bpm_tempo, info->grid_bpm_beat, g_offset);
		});
}


/// BPMを元に戻す
void reset_bpm() {
	sync_bpm();
	g_rate = 1.0f;
	set_bpm(g_tempo);
}


/// 測定したBPMを設定する
void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
	// タイマー停止
	KillTimer(NULL, bpm_timer_id);
	bpm_timer_id = 0;

	// 押下回数が2回以下の場合は処理しない
	if (bpm_count > 1) {
		float average_bpm = (float)std::round(bpm_sum / (bpm_count - 1));
		set_bpm(average_bpm);
	}

	// 初期化
	bpm_sum = 0.0;
	bpm_count = -1;
	sync_bpm();
}


/// BPMを計測する
void measure_bpm() {
	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsed = now - last_tap_time;
	double seconds = elapsed.count();

	if (bpm_timer_id != 0) KillTimer(NULL, bpm_timer_id); // 予約タイマー停止
	if (seconds <= 0.1) return; // チャタリング防止

	last_tap_time = now;

	// 最初の1回目は無視、2回目から計測する
	if (bpm_count <= 0) {
		bpm_sum = 0.0;
	}
	else {
		bpm_sum += (60.0 / seconds);
	}
	bpm_count++;

	// 1.5秒後にtimer_proc()を予約する
	bpm_timer_id = SetTimer(NULL, 0, 1500, (TIMERPROC)timer_proc);
}


/// AviUtl2 のメインウィンドウを取得する
HWND get_aviutl2_window() {
	const std::wstring className = L"aviutl2Manager";
	DWORD currentPid = GetCurrentProcessId();
	HWND hWnd = nullptr;

	while ((hWnd = FindWindowExW(nullptr, hWnd, className.c_str(), nullptr)) != nullptr) {
		DWORD windowPid = 0;
		GetWindowThreadProcessId(hWnd, &windowPid);

		if (windowPid == currentPid) {
			return hWnd; // 最初に見つかったものを返す
		}
	}

	return nullptr; // 見つからなかった場合
}


///	ログ出力機能初期化
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
	logger = handle;
}


/// プロジェクトファイルをロードした直後の関数
EXTERN_C __declspec(dllexport) void func_project_load(PROJECT_FILE* project) {
	// PostMessageを投げて処理してもらう (このタイミングでget_edit_info()してもうまくいかないため)
	::PostMessage(get_hwnd(), WM_PROJECT_LOAD, 0, 0);
}


/// シーン変更時の関数
EXTERN_C __declspec(dllexport) void func_scene_change(EDIT_SECTION* edit) {
	::PostMessage(get_hwnd(), WM_PROJECT_LOAD, 0, 0);
}


/// プラグインDLL初期化
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	if (version < TESTED_BETA_NO) {
		MessageBox(get_aviutl2_window(), PLUGIN_NAME L"を動作させるためには、AviUtl2 " TESTED_BETA L"が必要です。\nAviUtl2を更新してください。", PLUGIN_TITLE, MB_ICONWARNING);
		return false;
	}
	return true;
}


/// プラグイン登録
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	host->set_plugin_information(PLUGIN_INFO);
	edit_handle = host->create_edit_handle();
	create_plugin_window(host, GetModuleHandle(0));
	host->register_project_load_handler(func_project_load);
	host->register_change_scene_handler(func_scene_change);
}