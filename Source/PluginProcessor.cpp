#include "PluginProcessor.h"
#include "PluginEditor.h"

StreamBridgeProcessor::StreamBridgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void StreamBridgeProcessor::prepareToPlay (double sampleRate, int)
{
    juce::ignoreUnused (sampleRate);
}

bool StreamBridgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StreamBridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The signal passes through unchanged. We only measure it and send a copy to the
    // chosen output device. During an offline render/bounce nothing is sent.
    engine.process (buffer, getSampleRate(), isNonRealtime());
}

juce::AudioProcessorEditor* StreamBridgeProcessor::createEditor()
{
    return new StreamBridgeEditor (*this);
}

void StreamBridgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("DAWDiscordBridge");
    xml.setAttribute ("device", engine.getOutputDevice());
    xml.setAttribute ("gainDb", (double) engine.getGainDb());
    xml.setAttribute ("bufferStep", engine.getBufferStep());
    copyXmlToBinary (xml, destData);
}

void StreamBridgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("DAWDiscordBridge"))
        {
            engine.setGainDb ((float) xml->getDoubleAttribute ("gainDb", 0.0));
            engine.setBufferStep (xml->getIntAttribute ("bufferStep", 0));
            engine.setOutputDevice (xml->getStringAttribute ("device"));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StreamBridgeProcessor();
}
