#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>
#include <time.h>
#include <math.h>
#include <iostream>
#include <string>
#include <vector>
#include <functional>

#define STEP_INCREASE	10 
#define STEP_MIN		1
#define STEP_MAX		100000

using namespace std;

const string tab = "\t";

bool measurement_actice = false;
int64_t x_sum = 0;
uint64_t dx_abs = 0;

typedef function<void(void)> OnKeyEvent;

//---------------------------------------------------------------------------------------------------
void delay(int ms) {
	const clock_t start_time = clock();
	while (clock() < (start_time + ms));
}
//---------------------------------------------------------------------------------------------------
class CMouse {
public:
	enum EButtonState {
		BTN_DOWN,
		BTN_UP,
	};
	static void move(DWORD dx, DWORD dy) {
		mouse_event(0x1, dx, dy, 0, 0);
	}
	// min speed: pixel/ms
	static void move_x(DWORD dx, unsigned int s) {
		if (dx == 0 || s == 0) {
			return;
		}
		const unsigned int ms = 1000 * s;
		const unsigned int ms_per_step = 2;
		const unsigned int steps = ms / ms_per_step;
		
		const unsigned int x_step = dx / steps;
		const unsigned int x_rest = dx % steps;		// dx = x_step * steps + x_rest
		
		for (unsigned int i = 0; i < x_rest; i++) {
			move(x_step + 1, 0); 
			delay(ms_per_step);
		}	//total move x_rest * (x_step + 1)
		for (unsigned int i = 0; x_rest + i < steps; i++) {
			move(x_step, 0); 
			delay(ms_per_step);
		}	//total move (steps - x_rest) * x_step
		
		//total move x_rest * (x_step + 1) + (steps - x_rest) * x_step = x_rest + steps * x_step = dx
	}	
	static void move_x_500Hz(int64_t dx, uint64_t x_per_Hz) {
		if (dx == 0 || x_per_Hz == 0) {
			return;
		}
		const int sign = (dx > 0) ? 1 : -1;
		// calculate everything unsigned
		const uint8_t period_ms = 2; 	// 500Hz
		const uint64_t dx_abs = abs(dx);
		const uint64_t x_step_abs = (x_per_Hz * period_ms);
		const uint64_t x_rest_abs = dx_abs % x_step_abs;
		uint64_t steps = dx_abs / x_step_abs;
		
		const int64_t x_step = sign * x_step_abs;
		const int64_t x_rest = sign * x_rest_abs;
		
		//cout << to_string(dx) << " " << to_string(x_step) << " " << to_string(steps) << " " << to_string(x_rest) << endl; return;
		while (steps) {
			move(x_step, 0); 
			delay(period_ms);
			steps--;	
		}
		if (x_step_abs > 0) {
			move(x_rest, 0);
		}
	}
	static void right_button_event(EButtonState state) {
		DWORD dwFlags = 0;
		if (state == BTN_DOWN) {
			dwFlags = MOUSEEVENTF_RIGHTDOWN;	
		}
		else if (state == BTN_UP) {
			dwFlags = MOUSEEVENTF_RIGHTUP;	
		}
		else {
			return;
		}
		INPUT Input = {0};
		Input.type           = INPUT_MOUSE;
		Input.mi.dwFlags     = dwFlags;
		Input.mi.dwExtraInfo = 0;
		SendInput(1, &Input, sizeof(INPUT));
	}
	static void right_click(void) {
		right_button_event(BTN_DOWN);	
		delay(300);
		right_button_event(BTN_UP);
	}
};
//---------------------------------------------------------------------------------------------------
class CKey {
public:
	CKey(int keycode) {
		this->keycode = keycode;
		act_state = false;
		last_state = false;
		toggled = false;
		pressed = false;
		released = false;
		enabled = true;
	}
	void update(void) {
		if (!enabled) {
			return;
		}
		act_state = GetAsyncKeyState(keycode) & 0x8000;
		toggled = act_state != last_state;
		pressed = !last_state && act_state;
		released = last_state && !act_state;
		last_state = act_state;	
		if (toggled && OnToggledHandler) {
			OnToggledHandler();	
		}
		if (pressed && OnPressedHandler) {
			OnPressedHandler();	
		}
		if (released && OnReleasedHandler) {
			OnReleasedHandler();	
		}
	}
	int keycode;
	bool enabled;
	bool act_state;
	bool toggled;
	bool pressed;
	bool released;
	OnKeyEvent OnToggledHandler;
	OnKeyEvent OnPressedHandler;
	OnKeyEvent OnReleasedHandler;
private: 
	bool last_state;
};
//---------------------------------------------------------------------------------------------------
class CKeyFactory {
public:
	~CKeyFactory() {
		for (CKey *key : vec_pKeys) {
			delete key;
		}
	}
	CKey* addKey(int keycode) {
		CKey *pKey = new CKey(keycode);
		vec_pKeys.push_back(pKey);
		return pKey;
	}
	void updateKeys(void) {
		for (CKey *pKey : vec_pKeys) {
			pKey->update();
			if (pKey->toggled) {
				// prevent executing multiple handlers
				return;
			}
		}	
	}
private:
	vector<CKey*> vec_pKeys;
};

//---------------------------------------------------------------------------------------------------
void cout_colored(string str, int cl) {
	const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, cl);
	cout << str; 
	SetConsoleTextAttribute(hConsole, 15);	// reset back to default bright white
}
//---------------------------------------------------------------------------------------------------
void cout_error(string str_err) {
	cout_colored(string("Error: ") + str_err + string("!"), 12);	// red font, black background 
	cout << endl << endl;
}

size_t num_len = 0;
//---------------------------------------------------------------------------------------------------
LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_INPUT) {
		unsigned size = sizeof(RAWINPUT);
		static RAWINPUT raw[sizeof(RAWINPUT)];
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));
		if (raw->header.dwType == RIM_TYPEMOUSE && measurement_actice) {
			x_sum += (int64_t)raw->data.mouse.lLastX;
			dx_abs = abs(x_sum);
			// override the number in console line
			string str_clear = string(num_len, ' ');
			string str_set_carriage;
			while (num_len) {
				str_set_carriage += "\b";
				num_len--;
			}
			cout << str_set_carriage << str_clear << str_set_carriage;
			string str_num = to_string(dx_abs);
			num_len = str_num.length();
			cout << str_num;
		}
		return 0;
	}
	else { 
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
}

//---------------------------------------------------------------------------------------------------
int main(void) {
	// register a new message only window to hook into the message queue to receive raw mouse input
	WNDCLASSEX wx = {};
	wx.cbSize = sizeof(WNDCLASSEX);
	wx.lpfnWndProc = WindowProcedure;	// callback to handle messages
	wx.hInstance = GetModuleHandle(NULL);
	wx.lpszClassName = TEXT("RawInputWndClass");
	if (!RegisterClassEx(&wx)) {
		cout_error("error: Window Class Registration failed");
		return 1;
	}

	HWND hWnd = CreateWindowEx(0, wx.lpszClassName, TEXT("mouse_sense_matcher_RawInputWnd"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wx.hInstance, NULL);
	//cout << hex << hWnd;
	if (!hWnd) {
		cout_error("Window Creation failed");
		return 1;
	}

	RAWINPUTDEVICE Rid = {};
	Rid.usUsagePage = 0x01;
	Rid.usUsage = 0x02;
	Rid.dwFlags = RIDEV_INPUTSINK; // adds HID mouse and also ignores legacy mouse messages
	Rid.hwndTarget = hWnd;
	if (!RegisterRawInputDevices(&Rid, 1, sizeof(Rid))) {
		cout_error("Device registration failed");
		return 1;
	}

	ShowWindow(hWnd, SW_HIDE);
	///
	
	// resize main window
	HWND console = GetConsoleWindow();
	RECT r;
	GetWindowRect(console, &r);
	MoveWindow(console, r.left, r.top, 800, 800, TRUE);	

	/*for (int i = 0; i<255; i++) {
		cout_colored("TEST" + to_string(i), i);	cout << endl;
	};*/

	// menu 
	const int cl_blue_yellow = 105;
	cout_colored("==========================================================================", cl_blue_yellow); cout << endl;
	cout_colored("MOUSE SENSITIVITY MATCHER                                                 ", cl_blue_yellow); cout << endl;
	cout_colored("==========================================================================", cl_blue_yellow); cout << endl << endl;
	const int cl_orange_black = 6;
	cout << "Author: Mark Endres" << endl
		 << "Copyright (c) 2022 Mark Endres" << endl 
		 << endl
		 << endl
		 << "1. Meausre/Set distance (in pixels) of game with your correct sensitivity:" << endl;
	cout << tab; cout_colored("[F8]", cl_orange_black);
	cout << tab << "Load measured distance (in pixels) for 360 degree spin (hip)" << endl 
		 << tab << tab 	<< "- E.g. 16534 for BF3 (sense 0.036304, 450 DPI)" << endl 
		 << tab << tab 	<< "- E.g. 14698 for BF3 (sense 0.041467, 400 DPI)" << endl;
	cout << tab; cout_colored("[F9]", cl_orange_black);
	cout << tab 		<< "Start start/stop distance measurement for 360 degree spin (hip)" << endl 
		 << endl
		 << "2. Match sensitivity(s) (trial&error) in other game to achieve 360 degree turn:" << endl
		 << tab << "For Hip sensitivity:" << endl;
	cout << tab; cout_colored("[F10]", cl_orange_black);
	cout << tab 		<< "Repeat 100% of measured distance" << endl 
		 << tab << tab	<< "- Use to match Hip sense" << endl 
		 << tab << tab	<< "- Will alternate between clockwise/counter clockwise" << endl 
		 << endl
		 << tab << "For ADS (aim down sight) sensitivity:" << endl
		 << tab << "- Will alternate between clockwise/counter clockwise" << endl
		 << tab << "- Will automatically press/release right mouse button" << endl; 
	cout << tab; cout_colored("[F5]", cl_orange_black);
	cout << tab 	<< "Switch between ADS mode: hold (default) / toggle" << endl; 
	cout << tab; cout_colored("[F11]", cl_orange_black);
	cout << tab 	<< "Repeat 100% of measured distance" << endl
		 << tab << tab << "- Use to match 100% ADS sense" << endl; 
	cout << tab; cout_colored("[F12]", cl_orange_black);
	cout << tab 	<< "Repeat 200% of measured distance" << endl 
		 << tab << tab << "- Use to match 50% ADS sense" << endl 
		 << endl 
		 << "Optional keys to adjust crosshair x-position:" << endl;
	cout << tab; cout_colored("[LEFT]",  cl_orange_black); cout << tab << "Move one step left"  << endl;
	cout << tab; cout_colored("[RIGHT]", cl_orange_black); cout << tab << "Move one step right" << endl;
	cout << tab; cout_colored("[UP]",    cl_orange_black); cout << tab << "Increase step"       << endl;
	cout << tab; cout_colored("[DOWN]",  cl_orange_black); cout << tab << "Decrease step"       << endl
		 << endl;
	cout_colored("[X]", cl_orange_black);
	cout << tab 	<< "Exit" << endl 
		 << endl 
		 << endl;
	cout_colored("==========================================================================", cl_blue_yellow); cout << endl
		<< endl 
		 << endl;
	///

	bool exit = false;
	bool measurement_valid = false;
	bool rotation_clockwise = true;
	bool rotation_clockwise_ads = true;
	unsigned int step = STEP_MIN;
	bool blocked = false;
	bool useADStoggle = false;

	CKeyFactory keyFac;

	CKey *key_X     = keyFac.addKey(0x58);
	CKey *key_F8    = keyFac.addKey(VK_F8);
	CKey *key_F9    = keyFac.addKey(VK_F9);
	CKey *key_F10   = keyFac.addKey(VK_F10);
	CKey *key_F11   = keyFac.addKey(VK_F11);
	CKey *key_F12   = keyFac.addKey(VK_F12);
	CKey *key_F5    = keyFac.addKey(VK_F5);
	CKey *key_LEFT  = keyFac.addKey(VK_LEFT);
	CKey *key_RIGHT = keyFac.addKey(VK_RIGHT);
	CKey *key_UP    = keyFac.addKey(VK_UP);
	CKey *key_DOWN  = keyFac.addKey(VK_DOWN);
	
	// assign event handlers
	key_X->OnPressedHandler = [&] (void) -> void {
		exit = true;
	};
	
	key_F8->OnReleasedHandler = [&] (void) -> void {
		if (blocked) {
			return;
		}
		measurement_valid = false;
		cout << "Enter distance in pixels: ";
		//cin.ignore();
		string input;
		//cin >> input;
		getline(std::cin, input/*, '\n'*/);
		try {
			const int number = stoi(input);
			if (number <= 0) {
				cout_error("Input must be greater than zero");					
			}
			else {
				measurement_valid = true;
				dx_abs = number;
				cout << endl;
			}
		}
		catch (...) {
			cout_error("Input must be a positive number");			
		}	
	};
	
	key_F9->OnReleasedHandler = [&] (void) -> void {
		measurement_actice = !measurement_actice;
		if (measurement_actice) {
			cout << "Started measurement ..." << endl;
			cout << "x distance in pixels: ";
			blocked = true;
			// reset 
			num_len = 0;
			measurement_valid = false;
			rotation_clockwise = true;
			rotation_clockwise_ads = true;
			x_sum = 0;
			dx_abs = 0;
		}
		else {
			cout << endl << "Stopped measurement!" << endl << endl;
			blocked = false;
			if (dx_abs > 0) {
				measurement_valid = true;
			}
			else {
				measurement_valid = false;
				cout_error("x distance must be greater than zero");
			}
		}
	};
	
	key_F10->OnReleasedHandler = [&] (void) -> void {
		if (blocked) {
			return;
		}
		if (!measurement_valid) {
			cout_error("Start measurement for 360 degree at first");
			return;
		}
		CMouse::move_x_500Hz((int64_t)(rotation_clockwise ? dx_abs : -dx_abs), 10);
		rotation_clockwise = !rotation_clockwise;
	};
	
	auto mouse_move_ADS = [&] (uint64_t dx) -> void {
		if (blocked) {
			return;
		}
		if (!measurement_valid) {
			cout_error("Start measurement for 360 degree at first");
			return;
		}
				
		if (rotation_clockwise_ads) {
			useADStoggle ? CMouse::right_click() : CMouse::right_button_event(CMouse::BTN_DOWN);
			// wait a bit to zoom in
			delay(1000);
		}
		
		CMouse::move_x_500Hz((int64_t)(rotation_clockwise_ads ? dx : -dx), 20);
		
		if (!rotation_clockwise_ads) {
			// wait a bit before zoom out
			delay(1000);
			useADStoggle ? CMouse::right_click() : CMouse::right_button_event(CMouse::BTN_UP);
		}
		
		rotation_clockwise_ads = !rotation_clockwise_ads;		
	};
	
	key_F11->OnReleasedHandler = [&] (void) -> void {
		mouse_move_ADS(dx_abs);
	};
	
	key_F12->OnReleasedHandler = [&] (void) -> void {
		mouse_move_ADS(2 * dx_abs);
	};
	
	key_F5->OnReleasedHandler = [&] (void) -> void {
		useADStoggle = !useADStoggle;
		cout << "ADS mode: " << (useADStoggle ? "toggle" : "hold") << endl << endl;
	};
	
	key_LEFT->OnPressedHandler = [&] (void) -> void {
		CMouse::move(-step, 0);
	};
	
	key_RIGHT->OnPressedHandler = [&] (void) -> void {
		CMouse::move(step, 0);
	};
	
	key_UP->OnPressedHandler = [&] (void) -> void {
		step += STEP_INCREASE;
		if (step > STEP_MAX) {
			step = STEP_MAX;
		}	
	};
	
	key_DOWN->OnPressedHandler = [&] (void) -> void {
		if (step <= STEP_INCREASE) {
			step = STEP_MIN;
		}
		else {
			step -= STEP_INCREASE;
		}
	};
	///
		
	while (1) {
		// process message queue
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		
		keyFac.updateKeys();
		
		if (exit) {
			return 0;
		}	
	}
	// never gets here ...
	return 0;
}