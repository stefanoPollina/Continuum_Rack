// JUCE-level tests: factory scenes, host state, web-app patch files, transport and MIDI.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebBridge.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <stdexcept>

#ifndef RACK_FIXTURE_DIR
 #define RACK_FIXTURE_DIR ""
#endif

namespace
{
int passes = 0, failures = 0;
void check (bool ok, const juce::String& what)
{
    if (ok) ++passes;
    else { ++failures; std::cerr << "FAIL: " << what << "\n"; }
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    PositionInfo position;
};

void setParam (ContinuumRackProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.parameters.getParameter (id);
    if (prm == nullptr) throw std::runtime_error (("missing parameter " + id).toStdString());
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
float getParam (ContinuumRackProcessor& p, const juce::String& id) { return p.parameters.getRawParameterValue (id)->load(); }

struct Render { double peak = 0, rms = 0; bool finite = true; double seconds = 0; };

Render render (ContinuumRackProcessor& p, TestPlayHead* head, double seconds, int block = 512, double sr = 48000,
               std::function<void (int, juce::MidiBuffer&)> midiFn = {})
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Render r;
    double sum = 0; int64_t count = 0;
    const int blocks = (int) std::ceil (seconds * sr / block);
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (midiFn) midiFn (b, midi);
        p.processBlock (buf, midi);
        if (head && head->position.getIsPlaying())
            head->position.setPpqPosition (*head->position.getPpqPosition() + block * *head->position.getBpm() / 60.0 / sr);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) { r.finite = false; return r; }
                r.peak = std::max (r.peak, (double) std::abs (v));
                sum += (double) v * v; ++count;
            }
    }
    r.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    r.rms = std::sqrt (sum / std::max<int64_t> (1, count));
    return r;
}

void playingAt (TestPlayHead& h, double bpm, double ppq = 0)
{
    h.position.setIsPlaying (true);
    h.position.setBpm (bpm);
    h.position.setPpqPosition (ppq);
}

void testScenes()
{
    ContinuumRackProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (48000, 512);
    check (p.getNumPrograms() == 48, "48 factory scenes are host programs (got " + juce::String (p.getNumPrograms()) + ")");
    check (p.getProgramName (0) == "Squelch Dub", "first program is Squelch Dub");

    double worstRealtime = 0, totalAudio = 0, totalCpu = 0, hottest = 0;
    juce::String worstName, hottestName;
    int over = 0;
    for (int i = 0; i < p.getNumPrograms(); ++i)
    {
        p.setCurrentProgram (i);
        const auto s = p.getRackState();
        check (std::abs (getParam (p, "tempo") - s.tempo) < 0.5f, p.getProgramName (i) + ": tempo parameter follows the scene");
        playingAt (head, s.tempo);
        const double bars = 4, secs = bars * 4 * 60.0 / s.tempo;
        const auto r = render (p, &head, secs);
        check (r.finite, p.getProgramName (i) + ": finite output");
        check (r.rms > 1e-3, p.getProgramName (i) + ": audible (rms " + juce::String (r.rms, 5) + ")");
        check (r.peak <= 1.0, p.getProgramName (i) + ": output clip holds 0 dBFS (" + juce::String (r.peak, 3) + ")");
        const double rt = r.seconds / secs;
        totalAudio += secs; totalCpu += r.seconds;
        if (rt > worstRealtime) { worstRealtime = rt; worstName = p.getProgramName (i); }
        // and unclipped, to see how hot the engine itself runs
        setParam (p, "outputClip", 0);
        playingAt (head, s.tempo);
        const auto raw = render (p, &head, secs);
        setParam (p, "outputClip", 1);
        check (raw.finite && raw.peak < 4.0, p.getProgramName (i) + ": unclipped peak stays bounded (" + juce::String (raw.peak, 3) + ")");
        if (raw.peak > 1.0) ++over;
        if (raw.peak > hottest) { hottest = raw.peak; hottestName = p.getProgramName (i); }
        head.position.setIsPlaying (false);
        render (p, &head, 0.05);
    }
    std::cout << "scenes: " << over << " of " << p.getNumPrograms() << " exceed 0 dBFS before the output clip; hottest "
              << juce::String (juce::Decibels::gainToDecibels (hottest), 1) << " dBFS (" << hottestName << ")\n";
    std::cout << "scenes: " << juce::String (totalCpu / totalAudio * 100, 1) << "% of one core on average, worst "
              << juce::String (worstRealtime * 100, 1) << "% (" << worstName << ")\n";
    check (worstRealtime < 0.5, "every scene renders well inside real time");
}

void testStateRoundTrip()
{
    ContinuumRackProcessor a;
    a.setCurrentProgram (11);
    setParam (a, "master", 61); setParam (a, "delayTime", 0.41f); setParam (a, "lfoWave", 2);
    setParam (a, ContinuumRackProcessor::bankParamID (3, "pan"), 12);
    setParam (a, ContinuumRackProcessor::bankParamID (5, "mute"), 1);
    setParam (a, "seqMode", 1); setParam (a, "run", 1); setParam (a, "midiTarget", 4);
    a.setSlotEnabled (7, 7, true);
    a.setSlotType (7, 7, 5);

    juce::MemoryBlock saved;
    a.getStateInformation (saved);
    ContinuumRackProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());

    for (auto* prm : a.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            check (std::abs (getParam (a, r->paramID) - getParam (b, r->paramID)) < 1e-4f, "parameter " + r->paramID + " restored");
    check (crack::writeRackPatch (a.getRackState()) == crack::writeRackPatch (b.getRackState()), "rack patch restored exactly");
    check (b.getRackState().banks[7].slots[7].on && b.getRackState().banks[7].slots[7].type == 5, "edited slot restored");
    check (b.getSceneIndex() == 11 && b.getPatchName() == a.getPatchName(), "scene index and name restored");

    // identical renders
    a.prepareToPlay (44100, 256); b.prepareToPlay (44100, 256);
    juce::AudioBuffer<float> x (2, 256), y (2, 256);
    juce::MidiBuffer m;
    double diff = 0;
    for (int i = 0; i < 400; ++i)
    {
        a.processBlock (x, m); b.processBlock (y, m);
        for (int c = 0; c < 2; ++c) for (int s = 0; s < 256; ++s) diff = std::max (diff, (double) std::abs (x.getSample (c, s) - y.getSample (c, s)));
    }
    check (diff < 1e-6, "restored processor renders identically (max diff " + juce::String (diff) + ")");

    // malformed state is ignored
    const auto before = crack::writeRackPatch (b.getRackState());
    const char junk[] = "definitely not a plugin state";
    b.setStateInformation (junk, (int) sizeof junk);
    juce::MemoryBlock truncated (saved.getData(), saved.getSize() / 3);
    b.setStateInformation (truncated.getData(), (int) truncated.getSize());
    check (crack::writeRackPatch (b.getRackState()) == before, "malformed or truncated state leaves the rack unchanged");
}

void testWebPatches()
{
    // The web app's own example patch file.
    const auto fixture = juce::File (RACK_FIXTURE_DIR).getChildFile ("example-two-bank-send.json");
    check (fixture.existsAsFile(), "fixture exists: " + fixture.getFullPathName());
    ContinuumRackProcessor p;
    juce::String err;
    check (p.importRackPatch (fixture, err), "web example patch imports: " + err);
    const auto s = p.getRackState();
    check (s.cables.size() == 2 && s.cables[0].from == "bo:0" && s.cables[0].to == "bi:2", "send cable bank 1 -> bank 3 read");
    check (s.cables[1].from == "lfo" && s.cables[1].to == "bm:2", "LFO -> bank 3 MOD cable read");
    check (s.banks[0].slots[0].on && s.banks[2].slots[0].on, "enabled engines read");
    check (p.getSceneIndex() == -1 && p.getCurrentProgram() == 0, "imported patch is not a factory program");

    // Export then import gives the same rack.
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("continuum-rack-roundtrip.json");
    check (p.exportRackPatch (tmp, err), "export writes a file");
    ContinuumRackProcessor q;
    check (q.importRackPatch (tmp, err), "exported patch re-imports");
    check (crack::writeRackPatch (q.getRackState()) == crack::writeRackPatch (s), "export/import round trip is exact");
    const auto txt = tmp.loadFileAsString();
    check (txt.contains ("\"format\": \"continuum-synth-p01\"") || txt.contains ("\"format\":\"continuum-synth-p01\""), "export uses the web app's format id");
    tmp.deleteFile();

    // Bad files are refused.
    crack::RackState dummy;
    check (! crack::readRackPatch ("{\"format\":\"something-else\",\"state\":{}}", dummy, err), "wrong format id refused");
    check (! crack::readRackPatch ("{\"format\":\"continuum-synth-p01\",\"state\":{\"banks\":[]}}", dummy, err), "patch without 8 banks refused");
    check (! crack::readRackPatch ("{nope", dummy, err), "invalid JSON refused");

    // Every factory scene survives an export/import cycle.
    bool allSame = true;
    for (const auto& sc : p.getScenes())
    {
        crack::RackState back;
        if (! crack::readRackPatch (crack::writeRackPatch (sc.state), back, err) || crack::writeRackPatch (back) != crack::writeRackPatch (sc.state))
            allSame = false;
    }
    check (allSame, "all factory scenes round-trip through the patch format");
}

void testTransportAndMidi()
{
    ContinuumRackProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (48000, 480);
    p.setCurrentProgram (0);

    head.position.setIsPlaying (false);
    head.position.setBpm (118);
    head.position.setPpqPosition (0);
    auto stopped = render (p, &head, 1.0);
    check (stopped.peak < 1e-4, "host stopped: sequencer silent");

    playingAt (head, 118);
    auto playing = render (p, &head, 2.0);
    check (playing.rms > 1e-3, "host playing: sequencer runs");
    check (p.getEngine().usingHostClock.load(), "engine reports host sync");

    setParam (p, "seqMode", 2);
    render (p, &head, 3.0);   // let tails die
    auto off = render (p, &head, 0.5);
    check (off.peak < 1e-3, "clock off: silent (peak " + juce::String (off.peak) + ")");

    auto note = render (p, &head, 0.5, 480, 48000, [] (int b, juce::MidiBuffer& m) {
        if (b == 1) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 17); });
    check (note.rms > 1e-4, "MIDI note plays the rack with the sequencer off");

    // Larger host block than prepared still renders the whole buffer.
    juce::AudioBuffer<float> big (2, 2000);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 120), 1900);
    p.processBlock (big, m);
    float tailPeak = 0;
    for (int i = 1900; i < 2000; ++i) tailPeak = std::max (tailPeak, std::abs (big.getSample (0, i)));
    check (tailPeak > 0, "oversized host block is rendered to its end");

    // Mono output layout
    ContinuumRackProcessor mono;
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    check (mono.setBusesLayout (layout), "mono output layout accepted");
    mono.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> mb (1, 256);
    juce::MidiBuffer mm;
    mm.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), 0);
    double pk = 0;
    for (int i = 0; i < 40; ++i) { mono.processBlock (mb, mm); mm.clear(); pk = std::max (pk, (double) mb.getMagnitude (0, 256)); }
    check (pk > 1e-4, "mono output renders");
}

void testWebBridge()
{
    ContinuumRackProcessor p;
    p.prepareToPlay (48000, 512);
    RackWebBridge bridge (p);
    std::vector<std::pair<juce::String, juce::var>> out;
    bridge.send = [&] (const juce::Identifier& id, const juce::var& v) { out.push_back ({ id.toString(), v }); };
    juce::String savedName, savedJson;
    bridge.onSave = [&] (const juce::String& n, const juce::String& j) { savedName = n; savedJson = j; };
    auto count = [&] (const char* id) { int n = 0; for (auto& e : out) n += e.first == id ? 1 : 0; return n; };
    auto last = [&] (const char* id) { juce::var v; for (auto& e : out) if (e.first == id) v = e.second; return v; };
    auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> kv) {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : kv) o->setProperty (k, v);
        return juce::var (o);
    };

    bridge.tick();
    check (out.empty(), "bridge is silent until the page says hello");
    bridge.handle ("rackHello", obj ({}));
    check (count ("rackLoad") == 1, "hello: plugin sends the current rack to the page");
    juce::var loaded;
    check (! juce::JSON::parse (last ("rackLoad")["stateJson"].toString(), loaded).failed() && (int) loaded["tempo"] == 118
               && (int) last ("rackLoad")["sceneIndex"] == 0, "rackLoad holds Squelch Dub's state and scene index");

    // A real rackState payload captured from the page (Squelch Dub as the page sends it).
    const auto fixture = juce::File (RACK_FIXTURE_DIR).getChildFile ("page-rack-state.json");
    juce::var captured;
    check (! juce::JSON::parse (fixture.loadFileAsString(), captured).failed(), "captured page payload parses");
    juce::var st;
    juce::JSON::parse (captured["stateJson"].toString(), st);
    st["banks"][2].getDynamicObject()->setProperty ("level", 33);
    st["banks"][7]["slots"][7].getDynamicObject()->setProperty ("on", true);
    st["banks"][0]["slots"][0]["edit"].getDynamicObject()->setProperty ("cutoff", 12);
    st.getDynamicObject()->setProperty ("master", 61);
    out.clear();
    bridge.handle ("rackState", obj ({ { "stateJson", juce::JSON::toString (st, true) }, { "sceneIndex", 0 } }));
    const auto rs = p.getRackState();
    check (rs.banks[7].slots[7].on && std::abs (rs.banks[0].slots[0].edit.cutoff - 12) < 1e-4, "page edits reach the plugin's rack");
    check (std::abs (getParam (p, ContinuumRackProcessor::bankParamID (2, "level")) - 33) < 1e-3 && std::abs (getParam (p, "master") - 61) < 1e-3,
           "page knob edits move the matching host parameters");
    bridge.tick();
    check (count ("rackLoad") == 0 && count ("rackParams") == 0 && count ("rackStatus") == 1, "page edits are not echoed back");

    // Host automation goes to the page.
    out.clear();
    setParam (p, "drive", 44);
    bridge.tick();
    check (count ("rackParams") == 1 && (int) last ("rackParams")["drive"] == 44, "host automation is sent to the page");
    bridge.tick();
    check (count ("rackParams") == 1, "unchanged parameters are not resent");
    // A page edit must not undo automation of a different parameter.
    out.clear();
    st["banks"][1].getDynamicObject()->setProperty ("pan", 20);
    bridge.handle ("rackState", obj ({ { "stateJson", juce::JSON::toString (st, true) }, { "sceneIndex", 0 } }));
    check (std::abs (getParam (p, "drive") - 44) < 1e-3, "page edit leaves unrelated automated parameters alone");

    // Host preset change goes to the page.
    out.clear();
    p.setCurrentProgram (4);
    bridge.tick();
    check (count ("rackLoad") == 1 && (int) last ("rackLoad")["sceneIndex"] == 4, "host preset change reloads the page");

    // Notes from the page play through the engine.
    setParam (p, "seqMode", 2);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i) p.processBlock (buf, midi);   // silence the tails
    bridge.handle ("rackNote", obj ({ { "midi", 48 }, { "duration", 0.42 }, { "velocity", 1.0 }, { "bank", -1 }, { "slot", -1 }, { "force", false } }));
    double pk = 0;
    for (int i = 0; i < 20; ++i) { p.processBlock (buf, midi); pk = std::max (pk, (double) buf.getMagnitude (0, 0, 512)); }
    check (pk > 1e-3, "keyboard note from the page sounds (peak " + juce::String (pk) + ")");
    check (p.getEngine().countActiveVoices() > 0, "page note started voices");
    bridge.handle ("rackPanic", obj ({}));
    p.processBlock (buf, midi);
    check (p.getEngine().countActiveVoices() == 0, "PANIC from the page silences the engine");
    bridge.handle ("rackNote", obj ({ { "midi", 40 }, { "duration", 90.0 }, { "velocity", 0.65 }, { "bank", 6 }, { "slot", 6 }, { "force", true } }));
    p.processBlock (buf, midi);
    check (p.getEngine().countActiveVoices() == 1, "factory drone plays its exact slot even when switched off");
    bridge.handle ("rackStopNotes", obj ({}));
    for (int i = 0; i < 400; ++i) p.processBlock (buf, midi);
    check (p.getEngine().countActiveVoices() == 0, "stopping the drone releases it");

    // RUN SEQ with the clock Off turns the clock back to Host and runs it.
    bridge.handle ("rackRun", obj ({ { "run", true } }));
    check (getParam (p, "run") > 0.5f && (int) getParam (p, "seqMode") == 0, "RUN SEQ sets Run and re-enables the clock");
    for (int i = 0; i < 40; ++i) p.processBlock (buf, midi);
    out.clear();
    bridge.handle ("rackScope", obj ({ { "bank", 3 } }));
    bridge.tick();
    const auto status = last ("rackStatus");
    check ((bool) status["run"] && status["hits"].size() == 64 && status["octo"].size() == 8 && (int) status["octoBank"] == 3
               && status["master"].size() == 256, "status carries run state, LEDs, meters and scopes");
    bridge.handle ("rackRun", obj ({ { "run", false } }));
    check (getParam (p, "run") < 0.5f, "RUN SEQ off");

    bridge.handle ("rackSave", obj ({ { "name", "Continuum_Synth_8x8_Patch.json" }, { "json", "{\"format\":\"continuum-synth-p01\"}" } }));
    check (savedName == "Continuum_Synth_8x8_Patch.json" && savedJson.contains ("continuum-synth-p01"), "CORE SAVE from the page reaches the save dialog");

    // Garbage from the page is ignored.
    const auto before = crack::writeRackPatch (p.getRackState());
    bridge.handle ("rackState", obj ({ { "stateJson", "{not json" } }));
    bridge.handle ("rackState", obj ({ { "stateJson", "{\"banks\":[]}" } }));
    bridge.handle ("rackNote", obj ({ { "midi", "x" }, { "bank", 99 } }));
    bridge.handle ("unknownEvent", obj ({}));
    check (crack::writeRackPatch (p.getRackState()) == before, "malformed page messages leave the rack unchanged");
}

void testEditor()
{
    ContinuumRackProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    check (ed != nullptr && ed->getWidth() == 1440 && ed->isResizable(), "editor opens resizable at 1440 x 900");
    if (auto* shell = dynamic_cast<ContinuumRackEditor*> (ed.get()))
    {
        shell->showView (1);
        check (p.editorView.load() == 1, "native panel selected");
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ContinuumRackProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.editorView.load() == 1, "chosen view is saved with the project");
    }
    // Optional screenshot: RACK_SNAPSHOT=/path/to/editor.png
    if (const char* shot = std::getenv ("RACK_SNAPSHOT"))
    {
        p.prepareToPlay (48000, 512);
        TestPlayHead head;
        playingAt (head, 118);
        p.setPlayHead (&head);
        render (p, &head, 1.3);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out (shot);
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "editor snapshot written to " << shot << "\n";
    }
    p.setCurrentProgram (5);
    ed.reset();
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        testScenes();
        testStateRoundTrip();
        testWebPatches();
        testTransportAndMidi();
        testWebBridge();
        testEditor();
    }
    catch (const std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return 2;
    }
    std::cout << passes << " checks passed, " << failures << " failed\n";
    return failures ? 1 : 0;
}
