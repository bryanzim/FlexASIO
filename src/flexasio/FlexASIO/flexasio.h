#pragma once

#include "../FlexASIOUtil/portaudio.h"
#include "config.h"
#include "portaudio.h"
#include "stream_status.h"

#include <dechamps_ASIOUtil/asiosdk/asiosys.h>

#include <dechamps_ASIOUtil/asiosdk/asio.h>
#include <portaudio.h>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace flexasio
{

    class ASIOException : public std::runtime_error
    {
    public:
        template <typename... Args>
        explicit ASIOException(ASIOError asioError, Args&&... args) : std::runtime_error{ std::forward<Args>(args)... }, asioError{ asioError } {}
        [[nodiscard]] constexpr ASIOError GetASIOError() const noexcept { return asioError; }

    private:
        ASIOError asioError;
    };

    class FlexASIO final
    {
    public:
        explicit FlexASIO(void* sysHandle);

        void GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity);
        void GetChannels(long* numInputChannels, long* numOutputChannels);
        void GetChannelInfo(ASIOChannelInfo* info);
        [[nodiscard]] bool CanSampleRate(ASIOSampleRate sampleRate);
        void SetSampleRate(ASIOSampleRate requestedSampleRate);
        void GetSampleRate(ASIOSampleRate* sampleRateResult);

        void CreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
        void DisposeBuffers();

        void GetLatencies(long* inputLatency, long* outputLatency);
        void Start();
        void Stop();
        void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
        void OutputReady();

        void ControlPanel();

    private:
        struct SampleType
        {
            ASIOSampleType asio;
            PaSampleFormat pa;
            std::size_t size;
            GUID waveSubFormat;
        };

        enum class StreamExclusivity
        {
            SHARED,
            EXCLUSIVE
        };

        class PortAudioHandle
        {
        public:
            explicit PortAudioHandle();
            PortAudioHandle(const PortAudioHandle&) = delete;
            PortAudioHandle(const PortAudioHandle&&) = delete;
            ~PortAudioHandle();
        };

        class Win32HighResolutionTimer
        {
        public:
            explicit Win32HighResolutionTimer();
            Win32HighResolutionTimer(const Win32HighResolutionTimer&) = delete;
            Win32HighResolutionTimer(Win32HighResolutionTimer&&) = delete;
            ~Win32HighResolutionTimer();
            [[nodiscard]] DWORD GetTimeMilliseconds() const noexcept;
        };

        class PreparedState
        {
        public:
            PreparedState(FlexASIO& flexASIO, ASIOSampleRate sampleRate, ASIOBufferInfo* asioBufferInfos, long numChannels, long bufferSizeInFrames, ASIOCallbacks* callbacks);
            PreparedState(const PreparedState&) = delete;
            PreparedState(PreparedState&&) = delete;

            [[nodiscard]] constexpr StreamExclusivity GetStreamExclusivity() const noexcept { return streamWithExclusivity.exclusivity; }

            [[nodiscard]] bool IsChannelActive(bool isInput, long channel) const noexcept;

            void GetLatencies(long* inputLatency, long* outputLatency);
            void Start();
            void Stop();

            void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
            void OutputReady();

            void RequestReset() const;

        private:
            struct Buffers
            {
                Buffers(std::size_t bufferSetCount, std::size_t inputChannelCount, std::size_t outputChannelCount, std::size_t bufferSizeInFrames, std::size_t inputSampleSizeInBytes, std::size_t outputSampleSizeInBytes);
                ~Buffers();
                [[nodiscard]] std::byte* GetInputBuffer(std::size_t bufferSetIndex, std::size_t channelIndex) noexcept { return buffers.data() + bufferSetIndex * GetBufferSetSizeInBytes() + channelIndex * GetInputBufferSizeInBytes(); }
                [[nodiscard]] std::byte* GetOutputBuffer(std::size_t bufferSetIndex, std::size_t channelIndex) noexcept { return GetInputBuffer(bufferSetIndex, inputChannelCount) + channelIndex * GetOutputBufferSizeInBytes(); }
                [[nodiscard]] constexpr std::size_t GetBufferSetSizeInBytes() const noexcept { return buffers.size() / bufferSetCount; }
                [[nodiscard]] constexpr std::size_t GetInputBufferSizeInBytes() const noexcept
                {
                    if (buffers.empty())
                    {
                        return 0U;
                    }
                    return bufferSizeInFrames * inputSampleSizeInBytes;
                }
                [[nodiscard]] constexpr std::size_t GetOutputBufferSizeInBytes() const noexcept
                {
                    if (buffers.empty())
                    {
                        return 0U;
                    }
                    return bufferSizeInFrames * outputSampleSizeInBytes;
                }

                const std::size_t bufferSetCount;
                const std::size_t inputChannelCount;
                const std::size_t outputChannelCount;
                const std::size_t bufferSizeInFrames;
                const std::size_t inputSampleSizeInBytes;
                const std::size_t outputSampleSizeInBytes;

                // This is a giant buffer containing all ASIO buffers. It is organized as follows:
                // [ input channel 0 buffer 0 ] [ input channel 1 buffer 0 ] ... [ input channel N buffer 0 ] [ output channel 0 buffer 0 ] [ output channel 1 buffer 0 ] .. [ output channel N buffer 0 ]
                // [ input channel 0 buffer 1 ] [ input channel 1 buffer 1 ] ... [ input channel N buffer 1 ] [ output channel 0 buffer 1 ] [ output channel 1 buffer 1 ] .. [ output channel N buffer 1 ]
                // The reason why this is a giant blob is to slightly improve performance by (theroretically) improving memory locality.
                std::vector<std::byte> buffers;
            };

            class RunningState
            {
            public:
                explicit RunningState(PreparedState& preparedState);
                ~RunningState();

                // Note: the reason why this is not done in the constructor is to allow `PreparedState::Start()`
                // to properly set `PreparedState::runningState` before callbacks start flying. This is because
                // the ASIO host application may decide to call GetSamplePosition() or OutputReady() as soon
                // as bufferSwitch() is called without waiting for Start() to return - we don't want these calls
                // to race with `PreparedState::Start()` constructing `PreparedState::runningState`.
                void Start();

                void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) const;
                void OutputReady();

                PaStreamCallbackResult StreamCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags);
                void NoteStreamStatus(PaStreamCallbackFlags statusFlags) noexcept;
                [[nodiscard]] std::int64_t TicksToFrames(std::int64_t ticks) const noexcept;

            private:
                enum class State
                {
                    PRIMING,
                    PRIMED,
                    STEADYSTATE
                };

                struct SamplePosition
                {
                    ASIOSamples samples{};
                    ASIOTimeStamp timestamp{};
                };

                PreparedState& preparedState;
                const bool host_supports_timeinfo;
                enum class OutputReadyState
                {
                    NOT_READY,
                    READY,
                    STOPPING
                };
                std::optional<std::atomic<OutputReadyState>> outputReadyState;
                State state{ outputReadyState.has_value() ? State::PRIMING : State::PRIMED };
                // The index of the "unlocked" buffer (or "half-buffer", i.e. 0 or 1) that contains data not currently being processed by the ASIO host.
                long driverBufferIndex{ state == State::PRIMING ? 1 : 0 };
                std::atomic<SamplePosition> samplePosition;

                Win32HighResolutionTimer win32HighResolutionTimer;
                std::int64_t qpcFrequency{ 0 };
                std::int64_t lastSteadyCallbackQpc{ 0 };
                bool haveSteadyCallbackQpc{ false };
                // Declared before the active stream so stopping the stream joins the callback
                // before the icon thread and these counters are destroyed.
                std::optional<StreamStatus> streamStatus;
                std::optional<StreamStatusIcon> streamStatusIcon;
                ActiveStream activeStream;
            };

            static int StreamCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) noexcept;

            void OnConfigChange();
            [[nodiscard]] StreamStatusDescription MakeStreamStatusDescription() const;

            FlexASIO& flexASIO;
            const ASIOSampleRate sampleRate;
            const ASIOCallbacks callbacks;

            // PortAudio buffer addresses are dynamic and are only valid for the duration of the stream callback.
            // In contrast, ASIO buffer addresses are static and are valid for as long as the stream is running.
            // Thus we need our own buffer on top of PortAudio's buffers. This doens't add any latency because buffers are copied immediately.
            Buffers buffers;
            const std::vector<ASIOBufferInfo> bufferInfos;

            struct StreamWithExclusivity final
            {
                Stream stream;
                StreamExclusivity exclusivity;
            };
            const StreamWithExclusivity streamWithExclusivity;

            std::optional<RunningState> runningState;
            ConfigLoader::Watcher configWatcher;
        };

        static const SampleType float32;
        static const SampleType int32;
        static const SampleType int24;
        static const SampleType int16;
        static const std::array<std::pair<std::string_view, SampleType>, 4U> sampleTypes;
        static SampleType ParseSampleType(std::string_view str);
        static SampleType WaveFormatToSampleType(const WAVEFORMATEXTENSIBLE& waveFormat);
        static SampleType SelectSampleType(PaHostApiTypeId hostApiTypeId, const Device& device, const Config::Stream& streamConfig);
        static std::string DescribeSampleType(const SampleType&);
        static DWORD SelectChannelMask(PaHostApiTypeId hostApiTypeId, const Device& device, const Config::Stream& streamConfig);

        [[nodiscard]] int GetInputChannelCount() const noexcept;
        [[nodiscard]] int GetOutputChannelCount() const noexcept;

        struct BufferSizes
        {
            long minimum;
            long maximum;
            long preferred;
            long granularity;
        };
        [[nodiscard]] BufferSizes ComputeBufferSizes() const;

        long ComputeLatency(long latencyInFrames, bool output, std::size_t bufferSizeInFrames) const;
        long ComputeLatencyFromStream(PaStream* stream, bool output, std::size_t bufferSizeInFrames) const;

        template <typename Functor>
        decltype(auto) WithStreamParameters(bool inputEnabled, bool outputEnabled, double sampleRate, PaTime suggestedLatency, Functor functor) const;
        Stream OpenStream(const StreamParameters&, unsigned long framesPerBuffer, PaStreamCallback callback, void* callbackUserData) const;

        const HWND windowHandle{ nullptr };
        const ConfigLoader configLoader;
        const Config& config = configLoader.Initial();

        PortAudioDebugRedirector portAudioDebugRedirector;
        PortAudioHandle portAudioHandle;

        const HostApi hostApi;
        const std::optional<Device> inputDevice;
        const std::optional<Device> outputDevice;
        const std::optional<SampleType> inputSampleType;
        const std::optional<SampleType> outputSampleType;
        const DWORD inputChannelMask;
        const DWORD outputChannelMask;

        ASIOSampleRate sampleRate{ 0 };
        bool sampleRateWasAccessed{ false };
        bool hostSupportsOutputReady{ false };

        std::optional<PreparedState> preparedState;
    };

}