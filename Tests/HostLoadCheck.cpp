// Loads the built VST3 bundle through JUCE's VST3 hosting code, the way a DAW would:
// scan, instantiate, list programs and parameters, play the first scene with a running
// transport, save and restore state. usage: RackHostLoadCheck "path/to/Continuum Rack.vst3"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>

namespace
{
class PlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return pos; }
    PositionInfo pos;
};

int fail (const juce::String& why)
{
    std::cerr << "FAIL: " << why << "\n";
    return 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return fail ("pass the .vst3 bundle path");

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String (argv[1]));
    if (found.isEmpty()) return fail ("no plug-in found in bundle");
    const auto& desc = *found[0];
    std::cout << "found: " << desc.name << " by " << desc.manufacturerName << ", instrument=" << (desc.isInstrument ? "yes" : "no")
              << ", outputs=" << desc.numOutputChannels << "\n";
    if (! desc.isInstrument) return fail ("not registered as an instrument");

    juce::String err;
    auto plugin = format.createInstanceFromDescription (desc, 48000, 512, err);
    if (! plugin) return fail ("could not instantiate: " + err);

    std::cout << "programs: " << plugin->getNumPrograms() << " (first: " << plugin->getProgramName (0) << ")\n";
    std::cout << "parameters: " << plugin->getParameters().size() << " (49 rack parameters plus JUCE's MIDI CC and program parameters)\n";
    if (plugin->getNumPrograms() != 48) return fail ("expected 48 programs");

    PlayHead head;
    head.pos.setIsPlaying (true);
    head.pos.setBpm (118);
    head.pos.setPpqPosition (0);
    plugin->setPlayHead (&head);
    plugin->setCurrentProgram (0);
    plugin->prepareToPlay (48000, 512);
    std::cout << "latency after prepare: " << plugin->getLatencySamples() << " samples\n";
    if (plugin->getLatencySamples() != 288) return fail ("expected 6 ms (288 samples) of look-ahead latency");
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double sum = 0, peak = 0;
    for (int b = 0; b < 400; ++b)
    {
        buf.clear();
        plugin->processBlock (buf, midi);
        head.pos.setPpqPosition (*head.pos.getPpqPosition() + 512 * 118.0 / 60.0 / 48000.0);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) return fail ("non-finite output");
                sum += v * v; peak = std::max (peak, (double) std::abs (v));
            }
    }
    const double rms = std::sqrt (sum / (400.0 * 1024));
    std::cout << "rendered 4.3 s of Squelch Dub through the VST3: rms " << rms << ", peak " << peak << "\n";
    if (rms < 1e-3) return fail ("silent");

    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    plugin->setCurrentProgram (20);
    plugin->setStateInformation (state.getData(), (int) state.getSize());
    std::cout << "state: " << state.getSize() << " bytes saved and restored, current program " << plugin->getCurrentProgram() << "\n";
    if (plugin->getCurrentProgram() != 0) return fail ("state restore did not bring back the scene");

    if (plugin->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        std::cout << "editor: " << (ed ? juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) : juce::String ("none")) << "\n";
        if (ed) plugin->editorBeingDeleted (ed.get());
    }
    plugin->releaseResources();
    std::cout << "VST3 host check passed\n";
    return 0;
}
