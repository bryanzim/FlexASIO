#include "stream_status.h"

#include "log.h"

#include <shellapi.h>

#include <cmath>
#include <sstream>
#include <system_error>

namespace flexasio {
	namespace {

		constexpr UINT WM_APP_STOP = WM_APP + 1;
		constexpr UINT WM_TRAY = WM_APP + 2;
		constexpr UINT_PTR kRefreshTimerId = 1;
		constexpr UINT kRefreshMilliseconds = 250;
		constexpr wchar_t kWindowClassName[] = L"FlexASIOStreamStatus";
		// The resource-id macros are ANSI when UNICODE is unset, so spell the wide forms explicitly.
		const LPCWSTR kInformationIcon = MAKEINTRESOURCEW(32516);
		const LPCWSTR kArrowCursor = MAKEINTRESOURCEW(32512);

		std::wstring FormatBitrate(std::int64_t bitsPerSecond) {
			if (bitsPerSecond >= 1'000'000) {
				const auto mbpsTimes100 = (bitsPerSecond + 5'000) / 10'000;
				return std::to_wstring(mbpsTimes100 / 100) + L"." +
					(mbpsTimes100 % 100 < 10 ? L"0" : L"") + std::to_wstring(mbpsTimes100 % 100) + L" Mbps";
			}
			return std::to_wstring((bitsPerSecond + 500) / 1000) + L" kbps";
		}

		std::wstring FormatBits(const StreamStatusDescription& description) {
			const int bits = description.outputBits > 0 ? description.outputBits : description.inputBits;
			if (description.inputChannels > 0 && description.outputChannels > 0 &&
				description.inputBits > 0 && description.outputBits > 0 &&
				description.inputBits != description.outputBits) {
				return std::to_wstring(description.inputBits) + L"/" + std::to_wstring(description.outputBits) + L"-bit";
			}
			if (bits <= 0) return L"unknown bit depth";
			return std::to_wstring(bits) + L"-bit";
		}

		std::wstring FormatDevice(const StreamStatusDescription& description) {
			if (!description.inputDevice.empty() && !description.outputDevice.empty() && description.inputDevice != description.outputDevice)
				return L"in " + description.inputDevice + L" / out " + description.outputDevice;
			if (!description.outputDevice.empty()) return description.outputDevice;
			if (!description.inputDevice.empty()) return description.inputDevice;
			return L"(no device)";
		}

		std::wstring FormatTooltip(const StreamStatusDescription& description, const StreamStatus::Snapshot& snapshot) {
			const auto glitches = snapshot.inputOverflow + snapshot.inputUnderflow + snapshot.outputOverflow + snapshot.outputUnderflow;
			std::wstring tip = L"FlexASIO " + description.backend + L" " +
				std::to_wstring(static_cast<int>(std::llround(description.sampleRate))) + L"Hz " +
				std::to_wstring(description.inputChannels) + L"in/" + std::to_wstring(description.outputChannels) + L"out";
			const auto device = FormatDevice(description);
			if (!device.empty()) tip += L" " + device;
			tip += L" xrun " + std::to_wstring(glitches);
			if (tip.size() > 127) tip.resize(127);
			return tip;
		}

		std::wstring FormatPopup(const StreamStatusDescription& description, const StreamStatus::Snapshot& snapshot) {
			std::wostringstream text;
			text << L"Stream: " << description.backend << L"\n"
				<< L"Device: " << FormatDevice(description) << L"\n"
				<< static_cast<int>(std::llround(description.sampleRate)) << L" Hz, "
				<< FormatBits(description) << L", " << FormatBitrate(description.bitrateBitsPerSecond) << L"\n"
				<< L"Channels: " << description.inputChannels << L" in, " << description.outputChannels << L" out\n"
				<< L"Buffer: " << description.bufferFrames << L" frames\n";
			if (!snapshot.hasSlack) text << L"Slack min/max: n/a\n";
			else text << L"Slack min/max: " << snapshot.minSlackFrames << L" / " << snapshot.maxSlackFrames << L" frames\n";
			text << L"Input overflow: " << snapshot.inputOverflow << L"\n"
				<< L"Input underflow: " << snapshot.inputUnderflow << L"\n"
				<< L"Output overflow: " << snapshot.outputOverflow << L"\n"
				<< L"Output underflow: " << snapshot.outputUnderflow;
			return text.str();
		}

		HICON CreateStatusIcon() {
			constexpr int size = 32;
			BITMAPV5HEADER header{};
			header.bV5Size = sizeof(header);
			header.bV5Width = size;
			header.bV5Height = -size;
			header.bV5Planes = 1;
			header.bV5BitCount = 32;
			header.bV5Compression = BI_BITFIELDS;
			header.bV5RedMask = 0x00FF0000;
			header.bV5GreenMask = 0x0000FF00;
			header.bV5BlueMask = 0x000000FF;
			header.bV5AlphaMask = 0xFF000000;

			void* bits = nullptr;
			const HDC screen = GetDC(nullptr);
			const HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
			ReleaseDC(nullptr, screen);
			if (color == nullptr || bits == nullptr) {
				if (color != nullptr) DeleteObject(color);
				return LoadIconW(nullptr, kInformationIcon);
			}

			auto* pixels = static_cast<std::uint32_t*>(bits);
			for (int y = 0; y < size; ++y) {
				for (int x = 0; x < size; ++x) {
					const int dx = x - (size / 2);
					const int dy = y - (size / 2);
					const bool inside = dx * dx + dy * dy <= 14 * 14;
					std::uint32_t pixel = inside ? 0xFF1F6B3A : 0x00000000;
					const int bar = (x / 4) % 2 == 0 ? 6 : 10;
					if (inside && y > size / 2 - bar && y < size / 2 + bar && x > 6 && x < size - 6)
						pixel = 0xFFE7F6EC;
					pixels[y * size + x] = pixel;
				}
			}

			HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
			ICONINFO iconInfo{};
			iconInfo.fIcon = TRUE;
			iconInfo.hbmColor = color;
			iconInfo.hbmMask = mask;
			const HICON created = CreateIconIndirect(&iconInfo);
			DeleteObject(color);
			if (mask != nullptr) DeleteObject(mask);
			if (created != nullptr) return created;
			return LoadIconW(nullptr, kInformationIcon);
		}

		void RegisterMessageWindowClass(const WNDPROC windowProc) {
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = windowProc;
			windowClass.hCursor = LoadCursorW(nullptr, kArrowCursor);
			windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
			windowClass.lpszClassName = kWindowClassName;
			if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
				throw std::system_error(GetLastError(), std::system_category(), "Unable to register stream status window class");
		}

	}

	void StreamStatus::NoteGlitches(const bool inputOverflow, const bool inputUnderflow, const bool outputOverflow, const bool outputUnderflow) noexcept {
		if (inputOverflow) inputOverflowCount.fetch_add(1, std::memory_order_relaxed);
		if (inputUnderflow) inputUnderflowCount.fetch_add(1, std::memory_order_relaxed);
		if (outputOverflow) outputOverflowCount.fetch_add(1, std::memory_order_relaxed);
		if (outputUnderflow) outputUnderflowCount.fetch_add(1, std::memory_order_relaxed);
	}

	void StreamStatus::NoteSlackFrames(const std::int64_t slackFrames) noexcept {
		auto currentMin = minSlackFrames.load(std::memory_order_relaxed);
		while (slackFrames < currentMin &&
			!minSlackFrames.compare_exchange_weak(currentMin, slackFrames, std::memory_order_relaxed)) {
		}
		auto currentMax = maxSlackFrames.load(std::memory_order_relaxed);
		while (slackFrames > currentMax &&
			!maxSlackFrames.compare_exchange_weak(currentMax, slackFrames, std::memory_order_relaxed)) {
		}
	}

	StreamStatus::Snapshot StreamStatus::LoadSnapshot() const noexcept {
		Snapshot snapshot;
		snapshot.inputOverflow = inputOverflowCount.load(std::memory_order_relaxed);
		snapshot.inputUnderflow = inputUnderflowCount.load(std::memory_order_relaxed);
		snapshot.outputOverflow = outputOverflowCount.load(std::memory_order_relaxed);
		snapshot.outputUnderflow = outputUnderflowCount.load(std::memory_order_relaxed);
		snapshot.minSlackFrames = minSlackFrames.load(std::memory_order_relaxed);
		snapshot.maxSlackFrames = maxSlackFrames.load(std::memory_order_relaxed);
		snapshot.hasSlack = snapshot.minSlackFrames != INT64_MAX;
		return snapshot;
	}

	StreamStatusIcon::StreamStatusIcon(const StreamStatus& status) : status(status) {
		started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (started == nullptr) throw std::system_error(GetLastError(), std::system_category(), "Unable to create stream status event");
		thread = std::thread([this] { ThreadMain(); });
		WaitForSingleObject(started, INFINITE);
		if (messageWindow.load() == nullptr) {
			if (thread.joinable()) thread.join();
			CloseHandle(started);
			started = nullptr;
			throw std::runtime_error("Unable to create stream status window");
		}
	}

	StreamStatusIcon::~StreamStatusIcon() {
		if (const auto window = messageWindow.load()) PostMessageW(window, WM_APP_STOP, 0, 0);
		if (thread.joinable()) thread.join();
		if (started != nullptr) CloseHandle(started);
	}

	void StreamStatusIcon::ThreadMain() noexcept {
		try {
			RegisterMessageWindowClass(&StreamStatusIcon::WindowProc);
			const HWND window = CreateWindowExW(0, kWindowClassName, L"FlexASIO stream status", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, this);
			messageWindow.store(window);
			if (window != nullptr) {
				AddIcon();
				SetTimer(window, kRefreshTimerId, kRefreshMilliseconds, nullptr);
			}
		}
		catch (const std::exception& exception) {
			Log() << "Stream status icon failed to start: " << exception.what();
		}
		catch (...) {
			Log() << "Stream status icon failed to start";
		}
		SetEvent(started);
		if (messageWindow.load() == nullptr) return;

		MSG message;
		for (;;) {
			const auto result = GetMessageW(&message, nullptr, 0, 0);
			if (result <= 0) break;
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		KillTimer(messageWindow.load(), kRefreshTimerId);
		DestroyPopup();
		RemoveIcon();
		DestroyWindow(messageWindow.load());
		messageWindow.store(nullptr);
		if (font != nullptr) DeleteObject(font);
		font = nullptr;
	}

	void StreamStatusIcon::AddIcon() {
		icon = CreateStatusIcon();
		iconIsOwned = icon != nullptr && icon != LoadIconW(nullptr, kInformationIcon);
		notifyIcon = {};
		notifyIcon.cbSize = sizeof(notifyIcon);
		notifyIcon.hWnd = messageWindow.load();
		notifyIcon.uID = 1;
		notifyIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
		notifyIcon.uCallbackMessage = WM_TRAY;
		notifyIcon.hIcon = icon;
		const auto tip = FormatTooltip(status.Description(), status.LoadSnapshot());
		wcsncpy_s(notifyIcon.szTip, tip.c_str(), _TRUNCATE);
		if (!Shell_NotifyIconW(NIM_ADD, &notifyIcon)) {
			if (iconIsOwned && icon != nullptr) DestroyIcon(icon);
			icon = nullptr;
			iconIsOwned = false;
			throw std::system_error(GetLastError(), std::system_category(), "Unable to add stream status icon");
		}
		iconAdded = true;
		notifyIcon.uVersion = NOTIFYICON_VERSION_4;
		Shell_NotifyIconW(NIM_SETVERSION, &notifyIcon);
	}

	void StreamStatusIcon::RemoveIcon() noexcept {
		if (!iconAdded) return;
		Shell_NotifyIconW(NIM_DELETE, &notifyIcon);
		iconAdded = false;
		if (iconIsOwned && icon != nullptr) DestroyIcon(icon);
		icon = nullptr;
		iconIsOwned = false;
	}

	void StreamStatusIcon::UpdateTip() {
		if (!iconAdded) return;
		const auto tip = FormatTooltip(status.Description(), status.LoadSnapshot());
		notifyIcon.uFlags = NIF_TIP | NIF_SHOWTIP;
		wcsncpy_s(notifyIcon.szTip, tip.c_str(), _TRUNCATE);
		Shell_NotifyIconW(NIM_MODIFY, &notifyIcon);
	}

	void StreamStatusIcon::ShowPopup() {
		popupText = FormatPopup(status.Description(), status.LoadSnapshot());
		if (popup != nullptr) {
			InvalidateRect(popup, nullptr, TRUE);
			SetForegroundWindow(popup);
			return;
		}

		POINT cursor{};
		GetCursorPos(&cursor);
		RECT workArea{};
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
		constexpr int width = 460;
		constexpr int height = 230;
		int x = cursor.x;
		int y = cursor.y - height - 8;
		if (x + width > workArea.right) x = workArea.right - width;
		if (x < workArea.left) x = workArea.left;
		if (y < workArea.top) y = cursor.y + 8;
		if (y + height > workArea.bottom) y = workArea.bottom - height;

		if (font == nullptr) {
			font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
		}
		popup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClassName, L"FlexASIO",
			WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
			x, y, width, height, nullptr, nullptr, nullptr, this);
		if (popup != nullptr) SetForegroundWindow(popup);
	}

	void StreamStatusIcon::DestroyPopup() noexcept {
		if (popup == nullptr) return;
		const HWND window = popup;
		popup = nullptr;
		DestroyWindow(window);
	}

	void StreamStatusIcon::PaintPopup(const HWND popupWindow) {
		PAINTSTRUCT paint{};
		const HDC dc = BeginPaint(popupWindow, &paint);
		RECT client{};
		GetClientRect(popupWindow, &client);
		FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
		InflateRect(&client, -12, -12);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
		const HGDIOBJ previousFont = font != nullptr ? SelectObject(dc, font) : nullptr;
		DrawTextW(dc, popupText.c_str(), static_cast<int>(popupText.size()), &client, DT_LEFT | DT_TOP | DT_NOPREFIX);
		if (previousFont != nullptr) SelectObject(dc, previousFont);
		EndPaint(popupWindow, &paint);
	}

	LRESULT StreamStatusIcon::HandleMessage(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
		switch (message) {
		case WM_NCCREATE:
			// CreateWindow can paint before it returns, so remember the popup before the first WM_PAINT.
			if (messageWindow.load() != nullptr && window != messageWindow.load()) popup = window;
			break;
		case WM_TRAY: {
			const auto event = LOWORD(lParam);
			if (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK) ShowPopup();
			return 0;
		}
		case WM_TIMER:
			UpdateTip();
			if (popup != nullptr) {
				popupText = FormatPopup(status.Description(), status.LoadSnapshot());
				InvalidateRect(popup, nullptr, TRUE);
			}
			return 0;
		case WM_CLOSE:
			if (window == popup) {
				const HWND closing = popup;
				popup = nullptr;
				DestroyWindow(closing);
				return 0;
			}
			break;
		case WM_DESTROY:
			if (window == popup) popup = nullptr;
			return 0;
		case WM_PAINT:
			if (window == popup) {
				PaintPopup(window);
				return 0;
			}
			break;
		case WM_APP_STOP:
			PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	LRESULT CALLBACK StreamStatusIcon::WindowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept {
		if (message == WM_NCCREATE) {
			const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		}
		auto* self = reinterpret_cast<StreamStatusIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (self == nullptr) return DefWindowProcW(window, message, wParam, lParam);
		try {
			return self->HandleMessage(window, message, wParam, lParam);
		}
		catch (const std::exception& exception) {
			Log() << "Stream status window error: " << exception.what();
		}
		catch (...) {
			Log() << "Stream status window error";
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

}
