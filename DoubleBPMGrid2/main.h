#pragma once

#include <windows.h>
#include "plugin2.h"
#include "logger2.h"

// プラグイン情報定数
#define PLUGIN_NAME L"BPMグリッド倍化"
#define PLUGIN_VERSION L"v2.6"
#define TESTED_BETA L"beta53a"
#define TESTED_BETA_NO 2005301

// アクセサ
EDIT_HANDLE* get_edit_handle();
LOG_HANDLE* get_logger();
float get_tempo();
float get_rate();
float get_rate(EDIT_INFO* info);
float get_offset();
int get_beat();
float get_nearest_tempo(EDIT_INFO* info);
float get_nearest_offset(EDIT_INFO* info);
bool is_measuring();
float get_measuring_bpm();

// 変換
int offset_to_frame(float offset_sec, EDIT_INFO* info);

// 操作
void multiply_bpm(float new_rate);
void shift_grid(int direction);
void reset_bpm();
void measure_bpm();
void sync_bpm(EDIT_SECTION* edit = nullptr);

// タイマーコールバック (main.cppで実装)
void CALLBACK timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);
