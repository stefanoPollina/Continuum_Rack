#include "WebRackView.h"

#if JUCE_WEB_BROWSER

#include <BinaryData.h>

juce::String WebRackView::buildPage()
{
    juce::MemoryInputStream raw (BinaryData::index_html_gz, (size_t) BinaryData::index_html_gzSize, false);
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    const auto html = gz.readEntireStreamAsString();
    const auto bridgeJs = juce::String::fromUTF8 (BinaryData::pluginbridge_js, BinaryData::pluginbridge_jsSize);
    const int at = html.lastIndexOfIgnoreCase ("</body>");
    if (html.isEmpty() || at < 0) return {};
    return html.substring (0, at) + "<script>\n" + bridgeJs + "\n</script>\n" + html.substring (at);
}

WebRackView::WebRackView (ContinuumRackProcessor& p) : bridge (p)
{
    const auto html = buildPage().toStdString();
    page.resize (html.size());
    std::memcpy (page.data(), html.data(), html.size());

    auto options = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                     // the DAW's own folder is usually read-only; keep WebView2's profile in temp
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ContinuumRackWebView"))
                                     .withBackgroundColour (juce::Colour (0xff15171a))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& path) { return provide (path); });

    for (const auto& id : RackWebBridge::pageEvents())
        options = options.withEventListener (id, [this, id] (juce::var payload) { bridge.handle (id, payload); });

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    bridge.send = [this] (const juce::Identifier& id, const juce::var& v) {
        if (browser != nullptr) browser->emitEventIfBrowserIsVisible (id, v);
    };
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (25);
}

WebRackView::~WebRackView()
{
    stopTimer();
    bridge.send = nullptr;
}

void WebRackView::resized()
{
    if (browser != nullptr) browser->setBounds (getLocalBounds());
}

void WebRackView::visibilityChanged()
{
    // events are only delivered while the browser is showing; catch up on whatever changed
    if (isShowing()) bridge.resync();
}

void WebRackView::timerCallback()
{
    if (isShowing()) bridge.tick();
}

std::optional<juce::WebBrowserComponent::Resource> WebRackView::provide (const juce::String& path)
{
    if (path == "/" || path == "/index.html")
        return juce::WebBrowserComponent::Resource { page, "text/html" };
    return std::nullopt;
}

#endif
