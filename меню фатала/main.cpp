#include <windows.h>
#include <timeapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <memory>
#include <string>
#include <chrono>
#include <filesystem>
#include <algorithm>

#include <gui/gui.h>
#include <menu/menu.h>
#include <menu/init/init_anim.h>
#include <ren/renderer.h>
#include <ren/adapters/adapter_dx11.h>
#include <game/cfg.h>
#include <game/game.h>
#include <game/draw_manager.h>
#include <sdk/globalvars.h>
#include <sdk/inputsystem.h>

#include <resources/smallest_pixel.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

uint32_t utils::runtime_basis = 2166136261u;

namespace sdk
{
	game_t::game_t(uintptr_t _base, uint32_t tok) : base(_base)
	{
		game_dir = ENC2(util::get_game_dir());

		if (!std::filesystem::exists(DEC_INLINE(game_dir) + XOR("fatality")))
			std::filesystem::create_directories(DEC_INLINE(game_dir) + XOR("fatality"));
		if (!std::filesystem::exists(DEC_INLINE(game_dir) + XOR("fatality/scripts")))
			std::filesystem::create_directories(DEC_INLINE(game_dir) + XOR("fatality/scripts"));

		load_fonts();
	}

	void game_t::load_fonts()
	{
		DWORD n_fonts = 0;
		if (!smallest_pixel.empty())
		{
			mem_font_handles.push_back(AddFontMemResourceEx(smallest_pixel.data(), static_cast<DWORD>(smallest_pixel.size()), nullptr, &n_fonts));
		}
	}

	void game_t::remove_fonts() const
	{
		for (auto& handle : mem_font_handles)
			RemoveFontMemResourceEx(handle);
	}

	void game_t::init() {}
	void game_t::unload() const {}
}

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static bool g_bInitialized = false;

static void Log(const char* msg)
{
	FILE* f = nullptr;
	fopen_s(&f, "gui_log.txt", "a");
	if (f)
	{
		fprintf(f, "%s\n", msg);
		fclose(f);
	}
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* p)
{
	char buf[256];
	sprintf_s(buf, "CRASH: Exception 0x%08X at address 0x%p", p->ExceptionRecord->ExceptionCode, p->ExceptionRecord->ExceptionAddress);
	Log(buf);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer = nullptr;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	if (pBackBuffer)
	{
		g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
		pBackBuffer->Release();
	}
}

static void CleanupRenderTarget()
{
	if (g_mainRenderTargetView)
	{
		g_mainRenderTargetView->Release();
		g_mainRenderTargetView = nullptr;
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (g_bInitialized && evo::gui::ctx)
	{
		if (msg == WM_KEYDOWN && (wParam == VK_INSERT || wParam == VK_DELETE))
		{
			menu::men.toggle();
			return 0;
		}

		if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && menu::men.is_open())
		{
			if (!evo::gui::ctx->active_popups.empty())
				evo::gui::ctx->active_popups.back()->close();
			else
				menu::men.toggle();
			return 0;
		}

		evo::gui::ctx->should_process_hotkeys = true;
		evo::gui::ctx->refresh(msg, static_cast<uint32_t>(wParam), static_cast<uint32_t>(lParam));
	}

	switch (msg)
	{
	case WM_SIZE:
		if (g_bInitialized && g_pd3dDevice != nullptr && g_pSwapChain != nullptr && wParam != SIZE_MINIMIZED && LOWORD(lParam) > 0 && HIWORD(lParam) > 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
			CreateRenderTarget();
			if (evo::ren::draw.adapter)
			{
				evo::ren::draw.refresh();
			}
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	SetUnhandledExceptionFilter(CrashHandler);
	Log("wWinMain started");

	try
	{
		constexpr int WINDOW_WIDTH = 1024;
		constexpr int WINDOW_HEIGHT = 720;

		WNDCLASSEXW wc = {
			sizeof(WNDCLASSEXW),
			CS_CLASSDC,
			WndProc,
			0L,
			0L,
			hInstance,
			nullptr,
			LoadCursor(nullptr, IDC_ARROW),
			nullptr,
			nullptr,
			L"FatalityStandaloneGUIClass",
			nullptr
		};
		RegisterClassExW(&wc);
		Log("Window class registered");

		RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
		AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

		int screenW = GetSystemMetrics(SM_CXSCREEN);
		int screenH = GetSystemMetrics(SM_CYSCREEN);
		int windowW = wr.right - wr.left;
		int windowH = wr.bottom - wr.top;
		int posX = std::max(0, (screenW - windowW) / 2);
		int posY = std::max(0, (screenH - windowH) / 2);

		HWND hWnd = CreateWindowW(
			wc.lpszClassName,
			L"fatality.win - Standalone GUI Preview",
			WS_OVERLAPPEDWINDOW,
			posX, posY,
			windowW, windowH,
			nullptr, nullptr, wc.hInstance, nullptr
		);

		if (!hWnd)
		{
			Log("CreateWindowW failed");
			return 1;
		}
		Log("Window created");

		DXGI_SWAP_CHAIN_DESC sd{};
		sd.BufferCount = 1;
		sd.BufferDesc.Width = 0;
		sd.BufferDesc.Height = 0;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hWnd;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL featureLevel;
		const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
		Log("Calling D3D11CreateDeviceAndSwapChain hardware");

		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevelArray,
			2,
			D3D11_SDK_VERSION,
			&sd,
			&g_pSwapChain,
			&g_pd3dDevice,
			&featureLevel,
			&g_pd3dDeviceContext
		);

		char hr_buf[128];
		sprintf_s(hr_buf, "Hardware D3D11 hr = 0x%08X", (unsigned int)hr);
		Log(hr_buf);

		if (FAILED(hr))
		{
			Log("Hardware D3D11 failed, trying WARP");
			hr = D3D11CreateDeviceAndSwapChain(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				createDeviceFlags,
				featureLevelArray,
				2,
				D3D11_SDK_VERSION,
				&sd,
				&g_pSwapChain,
				&g_pd3dDevice,
				&featureLevel,
				&g_pd3dDeviceContext
			);
			sprintf_s(hr_buf, "WARP D3D11 hr = 0x%08X", (unsigned int)hr);
			Log(hr_buf);
			if (FAILED(hr))
			{
				Log("D3D11CreateDeviceAndSwapChain WARP also failed");
				MessageBoxW(nullptr, L"Failed to create Direct3D 11 device.", L"Error", MB_ICONERROR);
				return 1;
			}
		}

		Log("D3D11 device and swapchain created successfully");

		CreateRenderTarget();
		Log("Render target created");

		ShowWindow(hWnd, SW_SHOWNORMAL);
		UpdateWindow(hWnd);
		SetForegroundWindow(hWnd);
		timeBeginPeriod(1);
		Log("Window shown");

		// Setup dummy SDK objects for safe GUI execution
		static sdk::global_vars_t dummy_globals{};
		sdk::global_vars = &dummy_globals;

		static sdk::cinput_system dummy_input_system{};
		sdk::input_system = &dummy_input_system;

		sdk::hWindow = hWnd;
		sdk::game = std::make_unique<sdk::game_t>(0, 0);
		Log("Dummy game created");

		cfg.init();
		Log("Cfg initialized");

		// Initialize renderer
		evo::ren::draw.adapter = std::make_shared<evo::ren::adapter_dx11>(hWnd, g_pd3dDevice, g_pd3dDeviceContext);
		evo::ren::draw.create_layer(game_layer);
		evo::ren::draw.create_layer(17);
		evo::ren::draw.create_layer(63);

		draw_mgr.buf = evo::ren::draw.layers[game_layer];
		draw_mgr.buf->is_deferred = true;
		draw_mgr.buf->skip_dpi = true;

		menu::init_anim.on_adapter_ready();
		Log("Init anim on_adapter_ready done");
		evo::ren::draw.refresh();
		Log("Draw refresh done");
		evo::ren::draw.create_objects();
		Log("Draw create_objects done");

		evo::ren::draw.scale = 1.0f;
		evo::gui::ctx = std::make_unique<evo::gui::context>();
		Log("Gui context created");

		char win_dir_arr[256]{};
		GetSystemWindowsDirectoryA(win_dir_arr, 256);
		const std::string win_dir{ win_dir_arr };

		evo::ren::draw.fonts[FNV1A("esp")] = std::make_shared<evo::ren::font_gdi>("Smallest Pixel-7", 11.f, evo::ren::font_flag_outline | evo::ren::font_flag_no_dpi, 0, 0x45F);
		evo::ren::draw.fonts[FNV1A("esp_name")] = std::make_shared<evo::ren::font>((win_dir + "/fonts/segoeuib.ttf").c_str(), 13.f, evo::ren::font_flag_shadow | evo::ren::font_flag_no_dpi, 0, 0x45F);
		evo::ren::draw.fonts[FNV1A("lby")] = std::make_shared<evo::ren::font>((win_dir + "/fonts/verdanab.ttf").c_str(), 27.f, evo::ren::font_flag_shadow, 0, 0x80);

		menu::men.init();
		Log("Menu men.init done");

		// Center the GUI window inside client area
		if (const auto wnd = menu::men.main_wnd.lock())
		{
			wnd->pos = evo::ren::vec2{
				std::max(20.f, (WINDOW_WIDTH - wnd->size.x) * 0.5f),
				std::max(20.f, (WINDOW_HEIGHT - wnd->size.y) * 0.5f)
			};
			wnd->is_visible = true;
			wnd->is_taking_input = true;
		}

		evo::ren::draw.refresh();
		evo::ren::draw.create_objects();
		Log("Menu objects created");

		menu::init_anim.init_done = true;
		g_bInitialized = true;

		auto start_time = std::chrono::high_resolution_clock::now();
		auto prev_time = start_time;

		Log("Entering main loop");
		MSG msg{};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
				continue;
			}

			if (IsIconic(hWnd))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}

			auto now = std::chrono::high_resolution_clock::now();
			float elapsed = std::chrono::duration<float>(now - start_time).count();
			float delta = std::chrono::duration<float>(now - prev_time).count();
			prev_time = now;

			dummy_globals.real_time = elapsed;
			dummy_globals.frame_time = delta;

			g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);

			// Clean dark background (fatality theme)
			const float clear_color[4] = { 17.f / 255.f, 15.f / 255.f, 35.f / 255.f, 1.0f };
			g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);

			evo::ren::draw.time = elapsed;
			evo::ren::draw.frame_time = delta;
			evo::ren::draw.refresh();

			if (menu::init_anim.init_done)
			{
				if (menu::men.full_cycle_reset)
				{
					menu::men.full_cycle_reset = false;
					menu::group::rebuild_legit_layout();
					menu::group::rebuild_rage_layout();
					evo::gui::ctx->reset();
					evo::gui::ctx->should_update_old_value = false;
				}

				std::lock_guard lock(menu::men.render_mtx);
				evo::ren::draw.begin_frame();
				evo::gui::ctx->render();
				menu::men.finalize();
			}
			else
			{
				evo::ren::draw.begin_frame();
				menu::init_anim.render();
			}

			evo::ren::draw.end_frame();
			evo::ren::draw.render();
			evo::ren::draw.flush();

			if (menu::men.font_reset)
			{
				for (auto& [_, f] : evo::ren::draw.fonts)
				{
					if (!f) continue;
					f->destroy();
					f->create();
				}
				menu::men.font_was_reset = true;
				menu::men.font_reset = false;
			}

			g_pSwapChain->Present(1, 0);
		}

		timeEndPeriod(1);
		CleanupRenderTarget();
		if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
		if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
		if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

		DestroyWindow(hWnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		Log("Exited cleanly");
	}
	catch (const std::exception& e)
	{
		Log("Exception caught:");
		Log(e.what());
		MessageBoxA(nullptr, e.what(), "Fatality GUI Exception", MB_ICONERROR);
	}
	catch (...)
	{
		Log("Unknown exception caught");
		MessageBoxA(nullptr, "Unknown error occurred", "Fatality GUI Exception", MB_ICONERROR);
	}

	return 0;
}
