#include <windows.h>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

#include "main.h"
#include "gui.h"
#include "config2.h"
#include <unordered_map>

static std::wstring Plugin_Name;
static std::wstring Plugin_Title;
static std::wstring Plugin_Info;

struct SceneState {
    std::vector<BPM_INFO> grid_list;
    float rate = 1.0f; // 現在の倍率
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
COMMON_PLUGIN_TABLE common_plugin_table;

// 定数群
static constexpr double EPSILON_FRAME = 1e-12;
static constexpr float EPSILON_TEMPO = 1e-4f;

/// 倍率に合わせた拍数を計算する
static int calculate_beat(int beat, float rate) {
    return static_cast<int>(std::ceil(static_cast<double>(beat) * rate));
}

/// 内部状態に倍率をかけてAviUtl2に適用するBPM_INFOへ変換する
static BPM_INFO make_bpm_info(const BPM_INFO& grid, float rate) {
    BPM_INFO bpm{};
    bpm.tempo = grid.tempo * rate;
    bpm.beat = calculate_beat(grid.beat, rate);
    bpm.start = grid.start;
    bpm.offset = grid.offset;
    return bpm;
}

/// 現在カーソル以前で一番近いBPMセクションのインデックスを返す
static size_t get_nearest_grid_index(const std::vector<BPM_INFO>& grid_list, const EDIT_INFO& info) {
    size_t selected_index = 0;
    if (grid_list.empty()) return selected_index;

    const double cursor_seconds = (info.rate > 0 && info.scale > 0)
        ? static_cast<double>(info.frame) * static_cast<double>(info.scale) / static_cast<double>(info.rate)
        : 0.0;

    for (size_t i = 0; i < grid_list.size(); i++) {
        if (grid_list[i].start > cursor_seconds + EPSILON_FRAME) return selected_index;
        selected_index = i;
    }
    return selected_index;
}

/// 現在の編集シーンに紐づく状態を返す
static SceneState& get_scene_state(EDIT_INFO* info) {
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

/// 表示や操作対象になるBPMセクションの内部状態を返す
static BPM_INFO get_nearest_grid(EDIT_INFO* info) {
    EDIT_INFO local;
    if (info == nullptr && edit_handle) {
        edit_handle->get_edit_info(&local, sizeof(EDIT_INFO));
        info = &local;
    }

    SceneState& ss = get_scene_state(info);
    if (!ss.grid_list.empty()) {
        size_t index = info ? get_nearest_grid_index(ss.grid_list, *info) : 0;
        return ss.grid_list[index];
    }

    return { 120.0f, 0, 0.0, 0.0f };
}

/// アクセサ
float get_tempo() { return get_nearest_grid(nullptr).tempo; }
float get_rate() { return get_scene_state(nullptr).rate; }
float get_rate(EDIT_INFO* info) { return get_scene_state(info).rate; }
float get_offset() { return get_nearest_grid(nullptr).offset; }
int get_beat() { return get_nearest_grid(nullptr).beat; }
float get_nearest_tempo(EDIT_INFO* info) { return get_nearest_grid(info).tempo; }
float get_nearest_offset(EDIT_INFO* info) { return get_nearest_grid(info).offset; }
bool is_measuring() { return get_scene_state(nullptr).bpm_count >= 0; }
float get_measuring_bpm() { auto& s = get_scene_state(nullptr); return (s.bpm_count > 1) ? (float)(s.bpm_sum / s.bpm_count) : 0.0f; }
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

/// BPMが設定可能な範囲に収まっているかを返す
static bool is_valid_bpm(float tempo) {
    return std::isfinite(tempo) && 1.0f <= tempo && tempo <= 1000.0f;
}

/// 不正なBPMをログに出す
static void warn_invalid_bpm(float tempo) {
    if (!logger || !config) return;

    wchar_t buf[256];
    std::swprintf(buf, 256, config->translate(config, L"不正なBPM(%.2f)が入力されました。(設定可能範囲: 1-1000）"), tempo);
    logger->warn(logger, buf);
}

/// 現在のBPM設定をAviUtl2へ反映する
static bool apply_bpm(int scene_id) {
    if (edit_handle == nullptr) return false;

    struct ApplyParam {
        int scene_id;
        bool applied;
    } param{ scene_id, false };

    edit_handle->call_edit_section_param(&param, [](void* p, EDIT_SECTION* edit) {
        auto* param = (ApplyParam*)p;
        if (!edit || !edit->info || edit->info->scene_id != param->scene_id) return;

        auto it = g_scenes.find(param->scene_id);
        if (it == g_scenes.end() || it->second.grid_list.empty()) return;

        std::vector<BPM_INFO> bpm_list;
        bpm_list.reserve(it->second.grid_list.size());
        for (const auto& grid : it->second.grid_list) {
            bpm_list.push_back(make_bpm_info(grid, it->second.rate));
        }
        if (bpm_list.empty()) return;

        edit->set_grid_bpm_list(bpm_list.data(), (int)bpm_list.size(), sizeof(BPM_INFO));
        param->applied = true;
    });

    return param.applied;
}

/// シーンのBPMリストが未取得ならAviUtl2から取得する
static void ensure_scene_grid_list(int scene_id) {
    SceneState& ss = g_scenes[scene_id];
    if (!ss.grid_list.empty()) return;

    if (edit_handle != nullptr) {
        struct InitParam {
            int scene_id;
        } param{ scene_id };

        edit_handle->call_read_section_param(&param, [](void* p, EDIT_SECTION* edit) {
            auto* param = (InitParam*)p;
            if (!edit || !edit->info || edit->info->scene_id != param->scene_id) return;
            sync_bpm(edit);
        });
    }
}

/// グリッド設定の内部値を最新の状態に同期する
void sync_bpm(EDIT_SECTION* edit) {
    if (edit_handle == nullptr) return;

    // edit が未指定の場合は参照ロック内で現在シーンの情報を取り直す
    if (edit == nullptr) {
        edit_handle->call_read_section([](EDIT_SECTION* edit) {
            sync_bpm(edit);
        });
        return;
    }
    if (!edit->info) return;

    // AviUtl2側に設定されているBPMリストを取得する
    std::vector<BPM_INFO> actual_bpm_list;
    int bpm_num = edit->get_grid_bpm_list(nullptr, 0, sizeof(BPM_INFO));
    if (bpm_num > 0) {
        std::vector<BPM_INFO> bpm_list(bpm_num);
        int read_num = edit->get_grid_bpm_list(bpm_list.data(), bpm_num, sizeof(BPM_INFO));
        if (read_num > 0) {
            actual_bpm_list.reserve(read_num);
            for (int i = 0; i < read_num; i++) {
                actual_bpm_list.push_back(bpm_list[i]);
            }
        }
    }
    if (actual_bpm_list.empty()) return;

    // 未同期の場合は、現在値をそのまま基準値として持つ
    SceneState& ss = g_scenes[edit->info->scene_id];
    if (ss.grid_list.empty()) {
        ss.grid_list = actual_bpm_list;
        ss.rate = 1.0f;
        return;
    }

    // AviUtl2上でBPMセクションの構成やBPM値が変わった場合は、それを新しい基準値として持つ
    if (ss.grid_list.size() != actual_bpm_list.size()) {
        ss.grid_list = actual_bpm_list;
        ss.rate = 1.0f;
        return;
    }
    for (size_t i = 0; i < actual_bpm_list.size(); i++) {
        float expected_tempo = ss.grid_list[i].tempo * ss.rate;
        if (std::fabs(actual_bpm_list[i].tempo - expected_tempo) > EPSILON_TEMPO) {
            ss.grid_list = actual_bpm_list;
            ss.rate = 1.0f;
            return;
        }
    }

    // BPM倍率を保持したまま、start/offset だけ最新の値に追従させる
    for (size_t i = 0; i < actual_bpm_list.size(); i++) {
        BPM_INFO& grid = ss.grid_list[i];
        const BPM_INFO& actual = actual_bpm_list[i];

        grid.start = actual.start;
        grid.offset = actual.offset;

        // 自分で倍率適用したbeatは無視し、AviUtl2側で変更されたbeatだけ基準値として取り込む
        int calculated_beat = calculate_beat(grid.beat, ss.rate);
        if (actual.beat != calculated_beat) grid.beat = actual.beat;
    }
}

/// BPMを倍にする
void multiply_bpm(float new_rate) {
    // 現在シーンのBPMリストを用意し、次の倍率で全セクションが有効範囲内か確認する
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState& ss = g_scenes[info.scene_id];
    ensure_scene_grid_list(info.scene_id);

    float before_rate = ss.rate;
    float next_rate = ss.rate * new_rate;
    float invalid_tempo = 0.0f;
    bool valid_bpm_list = std::isfinite(next_rate);
    for (const auto& grid : ss.grid_list) {
        float tempo = grid.tempo * next_rate;
        if (!is_valid_bpm(tempo)) {
            invalid_tempo = tempo;
            valid_bpm_list = false;
            break;
        }
    }
    if (!valid_bpm_list) {
        warn_invalid_bpm(invalid_tempo);
        return;
    }

    // 倍率を更新して全BPMセクションをAviUtl2へ書き戻す
    ss.rate = next_rate;
    if (!apply_bpm(info.scene_id)) ss.rate = before_rate;
}


/// グリッドを左右に動かす (-1 / 1)
void shift_grid(int dir) {
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState& ss = g_scenes[info.scene_id];
    ensure_scene_grid_list(info.scene_id);
    if (ss.grid_list.empty()) return;

    size_t index = get_nearest_grid_index(ss.grid_list, info);
    BPM_INFO& grid = ss.grid_list[index];
    float before_offset = grid.offset;

    // 一旦フレーム数に変換後、±1したフレーム値の秒数を求める
    int current_f = offset_to_frame(grid.offset, &info);
    int next_f = current_f + dir;

    // フレーム番号をそのまま秒に変換
    grid.offset = (float)next_f * info.scale / info.rate;

    if (!apply_bpm(info.scene_id)) grid.offset = before_offset;
}


/// BPMを元に戻す
void reset_bpm() {
    // シーン倍率だけを1.0に戻し、保持している基準BPMリストを書き戻す
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState& ss = g_scenes[info.scene_id];
    ensure_scene_grid_list(info.scene_id);

    float before_rate = ss.rate;
    ss.rate = 1.0f;
    if (!apply_bpm(info.scene_id)) ss.rate = before_rate;
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

    SceneState& ss = g_scenes[scene_id];
    ss.bpm_timer_id = 0;

    // 押下回数が2回以下の場合は処理しない
    if (ss.bpm_count > 1) {
        // 計測開始時のシーンIDと現時点のアクティブなシーンIDが異なる場合は反映をスキップ
        bool can_apply_bpm = true;
        if (edit_handle) {
            EDIT_INFO cur_info;
            edit_handle->get_edit_info(&cur_info, sizeof(EDIT_INFO));
            if (cur_info.scene_id != scene_id) {
                can_apply_bpm = false;
            }
        }

        if (can_apply_bpm) {
            // 測定平均を整数BPMに丸め、測定状態を先に終了状態へ戻す
            float average_bpm = (float)std::round(ss.bpm_sum / ss.bpm_count);

            // BPMセット前に初期化
            ss.bpm_sum = 0.0;
            ss.bpm_count = -1;

            if (!is_valid_bpm(average_bpm)) {
                warn_invalid_bpm(average_bpm);
            }
            else {
                EDIT_INFO info;
                edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
                SceneState& cur_ss = g_scenes[info.scene_id];
                ensure_scene_grid_list(info.scene_id);

                // 現在倍率がかかった表示中BPMリストを基準値に変換し直す
                std::vector<BPM_INFO> current_bpm_list;
                current_bpm_list.reserve(cur_ss.grid_list.size());
                for (const auto& grid : cur_ss.grid_list) {
                    current_bpm_list.push_back(make_bpm_info(grid, cur_ss.rate));
                }
                if (current_bpm_list.empty()) return;

                // カーソル位置で有効なBPMセクションへ測定結果を反映する
                size_t index = get_nearest_grid_index(current_bpm_list, info);
                current_bpm_list[index].tempo = average_bpm;
                current_bpm_list[index].beat = 4; // 拍は固定で4にする

                // 測定値を新しい基準値として保持し、倍率をリセットしてAviUtl2へ反映する
                cur_ss.grid_list = current_bpm_list;
                cur_ss.rate = 1.0f;
                apply_bpm(info.scene_id);
            }
        }
    }
    else {
        // 初期化
        ss.bpm_sum = 0.0;
        ss.bpm_count = -1;
        update_gui();
    }
}


/// BPMを計測する
void measure_bpm() {
    EDIT_INFO info;
    edit_handle->get_edit_info(&info, sizeof(EDIT_INFO));
    SceneState& ss = g_scenes[info.scene_id];

    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = now - ss.last_tap_time;
    double seconds = elapsed.count();

    if (seconds <= 0.1) return; // チャタリング防止

    if (ss.bpm_timer_id != 0) KillTimer(NULL, ss.bpm_timer_id); // 予約タイマー停止
    // 既にマッピングがあれば削除
    if (ss.bpm_timer_id != 0) {
        g_timer_to_scene.erase(ss.bpm_timer_id);
    }

    ss.last_tap_time = now;

    // 最初の1回目は基準時刻だけ記録し、2回目から区間を加算する
    if (ss.bpm_count < 0) {
        ss.bpm_count = 0;
        ss.bpm_sum = 0.0;
    }
    else {
        ss.bpm_sum += (60.0 / seconds);
        ss.bpm_count++;
    }

    // 1.5秒後にtimer_proc()を予約する
    ss.bpm_timer_id = SetTimer(NULL, 0, 1500, (TIMERPROC)timer_proc);
    if (ss.bpm_timer_id != 0) {
        g_timer_to_scene[ss.bpm_timer_id] = info.scene_id;
    }
    update_gui();
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

    common_plugin_table = { Plugin_Name.c_str(), Plugin_Info.c_str() };
}


/// シーン・BPM変更時の処理
EXTERN_C __declspec(dllexport) void func_scene_change(EDIT_SECTION* edit) {
    sync_bpm(edit);

    // シーン切替後に、切替先シーンで計測中だった場合は計測結果を反映する
    if (edit && edit->info) {
        int sid = edit->info->scene_id;
        auto it = g_scenes.find(sid);
        if (it != g_scenes.end()) {
            SceneState& ss = it->second;
            if (ss.bpm_count >= 0) {
                // 既に予約済みのタイマーがあればクリアしてマッピングを削除
                if (ss.bpm_timer_id != 0) {
                    KillTimer(NULL, ss.bpm_timer_id);
                    g_timer_to_scene.erase(ss.bpm_timer_id);
                }
                // 終了処理がすぐ実行されるようにタイマーをセットする
                ss.bpm_timer_id = SetTimer(NULL, 0, 50, (TIMERPROC)timer_proc);
                if (ss.bpm_timer_id != 0) {
                    g_timer_to_scene[ss.bpm_timer_id] = sid;
                }
            }
        }
    }

    update_gui((edit && edit->info) ? edit->info : nullptr);
}


/// 必須バージョン番号を渡す
EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return TESTED_BETA_NO;
}


///	汎用プラグイン構造体のポインタを渡す
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
    return &common_plugin_table;
}


/// プラグイン登録
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    edit_handle = host->create_edit_handle();
    create_plugin_window(host, GetModuleHandle(0));
    host->register_change_scene_handler(func_scene_change);
}
