#include <windows.h>
#include <tchar.h>
#include <aviutl/filter.hpp>
#include <cstdlib>
#include <filter.h>
#include <string>

#define FILTER AviUtl::FilterPlugin

#define PLUGIN_NAME "BPMグリッド倍化"
#define PLUGIN_VERSION " v1.0 by Garech"
#define FULL_PLUGIN_NAME PLUGIN_NAME PLUGIN_VERSION

double power;
int orig_bpm;

FILTER* get_exeditfp(FILTER* fp) {
	AviUtl::SysInfo si;
	fp->exfunc->get_sys_info(NULL, &si);

	for (int i = 0; i < si.filter_n; i++) {
		FILTER* tfp = (FILTER*)fp->exfunc->get_filterp(i);
		if (tfp->information != NULL) {
			if (!strcmp(tfp->information, "拡張編集(exedit) version 0.92 by ＫＥＮくん")) return tfp;
		}
	}
	MessageBox(NULL, TEXT("拡張編集 version 0.92 が導入されていない環境ではBPMグリッド倍化プラグインを使用できません。"), TEXT(PLUGIN_NAME), MB_OK);
	return NULL;
}

VOID change_bpm(FILTER* fp, double pw) {
	HMODULE hModule;
	hModule = GetModuleHandle(_T("exedit.auf"));
	FILTER* exeditfp;

	exeditfp = get_exeditfp(fp);
	if (hModule != NULL && exeditfp != NULL) {
		uintptr_t bpmAddress = reinterpret_cast<uintptr_t>(hModule) + 0x00159190;
		int32_t* bpmPointer = reinterpret_cast<int32_t*>(bpmAddress);
		int32_t bpmValue = *bpmPointer;

		if (bpmValue != (int)(orig_bpm * power)) {
			orig_bpm = bpmValue;
			power = 1;
		}

		power *= pw;

		bpmValue = orig_bpm * power;
		*bpmPointer = bpmValue;

		PostMessageA(exeditfp->hwnd, WM_COMMAND, 1091, -1);
		keybd_event(VK_RETURN, 0, KEYEVENTF_EXTENDEDKEY | 0, 0);

	}
}

BOOL func_init(FILTER* fp) {
	power = 1;
	orig_bpm = 0;
	fp->exfunc->add_menu_item(fp, "BPMを半分にする（グリッド間隔を広める）", fp->hwnd, 1, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	fp->exfunc->add_menu_item(fp, "BPMを2倍にする（グリッド間隔を狭める）", fp->hwnd, 2, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	fp->exfunc->add_menu_item(fp, "BPMを元に戻す", fp->hwnd, 3, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
	return TRUE;
}

BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, AviUtl::EditHandle* editp, FILTER* fp) {
	if (message == WM_FILTER_COMMAND) {
		if (LOWORD(wparam) == 1) change_bpm(fp, static_cast<double>(1) / fp->track[0]);
		if (LOWORD(wparam) == 2) change_bpm(fp, fp->track[0]);
		if (LOWORD(wparam) == 3) change_bpm(fp, 1 / power);
	}
	else if (message == WM_COMMAND) {
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 0) change_bpm(fp, static_cast<double>(1) / fp->track[0]);
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 1) change_bpm(fp, fp->track[0]);
		if (LOWORD(wparam) == MID_FILTER_BUTTON + 2) change_bpm(fp, 1 / power);
	}
	return FALSE;
}

static const char* check_name[] = { "↑BPMを半分にする（グリッド間隔を広める）↑", "↓BPMを2倍にする（グリッド間隔を狭める）↓" , "BPMを元に戻す"};
static int   check_def[] = { -1, -1, -1 };
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
	3, check_name, check_def,
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