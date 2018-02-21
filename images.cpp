// Images Screen Saver
//
// This saver shows a sprite (.BMP) bouncing over a background (.JPG)
// The two images have been compressed in a ZIP file, and this zip
// is stored as an RT_RCDATA resource in the .scr file. At runtime,
// the zip/bmp/jpg are all extracted directly from resource into
// memory: no temporary files are used.
//
// As for the sprites, this saver demonstrates several techniques:
// (1) How to load JPEGs from memory. If you want to add this to your own code,
// you must #include <ole2.h> and <olectl.h> and copy the LoadJpeg() function.
// (2) How to load BMPs from memory. The code is in EnsureBitmaps()
// (3) How to make transparent sprites. The sprite is actually stored as two
// bitmaps, hbmClip and hbmSprite. The code for loading/generating the two
// is in EnsureBitmaps(). The code for drawing them is in OnPaint()
// (4) How to use a double-buffering to avoid flicker. The bitmap hbmBuffer
// stores our back-buffer. It's created in TSaverWindow() and used in OnPaint().
// (5) How to read zip files. The code for this is in EnsureBitmaps. Also,
// it's in the separate module UNZIP.CPP/UNZIP.H, which consists largely
// of code from www.info-zip.org. Thanks!
//

#pragma warning( disable: 4127 4800 4702 )
#include <string>
#include <vector>
#include <math.h>
#include <windows.h>
#include <commctrl.h>
#include <regstr.h>
#include <ole2.h>
#include <olectl.h>
#include <stdlib.h>
#include "SystemInfo.h"
#include <tchar.h>
typedef std::basic_string<TCHAR> tstring;
using namespace std;
#include "unzip.h"

const int BIOSTEXTLEN = 1000;
//
// These global variables are loaded at the start of WinMain
BOOL  MuteSound;
DWORD MouseThreshold;  // In pixels
DWORD PasswordDelay;   // In seconds. Doesn't apply to NT/XP/Win2k.
// also these, which are used by the General properties dialog
DWORD PasswordDelayIndex;  // 0=seconds, 1=minutes. Purely a visual thing.
DWORD MouseThresholdIndex; // 0=high, 1=normal, 2=low, 3=ignore. Purely visual
TCHAR Corners[5];          // "-YN-" or something similar
BOOL  HotServices;         // whether they're present or not
// and these are created when the dialog/saver starts
POINT InitCursorPos;
DWORD InitTime;        // in ms
bool  IsDialogActive;
bool  ReallyClose;     // for NT, so we know if a WM_CLOSE came from us or it.
// Some other minor global variables and prototypes
HINSTANCE hInstance = 0;
HICON hiconsm = 0, hiconbg = 0;
HBITMAP hbmmonitor = 0;  // bitmap for the monitor class
tstring SaverName;     // this is retrieved at the start of WinMain from String Resource 1
vector<RECT> monitors; // the rectangles of each monitor (smSaver) or of the preview window (smPreview)
struct TSaverWindow;
const UINT SCRM_GETMONITORAREA = WM_APP; // gets the client rectangle. 0=all, 1=topleft, 2=topright, 3=botright, 4=botleft corner
tstring GetLastErrorString();
void SetDlgItemUrl(HWND hdlg, int id, const TCHAR *url);
void   RegSave(const tstring name, int val);
void   RegSave(const tstring name, bool val);
void   RegSave(const tstring name, tstring val);
int    RegLoad(const tstring name, int def);
bool   RegLoad(const tstring name, bool def);
tstring RegLoad(const tstring name, tstring def);

//
// IMPORTANT GLOBAL VARIABLES:
enum TScrMode { smNone, smConfig, smPassword, smPreview, smSaver, smInstall, smUninstall };
TScrMode ScrMode = smNone;
vector<TSaverWindow*> SaverWindow;   // the saver windows, one per monitor. In preview mode there's just one.

// LoadJpeg: given an HGLOBAL containing jpeg data, we load it.
HBITMAP LoadJpeg(HGLOBAL hglob) {
	IStream *stream = 0; 
	HRESULT hr = CreateStreamOnHGlobal(hglob, FALSE, &stream);
	if(!SUCCEEDED(hr) || stream == 0) return 0;
	IPicture *pic;  
	hr = OleLoadPicture(stream, 0, FALSE, IID_IPicture, (LPVOID*)&pic);
	stream->Release();
	if(!SUCCEEDED(hr) || pic == 0) return 0;
	HBITMAP hbm0 = 0; 
	hr = pic->get_Handle((OLE_HANDLE*)&hbm0);
	if(!SUCCEEDED(hr) || hbm0 == 0) { 
		pic->Release(); 
		return 0; 
	}	
	// Now we make a copy of it into our own hbm
	DIBSECTION dibs; 
	GetObject(hbm0, sizeof(dibs), &dibs);
	if(dibs.dsBm.bmBitsPixel != 24) { 
		pic->Release(); 
		return 0; 
	}
	int w = dibs.dsBm.bmWidth;
	int h = dibs.dsBm.bmHeight;
	dibs.dsBmih.biClrUsed = 0; 
	dibs.dsBmih.biClrImportant = 0; 
	void *bits;
	HDC sdc = GetDC(0);
	HBITMAP hbm1 = CreateDIBSection(sdc, (BITMAPINFO*)&dibs.dsBmih, DIB_RGB_COLORS, &bits, 0, 0);
	//
	HDC hdc0 = CreateCompatibleDC(sdc);
	HDC hdc1 = CreateCompatibleDC(sdc);
	HGDIOBJ hold0 = SelectObject(hdc0, hbm0);
	HGDIOBJ hold1 = SelectObject(hdc1, hbm1);
	BitBlt(hdc1, 0, 0, w, h, hdc0, 0, 0, SRCCOPY);
	SelectObject(hdc0, hold0); 
	SelectObject(hdc1, hold1);
	DeleteDC(hdc0); 
	DeleteDC(hdc1);
	ReleaseDC(0, sdc);
	pic->Release();
	return hbm1;
}




// TSaverWindow: one is created for each saver window (be it preview, or the
// preview in the config dialog, or one for each monitor when running full-screen)
//
struct TSaverWindow {
	HWND hwnd; int id;          // id=-1 for a preview, or 0..n for full-screen on the specified monitor
	int bw, bh, sw, sh, cw, ch;    // dimensions of the background and sprite and client-area
	int x, y, dirx, diry;         // location and direction of the sprite
	unsigned int time;          // last time when we drew anything
	HBITMAP hbmBackground;      // the background
	HBITMAP hbmSprite, hbmClip; // the foreground object
	HBITMAP hbmBuffer;          // we use double-buffering
	SYSTEMTIME st;
	boolean bDone = false;
	char buffer[100];
	int n, s, prev_sec;
	SystemInfo *mySystemInfo;
	char sText[BIOSTEXTLEN] = { 0 };
	//
	TSaverWindow(HWND _hwnd, int _id) : hwnd(_hwnd), id(_id), hbmBackground(0), hbmSprite(0), hbmClip(0), hbmBuffer(0) {
		RECT rc; GetClientRect(hwnd, &rc); cw = rc.right; ch = rc.bottom;
		EnsureGraphicsLoaded();
		BITMAP bmp; 
		GetObject(hbmBackground, sizeof(bmp), &bmp); 
		bw = bmp.bmWidth; 
		bh = bmp.bmHeight;
		GetObject(hbmSprite, sizeof(bmp), &bmp); 
		sw = bmp.bmWidth; 
		sh = bmp.bmHeight;
		//
		GetSystemTime(&st);
		srand(st.wMilliseconds);

		x = (rand() + rand()) % (cw - sw);
		y = (rand() + rand()) % (ch - sh);
		dirx = (rand() % 5) - 1;
		diry = (rand() % 5) - 1;
		if(dirx == 0 && diry == 0) {
			dirx = 1; diry = 1;
		}
		time = GetTickCount();
		s = n = 0;
		prev_sec = -1;
		mySystemInfo = new SystemInfo();
		char sText2[BIOSTEXTLEN] = { 0 };
		mySystemInfo->getSystem(sText2, BIOSTEXTLEN - 1);
		s = sprintf_s(sText, BIOSTEXTLEN - 1, "System=%s User=%s Computer=%s",
			sText2,
			mySystemInfo->getUserName(),
			mySystemInfo->getComputerName());
		SetTimer(hwnd, 1, 50, NULL);
	}

	void EnsureGraphicsLoaded();
	void OtherWndProc(UINT, WPARAM, LPARAM) {}

	~TSaverWindow() {
		KillTimer(hwnd, 1);
		if(hbmBackground != 0) DeleteObject(hbmBackground); hbmBackground = 0;
		if(hbmSprite != 0) DeleteObject(hbmSprite); hbmSprite = 0;
		if(hbmClip != 0) DeleteObject(hbmClip); hbmClip = 0;
		if(hbmBuffer != 0) DeleteObject(hbmBuffer); hbmBuffer = 0;
		delete mySystemInfo;
	}


	void OnTimer() {
		unsigned int nowt = GetTickCount();
		int mul = (nowt - time) / 10;
		if(mul == 0) mul = 1;
		time = nowt;
		x += dirx * mul; y += diry * mul;
		if(x < 0) { x = 0; dirx = 1; diry = (rand() % 5) - 1; }
		if(x + sw >= cw) { x = cw - sw; dirx = -1; diry = (rand() % 5) - 1; }
		if(y < 0) { y = 0; diry = 1; dirx = (rand() % 5) - 1; }
		if(y + sh >= ch) { y = ch - sh; diry = -1; dirx = (rand() % 5) - 1; }		
		GetSystemTime(&st);
		int new_sec = st.wSecond;
		if(prev_sec != new_sec) {
			n = sprintf(buffer, "%d:%02d:%02d", st.wHour, st.wMinute, new_sec);
			prev_sec = new_sec;
		}
		InvalidateRect(hwnd, NULL, TRUE);
	}

	void OnPaint(HDC hdc, const RECT &rect) {		
		HDC bufdc = CreateCompatibleDC(hdc);
		SelectObject(bufdc, hbmBuffer);
		HDC memdc = CreateCompatibleDC(hdc);
		//		
		if(!bDone) {
			boolean bHasWhite = false;
			BITMAP  bm;
			GetObject(hbmBackground, sizeof(bm), &bm);
			unsigned char* buf = reinterpret_cast<unsigned char*>(bm.bmBits);
			size_t size = bm.bmWidth * bm.bmHeight * static_cast<size_t>(bm.bmBitsPixel * 0.125f);
			for(int i = 0; i < size; i++) {
				if(buf[i]) {
					buf[i]--;
				}
				bHasWhite |= buf[i];
			}
			bDone = !bHasWhite;
		}		
		//
		SelectObject(memdc, hbmBackground);
		SetStretchBltMode(bufdc, COLORONCOLOR);
		StretchBlt(bufdc, 0, 0, cw, ch, memdc, 0, 0, bw, bh, SRCCOPY);
		SelectObject(memdc, hbmClip);
		BitBlt(bufdc, x, y, sw, sh, memdc, 0, 0, SRCAND);
		SelectObject(memdc, hbmSprite);
		BitBlt(bufdc, x, y, sw, sh, memdc, 0, 0, SRCPAINT);
		DeleteDC(memdc);
		BitBlt(hdc, 0, 0, cw, ch, bufdc, 0, 0, SRCCOPY);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(0x30, 0x30, 0xA0));
		TextOut(hdc, rect.right - 70, 1, buffer, n);
		if(!bDone)
			TextOut(hdc, 1, 1, sText, s);
		DeleteDC(bufdc);		
	}
};





void TSaverWindow::EnsureGraphicsLoaded() {
	if(hbmBuffer == 0) {
		HDC sdc = GetDC(0);
		hbmBuffer = CreateCompatibleBitmap(sdc, cw, ch);
		ReleaseDC(0, sdc);
	}
	// As for the others, we won't load up the resource-zip if we don't have to:
	if(hbmBackground != 0 && hbmSprite != 0) return;
	//
	HRSRC hrsrc = FindResource(hInstance, _T("ZIPFILE"), RT_RCDATA); if(hrsrc == 0) return;
	DWORD size = SizeofResource(hInstance, hrsrc); if(size == 0) return;
	HGLOBAL hglob = LoadResource(hInstance, hrsrc); if(hglob == 0) return;
	void *buf = LockResource(hglob); if(buf == 0) return;
	HZIP hzip = OpenZip(buf, size, ZIP_MEMORY); if(hzip == 0) return;
	//
	if(hbmBackground == 0) {
		ZIPENTRY ze; int index; FindZipItem(hzip, "background.jpg", true, &index, &ze);
		if(index != -1) {
			HGLOBAL hglob = GlobalAlloc(GMEM_MOVEABLE, ze.unc_size);
			void *buf = GlobalLock(hglob);
			UnzipItem(hzip, index, buf, ze.unc_size, ZIP_MEMORY);
			GlobalUnlock(hglob);
			hbmBackground = LoadJpeg(hglob);
			GlobalFree(hglob);
		}
	}

	if(hbmSprite == 0) {
		ZIPENTRY ze; int index; FindZipItem(hzip, "sprite.bmp", true, &index, &ze);
		if(index != -1) {
			vector<byte> vbuf(ze.unc_size); byte *buf = &vbuf[0];
			UnzipItem(hzip, index, &buf[0], ze.unc_size, ZIP_MEMORY);
			BITMAPFILEHEADER *bfh = (BITMAPFILEHEADER*)buf;
			BITMAPINFOHEADER *bih = (BITMAPINFOHEADER*)(buf + sizeof(BITMAPFILEHEADER));
			int ncols = bih->biClrUsed; if(ncols == 0) ncols = 1 << bih->biBitCount;
			char *srcbits = (char*)(buf + bfh->bfOffBits);
			char *dstbits; hbmSprite = CreateDIBSection(NULL, (BITMAPINFO*)bih, DIB_RGB_COLORS, (void**)&dstbits, NULL, 0);
			unsigned int numbytes = bih->biSizeImage;
			if(numbytes == 0) numbytes = ((bih->biWidth*bih->biBitCount / 8 + 3) & 0xFFFFFFFC)*bih->biHeight;
			CopyMemory(dstbits, srcbits, numbytes);
			//
			BITMAP bmp; GetObject(hbmSprite, sizeof(BITMAP), &bmp);
			int w = bmp.bmWidth, h = bmp.bmHeight;
			//
			// Now we do sprite stuff: take the top-left-pixel-color as transparent.
			HDC screendc = GetDC(0);
			HDC bitdc = CreateCompatibleDC(screendc);
			HGDIOBJ holdb = SelectObject(bitdc, hbmSprite);
			SetBkColor(bitdc, RGB(0, 0, 0));
			//
			typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[2]; } MONOBITMAPINFO;
			MONOBITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = w;
			bmi.bmiHeader.biHeight = h;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 1;
			bmi.bmiHeader.biCompression = BI_RGB;
			bmi.bmiHeader.biSizeImage = ((w + 7) & 0xFFFFFFF8)*w / 8;
			bmi.bmiHeader.biXPelsPerMeter = 1000000;
			bmi.bmiHeader.biYPelsPerMeter = 1000000;
			bmi.bmiHeader.biClrUsed = 0;
			bmi.bmiHeader.biClrImportant = 0;
			bmi.bmiColors[0].rgbRed = 0;  bmi.bmiColors[0].rgbGreen = 0;  bmi.bmiColors[0].rgbBlue = 0;  bmi.bmiColors[0].rgbReserved = 0;
			bmi.bmiColors[1].rgbRed = 255; bmi.bmiColors[1].rgbGreen = 255; bmi.bmiColors[1].rgbBlue = 255; bmi.bmiColors[1].rgbReserved = 0;
			hbmClip = CreateDIBSection(screendc, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, (void**)&dstbits, NULL, 0);
			// Now create a mask, by blting the image onto the monochrome DDB, and onto DIB
			// (if you blt straight from image to mask, it choose closest mask colour, not absolute mask colour)
			HDC monodc = CreateCompatibleDC(screendc);
			HDC maskdc = CreateCompatibleDC(screendc);
			HBITMAP hmonobm = CreateBitmap(w, h, 1, 1, NULL);
			HGDIOBJ holdm = SelectObject(monodc, hmonobm);
			HGDIOBJ holdmask = SelectObject(maskdc, hbmClip);
			COLORREF transp = GetPixel(bitdc, 0, 0);
			SetBkColor(bitdc, transp); // use top-left pixel as transparent colour
			BitBlt(monodc, 0, 0, w, h, bitdc, 0, 0, SRCCOPY);
			BitBlt(maskdc, 0, 0, w, h, monodc, 0, 0, SRCCOPY);
			// the mask has 255 for the masked areas, and 0 for the real image areas.
			// Well, that has created the mask. Now we have to zero-out the original bitmap's masked area
			BitBlt(bitdc, 0, 0, w, h, monodc, 0, 0, 0x00220326);
			// 0x00220326 is the ternary raster operation 'DSna', which is reverse-polish for "bitdc AND (NOT monodc)"
			SelectObject(maskdc, holdmask); DeleteDC(maskdc);
			SelectObject(monodc, holdm); DeleteDC(monodc); DeleteObject(hmonobm);
			// 
			SelectObject(bitdc, holdb); DeleteDC(bitdc);
			ReleaseDC(0, screendc);
		}
	}

	CloseZip(hzip);
}

void ReadGeneralRegistry() {
	PasswordDelay = 15; 
	PasswordDelayIndex = 0; 
	MouseThreshold = 50; 
	IsDialogActive = false; // default values in case they're not in registry
	LONG res; 
	HKEY skey; 
	DWORD valtype, valsize, val;
	res = RegOpenKeyEx(HKEY_CURRENT_USER, REGSTR_PATH_SETUP _T("\\Screen Savers"), 0, KEY_READ, &skey);
	if(res != ERROR_SUCCESS) return;
	valsize = sizeof(val); 
	res = RegQueryValueEx(skey, _T("Password Delay"), 0, &valtype, (LPBYTE)&val, &valsize); 
	if(res == ERROR_SUCCESS) PasswordDelay = val;
	valsize = sizeof(val); 
	res = RegQueryValueEx(skey, _T("Password Delay Index"), 0, &valtype, (LPBYTE)&val, &valsize); 
	if(res == ERROR_SUCCESS) PasswordDelayIndex = val;
	valsize = sizeof(val); 
	res = RegQueryValueEx(skey, _T("Mouse Threshold"), 0, &valtype, (LPBYTE)&val, &valsize); 
	if(res == ERROR_SUCCESS) MouseThreshold = val;
	valsize = sizeof(val); 
	res = RegQueryValueEx(skey, _T("Mouse Threshold Index"), 0, &valtype, (LPBYTE)&val, &valsize); 
	if(res == ERROR_SUCCESS) MouseThresholdIndex = val;
	valsize = sizeof(val); 
	res = RegQueryValueEx(skey, _T("Mute Sound"), 0, &valtype, (LPBYTE)&val, &valsize);    
	if(res == ERROR_SUCCESS) MuteSound = val;
	valsize = 5 * sizeof(TCHAR); 
	res = RegQueryValueEx(skey, _T("Mouse Corners"), 0, &valtype, (LPBYTE)Corners, &valsize);
	for(int i = 0; i < 4; i++) { 
		if(Corners[i] != 'Y' && Corners[i] != 'N') 
			Corners[i] = '-'; 
	} 
	Corners[4] = 0;
	RegCloseKey(skey);
	//
	HotServices = FALSE;
	HINSTANCE sagedll = LoadLibrary(_T("sage.dll"));
	if(sagedll == 0) sagedll = LoadLibrary(_T("scrhots.dll"));
	if(sagedll != 0) {
		typedef BOOL(WINAPI *SYSTEMAGENTDETECT)();
		SYSTEMAGENTDETECT detectproc = (SYSTEMAGENTDETECT)GetProcAddress(sagedll, "System_Agent_Detect");
		if(detectproc != NULL) HotServices = detectproc();
		FreeLibrary(sagedll);
	}
}

void WriteGeneralRegistry() {
	LONG res; HKEY skey; DWORD val;
	res = RegCreateKeyEx(HKEY_CURRENT_USER, REGSTR_PATH_SETUP _T("\\Screen Savers"), 0, 0, 0, KEY_ALL_ACCESS, 0, &skey, 0);
	if(res != ERROR_SUCCESS) return;
	val = PasswordDelay; RegSetValueEx(skey, _T("Password Delay"), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
	val = PasswordDelayIndex; RegSetValueEx(skey, _T("Password Delay Index"), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
	val = MouseThreshold; RegSetValueEx(skey, _T("Mouse Threshold"), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
	val = MouseThresholdIndex; RegSetValueEx(skey, _T("Mouse Threshold Index"), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
	val = MuteSound ? 1 : 0; RegSetValueEx(skey, _T("Mute Sound"), 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
	RegSetValueEx(skey, _T("Mouse Corners"), 0, REG_SZ, (const BYTE*)Corners, 5 * sizeof(TCHAR));
	RegCloseKey(skey);
	//
	HINSTANCE sagedll = LoadLibrary(_T("sage.dll"));
	if(sagedll == 0) sagedll = LoadLibrary(_T("scrhots.dll"));
	if(sagedll != 0) {
		typedef VOID(WINAPI *SCREENSAVERCHANGED)();
		SCREENSAVERCHANGED changedproc = (SCREENSAVERCHANGED)GetProcAddress(sagedll, "Screen_Saver_Changed");
		if(changedproc != NULL) changedproc();
		FreeLibrary(sagedll);
	}
}

LRESULT CALLBACK SaverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TSaverWindow *sav; int id; HWND hmain;
#pragma warning( push )
#pragma warning( disable: 4244 4312 )
	if(msg == WM_CREATE) {
		CREATESTRUCT *cs = (CREATESTRUCT*)lParam; id = *(int*)cs->lpCreateParams; SetWindowLong(hwnd, 0, id);
		sav = new TSaverWindow(hwnd, id); SetWindowLong(hwnd, GWL_USERDATA, (LONG)sav);
		SaverWindow.push_back(sav);
	} else {
		sav = (TSaverWindow*)GetWindowLong(hwnd, GWL_USERDATA);
		id = GetWindowLong(hwnd, 0);
	}
#pragma warning( pop )
	if(id <= 0) hmain = hwnd; else hmain = SaverWindow[0]->hwnd;
	//
	if(msg == WM_TIMER) sav->OnTimer();
	else if(msg == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc); if(sav != 0) sav->OnPaint(ps.hdc, rc); EndPaint(hwnd, &ps); } else if(sav != 0) sav->OtherWndProc(msg, wParam, lParam);
	//
	switch(msg) {
	case WM_ACTIVATE:
	case WM_ACTIVATEAPP:
	case WM_NCACTIVATE:
	{
		TCHAR pn[100];
		GetClassName((HWND)lParam, pn, 100);
		bool ispeer = (_tcsncmp(pn, _T("ScrClass"), 8) == 0);
		if(ScrMode == smSaver && !IsDialogActive && LOWORD(wParam) == WA_INACTIVE && !ispeer) {			
			ReallyClose = true; 
			PostMessage(hmain, WM_CLOSE, 0, 0); 
			return 0;
		}
	} break;
	case WM_SETCURSOR:
	{
		if(ScrMode == smSaver && !IsDialogActive) {
			SetCursor(NULL);
		} else {
			SetCursor(LoadCursor(NULL, IDC_ARROW));
		}
	} return 0;
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_KEYDOWN:
	{
		if(ScrMode == smSaver && !IsDialogActive) {			
			ReallyClose = true;
			PostMessage(hmain, WM_CLOSE, 0, 0);
			return 0;
		}
	} break;
	case WM_MOUSEMOVE:
	{
		if(ScrMode == smSaver && !IsDialogActive) {
			POINT pt; GetCursorPos(&pt);
			int dx = pt.x - InitCursorPos.x; if(dx < 0) dx = -dx; int dy = pt.y - InitCursorPos.y; if(dy < 0) dy = -dy;
			if(dx > (int)MouseThreshold || dy > (int)MouseThreshold) {				
				ReallyClose = true; 
				PostMessage(hmain, WM_CLOSE, 0, 0);
			}
		}
	} return 0;
	case (WM_SYSCOMMAND):
	{
		if(ScrMode == smSaver) {
			if(wParam == SC_SCREENSAVE) {
				return 0;
			}
			if(wParam == SC_CLOSE) {
				return 0;
			}
		}
	} break;
	case (WM_CLOSE):
	{ if(id > 0) return 0; // secondary windows ignore this message
	if(id == -1) { DestroyWindow(hwnd); return 0; } // preview windows close obediently
	if(ReallyClose && !IsDialogActive) {
		BOOL CanClose = TRUE;
		if(GetTickCount() - InitTime > 1000 * PasswordDelay) {
			IsDialogActive = true; SendMessage(hwnd, WM_SETCURSOR, 0, 0);
			IsDialogActive = false; SendMessage(hwnd, WM_SETCURSOR, 0, 0); GetCursorPos(&InitCursorPos);
		}
		// note: all secondary monitors are owned by the primary. And we're the primary (id==0)
		// therefore, when we destroy ourself, windows will destroy the others first
		if(CanClose) { 			
			DestroyWindow(hwnd); 
		} 
	}
	} return 0;
	case (WM_DESTROY):
	{
		SetWindowLong(hwnd, 0, 0);
		SetWindowLong(hwnd, GWL_USERDATA, 0);
		for(vector<TSaverWindow*>::iterator i = SaverWindow.begin(); i != SaverWindow.end(); i++) {
			if(sav == *i)
				*i = 0;
		}
		delete sav;
		if((id == 0 && ScrMode == smSaver) || ScrMode == smPreview)
			PostQuitMessage(0);
	} break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ENUM-MONITOR-CALLBACK is part of DoSaver. Its stuff is in windef.h but
// requires WINVER>=0x0500. Well, we're doing LoadLibrary, so we know it's
// safe...
#ifndef SM_CMONITORS
DECLARE_HANDLE(HMONITOR);
#endif
//
BOOL CALLBACK EnumMonitorCallback(HMONITOR, HDC, LPRECT rc, LPARAM) {
	if(rc->left == 0 && rc->top == 0) monitors.insert(monitors.begin(), *rc);
	else monitors.push_back(*rc);
	return TRUE;
}

void DoSaver(HWND hparwnd, bool fakemulti) {
	if(ScrMode == smPreview) {
		RECT rc; GetWindowRect(hparwnd, &rc); monitors.push_back(rc);
	} else if(fakemulti) {
		int w = GetSystemMetrics(SM_CXSCREEN), x1 = w / 4, x2 = w * 2 / 3, h = x2 - x1; RECT rc;
		rc.left = x1; rc.top = x1; rc.right = x1 + h; rc.bottom = x1 + h; monitors.push_back(rc);
		rc.left = 0; rc.top = x1; rc.right = x1; rc.bottom = x1 + x1; monitors.push_back(rc);
		rc.left = x2; rc.top = x1 + h + x2 - w; rc.right = w; rc.bottom = x1 + h; monitors.push_back(rc);
	} else {
		int num_monitors = GetSystemMetrics(80); // 80=SM_CMONITORS
		if(num_monitors > 1) {
			typedef BOOL(CALLBACK *LUMONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);
			typedef BOOL(WINAPI *LUENUMDISPLAYMONITORS)(HDC, LPCRECT, LUMONITORENUMPROC, LPARAM);
			HINSTANCE husr = LoadLibrary(_T("user32.dll"));
			LUENUMDISPLAYMONITORS pEnumDisplayMonitors = 0;
			if(husr != NULL) pEnumDisplayMonitors = (LUENUMDISPLAYMONITORS)GetProcAddress(husr, "EnumDisplayMonitors");
			if(pEnumDisplayMonitors != NULL) (*pEnumDisplayMonitors)(NULL, NULL, EnumMonitorCallback, NULL);
			if(husr != NULL) FreeLibrary(husr);
		}
		if(monitors.size() == 0) {
			RECT rc; rc.left = 0; rc.top = 0; rc.right = GetSystemMetrics(SM_CXSCREEN); rc.bottom = GetSystemMetrics(SM_CYSCREEN);
			monitors.push_back(rc);
		}
	}
	//
	HWND hwnd = 0;
	if(ScrMode == smPreview) {
		RECT rc; GetWindowRect(hparwnd, &rc); int w = rc.right - rc.left, h = rc.bottom - rc.top;
		int id = -1; hwnd = CreateWindowEx(0, _T("ScrClass"), _T(""), WS_CHILD | WS_VISIBLE, 0, 0, w, h, hparwnd, NULL, hInstance, &id);
	} else {
		GetCursorPos(&InitCursorPos); InitTime = GetTickCount();
		for(int i = 0; i < (int)monitors.size(); i++) {
			const RECT &rc = monitors[i];
			DWORD exstyle = WS_EX_TOPMOST;
			HWND hthis = CreateWindowEx(exstyle, _T("ScrClass"), _T(""), WS_POPUP | WS_VISIBLE, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hwnd, NULL, hInstance, &i);
			if(i == 0) hwnd = hthis;
		}
	}
	if(hwnd != NULL) {
		UINT oldval;
		if(ScrMode == smSaver) SystemParametersInfo(SPI_SETSCREENSAVERRUNNING, 1, &oldval, 0);
		MSG msg;
		while(GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if(ScrMode == smSaver) SystemParametersInfo(SPI_SETSCREENSAVERRUNNING, 0, &oldval, 0);
	}
	//
	SaverWindow.clear();
	return;
}

BOOL CALLBACK GeneralDlgProc(HWND hwnd, UINT msg, WPARAM, LPARAM lParam) {
	switch(msg) {
	case (WM_INITDIALOG):
	{
		ShowWindow(GetDlgItem(hwnd, HotServices ? 102 : 101), SW_HIDE);
		SetDlgItemText(hwnd, 112, Corners);
		SendDlgItemMessage(hwnd, 109, CB_ADDSTRING, 0, (LPARAM)_T("seconds"));
		SendDlgItemMessage(hwnd, 109, CB_ADDSTRING, 0, (LPARAM)_T("minutes"));
		SendDlgItemMessage(hwnd, 109, CB_SETCURSEL, PasswordDelayIndex, 0);
		int n = PasswordDelay; if(PasswordDelayIndex == 1) n /= 60;
		TCHAR c[16]; wsprintf(c, _T("%i"), n); SetDlgItemText(hwnd, 107, c);
		SendDlgItemMessage(hwnd, 108, UDM_SETRANGE, 0, MAKELONG(99, 0));
		SendDlgItemMessage(hwnd, 105, CB_ADDSTRING, 0, (LPARAM)_T("High"));
		SendDlgItemMessage(hwnd, 105, CB_ADDSTRING, 0, (LPARAM)_T("Normal"));
		SendDlgItemMessage(hwnd, 105, CB_ADDSTRING, 0, (LPARAM)_T("Low"));
		SendDlgItemMessage(hwnd, 105, CB_ADDSTRING, 0, (LPARAM)_T("Keyboard only (ignore mouse movement)"));
		SendDlgItemMessage(hwnd, 105, CB_SETCURSEL, MouseThresholdIndex, 0);
		if(MuteSound) CheckDlgButton(hwnd, 113, BST_CHECKED);
		OSVERSIONINFO ver; ZeroMemory(&ver, sizeof(ver)); ver.dwOSVersionInfoSize = sizeof(ver); GetVersionEx(&ver);
		for(int i = 106; i < 111 && ver.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS; i++)
			ShowWindow(GetDlgItem(hwnd, i), SW_HIDE);
	} return TRUE;
	case (WM_NOTIFY):
	{
		LPNMHDR nmh = (LPNMHDR)lParam; UINT code = nmh->code;
		switch(code) {
		case (PSN_APPLY):
		{
			GetDlgItemText(hwnd, 112, Corners, 5);
			PasswordDelayIndex = SendDlgItemMessage(hwnd, 109, CB_GETCURSEL, 0, 0);
			TCHAR c[16]; GetDlgItemText(hwnd, 107, c, 16);
			int n = _ttoi(c);
			if(PasswordDelayIndex == 1) n *= 60; PasswordDelay = n;
			MouseThresholdIndex = SendDlgItemMessage(hwnd, 105, CB_GETCURSEL, 0, 0);
			if(MouseThresholdIndex == 0)
				MouseThreshold = 0;
			else if(MouseThresholdIndex == 1)
				MouseThreshold = 200;
			else if
				(MouseThresholdIndex == 2) MouseThreshold = 400;
			else MouseThreshold = 999999;
			MuteSound = (IsDlgButtonChecked(hwnd, 113) == BST_CHECKED);
			WriteGeneralRegistry();
			SetWindowLong(hwnd, DWL_MSGRESULT, PSNRET_NOERROR);
		} return TRUE;
		}
	} return FALSE;
	}
	return FALSE;
}

//
// MONITOR CONTROL -- either corners or a preview
//
LRESULT CALLBACK MonitorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_CREATE:
	{ TCHAR c[5]; GetWindowText(hwnd, c, 5); if(*c != 0) return 0;
	int id = -1; RECT rc; SendMessage(hwnd, SCRM_GETMONITORAREA, 0, (LPARAM)&rc);
	CreateWindow(_T("ScrClass"), _T(""), WS_CHILD | WS_VISIBLE, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, hwnd, NULL, hInstance, &id);
	} return 0;
	case WM_PAINT:
	{ if(hbmmonitor == 0) hbmmonitor = LoadBitmap(hInstance, _T("Monitor"));
	RECT rc; GetClientRect(hwnd, &rc);
	//
	PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
	HBITMAP hback = (HBITMAP)GetWindowLong(hwnd, GWL_USERDATA);
	if(hback != 0) {
		BITMAP bmp; GetObject(hback, sizeof(bmp), &bmp);
		if(bmp.bmWidth != rc.right || bmp.bmHeight != rc.bottom) { DeleteObject(hback); hback = 0; }
	}
	if(hback == 0) { hback = CreateCompatibleBitmap(ps.hdc, rc.right, rc.bottom); SetWindowLong(hwnd, GWL_USERDATA, (LONG)hback); }
	HDC backdc = CreateCompatibleDC(ps.hdc);
	HGDIOBJ holdback = SelectObject(backdc, hback);
	BitBlt(backdc, 0, 0, rc.right, rc.bottom, ps.hdc, 0, 0, SRCCOPY);
	//
	TCHAR corners[5]; GetWindowText(hwnd, corners, 5);
	HDC hdc = CreateCompatibleDC(ps.hdc);
	HGDIOBJ hold = SelectObject(hdc, hbmmonitor);
	StretchBlt(backdc, 0, 0, rc.right, rc.bottom, hdc, 0, 0, 184, 170, SRCAND);
	StretchBlt(backdc, 0, 0, rc.right, rc.bottom, hdc, 184, 0, 184, 170, SRCINVERT);
	RECT crc; SendMessage(hwnd, SCRM_GETMONITORAREA, 0, (LPARAM)&crc);
	//
	if(*corners != 0) FillRect(backdc, &crc, GetSysColorBrush(COLOR_DESKTOP));
	for(int i = 0; i < 4 && *corners != 0; i++) {
		RECT crc; SendMessage(hwnd, SCRM_GETMONITORAREA, i + 1, (LPARAM)&crc);
		int y = 0; if(corners[i] == 'Y') y = 22; else if(corners[i] == 'N') y = 44;
		BitBlt(backdc, crc.left, crc.top, crc.right - crc.left, crc.bottom - crc.top, hdc, 368, y, SRCCOPY);
		if(!HotServices) {
			DWORD col = GetSysColor(COLOR_DESKTOP);
			for(int y = crc.top; y < crc.bottom; y++) {
				for(int x = crc.left + (y & 1); x < crc.right; x += 2) SetPixel(backdc, x, y, col);
			}
		}
	}
	SelectObject(hdc, hold);
	DeleteDC(hdc);
	//
	BitBlt(ps.hdc, 0, 0, rc.right, rc.bottom, backdc, 0, 0, SRCCOPY);
	SelectObject(backdc, holdback);
	DeleteDC(backdc);
	EndPaint(hwnd, &ps);
	} return 0;
	case SCRM_GETMONITORAREA:
	{ RECT *prc = (RECT*)lParam;
	if(hbmmonitor == 0) hbmmonitor = LoadBitmap(hInstance, _T("Monitor"));
	// those are the client coordinates unscalled
	RECT wrc; GetClientRect(hwnd, &wrc); int ww = wrc.right, wh = wrc.bottom;
	RECT rc; rc.left = 16 * ww / 184; rc.right = 168 * ww / 184; rc.top = 17 * wh / 170; rc.bottom = 130 * wh / 170;
	*prc = rc; if(wParam == 0) return 0;
	if(wParam == 1) { prc->right = rc.left + 24; prc->bottom = rc.top + 22; } else if(wParam == 2) { prc->left = rc.right - 24; prc->bottom = rc.top + 22; } else if(wParam == 3) { prc->left = rc.right - 24; prc->top = rc.bottom - 22; } else if(wParam == 4) { prc->right = rc.left + 24; prc->top = rc.bottom - 22; }
	} return 0;
	case WM_LBUTTONDOWN:
	{ if(!HotServices) return 0;
	int x = LOWORD(lParam), y = HIWORD(lParam);
	TCHAR corners[5]; GetWindowText(hwnd, corners, 5);
	if(corners[0] == 0) return 0;
	int click = -1; for(int i = 0; i < 4; i++) {
		RECT rc; SendMessage(hwnd, SCRM_GETMONITORAREA, i + 1, (LPARAM)&rc);
		if(x >= rc.left && y >= rc.top && x < rc.right && y < rc.bottom) { click = i; break; }
	}
	if(click == -1) return 0;
	for(int j = 0; j < 4; j++) {
		if(corners[j] != '-' && corners[j] != 'Y' && corners[j] != 'N') corners[j] = '-';
	}
	corners[4] = 0;
	//
	HMENU hmenu = CreatePopupMenu();
	MENUITEMINFO mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(MENUITEMINFO);
	mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE | MIIM_DATA;
	mi.fType = MFT_STRING | MFT_RADIOCHECK;
	mi.wID = 'N'; mi.fState = MFS_ENABLED; if(corners[click] == 'N') mi.fState |= MFS_CHECKED;
	mi.dwTypeData = _T("Never"); mi.cch = sizeof(TCHAR)*_tcslen(mi.dwTypeData);
	InsertMenuItem(hmenu, 0, TRUE, &mi);
	mi.wID = 'Y'; mi.fState = MFS_ENABLED; if(corners[click] == 'Y') mi.fState |= MFS_CHECKED;
	mi.dwTypeData = _T("Now"); mi.cch = sizeof(TCHAR)*_tcslen(mi.dwTypeData);
	InsertMenuItem(hmenu, 0, TRUE, &mi);
	mi.wID = '-'; mi.fState = MFS_ENABLED; if(corners[click] != 'Y' && corners[click] != 'N') mi.fState |= MFS_CHECKED;
	mi.dwTypeData = _T("Default"); mi.cch = sizeof(TCHAR)*_tcslen(mi.dwTypeData);
	InsertMenuItem(hmenu, 0, TRUE, &mi);
	POINT pt; pt.x = x; pt.y = y; ClientToScreen(hwnd, &pt);
	int cmd = TrackPopupMenuEx(hmenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, NULL);
	if(cmd != 0) corners[click] = (char)cmd;
	corners[4] = 0; SetWindowText(hwnd, corners);
	InvalidateRect(hwnd, NULL, FALSE);
	} return 0;
	case WM_DESTROY:
	{ HBITMAP hback = (HBITMAP)SetWindowLong(hwnd, GWL_USERDATA, 0);
	if(hback != 0) DeleteObject(hback);
	} return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}


BOOL CALLBACK AboutDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM) {
	if(msg == WM_INITDIALOG) {
		SetDlgItemText(hdlg, 101, SaverName.c_str());
		SetDlgItemUrl(hdlg, 102, _T("http://www.wischik.com/lu/scr/"));
		SetDlgItemText(hdlg, 102, _T("www.wischik.com/scr"));
		return TRUE;
	} else if(msg == WM_COMMAND) {
		int id = LOWORD(wParam);
		if(id == IDOK || id == IDCANCEL) EndDialog(hdlg, id);
		return TRUE;
	} else return FALSE;
}


//
// PROPERTY SHEET SUBCLASSING -- this is to stick an "About" option on the sysmenu.
//
WNDPROC OldSubclassProc = 0;
LRESULT CALLBACK PropertysheetSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if(msg == WM_SYSCOMMAND && wParam == 3500) {
		DialogBox(hInstance, _T("DLG_ABOUT"), hwnd, AboutDlgProc);
		return 0;
	}
	if(OldSubclassProc != NULL) return CallWindowProc(OldSubclassProc, hwnd, msg, wParam, lParam);
	else return DefWindowProc(hwnd, msg, wParam, lParam);
}

int CALLBACK PropertysheetCallback(HWND hwnd, UINT msg, LPARAM) {
	if(msg != PSCB_INITIALIZED) return 0;
	HMENU hsysmenu = GetSystemMenu(hwnd, FALSE);
	AppendMenu(hsysmenu, MF_SEPARATOR, 1, _T("-"));
	AppendMenu(hsysmenu, MF_STRING, 3500, _T("About..."));
	OldSubclassProc = (WNDPROC)SetWindowLong(hwnd, GWL_WNDPROC, (LONG)PropertysheetSubclassProc);
	return 0;
}


void DoConfig(HWND hpar) {
	hiconbg = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
	hiconsm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	//  
	PROPSHEETHEADER psh; ZeroMemory(&psh, sizeof(psh));
	PROPSHEETPAGE psp[1]; ZeroMemory(psp, sizeof(PROPSHEETPAGE));
	psp[0].dwSize = sizeof(psp[0]);
	psp[0].dwFlags = PSP_DEFAULT;
	psp[0].hInstance = hInstance;
	psp[0].pszTemplate = _T("DLG_GENERAL");
	psp[0].pfnDlgProc = GeneralDlgProc;
	psh.dwSize = sizeof(psh);
	psh.dwFlags = PSH_NOAPPLYNOW | PSH_PROPSHEETPAGE | PSH_USEHICON | PSH_USECALLBACK;
	psh.hwndParent = hpar;
	psh.hInstance = hInstance;
	psh.hIcon = hiconsm;
	tstring cap = _T("Options for ") + SaverName; psh.pszCaption = cap.c_str();
	psh.nPages = 1;
	psh.nStartPage = 0;
	psh.ppsp = psp;
	psh.pfnCallback = PropertysheetCallback;
	PropertySheet(&psh);
	if(hiconbg != 0) DestroyIcon(hiconbg); hiconbg = 0;
	if(hiconsm != 0) DestroyIcon(hiconsm); hiconsm = 0;
	if(hbmmonitor != 0) DeleteObject(hbmmonitor); hbmmonitor = 0;
}


// This routine is for using ScrPrev. It's so that you can start the saver
// with the command line /p scrprev and it runs itself in a preview window.
// You must first copy ScrPrev somewhere in your search path
HWND CheckForScrprev() {
	HWND hwnd = FindWindow(_T("Scrprev"), NULL); // looks for the Scrprev class
	if(hwnd == NULL) // try to load it
	{
		STARTUPINFO si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
		si.cb = sizeof(si);
		TCHAR cmd[MAX_PATH]; _tcscpy(cmd, _T("Scrprev")); // unicode CreateProcess requires it writeable
		BOOL cres = CreateProcess(NULL, cmd, 0, 0, FALSE, CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE,
			0, 0, &si, &pi);
		if(!cres)  return NULL;
		DWORD wres = WaitForInputIdle(pi.hProcess, 2000);
		if(wres == WAIT_TIMEOUT) return NULL; 
		if(wres == 0xFFFFFFFF)  return NULL;
		hwnd = FindWindow(_T("Scrprev"), NULL);
	}
	if(hwnd == NULL) return NULL;
	::SetForegroundWindow(hwnd);
	hwnd = GetWindow(hwnd, GW_CHILD);
	if(hwnd == NULL)  return NULL; 
	return hwnd;
}


void DoInstall() {
	TCHAR windir[MAX_PATH]; GetWindowsDirectory(windir, MAX_PATH);
	TCHAR tfn[MAX_PATH]; UINT ures = GetTempFileName(windir, _T("pst"), 0, tfn);
	if(ures == 0) { MessageBox(NULL, _T("You must be logged on as system administrator to install screen savers"), _T("Saver Install"), MB_ICONINFORMATION | MB_OK); return; }
	DeleteFile(tfn);
	tstring fn = tstring(windir) + _T("\\") + SaverName + _T(".scr");
	DWORD attr = GetFileAttributes(fn.c_str());
	bool exists = (attr != 0xFFFFFFFF);
	tstring msg = _T("Do you want to install '") + SaverName + _T("' ?");
	if(exists) msg += _T("\r\n\r\n(This will replace the version that you have currently)");
	int res = MessageBox(NULL, msg.c_str(), _T("Saver Install"), MB_YESNOCANCEL);
	if(res != IDYES) return;
	TCHAR cfn[MAX_PATH]; GetModuleFileName(hInstance, cfn, MAX_PATH);
	SetCursor(LoadCursor(NULL, IDC_WAIT));
	BOOL bres = CopyFile(cfn, fn.c_str(), FALSE);
	if(!bres) {
		tstring msg = _T("There was an error installing the saver.\r\n\r\n\"") + GetLastErrorString() + _T("\"");
		MessageBox(NULL, msg.c_str(), _T("Saver Install"), MB_ICONERROR | MB_OK);
		SetCursor(LoadCursor(NULL, IDC_ARROW));
		return;
	}
	LONG lres; HKEY skey; DWORD disp; tstring val;
	tstring key = REGSTR_PATH_UNINSTALL _T("\\") + SaverName;
	lres = RegCreateKeyEx(HKEY_LOCAL_MACHINE, key.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &skey, &disp);
	if(lres == ERROR_SUCCESS) {
		val = SaverName + _T(" saver"); RegSetValueEx(skey, _T("DisplayName"), 0, REG_SZ, (const BYTE*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
		val = _T("\"") + fn + _T("\" /u"); RegSetValueEx(skey, _T("UninstallString"), 0, REG_SZ, (const BYTE*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
		RegSetValueEx(skey, _T("UninstallPath"), 0, REG_SZ, (const BYTE*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
		val = _T("\"") + fn + _T("\""); RegSetValueEx(skey, _T("ModifyPath"), 0, REG_SZ, (const BYTE*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
		val = fn; RegSetValueEx(skey, _T("DisplayIcon"), 0, REG_SZ, (const BYTE*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
		TCHAR url[1024]; int ures = LoadString(hInstance, 2, url, 1024); if(ures != 0) RegSetValueEx(skey, _T("HelpLink"), 0, REG_SZ, (const BYTE*)url, sizeof(TCHAR)*(_tcslen(url) + 1));
		RegCloseKey(skey);
	}
	SHELLEXECUTEINFO sex; ZeroMemory(&sex, sizeof(sex)); sex.cbSize = sizeof(sex);
	sex.fMask = SEE_MASK_NOCLOSEPROCESS;
	sex.lpVerb = _T("install");
	sex.lpFile = fn.c_str();
	sex.nShow = SW_SHOWNORMAL;
	bres = ShellExecuteEx(&sex);
	if(!bres) { SetCursor(LoadCursor(NULL, IDC_ARROW)); MessageBox(NULL, _T("The saver has been installed"), SaverName.c_str(), MB_OK); return; }
	WaitForInputIdle(sex.hProcess, 2000);
	CloseHandle(sex.hProcess);
	SetCursor(LoadCursor(NULL, IDC_ARROW));
}

void DoUninstall() {
	tstring key = REGSTR_PATH_UNINSTALL _T("\\") + SaverName;
	RegDeleteKey(HKEY_LOCAL_MACHINE, key.c_str());
	TCHAR fn[MAX_PATH]; GetModuleFileName(hInstance, fn, MAX_PATH);
	SetFileAttributes(fn, FILE_ATTRIBUTE_NORMAL); // remove readonly if necessary
	BOOL res = MoveFileEx(fn, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	//
	const TCHAR *c = fn, *lastslash = c;
	while(*c != 0) { if(*c == '\\' || *c == '/') lastslash = c + 1; c++; }
	tstring cap = SaverName + _T(" uninstaller");
	tstring msg;
	if(res) msg = _T("Uninstall completed. The saver will be removed next time you reboot.");
	else msg = _T("There was a problem uninstalling.\r\n")
		_T("To complete the uninstall manually, you should go into your Windows ")
		_T("directory and delete the file '") + tstring(lastslash) + _T("'");
	MessageBox(NULL, msg.c_str(), cap.c_str(), MB_OK);
}


// --------------------------------------------------------------------------------
// SetDlgItemUrl(hwnd,IDC_MYSTATIC,"http://www.wischik.com/lu");
//   This routine turns a dialog's static text control into an underlined hyperlink.
//   You can call it in your WM_INITDIALOG, or anywhere.
//   It will also set the text of the control... if you want to change the text
//   back, you can just call SetDlgItemText() afterwards.
// --------------------------------------------------------------------------------
void SetDlgItemUrl(HWND hdlg, int id, const TCHAR *url);

// Implementation notes:
// We have to subclass both the static control (to set its cursor, to respond to click)
// and the dialog procedure (to set the font of the static control). Here are the two
// subclasses:
LRESULT CALLBACK UrlCtlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK UrlDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
// When the user calls SetDlgItemUrl, then the static-control-subclass is added
// if it wasn't already there, and the dialog-subclass is added if it wasn't
// already there. Both subclasses are removed in response to their respective
// WM_DESTROY messages. Also, each subclass stores a property in its window,
// which is a HGLOBAL handle to a GlobalAlloc'd structure:
typedef struct { TCHAR *url; WNDPROC oldproc; HFONT hf; HBRUSH hb; } TUrlData;
// I'm a miser and only defined a single structure, which is used by both
// the control-subclass and the dialog-subclass. Both of them use 'oldproc' of course.
// The control-subclass only uses 'url' (in response to WM_LBUTTONDOWN),
// and the dialog-subclass only uses 'hf' and 'hb' (in response to WM_CTLCOLORSTATIC)
// There is one sneaky thing to note. We create our underlined font *lazily*.
// Initially, hf is just NULL. But the first time the subclassed dialog received
// WM_CTLCOLORSTATIC, we sneak a peak at the font that was going to be used for
// the control, and we create our own copy of it but including the underline style.
// This way our code works fine on dialogs of any font.

// SetDlgItemUrl: this is the routine that sets up the subclassing.
void SetDlgItemUrl(HWND hdlg, int id, const TCHAR *url) { // nb. vc7 has crummy warnings about 32/64bit. My code's perfect! That's why I hide the warnings.
#pragma warning( push )
#pragma warning( disable: 4312 4244 )
// First we'll subclass the edit control
	HWND hctl = GetDlgItem(hdlg, id);
	SetWindowText(hctl, url);
	HGLOBAL hold = (HGLOBAL)GetProp(hctl, _T("href_dat"));
	if(hold != NULL) // if it had been subclassed before, we merely need to tell it the new url
	{
		TUrlData *ud = (TUrlData*)GlobalLock(hold);
		delete[] ud->url;
		ud->url = new TCHAR[_tcslen(url) + 1]; _tcscpy(ud->url, url);
	} else {
		HGLOBAL hglob = GlobalAlloc(GMEM_MOVEABLE, sizeof(TUrlData));
		TUrlData *ud = (TUrlData*)GlobalLock(hglob);
		ud->oldproc = (WNDPROC)GetWindowLong(hctl, GWL_WNDPROC);
		ud->url = new TCHAR[_tcslen(url) + 1]; _tcscpy(ud->url, url);
		ud->hf = 0; ud->hb = 0;
		GlobalUnlock(hglob);
		SetProp(hctl, _T("href_dat"), hglob);
		SetWindowLong(hctl, GWL_WNDPROC, (LONG)UrlCtlProc);
	}
	//
	// Second we subclass the dialog
	hold = (HGLOBAL)GetProp(hdlg, _T("href_dlg"));
	if(hold == NULL) {
		HGLOBAL hglob = GlobalAlloc(GMEM_MOVEABLE, sizeof(TUrlData));
		TUrlData *ud = (TUrlData*)GlobalLock(hglob);
		ud->url = 0;
		ud->oldproc = (WNDPROC)GetWindowLong(hdlg, GWL_WNDPROC);
		ud->hb = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
		ud->hf = 0; // the font will be created lazilly, the first time WM_CTLCOLORSTATIC gets called
		GlobalUnlock(hglob);
		SetProp(hdlg, _T("href_dlg"), hglob);
		SetWindowLong(hdlg, GWL_WNDPROC, (LONG)UrlDlgProc);
	}
#pragma warning( pop )
}

// UrlCtlProc: this is the subclass procedure for the static control
LRESULT CALLBACK UrlCtlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HGLOBAL hglob = (HGLOBAL)GetProp(hwnd, _T("href_dat"));
	if(hglob == NULL) return DefWindowProc(hwnd, msg, wParam, lParam);
	TUrlData *oud = (TUrlData*)GlobalLock(hglob); TUrlData ud = *oud;
	GlobalUnlock(hglob); // I made a copy of the structure just so I could GlobalUnlock it now, to be more local in my code
	switch(msg) {
	case WM_DESTROY:
	{ RemoveProp(hwnd, _T("href_dat")); GlobalFree(hglob);
	if(ud.url != 0) delete[] ud.url;
	// nb. remember that ud.url is just a pointer to a memory block. It might look weird
	// for us to delete ud.url instead of oud->url, but they're both equivalent.
	} break;
	case WM_LBUTTONDOWN:
	{ HWND hdlg = GetParent(hwnd); if(hdlg == 0) hdlg = hwnd;
	ShellExecute(hdlg, _T("open"), ud.url, NULL, NULL, SW_SHOWNORMAL);
	} break;
	case WM_SETCURSOR:
	{ HCURSOR hc = LoadCursor(NULL, MAKEINTRESOURCE(32649)); // =IDC_HAND
	if(hc == 0) hc = LoadCursor(NULL, IDC_ARROW); // systems before Win2k didn't have the hand
	SetCursor(hc);
	return TRUE;
	}
	case WM_NCHITTEST:
	{ return HTCLIENT; // because normally a static returns HTTRANSPARENT, so disabling WM_SETCURSOR
	}
	}
	return CallWindowProc(ud.oldproc, hwnd, msg, wParam, lParam);
}

// UrlDlgProc: this is the subclass procedure for the dialog
LRESULT CALLBACK UrlDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HGLOBAL hglob = (HGLOBAL)GetProp(hwnd, _T("href_dlg"));
	if(hglob == NULL) return DefWindowProc(hwnd, msg, wParam, lParam);
	TUrlData *oud = (TUrlData*)GlobalLock(hglob); TUrlData ud = *oud;
	GlobalUnlock(hglob);
	switch(msg) {
	case WM_DESTROY:
	{ RemoveProp(hwnd, _T("href_dlg")); GlobalFree(hglob);
	if(ud.hb != 0) DeleteObject(ud.hb);
	if(ud.hf != 0) DeleteObject(ud.hf);
	} break;
	case WM_CTLCOLORSTATIC:
	{ HDC hdc = (HDC)wParam; HWND hctl = (HWND)lParam;
	// To check whether to handle this control, we look for its subclassed property!
	HANDLE hprop = GetProp(hctl, _T("href_dat")); if(hprop == NULL) return CallWindowProc(ud.oldproc, hwnd, msg, wParam, lParam);
	// There has been a lot of faulty discussion in the newsgroups about how to change
	// the text colour of a static control. Lots of people mess around with the
	// TRANSPARENT text mode. That is incorrect. The correct solution is here:
	// (1) Leave the text opaque. This will allow us to re-SetDlgItemText without it looking wrong.
	// (2) SetBkColor. This background colour will be used underneath each character cell.
	// (3) return HBRUSH. This background colour will be used where there's no text.
	SetTextColor(hdc, RGB(0, 0, 255));
	SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
	if(ud.hf == 0) { // we use lazy creation of the font. That's so we can see font was currently being used.
		TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
		LOGFONT lf;
		lf.lfHeight = tm.tmHeight;
		lf.lfWidth = 0;
		lf.lfEscapement = 0;
		lf.lfOrientation = 0;
		lf.lfWeight = tm.tmWeight;
		lf.lfItalic = tm.tmItalic;
		lf.lfUnderline = TRUE;
		lf.lfStrikeOut = tm.tmStruckOut;
		lf.lfCharSet = tm.tmCharSet;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = DEFAULT_QUALITY;
		lf.lfPitchAndFamily = tm.tmPitchAndFamily;
		GetTextFace(hdc, LF_FACESIZE, lf.lfFaceName);
		ud.hf = CreateFontIndirect(&lf);
		TUrlData *oud = (TUrlData*)GlobalLock(hglob); oud->hf = ud.hf; GlobalUnlock(hglob);
	}
	SelectObject(hdc, ud.hf);
	// Note: the win32 docs say to return an HBRUSH, typecast as a BOOL. But they
	// fail to explain how this will work in 64bit windows where an HBRUSH is 64bit.
	// I have supressed the warnings for now, because I hate them...
#pragma warning( push )
#pragma warning( disable: 4311 )
	return (BOOL)ud.hb;
#pragma warning( pop )
	}
	}
	return CallWindowProc(ud.oldproc, hwnd, msg, wParam, lParam);
}


tstring GetLastErrorString() {
	LPVOID lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
		GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
	tstring s((TCHAR*)lpMsgBuf);
	LocalFree(lpMsgBuf);
	return s;
}


void RegSave(const tstring name, DWORD type, void*buf, int size) {
	tstring path = _T("Software\\Scrplus\\") + SaverName;
	HKEY skey; LONG res = RegCreateKeyEx(HKEY_CURRENT_USER, path.c_str(), 0, 0, 0, KEY_ALL_ACCESS, 0, &skey, 0);
	if(res != ERROR_SUCCESS) return;
	RegSetValueEx(skey, name.c_str(), 0, type, (const BYTE*)buf, size);
	RegCloseKey(skey);
}
bool RegLoadDword(const tstring name, DWORD *buf) {
	tstring path = _T("Software\\Scrplus\\") + SaverName;
	HKEY skey; LONG res = RegOpenKeyEx(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &skey);
	if(res != ERROR_SUCCESS) return false;
	DWORD size = sizeof(DWORD);
	res = RegQueryValueEx(skey, name.c_str(), 0, 0, (LPBYTE)buf, &size);
	RegCloseKey(skey);
	return (res == ERROR_SUCCESS);
}

void RegSave(const tstring name, int val) {
	DWORD v = val; RegSave(name, REG_DWORD, &v, sizeof(v));
}
void RegSave(const tstring name, bool val) {
	RegSave(name, val ? 1 : 0);
}
void RegSave(const tstring name, tstring val) {
	RegSave(name, REG_SZ, (void*)val.c_str(), sizeof(TCHAR)*(val.length() + 1));
}
int RegLoad(const tstring name, int def) {
	DWORD val; bool res = RegLoadDword(name, &val);
	return res ? val : def;
}
bool RegLoad(const tstring name, bool def) {
	int b = RegLoad(name, def ? 1 : 0); return (b != 0);
}
tstring RegLoad(const tstring name, tstring def) {
	tstring path = _T("Software\\Scrplus\\") + SaverName;
	HKEY skey; LONG res = RegOpenKeyEx(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &skey);
	if(res != ERROR_SUCCESS) return def;
	DWORD size = 0; res = RegQueryValueEx(skey, name.c_str(), 0, 0, 0, &size);
	if(res != ERROR_SUCCESS) { RegCloseKey(skey); return def; }
	TCHAR *buf = new TCHAR[size];
	RegQueryValueEx(skey, name.c_str(), 0, 0, (LPBYTE)buf, &size);
	tstring s(buf); delete[] buf;
	RegCloseKey(skey);
	return s;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
	hInstance = h;
	TCHAR name[MAX_PATH];
	int sres = LoadString(hInstance, 1, name, MAX_PATH);
	if(sres == 0) {
		MessageBox(NULL, _T("Must store saver name as String Resource 1"), _T("Saver"), MB_ICONERROR | MB_OK);
		return 0;
	}
	SaverName = name;
	//
	TCHAR mod[MAX_PATH];
	GetModuleFileName(hInstance, mod, MAX_PATH);
	tstring smod(mod);
	bool isexe = (smod.find(_T(".exe")) != tstring::npos || smod.find(_T(".EXE")) != tstring::npos);
	//	
	TCHAR *c = GetCommandLine();
	if(*c == '\"') {
		c++; while(*c != 0 && *c != '\"') c++; if(*c == '\"') c++;
	} else { while(*c != 0 && *c != ' ') c++; }
	while(*c == ' ') c++;
	HWND hwnd = NULL; bool fakemulti = false;
	if(*c == 0) { 
		if(isexe) ScrMode = smInstall; 
		else ScrMode = smConfig; 
		hwnd = NULL;
	} else {
		if(*c == '-' || *c == '/') c++;
		if(*c == 'u' || *c == 'U') ScrMode = smUninstall;
		if(*c == 'p' || *c == 'P' || *c == 'l' || *c == 'L') {
			c++; while(*c == ' ' || *c == ':') c++;
			if(_tcsicmp(c, _T("scrprev")) == 0) hwnd = CheckForScrprev(); else hwnd = (HWND)_ttoi(c);
			ScrMode = smPreview;
		} else if(*c == 's' || *c == 'S') {
			ScrMode = smSaver; fakemulti = (c[1] == 'm' || c[1] == 'M');
		} else if(*c == 'c' || *c == 'C') {
			c++;
			while(*c == ' ' || *c == ':') c++;
			if(*c == 0)
				hwnd = GetForegroundWindow();
			else
				hwnd = (HWND)_ttoi(c);
			ScrMode = smConfig;
		} else if(*c == 'a' || *c == 'A') {
			c++;
			while(*c == ' ' || *c == ':')
				c++;
			hwnd = (HWND)_ttoi(c);
			ScrMode = smPassword;
		}
	}
	//
	if(ScrMode == smInstall) { DoInstall(); return 0; }
	if(ScrMode == smUninstall) { DoUninstall(); return 0; }
	if(ScrMode == smPassword) { return 0; }	
	//
	ReadGeneralRegistry();
	//
	INITCOMMONCONTROLSEX icx; ZeroMemory(&icx, sizeof(icx));
	icx.dwSize = sizeof(icx);
	icx.dwICC = ICC_UPDOWN_CLASS;
	InitCommonControlsEx(&icx);
	//
	WNDCLASS wc; ZeroMemory(&wc, sizeof(wc));
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = _T("ScrMonitor");
	wc.lpfnWndProc = MonitorWndProc;
	RegisterClass(&wc);
	//
	wc.lpfnWndProc = SaverWndProc;
	wc.cbWndExtra = 8;
	wc.lpszClassName = _T("ScrClass");
	RegisterClass(&wc);
	//
	if(ScrMode == smConfig) DoConfig(hwnd);
	else if(ScrMode == smSaver || ScrMode == smPreview) DoSaver(hwnd, fakemulti);
	//
	return 0;
}
