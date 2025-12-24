#pragma once

#include <windows.h>

// 前方宣言
struct HOST_APP_TABLE;

// プロジェクトロード時のメッセージID
#define WM_PROJECT_LOAD WM_USER + 64

// グローバルなハンドルのアクセサ
HWND get_hwnd();

// 操作
void create_plugin_window(HOST_APP_TABLE* host, HINSTANCE hInstance);
void update_gui();
