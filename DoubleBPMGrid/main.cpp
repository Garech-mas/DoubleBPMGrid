#include <windows.h>
#include <tchar.h>
#include <aviutl/filter.hpp>
#include <cstdlib>
#include <filter.h>
#include <string>
#include <chrono>

#define FILTER AviUtl::FilterPlugin

#define PLUGIN_NAME "BPMグリッド倍化"
#define PLUGIN_VERSION " v1.2 by Garech"
#define FULL_PLUGIN_NAME PLUGIN_NAME PLUGIN_VERSION

HMODULE hModule;
FILTER* exeditfp;
double power;
int orig_bpm;

std::chrono::system_clock::time_point tmpTime;
double bpmSum;
int bpmCount;

FILTER* get_exeditfp(FILTER* fp) {
	AviUtl::SysInfo si;
	fp->exfunc->get_sys_info(NULL, &si);

	for (int i = 0; i < si.filter_n; i++) {
		FILTER* tfp = (FILTER*)fp->exfunc->get_filterp(i);
		if (tfp->information != NULL) {
			if (!strcmp(tfp->information, "拡張編集(exedit) version 0.92 by ＫＥＮくん")) return tfp;
		}
	}
	return NULL;
}

VOID set_bpm(int bpm) {
	if (exeditfp != NULL) {
		uintptr_t isGridShownAddress = reinterpret_cast<uintptr_t>(hModule) + 0x00196760;
		int32_t* isGridShown = reinterpret_cast<int32_t*>(isGridShownAddress);

		uintptr_t bpmAddress = reinterpret_cast<uintptr_t>(hModule) + 0x00159190;
		int32_t* bpmPointer = reinterpret_cast<int32_t*>(bpmAddress);
		int32_t bpmValue = *bpmPointer;

		uintptr_t drawBpmAddress = reinterpret_cast<uintptr_t>(hModule) + 0x000A4078;
		double_t* drawBpmPointer = reinterpret_cast<double_t*>(drawBpmAddress);

		*bpmPointer = bpm;
		*drawBpmPointer = (double)bpm;

		if (isGridShown != 0) ::InvalidateRect(exeditfp->hwnd, nullptr, FALSE);

	}
	else {
		MessageBox(NULL, TEXT("拡張編集 version 0.92 が導入されていない環境ではBPMグリッド倍化プラグインを使用できません。"), TEXT(PLUGIN_NAME), MB_OK + MB_ICONERROR) ;
	}
}

VOID change_bpm(double pw) {
	if (exeditfp != NULL) {
		uintptr_t bpmAddress = reinterpret_cast<uintptr_t>(hModule) + 0x00159190;
		int32_t* bpmPointer = reinterpret_cast<int32_t*>(bpmAddress);
		int32_t bpmValue = *bpmPointer;

		if (bpmValue != (int)(orig_bpm * power)) {
			orig_bpm = bpmValue;
			power = 1;
		}

		power *= pw;

		bpmValue = orig_bpm * power;
		set_bpm(bpmValue);

	}
	else {
		set_bpm(0);
	}
}

VOID meas_bpm() {
	auto curTime = std::chrono::system_clock::now();
	double elapsedSeconds = std::chrono::duration<double>(curTime- tmpTime).count();
	if (elapsedSeconds < 0 || elapsedSeconds > 2) {

		bpmCount = 1;
		tmpTime = std::chrono::system_clock::now();
		bpmSum = 0;
	}
	else {
		bpmCount++;
		tmpTime = std::chrono::system_clock::now();
		// 直近の2回のボタン押下間の時間から BPM を計算する

		double bpm = 60 / elapsedSeconds;
		bpmSum += bpm;
		set_bpm(std::round(bpmSum / (bpmCount - 1)) * 10000);
	}
}

BOOL func_init(FILTER* fp) {
	power = 1;
	orig_bpm = 0;
	hModule = GetModuleHandle(_T("exedit.auf"));
	exeditfp = get_exeditfp(fp);
	fp->exfunc->add_menu_item(fp, "BPMを半分にする（グリッド間隔を広める）", fp->hwnd, 1, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	fp->exfunc->add_menu_item(fp, "BPMを2倍にする（グリッド間隔を狭める）", fp->hwnd, 2, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	fp->exfunc->add_menu_item(fp, "BPMを元に戻す", fp->hwnd, 3, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	fp->exfunc->add_menu_item(fp, "BPMを測定する", fp->hwnd, 4, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	return TRUE;
}

BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, FILTER* fp) {
	if (message == WM_FILTER_COMMAND) {
		if (LOWORD(wparam) == 1) change_bpm(static_cast<double>(1) / fp->track[0]);
		if (LOWORD(wparam) == 2) change_bpm(fp->track[0]);
		if (LOWORD(wparam) == 3) change_bpm(1 / power);
		if (LOWORD(wparam) == 4) meas_bpm();
	}
	else if (message == WM_COMMAND) {
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 0) change_bpm(static_cast<double>(1) / fp->track[0]);
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 1) change_bpm(fp->track[0]);
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 2) change_bpm(1 / power);
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 3) meas_bpm();
	}
	return FALSE;
}

static const char* check_name[] = { "↑BPMを半分にする（グリッド間隔を広める）↑", "↓BPMを2倍にする（グリッド間隔を狭める）↓" , "BPMを元に戻す" , "BPMを測定する" };
static int   check_def[] = { -1, -1, -1, -1 };
static const char* track_name[] = { "変更倍率" };
static int track_default[] = { 2 };	//	トラックバーの初期値
static int track_s[] = { 2 };	//	トラックバーの下限値
static int track_e[] = { 8 };	//	トラックバーの上限値

AviUtl::FilterPluginDLL filter = {
	AviUtl::FilterPlugin::Flag::AlwaysActive | AviUtl::FilterPlugin::Flag::ExInformation,
	0, 0,
	PLUGIN_NAME,
	1, track_name, track_default,
	track_s, track_e,
	4, check_name, check_def,
	NULL,
	func_init,
	NULL,
	NULL,
	func_WndProc,
	NULL,NULL,
	NULL,
	NULL,
	FULL_PLUGIN_NAME,
};

EXTERN_C AviUtl::FilterPluginDLL __declspec(dllexport)* __stdcall GetFilterTable(void) {
	return &filter;
}