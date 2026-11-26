#include "PluginEditor.h"
#include "NativeRack.h"
#include "WebRackView.h"

void ContinuumRackEditor::SwitchLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (over ? 0xffffa62e : 0xffff9000).darker (down ? 0.2f : 0.0f));
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
#if JUCE_WEB_BROWSER
    web = std::make_unique<WebRackView> (p);
    addChildComponent (*web);
#endif
    switchButton.setLookAndFeel (&look);
    switchButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff141414));
    switchButton.setTooltip ("Switch between the web rack and the native rack");
    switchButton.onClick = [this] { showView (rack.editorView.load() == 0 ? 1 : 0); };
    if (web != nullptr) addAndMakeVisible (switchButton);

    setResizable (true, true);
    setResizeLimits (800, 500, 4000, 3000);
    setSize (1440, 900);
    showView (hasWebView() ? rack.editorView.load() : 1);
}

ContinuumRackEditor::~ContinuumRackEditor()
{
    switchButton.setLookAndFeel (nullptr);
}

bool ContinuumRackEditor::hasWebView() const noexcept { return web != nullptr; }

void ContinuumRackEditor::showView (int view)
{
    if (! hasWebView()) view = 1;
    rack.editorView.store (view);
    if (web != nullptr) web->setVisible (view == 0);
    native->setVisible (view == 1);
    switchButton.setButtonText (view == 0 ? "NATIVE RACK" : "WEB RACK");
    switchButton.toFront (false);
    resized();
}

void ContinuumRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202020));
}

void ContinuumRackEditor::resized()
{
    const auto area = getLocalBounds();
    if (web != nullptr) web->setBounds (area);
    native->setBounds (area);
    switchButton.setBounds (area.getRight() - 132, area.getBottom() - 34, 112, 24);
}
