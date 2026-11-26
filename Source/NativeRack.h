#pragma once
// The native rack: the Continuum Rack's modules redrawn in JUCE, on the same 1740 x 2660
// layout as the web page (same module sizes, skins, knob colours and cables), bound to the
// plugin's parameters and rack state. No WebView involved.

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

class NativeRackCanvas final : public juce::Component, private juce::Timer
{
public:
    static constexpr int rackWidth = 1740, rackHeight = 2660;

    explicit NativeRackCanvas (ContinuumRackProcessor&);
    ~NativeRackCanvas() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // for tests
    int numControls() const noexcept { return (int) ctls.size(); }
    int numModules() const noexcept { return (int) modules.size(); }
    juce::String getStatus() const { return status; }
    bool clickControl (const juce::String& id, bool popup = false);   // simulates a click on a control by id
    bool dragKnob (const juce::String& id, float delta01);
    bool tapStep (int bank, int step);
    bool connect (const juce::String& fromJack, const juce::String& toJack);   // as dropping a cable

    struct Module
    {
        juce::Rectangle<int> r;
        juce::String skin, title, sub;
        juce::Colour bank;          // transparent when the module has no bank colour
        bool stripe = false, focused = false;
        int kind = 0;               // see the .cpp
    };
    struct Ctl
    {
        enum Kind { knob, slider, select, button, ledButton, led, jack, lcd, list, keys, stepGrid, scope, octo, vu, targetToggle } kind;
        juce::String id, label;
        juce::Rectangle<float> r;
        juce::String knobColour;    // black, grey, white, red, teal, orange, yellow, blue, cyan, bank
        juce::Colour accent, ledColour;
        std::function<float()> get01;
        std::function<void (float)> set01;
        std::function<juce::String()> text;
        std::function<void (int)> step;          // select arrows / list moves
        std::function<void()> click;
        std::function<void (const juce::MouseEvent&)> popup;
        std::function<bool()> lit;
        std::function<void()> begin, end;
        float def01 = 0.5f;
        int bank = -1, index = -1;
        juce::String jackId;
        bool small = false;
    };

private:
    void timerCallback() override;
    void refreshState();
    void rebuild();
    void say (const juce::String&);

    // control builders
    Ctl& add (Ctl c);
    Ctl& paramKnob (const juce::String& pid, float cx, float cy, float radius, const juce::String& colour, const juce::String& label, juce::Colour bank = {});
    Ctl& editKnob (int b, int s, float crack::EngineEdit::* field, float lo, float hi, float stp, float cx, float cy, float radius,
                   const juce::String& colour, const juce::String& label);
    Ctl& select (const juce::String& id, juce::Rectangle<float>, const juce::String& label, std::function<juce::String()> text, std::function<void (int)> step);
    Ctl& button (const juce::String& id, juce::Rectangle<float>, const juce::String& text, const juce::String& label, std::function<void()> click, juce::Colour accent = {});
    Ctl& ledButton (const juce::String& id, juce::Rectangle<float>, const juce::String& label, juce::Colour led, std::function<bool()> lit, std::function<void()> click);

    void buildTopRow();
    void buildSeq (juce::Rectangle<int>);
    void buildEditor (juce::Rectangle<int>);
    void buildStrip (int bank, juce::Rectangle<int>);
    void buildEngine (int bank, int slot, juce::Rectangle<int>);
    void buildLibraryRow();

    // painting
    void paintModule (juce::Graphics&, const Module&);
    void paintCtl (juce::Graphics&, const Ctl&);
    void paintKnob (juce::Graphics&, const Ctl&);
    void paintStepGrid (juce::Graphics&, const Ctl&);
    void paintScope (juce::Graphics&, juce::Rectangle<float>, const float* data, int n, juce::Colour, float gain);
    void paintKeys (juce::Graphics&, const Ctl&);
    void paintCables (juce::Graphics&);
    juce::Point<float> jackPos (const juce::String& id) const;

    // interaction
    int hit (juce::Point<float>) const;
    void setFocus (int b, int s);
    void playNote (int midi, double duration, int bank = -1, int slot = -1);
    int keyAt (juce::Point<float>, const Ctl&) const;
    void stepAt (const Ctl&, juce::Point<float>, int& lane, int& step) const;
    void editSlot (int b, int s, const std::function<void (crack::Slot&)>&);
    void setParam (const juce::String& pid, float value);
    float paramValue (const juce::String& pid) const;

    ContinuumRackProcessor& proc;
    crack::RackState rs;
    uint32_t serial = 0;
    std::vector<Module> modules;
    std::vector<Ctl> ctls;
    juce::String status;

    // view state, as in the web app's `ui`
    int focusBank = 0, focusSlot = 0, editBank = 0, editStep = 0, octoBank = 0, scenePick = 0, sceneTop = 0;
    int libPick = 0, libTop = 0, libBank = 0, libSlot = 0, octave = 3;
    bool octoFollow = true;

    // interaction state
    int dragCtl = -1, hoverCtl = -1;
    float dragStart01 = 0;
    juce::Point<float> dragFrom;
    juce::String cableFrom;           // jack id while dragging a cable
    juce::Point<float> cableTo;
    int heldKey = -1;

    // live data
    std::array<uint32_t, 64> lastHits {};
    std::array<double, 64> hitTime {};
    std::array<float, 9> vu {};
    std::array<int, 8> lastStep {};
    std::vector<float> masterWave, octoWave;
    uint32_t lastMidiCount = 0;
    double midiHitTime = -1e9;
    int midiNote = -1;
    bool lastRun = false;
    std::vector<float> lastVal;
    std::vector<juce::String> lastText;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeRackCanvas)
};

// Scrollable, zoomable host for the canvas (Ctrl/Cmd + wheel zooms, opens fitted to width).
class NativeRackView final : public juce::Component
{
public:
    explicit NativeRackView (ContinuumRackProcessor&);
    void resized() override;
    void setZoom (float z, bool fromUser = false);
    float getZoom() const noexcept { return zoom; }
    NativeRackCanvas& getCanvas() noexcept { return canvas; }

private:
    NativeRackCanvas canvas;
    juce::Component holder;
    juce::Viewport viewport;
    float zoom = 0;
    bool userZoomed = false;   // until the user zooms, the rack is fitted to the window width
};
