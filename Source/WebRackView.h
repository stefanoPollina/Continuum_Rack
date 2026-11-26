#pragma once
// The web rack view: the Continuum Rack page itself, running in the plugin's WebView
// (WebView2 on Windows, WKWebView on macOS, WebKitGTK on Linux), wired to the C++ engine
// through RackWebBridge. Only built where JUCE_WEB_BROWSER is enabled.

#include "WebBridge.h"

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WEB_BROWSER

class WebRackView final : public juce::Component, private juce::Timer
{
public:
    explicit WebRackView (ContinuumRackProcessor&);
    ~WebRackView() override;
    void resized() override;
    void visibilityChanged() override;

    // The page with the bridge script inserted before </body>; empty if resources are missing.
    static juce::String buildPage();

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> provide (const juce::String& path);

    RackWebBridge bridge;
    std::vector<std::byte> page;
    std::unique_ptr<juce::WebBrowserComponent> browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebRackView)
};

#else

// Built without a WebView: the plugin window shows only the native panel.
class WebRackView final : public juce::Component {};

#endif
