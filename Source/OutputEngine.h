#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <vector>

// Takes audio from the DAW's audio thread (process) and plays a copy of it on a
// Windows output device (WASAPI shared mode, called "Windows Audio" in JUCE).
// Windows (and therefore Discord) sees this as a normal audio stream of the DAW process.
class OutputEngine : public juce::AudioIODeviceCallback,
                     private juce::AsyncUpdater,
                     private juce::Timer
{
public:
    enum class Status { off, connecting, connected, deviceMissing, error };

    static constexpr int numBufferSteps = 5;
    static int bufferMsForStep (int step) noexcept { return 40 + 40 * step; } // 40 ... 200 ms

    OutputEngine();
    ~OutputEngine() override;

    // ---- Message thread only ----
    juce::StringArray getOutputDeviceNames();
    juce::String getLastError() const { return lastError; }
    float takePeak (int channel) noexcept; // returns the peak since the last call and resets it

    // ---- Any thread ----
    void setOutputDevice (const juce::String& deviceName); // opens asynchronously, empty = off
    juce::String getOutputDevice() const;
    Status getStatus() const noexcept { return status.load(); }

    void setGainDb (float db) noexcept;
    float getGainDb() const noexcept { return gainDb.load(); }

    void setBufferStep (int step) noexcept;
    int getBufferStep() const noexcept { return bufferStep.load(); }
    int getBufferMs() const noexcept { return bufferMsForStep (bufferStep.load()); }

    // ---- DAW audio thread ----
    void process (const juce::AudioBuffer<float>& buffer, double sampleRate, bool offlineRender) noexcept;

    // ---- Status for the editor ----
    double getBufferedMs() const noexcept;
    int getUnderruns() const noexcept { return underruns.load(); }
    int getOverruns() const noexcept { return overruns.load(); }
    bool isReceivingBlocks() const noexcept; // the DAW is calling the plugin
    bool isStreaming() const noexcept;       // the DAW is feeding audio into the stream
    bool isOfflineRender() const noexcept { return offline.load(); }

    // ---- AudioIODeviceCallback (audio thread of the output device) ----
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String&) override { deviceError = true; }

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void ensureInitialised();
    void shutDownDevice();
    void attemptConnect (const juce::String& wanted);

    static constexpr int ringSize = 1 << 17;    // about 0.68 s at 192 kHz
    static constexpr int scratchSize = 1 << 15;

    juce::AudioDeviceManager manager;
    bool initialised = false;
    juce::String lastError;

    mutable juce::CriticalSection lock;
    juce::String desiredDevice; // protected by lock

    juce::AbstractFifo fifo { ringSize };
    juce::AudioBuffer<float> ring { 2, ringSize };

    std::atomic<Status> status { Status::off };
    std::atomic<bool> active { false };
    std::atomic<bool> offline { false };
    std::atomic<bool> deviceError { false };
    std::atomic<double> srcRate { 0.0 };
    std::atomic<double> deviceRate { 0.0 };
    std::atomic<float> gain { 1.0f };
    std::atomic<float> gainDb { 0.0f };
    std::atomic<int> bufferStep { 0 };
    std::atomic<int> underruns { 0 }, overruns { 0 };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<juce::uint32> lastBlockMs { 0 }, lastPushMs { 0 }, lastDeviceCallbackMs { 0 };

    // only used on the output device's audio thread
    bool primed = false;
    int lastStep = -1;
    juce::LagrangeInterpolator interp[2];
    std::vector<float> scratch[2];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;
};
