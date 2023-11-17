#include <SDL.h>
#include <SDL_error.h>
#include <SDL_events.h>
#include <SDL_log.h>
#include <SDL_render.h>
#include <SDL_video.h>
#include <SDL_syswm.h>
#include <blaze/color.hpp>
#include <map>
#include <string>
#include <sstream>
#include <blaze/Bus.hpp>
#include <SDL_ttf.h>
#include <blaze/util.hpp>

// Define SNES key constants
#define SNES_KEY_UP      0
#define SNES_KEY_DOWN    1
#define SNES_KEY_LEFT    2
#define SNES_KEY_RIGHT   3
#define SNES_KEY_A       4
#define SNES_KEY_B       5
#define SNES_KEY_X       6
#define SNES_KEY_Y       7
#define SNES_KEY_START   8
#define SNES_KEY_SELECT  9
#define SNES_KEY_L       10
#define SNES_KEY_R       11

#ifdef _WIN32
	#include <windows.h>
	#include <windowsx.h>
	#include <shobjidl.h>
#endif // _WIN32

namespace Blaze {
	static constexpr int defaultWindowWidth         = 800;
	static constexpr int defaultWindowHeight        = 600;
	static constexpr const char* defaultWindowTitle = "Blaze";
	static constexpr Color defaultWindowColor { 0, 0, 0 };

#ifdef _WIN32
	enum MenuID: UINT_PTR {
		FileExit = 1,
		FileOpen = 2,
		FileClose = 3,
		EditOptions = 4,
		EditContinuousExecution = 7,
		ViewShowDebugger = 5,
		HelpHelp = 6,

		DebuggerTextView = 100,
		DebuggerContinue = 101,
		DebuggerPause = 102,
		DebuggerNext = 103,
		DebuggerInto = 104,
		DebuggerRegisterView = 105,
	};

	static constexpr LPCSTR debuggerWindowClassName = TEXT("Blaze Debugger Window Class");
	static constexpr int defaultDebuggerWindowWidth = 400;
	static constexpr int defaultDebuggerWindowHeight = 600;
	static constexpr int debuggerButtonAreaHeight = 26;
	static constexpr int debuggerButtonY = 3;
	static constexpr int debuggerButtonHeight = 20;
	static constexpr int debuggerButtonXMargin = 5;
	static constexpr int debuggerButtonYMargin = 3;
	static constexpr int debuggerRegisterViewHeight = 200;

	static WNDCLASS debuggerWindowClass = {};
	static HMENU editMenu = nullptr;
#endif // _WIN32

	static bool continuousExecution = true;
	static Bus bus;
	static Address breakpoint = UINT32_MAX;
	static bool romLoaded = false;
} // namespace Blaze

// Function to map SDL keycodes to SNES keys
int mapSDLToSNES(SDL_Keycode sdlKey) {

    switch (sdlKey) {
        case SDLK_UP:
            return SNES_KEY_UP;
        case SDLK_DOWN:
            return SNES_KEY_DOWN;
        case SDLK_LEFT:
            return SNES_KEY_LEFT;
        case SDLK_RIGHT:
            return SNES_KEY_RIGHT;
        case SDLK_x:
            return SNES_KEY_A; // map x key to snes A
        case SDLK_z:
            return SNES_KEY_B; // map z key to snes B
        case SDLK_v:
            return SNES_KEY_X; // map v key to snes X
        case SDLK_c:
            return SNES_KEY_Y; // map c key to snes y
        case SDLK_RETURN: // could change start mapping
            return SNES_KEY_START;
        case SDLK_SPACE:  // could change select mapping
            return SNES_KEY_SELECT;
        case SDLK_a:
            return SNES_KEY_L;
        case SDLK_s:
            return SNES_KEY_R;
        default:
            return -1; // unmapped keys
    }
}

#ifdef _WIN32
static void setContinuousExecution(bool continuousExecution) {
	Blaze::continuousExecution = continuousExecution;
	MENUITEMINFO info = {};

	info.cbSize = sizeof(info);
	info.fMask = MIIM_STATE;
	info.fState = continuousExecution ? MFS_CHECKED : MFS_UNCHECKED;

	SetMenuItemInfo(Blaze::editMenu, Blaze::MenuID::EditContinuousExecution, FALSE, &info);
};

static bool openROMDialog(std::string& outPath) {
	HRESULT hr;
	IFileDialog* fileDialog = nullptr;
	IShellItem* item = nullptr;
	LPWSTR filePath = nullptr;
	int requiredChars = 0;
	int writtenChars = 0;

	hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&fileDialog));
	if (!SUCCEEDED(hr)) {
		CoUninitialize();
		return false;
	}

	hr = fileDialog->Show(nullptr);
	if (!SUCCEEDED(hr)) {
		fileDialog->Release();
		CoUninitialize();
		return false;
	}

	hr = fileDialog->GetResult(&item);
	if (!SUCCEEDED(hr)) {
		fileDialog->Release();
		CoUninitialize();
		return false;
	}

	hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filePath);
	if (!SUCCEEDED(hr)) {
		item->Release();
		fileDialog->Release();
		CoUninitialize();
		return false;
	}

	requiredChars = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
	if (requiredChars == 0) {
		CoTaskMemFree(filePath);
		item->Release();
		fileDialog->Release();
		CoUninitialize();
		return false;
	}

	outPath.resize(requiredChars);

	writtenChars = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, outPath.data(), requiredChars, nullptr, nullptr);
	if (writtenChars == 0) {
		CoTaskMemFree(filePath);
		item->Release();
		fileDialog->Release();
		CoUninitialize();
		return false;
	}

	outPath.resize(writtenChars);

	CoTaskMemFree(filePath);
	item->Release();
	fileDialog->Release();
	CoUninitialize();

	// trim off null characters
	while (!outPath.empty() && outPath[outPath.size() - 1] == '\0') {
		outPath.resize(outPath.size() - 1);
	}

	return true;
};

static LPCSTR fontFace = nullptr;

static HWND win32DebuggerTextWindow = nullptr;
static HWND win32DebuggerRegWindow = nullptr;

static std::wstring utf8ToUTF16(const std::string& contents) {
	std::wstring wideContents;
	int wideChars = 0;

	if (!contents.empty()) {
		wideChars = MultiByteToWideChar(CP_UTF8, 0, contents.c_str(), (int)contents.size(), nullptr, 0);
		if (wideChars == 0) {
			throw std::runtime_error("Invalid UTF-8 string");
		}

		wideContents.resize(wideChars);

		MultiByteToWideChar(CP_UTF8, 0, contents.c_str(), (int)contents.size(), wideContents.data(), wideChars);
	}

	return wideContents;
};

static void updateDisassembly() {
	std::string contents;
	std::string regContents;
	std::vector<Blaze::CPU::DisassembledInstruction> disassembledInstructions;
	Blaze::Address PC;

	if (win32DebuggerTextWindow == nullptr || win32DebuggerRegWindow == nullptr) {
		return;
	}

	if (!Blaze::romLoaded) {
		contents = "No ROM loaded";
		regContents = "No ROM loaded";
	} else if (Blaze::continuousExecution) {
		contents = "Can't display disassembly while CPU is running";
		regContents = "Can't display registers while CPU is running";
	} else {
		PC = Blaze::concat24(Blaze::bus.cpu.PBR, Blaze::bus.cpu.PC);

		disassembledInstructions = Blaze::CPU::disassemble(Blaze::bus, PC, 10, Blaze::bus.cpu.memoryAndAccumulatorAre8Bit(), Blaze::bus.cpu.indexRegistersAre8Bit(), Blaze::bus.cpu.usingEmulationMode(), Blaze::bus.cpu.getFlag(Blaze::CPU::flags::c));

		if (disassembledInstructions.empty()) {
			contents = "Failed to disassemble memory at " + Blaze::valueToHexString(PC, 6, "$");
		} else {
			contents = "   ADDR  | CODE\n ------- | ----\n";
			for (const auto& disassembledInstruction: disassembledInstructions) {
				contents += " " + Blaze::valueToHexString(disassembledInstruction.address, 6, "$") + " | ";
				contents += disassembledInstruction.code + "\n";
			}

			// remove the final newline
			contents.erase(contents.end() - 1);
		}

		regContents = Blaze::bus.cpu.usingEmulationMode() ? "emulation mode\n" : "native mode\n";
		regContents += "P = ";

	#define DISASSEMBLY_P_CHECK(_name) \
		if ((Blaze::bus.cpu.P & Blaze::CPU::flags::_name) != 0) { \
			regContents += #_name; \
		} else { \
			regContents += '-'; \
		}

		DISASSEMBLY_P_CHECK(n);
		DISASSEMBLY_P_CHECK(v);
		DISASSEMBLY_P_CHECK(m);
		DISASSEMBLY_P_CHECK(x);
		DISASSEMBLY_P_CHECK(d);
		DISASSEMBLY_P_CHECK(i);
		DISASSEMBLY_P_CHECK(z);
		DISASSEMBLY_P_CHECK(c);

	#undef DISASSEMBLY_P_CHECK

		regContents += '\n';

		regContents += "PBR = " + Blaze::valueToHexString(Blaze::bus.cpu.PBR, 2, "$") + "   DBR = " + Blaze::valueToHexString(Blaze::bus.cpu.DBR, 2, "$") + "\n";
		regContents += "DR  = " + Blaze::valueToHexString(Blaze::bus.cpu.DR, 4, "$") + " SP  = " + Blaze::valueToHexString(Blaze::bus.cpu.SP, 4, "$") + "\n";
		regContents += "PC  = " + Blaze::valueToHexString(Blaze::bus.cpu.PC, 4, "$") + "\n";
		regContents += '\n';

		regContents += "A   = " + Blaze::valueToHexString(Blaze::bus.cpu.A.forceLoadFull(), 4, "$") + "\n";
		regContents += "X   = " + Blaze::valueToHexString(Blaze::bus.cpu.X.forceLoadFull(), 4, "$") + " Y   = " + Blaze::valueToHexString(Blaze::bus.cpu.Y.forceLoadFull(), 4, "$") + "\n";
	}

#if defined(UNICODE)
	Edit_SetText(win32DebuggerTextWindow, utf8ToUTF16(contents).c_str());
#else
	Edit_SetText(win32DebuggerTextWindow, contents.c_str());
#endif

#if defined(UNICODE)
	Edit_SetText(win32DebuggerRegWindow, utf8ToUTF16(regContents).c_str());
#else
	Edit_SetText(win32DebuggerRegWindow, regContents.c_str());
#endif
};

static LRESULT CALLBACK debuggerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HWND continueButton;
	static HWND pauseButton;
	static HWND nextButton;
	static HWND intoButton;

	switch (uMsg) {
		case WM_CLOSE:
			ShowWindow(hwnd, SW_HIDE);
			return 0;

		case WM_CREATE: {
			auto hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
			HFONT hFont = nullptr;

			hFont = CreateFont(0, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontFace);

			win32DebuggerTextWindow = CreateWindowEx(0, TEXT("Edit"), nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY, 0, Blaze::debuggerButtonAreaHeight, 0, 0, hwnd, (HMENU)Blaze::MenuID::DebuggerTextView, hInst, nullptr);
			if (!win32DebuggerTextWindow) {
				abort();
			}

			SetWindowFont(win32DebuggerTextWindow, hFont, FALSE);

			win32DebuggerRegWindow = CreateWindowEx(0, TEXT("Edit"), nullptr, WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY, 0, 0, 0, 0, hwnd, (HMENU)Blaze::MenuID::DebuggerRegisterView, hInst, nullptr);
			if (!win32DebuggerRegWindow) {
				abort();
			}

			SetWindowFont(win32DebuggerRegWindow, hFont, FALSE);

			continueButton = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Continue"), WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, Blaze::debuggerButtonY, 0, Blaze::debuggerButtonHeight, hwnd, (HMENU)Blaze::MenuID::DebuggerContinue, hInst, nullptr);
			pauseButton = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Pause"), WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, Blaze::debuggerButtonY, 0, Blaze::debuggerButtonHeight, hwnd, (HMENU)Blaze::MenuID::DebuggerPause, hInst, nullptr);
			nextButton = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Next"), WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, Blaze::debuggerButtonY, 0, Blaze::debuggerButtonHeight, hwnd, (HMENU)Blaze::MenuID::DebuggerNext, hInst, nullptr);
			intoButton = CreateWindowEx(0, TEXT("BUTTON"), TEXT("Step Into"), WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, Blaze::debuggerButtonY, 0, Blaze::debuggerButtonHeight, hwnd, (HMENU)Blaze::MenuID::DebuggerInto, hInst, nullptr);

			updateDisassembly();

			return 0;
		}

		case WM_SIZE: {
			auto width = LOWORD(lParam);
			auto height = HIWORD(lParam);

			auto buttonWidth = (std::max<decltype(width)>(width, Blaze::debuggerButtonXMargin * 5) - (Blaze::debuggerButtonXMargin * 5)) / 4;

			MoveWindow(win32DebuggerTextWindow, 0, Blaze::debuggerButtonAreaHeight, width, (height - Blaze::debuggerButtonAreaHeight) - Blaze::debuggerRegisterViewHeight, TRUE);
			MoveWindow(win32DebuggerRegWindow, 0, height - Blaze::debuggerRegisterViewHeight, width, Blaze::debuggerRegisterViewHeight, TRUE);
			MoveWindow(continueButton, Blaze::debuggerButtonXMargin * 1 + buttonWidth * 0, Blaze::debuggerButtonY, buttonWidth, Blaze::debuggerButtonHeight, TRUE);
			MoveWindow(pauseButton, Blaze::debuggerButtonXMargin * 2 + buttonWidth * 1, Blaze::debuggerButtonY, buttonWidth, Blaze::debuggerButtonHeight, TRUE);
			MoveWindow(nextButton, Blaze::debuggerButtonXMargin * 3  + buttonWidth * 2, Blaze::debuggerButtonY, buttonWidth, Blaze::debuggerButtonHeight, TRUE);
			MoveWindow(intoButton, Blaze::debuggerButtonXMargin * 4  + buttonWidth * 3, Blaze::debuggerButtonY, buttonWidth, Blaze::debuggerButtonHeight, TRUE);
			return 0;
		}

		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case Blaze::DebuggerContinue:
					if (HIWORD(wParam) == BN_CLICKED) {
						setContinuousExecution(true);
						updateDisassembly();
					}
					break;
				case Blaze::DebuggerPause:
					if (HIWORD(wParam) == BN_CLICKED) {
						setContinuousExecution(false);
						updateDisassembly();
					}
					break;
				case Blaze::DebuggerNext:
					if (HIWORD(wParam) == BN_CLICKED && !Blaze::continuousExecution) {
						Blaze::Address PC = Blaze::concat24(Blaze::bus.cpu.PBR, Blaze::bus.cpu.PC);
						Blaze::CPU::Instruction instrInfo = Blaze::CPU::decodeInstruction(Blaze::bus.read8(PC), Blaze::bus.cpu.memoryAndAccumulatorAre8Bit());
						if (instrInfo.opcode == Blaze::CPU::Opcode::JSR || instrInfo.opcode == Blaze::CPU::Opcode::JSL) {
							// these are subroutine execution instructions; clicking "next" is not supposed to go into them (that's what "step into" is for)
							//
							// instead, let's set a breakpoint and continue execution
							Blaze::breakpoint = PC + instrInfo.size;
							setContinuousExecution(true);
							updateDisassembly();
						} else {
							Blaze::bus.cpu.execute();
							updateDisassembly();
						}
					}
					break;
				case Blaze::DebuggerInto:
					if (HIWORD(wParam) == BN_CLICKED && !Blaze::continuousExecution) {
						Blaze::bus.cpu.execute();
						updateDisassembly();
					}
					break;
			}

			return 0;
		}

		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);

			FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

			EndPaint(hwnd, &ps);

			return 0;
		}

		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}
};
#endif

static int createText(const std::string& text, const SDL_Color& color, TTF_Font* font, SDL_Renderer* renderer, SDL_Texture*& outTexture, int& outWidth, int& outHeight) {
	SDL_Surface* tmpSurface = TTF_RenderUTF8_Solid_Wrapped(font, text.c_str(), color, 0);
	if (!tmpSurface) {
		return -1;
	}

	outTexture = SDL_CreateTextureFromSurface(renderer, tmpSurface);
	if (!outTexture) {
		SDL_FreeSurface(tmpSurface);
		return -1;
	}

	outWidth = tmpSurface->w;
	outHeight = tmpSurface->h;

	SDL_FreeSurface(tmpSurface);

	return 0;
};

int main(int argc, char** argv) {
	SDL_Window* mainWindow;
	SDL_Renderer* renderer;
	SDL_Surface* surface;
	SDL_Event event;
	std::map<int, bool> keyboard;
	bool running = true;
	SDL_SysWMinfo mainWindowInfo;
	TTF_Font* font = nullptr;
	bool& romLoaded = Blaze::romLoaded;
	Blaze::Bus& bus = Blaze::bus;

#ifdef _WIN32
	HWND win32MainWindow = nullptr;
	HWND win32DebuggerWindow = nullptr;
	HMENU mainMenu = nullptr;
	HMENU fileMenu = nullptr;
	HMENU& editMenu = Blaze::editMenu;
	HMENU viewMenu = nullptr;
	HMENU helpMenu = nullptr;
#endif // _WIN32

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL: %s", SDL_GetError());
		return 1;
	}

	if (TTF_Init() < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL_ttf: %s", TTF_GetError());
		return 1;
	}

#ifdef _WIN32
	font = TTF_OpenFont("C:\\Windows\\Fonts\\FiraCode-Regular.ttf", 16);
	fontFace = TEXT("Fira Code");
#else
	#warning TODO
#endif
	if (!font) {
#ifdef _WIN32
		// try another font
		font = TTF_OpenFont("C:\\Windows\\Fonts\\consola.ttf", 16);
		fontFace = TEXT("Consolas");
		if (!font) {
#else
		#warning TODO
#endif
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load font: %s", TTF_GetError());
			return 1;
#ifdef _WIN32
		}
#else
		#warning TODO
#endif
	}

	if (SDL_CreateWindowAndRenderer(Blaze::defaultWindowWidth, Blaze::defaultWindowHeight, SDL_WINDOW_RESIZABLE, &mainWindow, &renderer) < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create main window and renderer: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_SetWindowTitle(mainWindow, Blaze::defaultWindowTitle);

	SDL_VERSION(&mainWindowInfo.version);
	if (!SDL_GetWindowWMInfo(mainWindow, &mainWindowInfo)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get window handle: %s", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(mainWindow);
		SDL_Quit();
		return 1;
	}

#ifdef _WIN32
	win32MainWindow = mainWindowInfo.info.win.window;

	// set up the menus
	{
		mainMenu = CreateMenu();
		fileMenu = CreateMenu();
		editMenu = CreateMenu();
		viewMenu = CreateMenu();
		helpMenu = CreateMenu();

		AppendMenu(mainMenu, MF_POPUP, (UINT_PTR)fileMenu, "File");

		AppendMenu(fileMenu, MF_STRING, Blaze::MenuID::FileOpen, "Open ROM");
		AppendMenu(fileMenu, MF_STRING, Blaze::MenuID::FileClose, "Close ROM");
		AppendMenu(fileMenu, MF_STRING, Blaze::MenuID::FileExit, "Exit");

		AppendMenu(mainMenu, MF_POPUP, (UINT_PTR)editMenu, "Edit");

		AppendMenu(editMenu, MF_STRING, Blaze::MenuID::EditOptions, "Options");
		AppendMenu(editMenu, MF_STRING, Blaze::MenuID::EditContinuousExecution, "Continuous Execution");

		AppendMenu(mainMenu, MF_POPUP, (UINT_PTR)viewMenu, "View");

		AppendMenu(viewMenu, MF_STRING, Blaze::MenuID::ViewShowDebugger, "Show Debugger");

		AppendMenu(mainMenu, MF_POPUP, (UINT_PTR)helpMenu, "Help");

		AppendMenu(helpMenu, MF_STRING, Blaze::MenuID::HelpHelp, "Help");

		SetMenu(win32MainWindow, mainMenu);
	}

	setContinuousExecution(true);

	// enable Win32 events in the SDL event loop
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

	// set up the debugger window

	Blaze::debuggerWindowClass.lpfnWndProc = debuggerWindowProc;
	Blaze::debuggerWindowClass.hInstance = mainWindowInfo.info.win.hinstance;
	Blaze::debuggerWindowClass.lpszClassName = Blaze::debuggerWindowClassName;

	RegisterClass(&Blaze::debuggerWindowClass);

	win32DebuggerWindow = CreateWindowEx(0, Blaze::debuggerWindowClassName, TEXT("Debugger Window"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Blaze::defaultDebuggerWindowWidth, Blaze::defaultDebuggerWindowHeight, nullptr, nullptr, Blaze::debuggerWindowClass.hInstance, nullptr);
	if (win32DebuggerWindow == nullptr) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create debugger window: %lu", GetLastError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(mainWindow);
		SDL_Quit();
		return 1;
	}
#endif // _WIN32

	std::string debugBuffer;
	bus.cpu.putCharacterHook = [&](char character) {
		debugBuffer.push_back(character);
	};

	if (argc > 1) {
		std::string path = argv[1];
		std::stringstream output;

		output << "Got ROM: " << path;
		output << '\n';

		try {
			bus.rom.load(path);

			if (bus.rom.type() == Blaze::ROM::Type::INVALID) {
				output << "Failed to load ROM";
			} else {
				output << "Loaded ROM with name: " << bus.rom.name();

				// when a ROM is loaded, we need to reset all components
				bus.reset();

				romLoaded = true;

				updateDisassembly();
			}
		} catch (const std::runtime_error& e) {
			output << "Failed to load ROM:\n" << e.what();
		}

		output << '\n';

		debugBuffer = output.str();
	}

	// main event loop
	while (running) {
		// process all events for this frame
		while (SDL_PollEvent(&event)) {
			int snesKey;

			switch (event.type) {
				case SDL_QUIT:
					// exit if window closed
					running = false;
					break;

				case SDL_KEYDOWN:
					snesKey = mapSDLToSNES(event.key.keysym.sym);
					// update emulator state
					break;

				case SDL_KEYUP:
					snesKey = mapSDLToSNES(event.key.keysym.sym);
					// update emulator state
					break;

#ifdef _WIN32
			case SDL_SYSWMEVENT:
				if (event.syswm.msg->msg.win.msg == WM_COMMAND) {
					switch (static_cast<Blaze::MenuID>(LOWORD(event.syswm.msg->msg.win.wParam))) {
						case Blaze::MenuID::FileOpen: {
							std::string path;
							std::stringstream output;

							if (openROMDialog(path)) {
								output << "Got ROM: " << path;
								output << '\n';

								try {
									bus.rom.load(path);

									if (bus.rom.type() == Blaze::ROM::Type::INVALID) {
										output << "Failed to load ROM";
									} else {
										output << "Loaded ROM with name: " << bus.rom.name();

										// when a ROM is loaded, we need to reset all components
										bus.reset();

										romLoaded = true;

										updateDisassembly();
									}
								} catch (const std::runtime_error& e) {
									output << "Failed to load ROM:\n" << e.what();
								}
							} else {
								output << "Failed to open ROM selection dialog";
							}

							output << '\n';

							debugBuffer = output.str();
						} break;

						case Blaze::MenuID::FileClose: {
							// when a ROM is unloaded, we need to reset all components
							bus.reset();
							bus.rom.reset(&bus); // we also reset the ROM
							romLoaded = false;
							debugBuffer = "";
						} break;

						case Blaze::MenuID::FileExit: {
							running = false;
						} break;

						case Blaze::MenuID::EditOptions: {
							// TODO
						} break;

						case Blaze::MenuID::EditContinuousExecution: {
							setContinuousExecution(!Blaze::continuousExecution);
						} break;

						case Blaze::MenuID::ViewShowDebugger: {
							ShowWindow(win32DebuggerWindow, SW_SHOW);
						} break;

						case Blaze::MenuID::HelpHelp: {
							// TODO
						} break;

						default: {
							// what to do here?
						} break;
					}
				}
				break;
#endif // _WIN32

			default:
				break;
			}
		}

		if (!running) {
			break;
		}

		// clear the window
		SDL_SetRenderDrawColor(renderer, Blaze::defaultWindowColor.r, Blaze::defaultWindowColor.g, Blaze::defaultWindowColor.b, Blaze::defaultWindowColor.a);
		SDL_RenderClear(renderer);

		if (Blaze::concat24(bus.cpu.PBR, bus.cpu.PC) == Blaze::breakpoint) {
			Blaze::breakpoint = UINT32_MAX;
			setContinuousExecution(false);
			updateDisassembly();
		}

		if (romLoaded && Blaze::continuousExecution) {
			// execute a single instruction
			bus.cpu.execute();
		}

		// render the debug buffer
		if (!debugBuffer.empty()) {
			SDL_Rect rect = {
				0, 0,
			};
			SDL_Color color = {
				// white
				255, 255, 255,
			};
			SDL_Texture* texture = nullptr;

			if (createText(debugBuffer, color, font, renderer, texture, rect.w, rect.h) < 0) {
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create text texture: %s", SDL_GetError());
				abort();
			}
			SDL_RenderCopy(renderer, texture, nullptr, &rect);
			SDL_DestroyTexture(texture);
		}

		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(mainWindow);

	TTF_CloseFont(font);

	TTF_Quit();
	SDL_Quit();

	return 0;
};
