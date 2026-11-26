#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff151714), rail (0xff1f221e), panelC (0xff2a2d27), ink (0xffe8e5dc), sub (0xff9a9c92), gold (0xffdeae62);
const juce::uint32 bankHex[] = { 0xffdeae62, 0xfff59d87, 0xffed9ac1, 0xffc8a5f0, 0xff87bafd, 0xff54c9e3, 0xff5fceb3, 0xffa7c474 };
const juce::uint32 engineHex[] = { 0xffc9ccd1, 0xffe9e4d6, 0xff3a3e44, 0xff1b1b1d, 0xff23304d, 0xffd9c7a3, 0xfff4f2ec, 0xff5a2030 };
juce::Colour bankColour (int b) { return juce::Colour (bankHex[b & 7]); }
juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }   // literals below hold · ↓ ↑
constexpr int W = 1200, H = 760;
} // namespace

// ───────────────────────────────────────────────────────────── look and feel
RackLookAndFeel::RackLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff10120f));
    setColour (juce::ComboBox::textColourId, juce::Colour (0xffffc46b));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3b3f37));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff20231f));
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, gold.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff33372f));
    setColour (juce::TextButton::buttonOnColourId, gold);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1a16));
}

void RackLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.withSizeKeepingCentre (d, d);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (c.getCentreX(), c.getCentreY(), d * 0.5f - 1, d * 0.5f - 1, 0, start, end, true);
    g.setColour (juce::Colour (0xff0d0e0c));
    g.strokePath (track, juce::PathStrokeType (2.5f));
    juce::Path val;
    val.addCentredArc (c.getCentreX(), c.getCentreY(), d * 0.5f - 1, d * 0.5f - 1, 0, start, angle, true);
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (2.5f));

    const auto cap = c.reduced (d * 0.17f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e46), cap.getX(), cap.getY(), juce::Colour (0xff181a16), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (0xff060706));
    g.drawEllipse (cap, 1.0f);
    juce::Path tick;
    tick.addRoundedRectangle (-1.2f, -cap.getHeight() * 0.5f + 1.5f, 2.4f, cap.getHeight() * 0.32f, 1.0f);
    g.setColour (ink);
    g.fillPath (tick, juce::AffineTransform::rotation (angle).translated (cap.getCentreX(), cap.getCentreY()));
}

void RackLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : base;
    if (over) c = c.brighter (0.12f);
    if (down) c = c.darker (0.2f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (0xff0a0b09));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

juce::Font RackLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (12.0f, h * 0.55f), juce::Font::bold));
}

void RackLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 2.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 11, ay = h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2, ax + 3.5f, ay - 2, ax, ay + 2.5f);
    g.setColour (sub);
    g.fillPath (arrow);
}

juce::Font RackLookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }
juce::Font RackLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

// ───────────────────────────────────────────────────────────── widgets
void RackKnob::init (juce::Component& owner, juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text, juce::Colour accent)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setPopupDisplayEnabled (true, true, &owner);
    owner.addAndMakeVisible (slider);
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, sub);
    label.setInterceptsMouseClicks (false, false);
    owner.addAndMakeVisible (label);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    slider.setDoubleClickReturnValue (true, (double) state.getParameterRange (id).convertFrom0to1 (state.getParameter (id)->getDefaultValue()));
}

void RackKnob::place (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromBottom (12));
    slider.setBounds (r);
}

void RackChoice::init (juce::Component& owner, juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        box.addItemList (c->choices, 1);
    owner.addAndMakeVisible (box);
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, sub);
    owner.addAndMakeVisible (label);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box);
}

void RackChoice::place (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromTop (12));
    box.setBounds (r.removeFromTop (22));
}

void MeterBar::setLevel (float v)
{
    v = juce::jlimit (0.0f, 1.5f, v);
    if (std::abs (v - level) > 0.002f) { level = v; repaint(); }
}

void MeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b0c0a));
    g.fillRoundedRectangle (r, 2);
    const int segs = 12;
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float lit = juce::jmap (db, -48.0f, 0.0f, 0.0f, (float) segs);
    const float sh = r.getHeight() / segs;
    for (int i = 0; i < segs; ++i)
    {
        const auto seg = juce::Rectangle<float> (r.getX() + 1, r.getBottom() - (i + 1) * sh + 1, r.getWidth() - 2, sh - 1.5f);
        const auto c = i >= 10 ? juce::Colour (0xffff3b30) : i >= 8 ? juce::Colour (0xffffd23c) : juce::Colour (0xff3cff6e);
        g.setColour (i < lit ? c : c.withAlpha (0.1f));
        g.fillRect (seg);
    }
}

SlotButton::SlotButton (ContinuumRackEditor& e, int b, int s) : editor (e), bank (b), slot (s) {}

void SlotButton::setSlot (bool isOn, int t)
{
    if (isOn != on || t != type) { on = isOn; type = t; repaint(); }
}

void SlotButton::flash() { glow = 1.0f; repaint(); }

void SlotButton::tick()
{
    if (glow > 0) { glow = juce::jmax (0.0f, glow - 0.18f); repaint(); }
}

void SlotButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const auto skin = juce::Colour (engineHex[type & 7]);
    g.setColour (on ? skin : skin.withAlpha (0.14f));
    g.fillRoundedRectangle (r, 3);
    g.setColour (on ? juce::Colour (0xff0a0a09) : juce::Colour (0xff3a3d36));
    g.drawRoundedRectangle (r, 3, 1);
    const bool dark = skin.getPerceivedBrightness() < 0.5f;
    g.setColour (on ? (dark ? ink : juce::Colour (0xff1c1c1a)) : sub.withAlpha (0.6f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (crack::engineShortName (type), r.withTrimmedBottom (8), juce::Justification::centred);
    g.setFont (juce::FontOptions (8.0f));
    g.drawText (juce::String (slot + 1), r.removeFromBottom (11), juce::Justification::centred);
    const auto led = juce::Rectangle<float> (r.getRight() - 8, r.getY() + 3, 5, 5);
    g.setColour (juce::Colour (0xffff9000).withAlpha (on ? 0.18f + 0.82f * glow : 0.05f));
    g.fillEllipse (led);
}

void SlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (e.mods.isPopupMenu()) editor.showSlotMenu (bank, slot, on, type);
    else editor.rack.setSlotEnabled (bank, slot, ! on);
}

void StepStrip::setPattern (const crack::Pattern& p) { pattern = p; repaint(); }

void StepStrip::setPlayhead (int s)
{
    if (s != playhead) { playhead = s; repaint(); }
}

void StepStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float cw = r.getWidth() / crack::maxSteps;
    for (int i = 0; i < crack::maxSteps; ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + i * cw, r.getY(), cw, r.getHeight()).reduced (0.7f, 2);
        const bool inLen = i < pattern.length;
        const auto& st = pattern.steps[(size_t) i];
        juce::Colour c = juce::Colour (0xff10120f);
        if (inLen && i % 4 == 0) c = juce::Colour (0xff1b1e19);
        if (! inLen) c = juce::Colour (0xff0c0d0b);
        g.setColour (c);
        g.fillRect (cell);
        if (inLen && st.on)
        {
            const float v = juce::jlimit (0.25f, 1.0f, st.velocity / 127.0f);
            const float hgt = cell.getHeight() * v;
            g.setColour (pattern.mute ? sub.withAlpha (0.4f) : colour);
            g.fillRect (cell.withTop (cell.getBottom() - hgt));
        }
        if (i == playhead)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawRect (cell, 1.2f);
        }
    }
}

BankRow::BankRow (ContinuumRackEditor& e, int b) : editor (e), bank (b), colour (bankColour (b)), steps (bankColour (b))
{
    auto& st = e.rack.parameters;
    level.init (*this, st, ContinuumRackProcessor::bankParamID (b, "level"), "LEVEL", colour);
    pan.init (*this, st, ContinuumRackProcessor::bankParamID (b, "pan"), "PAN", colour);
    cutoff.init (*this, st, ContinuumRackProcessor::bankParamID (b, "cutoff"), "FILTER", colour);
    mute.setClickingTogglesState (true);
    mute.setTooltip ("Mute this bank's sequencer lane");
    addAndMakeVisible (mute);
    muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ContinuumRackProcessor::bankParamID (b, "mute"), mute);
    insert.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    insert.setColour (juce::Label::textColourId, juce::Colour (0xffffc46b));
    insert.setColour (juce::Label::backgroundColourId, juce::Colour (0xff10120f));
    insert.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (insert);
    addAndMakeVisible (meter);
    for (int s = 0; s < crack::numSlots; ++s)
    {
        slots.push_back (std::make_unique<SlotButton> (e, b, s));
        addAndMakeVisible (*slots.back());
    }
    addAndMakeVisible (steps);
}

void BankRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2, 2);
    g.setColour (panelC);
    g.fillRoundedRectangle (r, 4);
    g.setColour (colour);
    g.fillRoundedRectangle (r.removeFromLeft (5), 2);
    g.setColour (ink);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BANK " + juce::String (bank + 1), juce::Rectangle<int> (14, 6, 70, 18), juce::Justification::left);
    g.setColour (sub);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("STRIP", juce::Rectangle<int> (14, 24, 70, 12), juce::Justification::left);
}

void BankRow::resized()
{
    auto r = getLocalBounds().reduced (4, 4);
    r.removeFromLeft (76);
    level.place (r.removeFromLeft (50));
    pan.place (r.removeFromLeft (50));
    cutoff.place (r.removeFromLeft (50));
    r.removeFromLeft (6);
    auto col = r.removeFromLeft (58);
    insert.setBounds (col.removeFromTop (col.getHeight() / 2).reduced (0, 3));
    mute.setBounds (col.reduced (0, 3));
    r.removeFromLeft (6);
    meter.setBounds (r.removeFromLeft (9).reduced (0, 3));
    r.removeFromLeft (8);
    auto slotArea = r.removeFromLeft (8 * 54);
    for (auto& s : slots) s->setBounds (slotArea.removeFromLeft (54));
    r.removeFromLeft (10);
    steps.setBounds (r.reduced (0, 6));
}

void BankRow::update (const crack::RackState& s)
{
    const auto& b = s.banks[(size_t) bank];
    insert.setText (crack::insertLabel (b.insert), juce::dontSendNotification);
    for (int i = 0; i < crack::numSlots; ++i)
        slots[(size_t) i]->setSlot (b.slots[(size_t) i].on, b.slots[(size_t) i].type);
    steps.setPattern (s.patterns[(size_t) bank]);
}

void BankRow::tick()
{
    const auto& eng = editor.rack.getEngine();
    meter.setLevel (eng.bankPeak[(size_t) bank].load (std::memory_order_relaxed) / 0.42f);
    steps.setPlayhead (eng.playStep[(size_t) bank].load (std::memory_order_relaxed));
    for (int i = 0; i < crack::numSlots; ++i)
    {
        const auto h = eng.slotHits[(size_t) bank][(size_t) i].load (std::memory_order_relaxed);
        if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; slots[(size_t) i]->flash(); }
        slots[(size_t) i]->tick();
    }
}

// ───────────────────────────────────────────────────────────── editor
ContinuumRackEditor::ContinuumRackEditor (ContinuumRackProcessor& p) : AudioProcessorEditor (p), rack (p)
{
    setLookAndFeel (&lnf);
    auto& st = p.parameters;

    title.setText ("CONTINUUM  RACK", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, gold);
    addAndMakeVisible (title);

    patchLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    patchLabel.setColour (juce::Label::textColourId, ink);
    descLabel.setFont (juce::FontOptions (11.5f));
    descLabel.setColour (juce::Label::textColourId, sub);
    descLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (patchLabel);
    addAndMakeVisible (descLabel);

    refreshSceneBox();
    sceneBox.setTextWhenNothingSelected ("Imported patch");
    sceneBox.onChange = [this] {
        const int idx = sceneBox.getSelectedId() - 1;
        if (idx >= 0 && idx != rack.getSceneIndex()) rack.setCurrentProgram (idx);
    };
    addAndMakeVisible (sceneBox);
    prevScene.onClick = [this] {
        const int n = (int) rack.getScenes().size();
        if (n) rack.setCurrentProgram ((juce::jmax (0, rack.getSceneIndex()) + n - 1) % n);
    };
    nextScene.onClick = [this] {
        const int n = (int) rack.getScenes().size();
        if (n) rack.setCurrentProgram ((rack.getSceneIndex() + 1) % n);
    };
    addAndMakeVisible (prevScene);
    addAndMakeVisible (nextScene);

    importButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Load a Continuum Rack patch", juce::File(), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (! rack.importRackPatch (f, err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load patch", err);
        });
    };
    exportButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save rack patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                           .getChildFile ("Continuum_Synth_8x8_Patch.json"), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (! rack.exportRackPatch (f.withFileExtension ("json"), err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save patch", err);
        });
    };
    importButton.setTooltip (utf8 ("Load a rack patch saved from the Continuum Rack web app (CORE ↓ SAVE)"));
    exportButton.setTooltip (utf8 ("Save this rack as a patch the web app can load (CORE ↑ LOAD)"));
    addAndMakeVisible (importButton);
    addAndMakeVisible (exportButton);

    seqMode.init (*this, st, "seqMode", "CLOCK");
    runButton.setClickingTogglesState (true);
    addAndMakeVisible (runButton);
    runAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, "run", runButton);
    clockStatus.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    clockStatus.setColour (juce::Label::textColourId, juce::Colour (0xff3cff6e));
    addAndMakeVisible (clockStatus);
    tempo.init (*this, st, "tempo", "TEMPO", gold);
    root.init (*this, st, "root", "ROOT", gold);
    master.init (*this, st, "master", "LEVEL", juce::Colour (0xffff9000));
    drive.init (*this, st, "drive", "DRIVE", juce::Colour (0xffff9000));
    echoMix.init (*this, st, "delayMix", "MIX", juce::Colour (0xff54c9e3));
    echoTime.init (*this, st, "delayTime", "TIME", juce::Colour (0xff54c9e3));
    echoFb.init (*this, st, "feedback", "FEEDBK", juce::Colour (0xff54c9e3));
    lfoRate.init (*this, st, "lfoRate", "RATE", juce::Colour (0xff5fceb3));
    lfoDepth.init (*this, st, "lfoDepth", "DEPTH", juce::Colour (0xff5fceb3));
    lfoDest.init (*this, st, "lfoDest", "DEST");
    lfoWave.init (*this, st, "lfoWave", "WAVE");
    send.init (*this, st, "sendLevel", "SEND", juce::Colour (0xffc8a5f0));
    midiGate.init (*this, st, "midiGate", "NOTES");
    midiTarget.init (*this, st, "midiTarget", "TARGET");
    voicesLabel.setFont (juce::FontOptions (10.0f));
    voicesLabel.setColour (juce::Label::textColourId, sub);
    addAndMakeVisible (voicesLabel);
    addAndMakeVisible (masterMeter);

    for (int b = 0; b < crack::numBanks; ++b)
    {
        rows.push_back (std::make_unique<BankRow> (*this, b));
        addAndMakeVisible (*rows.back());
    }

    setSize (W, H);
    refreshPatch();
    startTimerHz (30);
}

ContinuumRackEditor::~ContinuumRackEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ContinuumRackEditor::refreshSceneBox()
{
    sceneBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& s : rack.getScenes()) sceneBox.addItem (s.name, id++);
}

void ContinuumRackEditor::refreshPatch()
{
    shownSerial = rack.getPatchSerial();
    const auto s = rack.getRackState();
    const int idx = rack.getSceneIndex();
    sceneBox.setSelectedId (idx >= 0 ? idx + 1 : 0, juce::dontSendNotification);
    juce::String name = rack.getPatchName();
    if (idx >= 0 && idx < (int) rack.getScenes().size())
        name = rack.getScenes()[(size_t) idx].name + utf8 ("   ·   ") + rack.getScenes()[(size_t) idx].tag;
    patchLabel.setText (name, juce::dontSendNotification);
    descLabel.setText (rack.getPatchDescription(), juce::dontSendNotification);
    for (auto& r : rows) r->update (s);
}

void ContinuumRackEditor::timerCallback()
{
    if (rack.getPatchSerial() != shownSerial) refreshPatch();
    const auto& eng = rack.getEngine();
    for (auto& r : rows) r->tick();
    masterMeter.setLevel (eng.masterPeak.load (std::memory_order_relaxed));
    const int mode = (int) rack.parameters.getRawParameterValue ("seqMode")->load();
    const bool host = eng.usingHostClock.load();
    clockStatus.setText (mode == 2 ? juce::String ("SEQ OFF") : host ? juce::String ("HOST SYNC") : (mode == 0 ? utf8 ("NO HOST CLOCK · RUN") : juce::String ("INTERNAL")), juce::dontSendNotification);
    clockStatus.setColour (juce::Label::textColourId, mode == 2 ? sub : host ? juce::Colour (0xff3cff6e) : juce::Colour (0xffffd23c));
    voicesLabel.setText (juce::String (eng.activeVoices.load()) + " / " + juce::String (crack::RackEngine::maxVoices) + " voices", juce::dontSendNotification);
}

void ContinuumRackEditor::showSlotMenu (int bank, int slot, bool on, int type)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Bank " + juce::String (bank + 1) + utf8 (" · Engine ") + juce::String (slot + 1));
    m.addItem (1, on ? "Disable engine" : "Enable engine");
    m.addSeparator();
    for (int t = 0; t < 8; ++t) m.addItem (10 + t, crack::engineTitle (t), true, t == type);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rows[(size_t) bank]->slots[(size_t) slot].get()),
                     [this, bank, slot, on] (int r) {
                         if (r == 1) rack.setSlotEnabled (bank, slot, ! on);
                         else if (r >= 10) rack.setSlotType (bank, slot, r - 10);
                     });
}

void ContinuumRackEditor::panel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour c) const
{
    auto f = r.toFloat().reduced (2);
    g.setColour (panelC);
    g.fillRoundedRectangle (f, 4);
    g.setColour (c);
    g.fillRect (f.removeFromTop (3));
    g.setColour (ink);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (t, r.reduced (10, 6).removeFromTop (14), juce::Justification::left);
    for (auto p : { juce::Point<float> (r.getX() + 6.0f, r.getBottom() - 8.0f), juce::Point<float> (r.getRight() - 9.0f, r.getBottom() - 8.0f) })
    {
        g.setColour (juce::Colour (0xff141513));
        g.fillEllipse (p.x, p.y, 4, 4);
    }
}

void ContinuumRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (rail);
    g.fillRect (0, 0, getWidth(), 58);
    g.setColour (juce::Colour (0xff0b0c0a));
    g.drawHorizontalLine (58, 0, (float) getWidth());
    panel (g, clockArea, utf8 ("CORE · CLOCK"), gold);
    panel (g, masterArea, "MASTER", juce::Colour (0xffff9000));
    panel (g, echoArea, "ECHO", juce::Colour (0xff54c9e3));
    panel (g, lfoArea, "LFO", juce::Colour (0xff5fceb3));
    panel (g, routerArea, "ROUTER", juce::Colour (0xffc8a5f0));
    panel (g, midiArea, "MIDI IN", juce::Colour (0xfff59d87));
    g.setColour (sub);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("Click an engine to switch it on or off; right-click to change its type. Patches load and save in the web app's format.",
                getLocalBounds().removeFromBottom (20).reduced (12, 0), juce::Justification::centredLeft);
}

void ContinuumRackEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (58).reduced (12, 8);
    title.setBounds (top.removeFromLeft (190));
    exportButton.setBounds (top.removeFromRight (96).reduced (0, 6));
    top.removeFromRight (6);
    importButton.setBounds (top.removeFromRight (96).reduced (0, 6));
    top.removeFromRight (14);
    nextScene.setBounds (top.removeFromRight (26).reduced (0, 6));
    sceneBox.setBounds (top.removeFromRight (200).reduced (2, 6));
    prevScene.setBounds (top.removeFromRight (26).reduced (0, 6));
    top.removeFromRight (12);
    patchLabel.setBounds (top.removeFromTop (top.getHeight() / 2));
    descLabel.setBounds (top);

    r.removeFromTop (6);
    auto mods = r.removeFromTop (118).reduced (8, 0);
    clockArea = mods.removeFromLeft (300);
    masterArea = mods.removeFromLeft (150);
    echoArea = mods.removeFromLeft (190);
    lfoArea = mods.removeFromLeft (250);
    routerArea = mods.removeFromLeft (80);
    midiArea = mods;

    auto inner = [] (juce::Rectangle<int> a) { return a.reduced (10, 6).withTrimmedTop (18); };
    {
        auto a = inner (clockArea);
        auto left = a.removeFromLeft (118);
        seqMode.place (left.removeFromTop (36));
        left.removeFromTop (4);
        runButton.setBounds (left.removeFromTop (24).removeFromLeft (56));
        clockStatus.setBounds (left.removeFromTop (18));
        a.removeFromLeft (8);
        tempo.place (a.removeFromLeft (70).withTrimmedBottom (8));
        root.place (a.removeFromLeft (70).withTrimmedBottom (8));
    }
    {
        auto a = inner (masterArea);
        master.place (a.removeFromLeft (60).withTrimmedBottom (8));
        drive.place (a.removeFromLeft (60).withTrimmedBottom (8));
    }
    {
        auto a = inner (echoArea);
        echoMix.place (a.removeFromLeft (56).withTrimmedBottom (8));
        echoTime.place (a.removeFromLeft (56).withTrimmedBottom (8));
        echoFb.place (a.removeFromLeft (56).withTrimmedBottom (8));
    }
    {
        auto a = inner (lfoArea);
        lfoRate.place (a.removeFromLeft (60).withTrimmedBottom (8));
        lfoDepth.place (a.removeFromLeft (60).withTrimmedBottom (8));
        a.removeFromLeft (6);
        lfoDest.place (a.removeFromTop (36));
        a.removeFromTop (4);
        lfoWave.place (a.removeFromTop (36));
    }
    {
        auto a = inner (routerArea);
        send.place (a.withTrimmedBottom (8));
    }
    {
        auto a = inner (midiArea);
        masterMeter.setBounds (a.removeFromRight (10).withTrimmedBottom (8));
        a.removeFromRight (8);
        auto c1 = a.removeFromLeft (a.getWidth() / 2 - 3);
        midiGate.place (c1.removeFromTop (36));
        a.removeFromLeft (6);
        midiTarget.place (a.removeFromTop (36));
        voicesLabel.setBounds (c1.removeFromTop (30).withTrimmedTop (10));
    }

    r.removeFromTop (6);
    r.removeFromBottom (20);
    auto banks = r.reduced (8, 0);
    const int rowH = banks.getHeight() / crack::numBanks;
    for (auto& row : rows) row->setBounds (banks.removeFromTop (rowH));
}
