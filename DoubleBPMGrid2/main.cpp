#include <windows.h>
#include <cmath>
#include <chrono>
#include <string>

#include "plugin2.h"
#include "logger2.h"

#include "main.h"
#include "gui.h"
#include "config2.h"
#include <unordered_map>

static std::wstring Plugin_Name;
static std::wstring Plugin_Title;
static std::wstring Plugin_Info;

// シーン情報を scene_id と紐づけて保持する
struct GlobalState {
    float tempo;   // 現在のBPM
    float rate;    // 現在の倍率
    float offset;  // 現在の基準位置 (秒)
    int beat; // 現在の拍
};

struct SceneState {
    GlobalState grid{ 120.0f, 1.0f, 0.0f, 0 };
    // BPM測定用（シーンごと）
    std::chrono::steady_clock::time_point last_tap_time;
    double bpm_sum = 0.0;
    int bpm_count = -1; // -1: 未開始、 0: 計測モード開始、1+: 計測中
    UINT_PTR bpm_timer_id = 0;
};

static std::unordered_map<int, SceneState> g_scenes; // scene_id -> state
static std::unordered_map<UINT_PTR, int> g_timer_to_scene; // timer_id -> scene_id

EDIT_HANDLE* edit_handle = nullptr;
LOG_HANDLE* logger = nullptr;
CONFIG_HANDLE* config = nullptr;

// 定数群
static constexpr float EPSILON_ROUNDING = 1e-6f; // 丸め誤差補正値
static constexpr double EPSILON_FRAME = 1e-12;

// ヘルパー: 現在の編集セクション（もしくは与えられたinfo）の scene_id に紐づく SceneState を返す
static SceneState& get_scene_state_for_info(EDIT_INFO* info) {
    int sid = 0;
    EDIT_INFO local;
    if (info == nullptr) {
        if (edit_handle) {
            edit_handle->get_edit_info(&local, sizeof(EDIT_INFO));
            info = &local;
        }
    }
    if (info) sid = info->scene_id;
    return g_scenes[sid];
}

// アクセサ
float get_tempo() { return get_scene_state_for_info(nullptr).grid.tempo; }
float get_rate() { return get_scene_state_for_info(nullptr).grid.rate; }
float get_offset() { return get_scene_state_for_info(nullptr).grid.offset; }
int get_beat() { return get_scene_state_for_info(nullptr).grid.beat; }
bool is_measuring() { return get_scene_state_for_info(nullptr).bpm_count >= 0; }
float get_measuring_bpm() { auto &s = get_scene_state_for_info(nullptr); return (s.bpm_count > 1) ? (float)(s.bpm_sum / (s.bpm_count - 1)) : 0.0f; }

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
		std::swprintf(buf, 256, config->translate(config, L"不正なBPM(%.2f)が入力されました。(設定可能範囲: 1-1000）"), new_tempo);
		logger->warn(logger, buf);
		return false;
	}

    edit_handle->call_edit_section_param(
        (void*)&new_tempo,
        [](void* new_tempo, EDIT_SECTION* edit) {
            // edit->info->scene_id に紐づく状態を参照して拍を計算
            int sid = edit->info ? edit->info->scene_id : 0;
            GlobalState gs = {120.0f, 1.0f, 0.0f, 0};
            auto it = g_scenes.find(sid);
            if (it != g_scenes.end()) gs = it->second.grid;
            int new_beat = static_cast<int>(std::ceil(static_cast<double>(gs.beat) * gs.rate));
            edit->set_grid_bpm(*(float*)new_tempo, new_beat, edit->info->grid_bpm_offset);
        }
    );
	return true;
}

/// グリッド設定の内部値を最新の状態に同期する
void sync_bpm(EDIT_INFO* info_param) {
    if (edit_handle == nullptr) return;

    EDIT_INFO info_local;
    EDIT_INFO* info = info_param;
    if (info == nullptr) {
        edit_handle->get_edit_info(&info_local, sizeof(EDIT_INFO));
        info = &info_local;
    }

    // シーン固有の状態に同期
    SceneState &ss = g_scenes[info->scene_id];
    GlobalState &gs = ss.grid;

    float actual_bpm = info->grid_bpm_tempo;
    float calculated_bpm = gs.tempo * gs.rate;
    // beat は gs.beat に gs.rate を掛けて切り上げた値
    int calculated_beat = static_cast<int>(std::ceil(static_cast<double>(gs.beat) * gs.rate));

    if (fabs(actual_bpm - calculated_bpm) > EPSILON_ROUNDING) {
        gs.tempo = actual_bpm;
        gs.offset = (float)info->grid_bpm_offset;
        gs.beat = info->grid_bpm_beat;
        gs.rate = 1.0f;
    }
    else {
        // 基準位置が変更された場合
        if (fabs((float)info->grid_bpm_offset - gs.offset) > EPSILON_ROUNDING) {
            gs.offset = (float)info->grid_bpm_offset;
        }

        // 拍が変更された場合
        if (info->grid_bpm_beat != calculated_beat) {
            gs.beat = info->grid_bpm_beat;
        }
    }
}

/// BPMを倍にする
void multiply_bpm(float new_rate) {
    sync_bpm();
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState &ss = g_scenes[info.scene_id];
    float before_rate = ss.grid.rate;
    ss.grid.rate *= new_rate;

    if (!set_bpm(ss.grid.tempo * ss.grid.rate)) {
        ss.grid.rate = before_rate;
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

        // シーン状態を更新
        int sid = info ? info->scene_id : 0;
        SceneState &ss = g_scenes[sid];

        // フレーム番号をそのまま秒に変換
        ss.grid.offset = (float)next_f * info->scale / info->rate;

        edit->set_grid_bpm(info->grid_bpm_tempo, info->grid_bpm_beat, ss.grid.offset);
    });
}


/// BPMを元に戻す
void reset_bpm() {
    sync_bpm();
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState &ss = g_scenes[info.scene_id];
    ss.grid.rate = 1.0f;
    set_bpm(ss.grid.tempo);
}


/// 測定したBPMを設定する
void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    // タイマーIDから紐づくシーンを直接検索する
    auto it = g_timer_to_scene.find(id);
    if (it == g_timer_to_scene.end()) return; // 見つからなければ無視

    int scene_id = it->second;
    // タイマー停止
    KillTimer(NULL, id);
    g_timer_to_scene.erase(it);

    SceneState &ss = g_scenes[scene_id];
    ss.bpm_timer_id = 0;

    // 押下回数が2回以下の場合は処理しない
    if (ss.bpm_count > 1) {
        // 計測開始時のシーンID と、現時点でのアクティブなシーンID が異なる場合は反映をスキップ
        bool apply_bpm = true;
        if (edit_handle) {
            EDIT_INFO cur_info;
            edit_handle->get_edit_info(&cur_info, sizeof(EDIT_INFO));
            if (cur_info.scene_id != scene_id) {
                apply_bpm = false;
            }
        }

        if (apply_bpm) {
            float average_bpm = (float)std::round(ss.bpm_sum / (ss.bpm_count - 1));
            set_bpm(average_bpm);
        }
        // 違っていれば何もしない（タイマーは既に停止している）
    }

    // 初期化
    ss.bpm_sum = 0.0;
    ss.bpm_count = -1;
    sync_bpm();
}


/// BPMを計測する
void measure_bpm() {
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState &ss = g_scenes[info.scene_id];

    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = now - ss.last_tap_time;
    double seconds = elapsed.count();

    if (ss.bpm_timer_id != 0) KillTimer(NULL, ss.bpm_timer_id); // 予約タイマー停止
    // 既にマッピングがあれば削除
    if (ss.bpm_timer_id != 0) {
        g_timer_to_scene.erase(ss.bpm_timer_id);
    }
    if (seconds <= 0.1) return; // チャタリング防止

    ss.last_tap_time = now;

    // 最初の1回目は無視、2回目から計測する
    if (ss.bpm_count <= 0) {
        ss.bpm_sum = 0.0;
    }
    else {
        ss.bpm_sum += (60.0 / seconds);
    }
    ss.bpm_count++;

    // 1.5秒後にtimer_proc()を予約する
    ss.bpm_timer_id = SetTimer(NULL, 0, 1500, (TIMERPROC)timer_proc);
    if (ss.bpm_timer_id != 0) {
        g_timer_to_scene[ss.bpm_timer_id] = info.scene_id;
    }
    update_gui();
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


///	設定関連初期化
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
	config = handle;

	Plugin_Name = config->translate(config, PLUGIN_NAME);
	Plugin_Title = Plugin_Name + L" " + PLUGIN_VERSION;

	LPCWSTR info_fmt = config->translate(config, L"%ls %ls (テスト済: %ls) by Garech");
	wchar_t info_buf[512];
	std::swprintf(info_buf, 512, info_fmt, Plugin_Name.c_str(), PLUGIN_VERSION, TESTED_BETA);
	Plugin_Info = info_buf;
}


/// シーン変更時の関数
EXTERN_C __declspec(dllexport) void func_scene_change(EDIT_SECTION* edit) {
    sync_bpm(edit->info);
    update_gui(edit->info);
}


/// プラグインDLL初期化
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
	if (version < TESTED_BETA_NO) {
		wchar_t msg[512];
		std::swprintf(msg, 512, config->translate(config, L"%lsを動作させるためには、AviUtl2 %lsが必要です。\nAviUtl2を更新してください。"), Plugin_Name.c_str(), TESTED_BETA);
		MessageBox(get_aviutl2_window(), msg, Plugin_Title.c_str(), MB_ICONWARNING);
		return false;
	}
	return true;
}


/// プラグイン登録
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	host->set_plugin_information(Plugin_Info.c_str());
	edit_handle = host->create_edit_handle();
	create_plugin_window(host, GetModuleHandle(0));
	host->register_change_scene_handler(func_scene_change);
}