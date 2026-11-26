#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class NativeRackView;
class WebRackView;
class CompactPanel;

// The plugin window. It shows the Continuum Rack in one of three ways, chosen with the
// buttons in the bar along the bottom (the choice is saved with the project):
//   WEB RACK    - the Continuum Rack web page itself, unchanged, in a WebView, driving the C++ engine
//   NATIVE RACK - the same rack redrawn natively in JUCE
//   PANEL       - the first, compact control-panel design
class ContinuumRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ContinuumRackEditor (ContinuumRackProcessor&);
    ~ContinuumRackEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    bool hasWebView() const noexcept;
    enum View { webRack = 0, nativeRack = 1, panel = 2 };
    void showView (int view);
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
    std::unique_ptr<CompactPanel> compact;
    juce::TextButton webButton { "WEB RACK" }, nativeButton { "NATIVE RACK" }, panelButton { "PANEL" };
    juce::Label viewLabel;
    static constexpr int barHeight = 30;   // the switch lives in its own strip: a WebView covers anything drawn over it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContinuumRackEditor)
};
