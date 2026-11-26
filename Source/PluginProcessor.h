#pragma once

#include "RackEngine.h"
#include "RackPatch.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class ContinuumRackProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    ContinuumRackProcessor();
    ~ContinuumRackProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    // Factory scenes are the host's program list.
    int getNumPrograms() override { return juce::jmax (1, (int) scenes.size()); }
    int getCurrentProgram() override { return juce::jmax (0, currentScene.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int size) override;

    // ── for the editor (message thread) ──────────────────────────────────────────
    const std::vector<crack::Scene>& getScenes() const noexcept { return scenes; }
    juce::String getPatchName() const;
    juce::String getPatchDescription() const;
    crack::RackState getRackState() const;               // current patch with live parameter values
    bool importRackPatch (const juce::File&, juce::String& error);
    bool exportRackPatch (const juce::File&, juce::String& error) const;
    void setSlotEnabled (int bank, int slot, bool on);
    void setSlotType (int bank, int slot, int type);
    uint32_t getPatchSerial() const noexcept { return patchSerial.load(); }
    const crack::RackEngine& getEngine() const noexcept { return engine; }
    int getSceneIndex() const noexcept { return currentScene.load(); }   // -1 for an imported patch
    double getPreparedSampleRate() const noexcept { return preparedRate; }

    // ── for the web rack view ────────────────────────────────────────────────────
    // Applies a rack edited in the page. Parameters are set only where they differ from
    // `previous` (all of them when null). Returns the new patch serial.
    uint32_t applyEditorState (const crack::RackState&, int sceneIndex, const crack::RackState* previous);
    bool queueUiNote (const crack::UiNote&);
    void requestStopUiNotes() noexcept { stopUiNotesFlag.store (true); }
    void requestPanic() noexcept { panicFlag.store (true); }
    void setScopeBank (int b) noexcept { engine.scopeBank.store (b); }

    // ── for the native rack view ─────────────────────────────────────────────────
    // Edits the rack (not the host parameters) and publishes it to the audio thread.
    void editRack (const std::function<void (crack::RackState&)>& change);
    const std::vector<crack::EnginePatch>& getEnginePatches() const noexcept { return enginePatches; }
    const std::vector<crack::MosaicTableInfo>& getWaveTableInfos() const noexcept { return library.mosaicTables(); }

    // Which editor view is showing (0 = web rack, 1 = native); saved with the project.
    std::atomic<int> editorView { 0 };

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String bankParamID (int bank, const char* what);

private:
    void timerCallback() override;
    void loadState (const crack::RackState&, const juce::String& name, const juce::String& desc, int sceneIndex, bool updateParams);
    void publish (std::unique_ptr<crack::Program>);
    void foldParams (crack::RackState&) const;
    void pushParamsFromState (const crack::RackState&, const crack::RackState* previous = nullptr);

    crack::WaveLibrary library;
    std::vector<crack::Scene> scenes;
    std::vector<crack::EnginePatch> enginePatches;
    crack::RackEngine engine;

    // Program hand-over: the audio thread reads `live` at each block start.
    std::atomic<crack::Program*> live { nullptr };
    mutable std::mutex programLock;      // guards everything below on the message side
    std::vector<std::unique_ptr<crack::Program>> programs;
    struct Retired { std::unique_ptr<crack::Program> p; double when; };
    std::vector<Retired> retired;
    crack::RackState currentState;      // last state handed to buildProgram (without live params)
    juce::String patchName, patchDesc;

    std::atomic<int> currentScene { 0 };
    std::atomic<uint32_t> patchSerial { 0 };

    std::vector<crack::MidiEvent> midiEvents;
    juce::AbstractFifo uiFifo { 256 };
    std::array<crack::UiNote, 256> uiFifoData {};
    std::atomic<bool> stopUiNotesFlag { false }, panicFlag { false };
    std::vector<float> monoScratch;
    int preparedBlock = 512;
    double preparedRate = 48000;
    struct P;
    std::unique_ptr<P> p;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContinuumRackProcessor)
};
