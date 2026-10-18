#include "PluginEditor.h"

namespace
{
    constexpr int offItemId = 1;
    constexpr int missingItemId = 1000;

    const juce::Colour meterGreen  (0xff3fbf5f);
    const juce::Colour meterYellow (0xffe6c229);
    const juce::Colour meterRed    (0xffe5484d);

    juce::Font makeFont (float height, bool bold = false)
    {
        juce::Font f { juce::FontOptions (height) };
        return bold ? f.boldened() : f;
    }

    void drawTip (juce::Graphics& g, juce::Rectangle<int> bounds,
                  const juce::String& head, const juce::String& body, juce::Colour colour)
    {
        juce::AttributedString text;
        text.setJustification (juce::Justification::topLeft);
        text.append (head, makeFont (12.0f, true), colour);
        text.append (body, makeFont (12.0f), colour);
        text.draw (g, bounds.toFloat());
    }
}

// ------------------------------------------------------------------ small components

void LevelMeter::setLevels (float leftDb, float rightDb)
{
    if (std::abs (leftDb - left) < 0.05f && std::abs (rightDb - right) < 0.05f)
        return;

    left = leftDb;
    right = rightDb;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float labelWidth = 14.0f;
    const float gap = 4.0f;
    const float barHeight = (area.getHeight() - gap) * 0.5f;
    const auto textColour = findColour (juce::Label::textColourId).withAlpha (0.7f);

    auto drawBar = [&] (juce::Rectangle<float> r, float db, const juce::String& name)
    {
        g.setColour (textColour);
        g.setFont (makeFont (11.0f));
        g.drawText (name, r.removeFromLeft (labelWidth).toNearestInt(), juce::Justification::centredLeft);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 2.0f);

        const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f));
        if (norm > 0.0f)
        {
            juce::ColourGradient gradient (meterGreen, r.getX(), 0.0f, meterRed, r.getRight(), 0.0f, false);
            gradient.addColour (0.70, meterGreen);
            gradient.addColour (0.88, meterYellow);
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (r.withWidth (r.getWidth() * norm), 2.0f);
        }
    };

    drawBar (area.withHeight (barHeight), left, "L");
    drawBar (area.withTrimmedTop (barHeight + gap), right, "R");
}

void StatusLamp::setOn (bool shouldBeOn)
{
    if (on == shouldBeOn)
        return;

    on = shouldBeOn;
    repaint();
}

void StatusLamp::paint (juce::Graphics& g)
{
    g.setColour (on ? meterGreen : juce::Colours::grey.withAlpha (0.5f));
    g.fillEllipse (getLocalBounds().toFloat().reduced (1.0f));
}

// ------------------------------------------------------------------ editor

StreamBridgeEditor::StreamBridgeEditor (StreamBridgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (440, 462);

    auto setupLabel = [this] (juce::Label& label, const juce::String& text, float height,
                              bool bold, juce::Justification justification, float alpha)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (makeFont (height, bold));
        label.setJustificationType (justification);
        label.setColour (juce::Label::textColourId,
                         getLookAndFeel().findColour (juce::Label::textColourId).withAlpha (alpha));
        label.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (label);
    };

    setupLabel (titleLabel, "DAW-Discord-Bridge", 20.0f, true, juce::Justification::centredLeft, 1.0f);
    setupLabel (deviceLabel, "Output device (Windows Audio)", 14.0f, false, juce::Justification::centredLeft, 1.0f);
    setupLabel (meterLabel, "Signal from your DAW", 14.0f, false, juce::Justification::centredLeft, 1.0f);
    setupLabel (lampLabel, "Waiting for DAW audio", 12.0f, false, juce::Justification::centredLeft, 0.8f);
    setupLabel (gainLabel, "Stream volume", 14.0f, false, juce::Justification::centredLeft, 1.0f);
    setupLabel (bufferLabel, {}, 14.0f, false, juce::Justification::centredLeft, 1.0f);
    setupLabel (lessDelayLabel, "Less delay", 12.0f, false, juce::Justification::centredLeft, 0.7f);
    setupLabel (fewerDropoutsLabel, "Fewer dropouts", 12.0f, false, juce::Justification::centredRight, 0.7f);
    setupLabel (statusLabel, {}, 12.0f, false, juce::Justification::topLeft, 0.8f);

    addAndMakeVisible (deviceBox);
    addAndMakeVisible (refreshButton);
    addAndMakeVisible (meter);
    addAndMakeVisible (lamp);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (bufferSlider);

    // Stream volume
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setRange (-60.0, 6.0, 0.1);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setValue (proc.engine.getGainDb(), juce::dontSendNotification);
    gainSlider.onValueChange = [this] { proc.engine.setGainDb ((float) gainSlider.getValue()); };

    // Stream buffer: 5 steps, 40 ... 200 ms
    bufferSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bufferSlider.setRange (0.0, (double) (OutputEngine::numBufferSteps - 1), 1.0);
    bufferSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bufferSlider.setValue (proc.engine.getBufferStep(), juce::dontSendNotification);
    bufferSlider.onValueChange = [this]
    {
        proc.engine.setBufferStep ((int) std::lround (bufferSlider.getValue()));
        updateBufferLabel();
    };
    updateBufferLabel();

    // Output device
    deviceBox.onChange = [this]
    {
        const int id = deviceBox.getSelectedId();
        if (id == missingItemId)
            return; // the entry for a device that is currently not found is not selectable

        proc.engine.setOutputDevice (id == offItemId ? juce::String() : deviceBox.getText());
    };

    refreshButton.onClick = [this] { refreshDevices(); };

    refreshDevices();
    updateStatusText();
    lastStatus = proc.engine.getStatus();
    startTimerHz (30);
}

void StreamBridgeEditor::refreshDevices()
{
    const auto current = proc.engine.getOutputDevice();

    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItem ("Off", offItemId);

    int id = 2;
    int selected = offItemId;
    bool found = current.isEmpty();

    for (auto& name : proc.engine.getOutputDeviceNames())
    {
        deviceBox.addItem (name, id);
        if (name == current)
        {
            selected = id;
            found = true;
        }
        ++id;
    }

    if (! found)
    {
        deviceBox.addItem (current + " (not found)", missingItemId);
        selected = missingItemId;
    }

    deviceBox.setSelectedId (selected, juce::dontSendNotification);
}

void StreamBridgeEditor::updateBufferLabel()
{
    bufferLabel.setText ("Stream buffer: " + juce::String (proc.engine.getBufferMs()) + " ms",
                         juce::dontSendNotification);
}

void StreamBridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    const auto tipColour = getLookAndFeel().findColour (juce::Label::textColourId).withAlpha (0.65f);

    drawTip (g, tipDeviceBounds, "Tip: ",
             "Choose an output device you are not actively listening to. DAW-Discord-Bridge sends a copy of "
             "your DAW's audio to it through Windows, so Discord can pick it up when you stream. "
             "Your normal DAW output is not affected.",
             tipColour);

    drawTip (g, tipBufferBounds, "Tip: ",
             "Hear crackling or stuttering in your Discord stream? Move the slider to the right, "
             "one step at a time.",
             tipColour);
}

void StreamBridgeEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 14);

    titleLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);

    deviceLabel.setBounds (area.removeFromTop (20));
    auto row = area.removeFromTop (28);
    refreshButton.setBounds (row.removeFromRight (72));
    row.removeFromRight (6);
    deviceBox.setBounds (row);
    area.removeFromTop (6);
    tipDeviceBounds = area.removeFromTop (64);
    area.removeFromTop (10);

    meterLabel.setBounds (area.removeFromTop (20));
    auto meterRow = area.removeFromTop (26);
    auto lampArea = meterRow.removeFromRight (160);
    meter.setBounds (meterRow.withTrimmedRight (10));
    lamp.setBounds (lampArea.removeFromLeft (16).withSizeKeepingCentre (12, 12));
    lampLabel.setBounds (lampArea.withTrimmedLeft (4));
    area.removeFromTop (12);

    gainLabel.setBounds (area.removeFromTop (20));
    gainSlider.setBounds (area.removeFromTop (26));
    area.removeFromTop (12);

    bufferLabel.setBounds (area.removeFromTop (20));
    bufferSlider.setBounds (area.removeFromTop (26));
    auto ends = area.removeFromTop (18);
    lessDelayLabel.setBounds (ends.removeFromLeft (ends.getWidth() / 2));
    fewerDropoutsLabel.setBounds (ends);
    area.removeFromTop (4);
    tipBufferBounds = area.removeFromTop (34);
    area.removeFromTop (10);

    statusLabel.setBounds (area);
}

void StreamBridgeEditor::updateStatusText()
{
    auto& e = proc.engine;
    const auto device = e.getOutputDevice();
    juce::String text;

    if (device.isEmpty())
    {
        text = "No output device selected. Your DAW signal still passes through unchanged.";
    }
    else
    {
        switch (e.getStatus())
        {
            case OutputEngine::Status::off:
            case OutputEngine::Status::connecting:
                text = "Connecting to the output device...";
                break;

            case OutputEngine::Status::deviceMissing:
                text = "Device not found: " + device + ". It will reconnect automatically when it is available again.";
                break;

            case OutputEngine::Status::error:
                text = "Could not open the device (" + e.getLastError() + "). Trying again...";
                break;

            case OutputEngine::Status::connected:
                if (e.isOfflineRender())
                    text = "Offline render in progress: stream output is paused.";
                else if (! e.isStreaming())
                    text = "Connected. Idle: waiting for audio from your DAW.";
                else
                    text = "Connected.   Buffer: " + juce::String (e.getBufferedMs(), 0) + " ms   |   Dropouts: "
                         + juce::String (e.getUnderruns()) + "   |   Overflows: " + juce::String (e.getOverruns());
                break;
        }
    }

    statusLabel.setText (text, juce::dontSendNotification);
}

void StreamBridgeEditor::timerCallback()
{
    auto& e = proc.engine;

    // Level meter: instant rise, fall of about 30 dB per second
    const float l = juce::Decibels::gainToDecibels (e.takePeak (0), -100.0f);
    const float r = juce::Decibels::gainToDecibels (e.takePeak (1), -100.0f);
    shownLeftDb = juce::jmax (l, shownLeftDb - 1.0f);
    shownRightDb = juce::jmax (r, shownRightDb - 1.0f);
    meter.setLevels (shownLeftDb, shownRightDb);

    // "Receiving audio" lamp: lit while there is a signal, stays on for a moment afterwards
    if (juce::jmax (l, r) > -60.0f)
        lampHold = 12;
    else if (lampHold > 0)
        --lampHold;

    const bool receiving = lampHold > 0;
    lamp.setOn (receiving);
    lampLabel.setText (receiving ? "Receiving audio"
                                 : (e.isReceivingBlocks() ? "No signal (silence)" : "Waiting for DAW audio"),
                       juce::dontSendNotification);

    // Reload the device list when the connection state changes (e.g. a device came back)
    const auto status = e.getStatus();
    if (status != lastStatus)
    {
        lastStatus = status;
        refreshDevices();
        updateStatusText();
    }

    if (++tick % 4 == 0)
        updateStatusText();
}
