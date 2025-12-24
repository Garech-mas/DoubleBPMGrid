#pragma once

#include <windows.h>

// 前方宣言
struct EDIT_HANDLE;
struct LOG_HANDLE;

// プラグイン情報定数
#define PLUGIN_NAME L"BPMグリッド倍化"
#define PLUGIN_VERSION L"v2.00"
#define TESTED_BETA L"beta25"
#define TESTED_BETA_NO 2002500
#define PLUGIN_TITLE PLUGIN_NAME " " PLUGIN_VERSION
#define PLUGIN_INFO PLUGIN_NAME " " PLUGIN_VERSION L" (テスト済: " TESTED_BETA L") by Garech"

// アクセサ
EDIT_HANDLE* get_edit_handle();
LOG_HANDLE* get_logger();
float get_tempo();
float get_rate();
float get_offset();
bool is_measuring();

// 変換
int offset_to_frame(float offset_sec, EDIT_INFO* info);

// 操作
void multiply_bpm(float new_rate);
void shift_grid(int direction);
void reset_bpm();
void measure_bpm();
void sync_bpm(); 

// タイマーコールバック (main.cppで実装)
void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);