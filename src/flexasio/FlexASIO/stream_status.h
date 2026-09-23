#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace flexasio {

	struct StreamStatusDescription {
		std::wstring backend;
		std::wstring inputDevice;
		std::wstring outputDevice;
		double sampleRate = 0;
		int inputBits = 0;
		int outputBits = 0;
		int inputChannels = 0;
		int outputChannels = 0;
		long bufferFrames = 0;
		std::int64_t bitrateBitsPerSecond = 0;
	};

	// Published by the PortAudio callback through atomics. The audio thread must not
	// take locks, allocate, or touch the window from this object.
	class StreamStatus final {
	public:
		explicit StreamStatus(StreamStatusDescription description) noexcept : description(std::move(description)) {}

		const StreamStatusDescription& Description() const noexcept { return description; }

		void NoteGlitches(bool inputOverflow, bool inputUnderflow, bool outputOverflow, bool outputUnderflow) noexcept;
		void NoteSlackFrames(std::int64_t slackFrames) noexcept;
		void Reset() noexcept;

		struct Snapshot {
			std::uint64_t inputOverflow = 0;
			std::uint64_t inputUnderflow = 0;
			std::uint64_t outputOverflow = 0;
			std::uint64_t outputUnderflow = 0;
			std::int64_t minSlackFrames = 0;
			std::int64_t averageSlackFrames = 0;
			std::int64_t maxSlackFrames = 0;
			bool hasSlack = false;
		};
		Snapshot LoadSnapshot() const noexcept;

	private:
		StreamStatusDescription description;
		std::atomic<std::uint64_t> inputOverflowCount{ 0 };
		std::atomic<std::uint64_t> inputUnderflowCount{ 0 };
		std::atomic<std::uint64_t> outputOverflowCount{ 0 };
		std::atomic<std::uint64_t> outputUnderflowCount{ 0 };
		std::atomic<std::int64_t> minSlackFrames{ INT64_MAX };
		std::atomic<std::int64_t> maxSlackFrames{ INT64_MIN };
		std::atomic<std::int64_t> slackSumFrames{ 0 };
		std::atomic<std::uint64_t> slackSampleCount{ 0 };
	};

	// Notification-area icon and click popup. Owns a thread with its own message loop
	// because the ASIO host does not pump messages for the driver.
	class StreamStatusIcon final {
	public:
		explicit StreamStatusIcon(StreamStatus& status);
		~StreamStatusIcon();

		StreamStatusIcon(const StreamStatusIcon&) = delete;
		StreamStatusIcon& operator=(const StreamStatusIcon&) = delete;

	private:
		void ThreadMain() noexcept;
		void AddIcon();
		void RemoveIcon() noexcept;
		void UpdateTip();
		void ShowPopup();
		void DestroyPopup() noexcept;
		void PaintPopup(HWND popupWindow);
		LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

		StreamStatus& status;
		HANDLE started = nullptr;
		std::thread thread;
		std::atomic<HWND> messageWindow{ nullptr };
		HWND popup = nullptr;
		HWND resetButton = nullptr;
		HICON icon = nullptr;
		bool iconIsOwned = false;
		HFONT font = nullptr;
		NOTIFYICONDATAW notifyIcon{};
		bool iconAdded = false;
		std::wstring popupText;
	};

}
