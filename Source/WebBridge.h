#pragma once
// Message-thread glue between the web rack page (Resources/web/plugin-bridge.js) and the
// processor. Has no WebView dependency, so it can be tested without a browser: events from
// the page go into handle(); events for the page come out through `send`.
//
// page -> plugin: rackHello, rackState, rackNote, rackStopNotes, rackRun, rackPanic, rackSave, rackScope
// plugin -> page: rackLoad, rackParams, rackStatus

#include "PluginProcessor.h"

#include <functional>

class RackWebBridge
{
public:
    explicit RackWebBridge (ContinuumRackProcessor&);

    std::function<void (const juce::Identifier&, const juce::var&)> send;
    // Called for rackSave; the default opens a native save dialog.
    std::function<void (const juce::String& name, const juce::String& json)> onSave;

    static const juce::StringArray& pageEvents();
    void handle (const juce::Identifier& event, const juce::var& payload);

    // Call regularly (the view's timer): status every call, params/patch when they change.
    void tick();

    bool pageReady() const noexcept { return ready; }
    // Re-sends the whole rack (after the view was hidden and missed updates).
    void resync() { if (ready) sendLoad(); }

private:
    void sendLoad();
    juce::var paramSnapshot() const;

    ContinuumRackProcessor& proc;
    bool ready = false;
    uint32_t seenSerial = 0;
    juce::String lastParams;
    crack::RackState lastPageState;
    bool havePageState = false;
    uint32_t lastMidiCount = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
