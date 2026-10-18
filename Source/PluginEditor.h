#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Two thin horizontal bars (L / R) showing the level coming from the DAW (-60 ... 0 dB)
class LevelMeter : public juce::Component
{
public:
    void setLevels (float leftDb, float rightDb);
    void paint (juce::Graphics&) override;

private:
    float left = -100.0f, right = -100.0f;
};

// Small round indicator light
class StatusLamp : public juce::Component
{
public:
    void setOn (bool shouldBeOn);
    void paint (juce::Graphics&) override;

private:
    bool on = false;
};

class StreamBridgeEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit StreamBridgeEditor (StreamBridgeProcessor&);
    ~StreamBridgeEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshDevices();
    void updateBufferLabel();
    void updateStatusText();

    StreamBridgeProcessor& proc;

    juce::Label titleLabel;
    juce::Label deviceLabel;
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "Refresh" };

    juce::Label meterLabel;
    LevelMeter meter;
    StatusLamp lamp;
    juce::Label lampLabel;

    juce::Label gainLabel;
    juce::Slider gainSlider;

    juce::Label bufferLabel;
    juce::Slider bufferSlider;
    juce::Label lessDelayLabel;
    juce::Label fewerDropoutsLabel;

    juce::Label statusLabel;

    juce::Rectangle<int> tipDeviceBounds, tipBufferBounds;

    float shownLeftDb = -100.0f, shownRightDb = -100.0f;
    int lampHold = 0;
    int tick = 0;
    OutputEngine::Status lastStatus = OutputEngine::Status::off;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamBridgeEditor)
};
