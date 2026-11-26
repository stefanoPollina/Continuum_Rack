#include "PluginEditor.h"
#include "NativeRack.h"
#include "CompactPanel.h"
#include "WebRackView.h"

void ContinuumRackEditor::SwitchLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto c = on ? juce::Colour (0xffff9000) : juce::Colour (0xff3a3d42);
    if (! b.isEnabled()) c = juce::Colour (0xff26282b);
    g.setColour (c.brighter (over && b.isEnabled() ? 0.12f : 0.0f).darker (down ? 0.2f : 0.0f));
    g.fillRoundedRectangle (r, 4);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 4, 1);
}

juce::Font ContinuumRackEditor::SwitchLook::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions (11.0f, juce::Font::bold));
}

ContinuumRackEditor::ContinuumRackEditor (ContinuumRackProcessor& p) : AudioProcessorEditor (p), rack (p)
{
    native = std::make_unique<NativeRackView> (p);
    addChildComponent (*native);
    compact = std::make_unique<CompactPanel> (p);
    addChildComponent (*compact);
#if JUCE_WEB_BROWSER
    // CONTINUUM_RACK_NO_WEBVIEW is set by the CI tests, which run without a desktop session
    if (juce::SystemStats::getEnvironmentVariable ("CONTINUUM_RACK_NO_WEBVIEW", {}).isEmpty())
    {
        web = std::make_unique<WebRackView> (p);
        addChildComponent (*web);
    }
#endif
    const std::pair<juce::TextButton*, int> buttons[] = { { &webButton, webRack }, { &nativeButton, nativeRack }, { &panelButton, panel } };
    for (auto [b, v] : buttons)
    {
        b->setLookAndFeel (&look);
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe8e5dc));
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff141414));
        b->setClickingTogglesState (false);
        const int view = v;
        b->onClick = [this, view] { showView (view); };
        addAndMakeVisible (*b);
    }
    webButton.setEnabled (web != nullptr);
    webButton.setTooltip ("Your Continuum Rack page, played by the plugin engine");
    nativeButton.setTooltip ("The rack redrawn natively");
    panelButton.setTooltip ("The first design: a compact control panel");
    viewLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    viewLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8b8b0));
    addAndMakeVisible (viewLabel);

    setResizable (true, true);
    setResizeLimits (800, 500, 4000, 3000);
    setSize (1440, 900);
    int start = rack.editorView.load();
    if (start == webRack && ! hasWebView()) start = nativeRack;
    showView (start);
}

ContinuumRackEditor::~ContinuumRackEditor()
{
    for (auto* b : { &webButton, &nativeButton, &panelButton }) b->setLookAndFeel (nullptr);
}

bool ContinuumRackEditor::hasWebView() const noexcept { return web != nullptr; }

void ContinuumRackEditor::showView (int view)
{
    if (view == webRack && ! hasWebView()) view = nativeRack;
    view = juce::jlimit (0, 2, view);
    rack.editorView.store (view);
    if (web != nullptr) web->setVisible (view == webRack);
    native->setVisible (view == nativeRack);
    compact->setVisible (view == panel);
    webButton.setToggleState (view == webRack, juce::dontSendNotification);
    nativeButton.setToggleState (view == nativeRack, juce::dontSendNotification);
    panelButton.setToggleState (view == panel, juce::dontSendNotification);
    static const char* const names[] = { "WEB RACK  \xc2\xb7  your Continuum Rack page, played by the plugin engine",
                                         "NATIVE RACK  \xc2\xb7  the rack redrawn natively",
                                         "PANEL  \xc2\xb7  the first compact design" };
    viewLabel.setText (juce::String::fromUTF8 ("CONTINUUM RACK  \xc2\xb7  ") + juce::String::fromUTF8 (names[view]), juce::dontSendNotification);
    resized();
}

void ContinuumRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202020));
    auto bar = getLocalBounds().removeFromBottom (barHeight);
    g.setColour (juce::Colour (0xff141513));
    g.fillRect (bar);
    g.setColour (juce::Colour (0xffff9000));
    g.fillRect (bar.removeFromTop (2));
}

void ContinuumRackEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromBottom (barHeight).withTrimmedTop (2);
    if (web != nullptr) web->setBounds (area);
    native->setBounds (area);
    // the panel keeps its 1200 x 760 layout and scales to the window
    const float sx = area.getWidth() / (float) CompactPanel::designWidth, sy = area.getHeight() / (float) CompactPanel::designHeight;
    const float scale = juce::jmin (sx, sy);
    compact->setBounds (0, 0, CompactPanel::designWidth, CompactPanel::designHeight);
    compact->setTransform (juce::AffineTransform::scale (scale).translated ((area.getWidth() - CompactPanel::designWidth * scale) * 0.5f, 0.0f));
    for (auto* b : { &panelButton, &nativeButton, &webButton })
        b->setBounds (bar.removeFromRight (b == &webButton ? 110 : 120).reduced (3, 3));
    viewLabel.setBounds (bar.reduced (8, 0));
}
