#include "OutputEngine.h"
#include <cmath>

namespace
{
    constexpr juce::uint32 dawIdleMs = 250;     // no audio pushed for this long = DAW stopped
    constexpr juce::uint32 blockSilenceMs = 500; // no blocks at all for this long = nothing arrives
    constexpr juce::uint32 deviceStallMs = 3000; // output device produced no callbacks for this long
    constexpr int reconnectIntervalMs = 2000;

    inline bool isRecent (juce::uint32 then, juce::uint32 windowMs) noexcept
    {
        return (juce::Time::getMillisecondCounter() - then) < windowMs;
    }

    inline void raisePeak (std::atomic<float>& peak, float value) noexcept
    {
        float current = peak.load (std::memory_order_relaxed);
        while (value > current && ! peak.compare_exchange_weak (current, value)) {}
    }

    // Everything below -2 dBFS passes unchanged. Above that the signal is bent smoothly
    // towards 0 dBFS and can never exceed it.
    inline float softClip (float x) noexcept
    {
        constexpr float threshold = 0.7943282f; // -2 dBFS
        constexpr float headroom = 1.0f - threshold;

        if (! std::isfinite (x))
            return 0.0f;

        const float a = std::abs (x);
        if (a <= threshold)
            return x;

        const float y = threshold + headroom * std::tanh ((a - threshold) / headroom);
        return x < 0.0f ? -y : y;
    }
}

OutputEngine::OutputEngine()
{
    for (auto& s : scratch)
        s.assign ((size_t) scratchSize, 0.0f);
    ring.clear();
}

OutputEngine::~OutputEngine()
{
    cancelPendingUpdate();
    stopTimer();
    active = false;

    if (initialised)
    {
        manager.removeAudioCallback (this);
        manager.closeAudioDevice();
    }
}

// ------------------------------------------------------------------ settings

void OutputEngine::setGainDb (float db) noexcept
{
    db = juce::jlimit (-60.0f, 6.0f, db);
    gainDb.store (db);
    gain.store (juce::Decibels::decibelsToGain (db, -60.0f));
}

void OutputEngine::setBufferStep (int step) noexcept
{
    bufferStep.store (juce::jlimit (0, numBufferSteps - 1, step));
}

// ------------------------------------------------------------------ device handling (message thread)

void OutputEngine::ensureInitialised()
{
    if (initialised)
        return;

    initialised = true;
    manager.initialise (0, 2, nullptr, false);
    manager.closeAudioDevice(); // do not open anything until the user picks a device

    // WASAPI shared mode is called "Windows Audio" in JUCE
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == "Windows Audio")
        {
            manager.setCurrentAudioDeviceType (type->getTypeName(), true);
            break;
        }
    }
    manager.closeAudioDevice();
}

juce::StringArray OutputEngine::getOutputDeviceNames()
{
    ensureInitialised();

    if (auto* type = manager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        return type->getDeviceNames (false); // false = output devices
    }
    return {};
}

void OutputEngine::setOutputDevice (const juce::String& deviceName)
{
    {
        const juce::ScopedLock sl (lock);

        // Hosts sometimes restore the same state several times: keep a working connection
        if (deviceName == desiredDevice && status.load() == Status::connected)
            return;

        desiredDevice = deviceName;
    }

    status = deviceName.isEmpty() ? Status::off : Status::connecting;
    triggerAsyncUpdate(); // the device itself is opened later on the message thread
}

juce::String OutputEngine::getOutputDevice() const
{
    const juce::ScopedLock sl (lock);
    return desiredDevice;
}

void OutputEngine::shutDownDevice()
{
    active = false;

    if (initialised)
    {
        manager.removeAudioCallback (this);
        manager.closeAudioDevice();
    }
}

void OutputEngine::attemptConnect (const juce::String& wanted)
{
    ensureInitialised();
    shutDownDevice();
    lastError.clear();
    deviceError = false;

    if (! getOutputDeviceNames().contains (wanted))
    {
        status = Status::deviceMissing;
        return;
    }

    status = Status::connecting;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    manager.getAudioDeviceSetup (setup);
    setup.outputDeviceName = wanted;
    setup.inputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = true;
    setup.sampleRate = 0.0; // the device's default rate
    setup.bufferSize = 0;

    const auto error = manager.setAudioDeviceSetup (setup, true);

    if (error.isEmpty() && manager.getCurrentAudioDevice() != nullptr)
    {
        lastDeviceCallbackMs.store (juce::Time::getMillisecondCounter());
        manager.addAudioCallback (this);
        active = true;
        status = Status::connected;
    }
    else
    {
        lastError = error.isNotEmpty() ? error : juce::String ("unknown error");
        status = Status::error;
    }
}

void OutputEngine::handleAsyncUpdate()
{
    const auto wanted = getOutputDevice();

    if (wanted.isEmpty())
    {
        stopTimer();
        shutDownDevice();
        lastError.clear();
        status = Status::off;
        return;
    }

    attemptConnect (wanted);
    startTimer (reconnectIntervalMs); // keeps watching the device and reconnects if it disappears
}

void OutputEngine::timerCallback()
{
    const auto wanted = getOutputDevice();

    if (wanted.isEmpty())
    {
        stopTimer();
        return;
    }

    if (status.load() == Status::connected)
    {
        auto* device = manager.getCurrentAudioDevice();
        const bool healthy = device != nullptr
                          && device->isOpen()
                          && ! deviceError.load()
                          && isRecent (lastDeviceCallbackMs.load(), deviceStallMs);
        if (healthy)
            return;
    }

    attemptConnect (wanted);
}

// ------------------------------------------------------------------ DAW audio thread

void OutputEngine::process (const juce::AudioBuffer<float>& buffer, double sampleRate, bool offlineRender) noexcept
{
    srcRate.store (sampleRate);
    offline.store (offlineRender);

    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh <= 0)
        return;

    const auto now = juce::Time::getMillisecondCounter();
    lastBlockMs.store (now);

    // Level meter (the signal itself stays untouched)
    raisePeak (peakL, buffer.getMagnitude (0, 0, n));
    raisePeak (peakR, buffer.getMagnitude (juce::jmin (1, numCh - 1), 0, n));

    // While the DAW renders/bounces faster than real time nothing is sent to the stream
    if (offlineRender || ! active.load())
        return;

    lastPushMs.store (now);

    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = buffer.getReadPointer (juce::jmin (ch, numCh - 1));
        float* dst = ring.getWritePointer (ch);
        if (size1 > 0) juce::FloatVectorOperations::copy (dst + start1, src, size1);
        if (size2 > 0) juce::FloatVectorOperations::copy (dst + start2, src + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);

    if (size1 + size2 < n)
        ++overruns;
}

float OutputEngine::takePeak (int channel) noexcept
{
    return (channel == 0 ? peakL : peakR).exchange (0.0f);
}

double OutputEngine::getBufferedMs() const noexcept
{
    const double sr = srcRate.load();
    return sr > 0.0 ? 1000.0 * fifo.getNumReady() / sr : 0.0;
}

bool OutputEngine::isReceivingBlocks() const noexcept
{
    return isRecent (lastBlockMs.load(), blockSilenceMs);
}

bool OutputEngine::isStreaming() const noexcept
{
    return isRecent (lastPushMs.load(), dawIdleMs);
}

// ------------------------------------------------------------------ output device audio thread

void OutputEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceRate = device->getCurrentSampleRate();
    primed = false;
    lastStep = -1;

    for (auto& i : interp)
        i.reset();

    smoothedGain.reset (deviceRate.load(), 0.03);
    smoothedGain.setCurrentAndTargetValue (gain.load());
}

void OutputEngine::audioDeviceStopped()
{
    primed = false;
}

void OutputEngine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                      float* const* out, int numOutCh,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutCh; ++ch)
        juce::FloatVectorOperations::clear (out[ch], numSamples);

    lastDeviceCallbackMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    const double sRate = srcRate.load();
    const double dRate = deviceRate.load();
    if (! active.load() || sRate <= 0.0 || dRate <= 0.0 || numOutCh < 1)
        return;

    // Is the DAW currently feeding audio? If not (stopped/paused) running dry is normal.
    const bool dawFeeding = isRecent (lastPushMs.load(), dawIdleMs);

    // The buffer size was changed: start filling again
    const int step = bufferStep.load();
    if (step != lastStep)
    {
        lastStep = step;
        primed = false;
    }

    const int target = (int) (bufferMsForStep (step) * 0.001 * sRate);
    int fill = fifo.getNumReady();

    // Start-up / after a gap: wait until enough audio is buffered, drop anything older
    if (! primed)
    {
        if (fill < target)
            return;
        if (fill > target)
            fifo.finishedRead (fill - target);
        primed = true;
        fill = target;
    }

    // The buffer ran too full (e.g. the DAW ran faster): cut back to the target
    if (fill > 3 * target)
    {
        fifo.finishedRead (fill - target);
        fill = target;
    }

    // Sample rate conversion + gentle correction of small clock differences
    const double err = juce::jlimit (-1.0, 1.0, (double) (fill - target) / (double) juce::jmax (1, target));
    const double ratio = (sRate / dRate) * (1.0 + 0.005 * err);

    const int needed = (int) std::ceil (numSamples * ratio) + 4;
    if (needed > scratchSize)
        return;

    if (fill < needed)
    {
        if (dawFeeding)
            ++underruns; // a real dropout: audio is arriving but not fast enough
        primed = false;
        return;
    }

    int s1, b1, s2, b2;
    fifo.prepareToRead (needed, s1, b1, s2, b2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* r = ring.getReadPointer (ch);
        if (b1 > 0) juce::FloatVectorOperations::copy (scratch[ch].data(), r + s1, b1);
        if (b2 > 0) juce::FloatVectorOperations::copy (scratch[ch].data() + b1, r + s2, b2);
    }

    const int used = interp[0].process (ratio, scratch[0].data(), out[0], numSamples);
    if (numOutCh > 1)
        interp[1].process (ratio, scratch[1].data(), out[1], numSamples);

    fifo.finishedRead (used);

    // Volume (smoothed, no clicks) followed by the soft clipper as a safety net
    smoothedGain.setTargetValue (gain.load());
    const int numCh = juce::jmin (numOutCh, 2);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            out[ch][i] = softClip (out[ch][i] * g);
    }
}
