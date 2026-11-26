#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class NativeRackView;
class WebRackView;

// The plugin window. It shows the Continuum Rack in one of two ways, switched with the
// button in the bottom-right corner (the choice is saved with the project):
//   WEB RACK    - the Continuum Rack web page itself, in a WebView, driving the C++ engine
//   NATIVE RACK - the same rack redrawn natively in JUCE
class ContinuumRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ContinuumRackEditor (ContinuumRackProcessor&);
    ~ContinuumRackEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    bool hasWebView() const noexcept;
    void showView (int view);   // 0 = web rack, 1 = native rack
    NativeRackView* getNativeView() noexcept { return native.get(); }

private:
    struct SwitchLook : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
    };

    ContinuumRackProcessor& rack;
    SwitchLook look;
    std::unique_ptr<NativeRackView> native;
    std::unique_ptr<WebRackView> web;
    juce::TextButton switchButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContinuumRackEditor)
};
