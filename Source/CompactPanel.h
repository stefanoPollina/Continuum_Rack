#pragma once

// The first design: a compact, logical control panel (scenes, clock, master, echo, LFO,
// router, MIDI and eight bank rows with engine slots and step view), drawn at 1200 x 760
// and scaled to the window.

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

class RackLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
};

class CompactPanel;

// One labelled knob bound to a host parameter.
struct RackKnob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    void init (juce::Component& owner, juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text, juce::Colour);
    void place (juce::Rectangle<int>);
};

struct RackChoice
{
    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
    void init (juce::Component& owner, juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& text);
    void place (juce::Rectangle<int>);
};

class MeterBar final : public juce::Component
{
public:
    void setLevel (float v);
    void paint (juce::Graphics&) override;
private:
    float level = 0;
};

class SlotButton final : public juce::Component
{
public:
    SlotButton (CompactPanel&, int bank, int slot);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setSlot (bool on, int type);
    void flash();
    void tick();
private:
    CompactPanel& editor;
    int bank, slot;
    bool on = false;
    int type = 0;
    float glow = 0;
};

class StepStrip final : public juce::Component
{
public:
    explicit StepStrip (juce::Colour c) : colour (c) {}
    void setPattern (const crack::Pattern&);
    void setPlayhead (int step);
    void paint (juce::Graphics&) override;
private:
    juce::Colour colour;
    crack::Pattern pattern;
    int playhead = -1;
};

class BankRow final : public juce::Component
{
public:
    BankRow (CompactPanel&, int bank);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update (const crack::RackState&);
    void tick();

private:
    friend class CompactPanel;
    CompactPanel& editor;
    int bank;
    juce::Colour colour;
    RackKnob level, pan, cutoff;
    juce::TextButton mute { "SEQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach;
    juce::Label insert;
    MeterBar meter;
    std::vector<std::unique_ptr<SlotButton>> slots;
    StepStrip steps;
    std::array<uint32_t, crack::numSlots> lastHits {};
};

class CompactPanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 1200, designHeight = 760;
    explicit CompactPanel (ContinuumRackProcessor&);
    ~CompactPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    ContinuumRackProcessor& rack;
    void showSlotMenu (int bank, int slot, bool on, int type);

private:
    void timerCallback() override;
    void refreshPatch();
    void refreshSceneBox();
    void panel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour) const;

    RackLookAndFeel lnf;
    juce::Label title, patchLabel, descLabel, clockStatus, voicesLabel;
    juce::ComboBox sceneBox;
    juce::TextButton prevScene { "<" }, nextScene { ">" }, importButton { "LOAD PATCH" }, exportButton { "SAVE PATCH" };
    RackChoice seqMode, midiGate, midiTarget, lfoDest, lfoWave;
    juce::TextButton runButton { "RUN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> runAttach;
    RackKnob tempo, root, master, drive, echoMix, echoTime, echoFb, lfoRate, lfoDepth, send;
    MeterBar masterMeter;
    std::vector<std::unique_ptr<BankRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    uint32_t shownSerial = 0;
    juce::Rectangle<int> clockArea, masterArea, echoArea, lfoArea, routerArea, midiArea, banksArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompactPanel)
};
