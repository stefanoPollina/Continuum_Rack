#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

namespace
{
constexpr int stateVersion = 1;
constexpr double retireAfterSeconds = 5.0;

juce::String gunzip (const void* data, size_t size)
{
    juce::MemoryInputStream raw (data, size, false);
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    return gz.readEntireStreamAsString();
}

juce::MemoryBlock gunzipBinary (const void* data, size_t size)
{
    juce::MemoryInputStream raw (data, size, false);
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock out;
    gz.readIntoMemoryBlock (out);
    return out;
}

// Parameter choice order -> engine enums.
const crack::Wave lfoWaves[] = { crack::Wave::sine, crack::Wave::triangle, crack::Wave::square, crack::Wave::sawtooth };
int lfoWaveIndex (crack::Wave w)
{
    for (int i = 0; i < 4; ++i) if (lfoWaves[i] == w) return i;
    return 0;
}
} // namespace

struct ContinuumRackProcessor::P
{
    std::atomic<float>* master = nullptr, *drive = nullptr, *delayMix = nullptr, *delayTime = nullptr, *feedback = nullptr;
    std::atomic<float>* lfoRate = nullptr, *lfoDepth = nullptr, *lfoDest = nullptr, *lfoWave = nullptr;
    std::atomic<float>* sendLevel = nullptr, *root = nullptr, *tempo = nullptr;
    std::atomic<float>* seqMode = nullptr, *run = nullptr, *midiGate = nullptr, *midiTarget = nullptr, *outputClip = nullptr;
    std::array<std::atomic<float>*, crack::numBanks> level {}, pan {}, cutoff {}, mute {};
};

juce::String ContinuumRackProcessor::bankParamID (int bank, const char* what)
{
    return "bank" + juce::String (bank + 1) + "_" + what;
}

juce::AudioProcessorValueTreeState::ParameterLayout ContinuumRackProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pct = [] (const char* id, const char* name, float lo, float hi, float def) {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 1.0f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%"));
    };
    layout.add (pct ("master", "Master Level", 0, 100, 55));
    layout.add (pct ("drive", "Master Drive", 0, 100, 0));
    layout.add (pct ("delayMix", "Echo Mix", 0, 65, 12));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "delayTime", 1 }, "Echo Time", NormalisableRange<float> (0.06f, 0.8f, 0.01f), 0.25f,
                                                       AudioParameterFloatAttributes().withLabel ("s")));
    layout.add (pct ("feedback", "Echo Feedback", 0, 70, 12));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 1 }, "LFO Rate", NormalisableRange<float> (0.05f, 15.0f, 0.05f, 0.4f), 2.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (pct ("lfoDepth", "LFO Depth", 0, 100, 20));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoDest", 1 }, "LFO Destination", StringArray { "Filter", "Amp", "Pan" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoWave", 1 }, "LFO Wave", StringArray { "Sine", "Triangle", "Square", "Saw" }, 0));
    layout.add (pct ("sendLevel", "Send Level", 0, 40, 8));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "root", 1 }, "Root Note", 24, 107, 48,
                                                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
                                                         return MidiMessage::getMidiNoteName (v, true, true, 3); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Internal Tempo", NormalisableRange<float> (40, 240, 1), 120,
                                                       AudioParameterFloatAttributes().withLabel ("BPM")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqMode", 1 }, "Sequencer Clock", StringArray { "Host transport", "Internal", "Off" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "run", 1 }, "Internal Run", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "midiGate", 1 }, "MIDI Notes", StringArray { "Gated", "One-shot (web)" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "outputClip", 1 }, "Clip at 0 dBFS", true));
    StringArray targets { "All banks" };
    for (int b = 1; b <= crack::numBanks; ++b) targets.add ("Bank " + String (b));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "midiTarget", 1 }, "MIDI Target", targets, 0));

    for (int b = 0; b < crack::numBanks; ++b)
    {
        const String n = "Bank " + String (b + 1) + " ";
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bankParamID (b, "level"), 1 }, n + "Level", NormalisableRange<float> (0, 100, 1), 70,
                                                           AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bankParamID (b, "pan"), 1 }, n + "Pan", NormalisableRange<float> (0, 100, 1), 50,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                                               const int x = roundToInt (v);
                                                               return x == 50 ? String ("C") : x < 50 ? "L" + String (50 - x) : "R" + String (x - 50); })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bankParamID (b, "cutoff"), 1 }, n + "Filter", NormalisableRange<float> (0, 100, 1), 78,
                                                           AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bankParamID (b, "mute"), 1 }, n + "Seq Mute", false));
    }
    return layout;
}

ContinuumRackProcessor::ContinuumRackProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ContinuumRack", createLayout()),
      p (std::make_unique<P>())
{
    auto raw = [this] (const juce::String& id) { return parameters.getRawParameterValue (id); };
    p->master = raw ("master"); p->drive = raw ("drive"); p->delayMix = raw ("delayMix");
    p->delayTime = raw ("delayTime"); p->feedback = raw ("feedback"); p->lfoRate = raw ("lfoRate");
    p->lfoDepth = raw ("lfoDepth"); p->lfoDest = raw ("lfoDest"); p->lfoWave = raw ("lfoWave");
    p->sendLevel = raw ("sendLevel"); p->root = raw ("root"); p->tempo = raw ("tempo");
    p->seqMode = raw ("seqMode"); p->run = raw ("run"); p->midiGate = raw ("midiGate"); p->midiTarget = raw ("midiTarget");
    p->outputClip = raw ("outputClip");
    for (int b = 0; b < crack::numBanks; ++b)
    {
        p->level[(size_t) b] = raw (bankParamID (b, "level"));
        p->pan[(size_t) b] = raw (bankParamID (b, "pan"));
        p->cutoff[(size_t) b] = raw (bankParamID (b, "cutoff"));
        p->mute[(size_t) b] = raw (bankParamID (b, "mute"));
    }

    const auto wt = gunzipBinary (BinaryData::wavetables_bin_gz, (size_t) BinaryData::wavetables_bin_gzSize);
    const bool tablesOk = library.loadMosaicBank (wt.getData(), wt.getSize());
    jassert (tablesOk);
    juce::ignoreUnused (tablesOk);

    juce::String error;
    scenes = crack::parseScenes (gunzip (BinaryData::scenes_json_gz, (size_t) BinaryData::scenes_json_gzSize), error);
    jassert (! scenes.empty());

    midiEvents.reserve (1024);
    setLatencySamples ((int) std::lround (0.006 * 48000.0));   // updated for the real rate in prepareToPlay
    if (! scenes.empty())
        loadState (scenes[0].state, scenes[0].name, scenes[0].desc, 0, true);
    else
        loadState (crack::makeDefaultRack(), "Init", "", 0, true);
    startTimerHz (2);
}

ContinuumRackProcessor::~ContinuumRackProcessor()
{
    stopTimer();
}

bool ContinuumRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && layouts.getMainInputChannelSet().isDisabled();
}

void ContinuumRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlock = juce::jmax (16, samplesPerBlock);
    preparedRate = sampleRate > 0 ? sampleRate : 48000.0;
    engine.prepare (sampleRate, preparedBlock);
    setLatencySamples (engine.getLatencySamples());
    engine.setProgram (live.load (std::memory_order_acquire));
    monoScratch.assign ((size_t) preparedBlock, 0.0f);
    midiEvents.reserve (1024);
}

void ContinuumRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto* prog = live.load (std::memory_order_acquire);
    engine.setProgram (prog);

    crack::LiveParams lp;
    lp.master = p->master->load(); lp.drive = p->drive->load(); lp.delayMix = p->delayMix->load();
    lp.delayTime = p->delayTime->load(); lp.feedback = p->feedback->load(); lp.lfoRate = p->lfoRate->load();
    lp.lfoDepth = p->lfoDepth->load();
    lp.lfoDest = (crack::LfoDest) juce::jlimit (0, 2, (int) p->lfoDest->load());
    lp.lfoWave = lfoWaves[juce::jlimit (0, 3, (int) p->lfoWave->load())];
    lp.sendLevel = p->sendLevel->load(); lp.root = p->root->load(); lp.tempo = p->tempo->load();
    for (size_t b = 0; b < crack::numBanks; ++b)
    {
        lp.level[b] = p->level[b]->load(); lp.pan[b] = p->pan[b]->load();
        lp.cutoff[b] = p->cutoff[b]->load(); lp.seqMute[b] = p->mute[b]->load() > 0.5f;
    }
    crack::EngineOptions opt;
    opt.seqMode = (crack::SeqMode) juce::jlimit (0, 2, (int) p->seqMode->load());
    opt.internalRun = p->run->load() > 0.5f;
    opt.midiGate = p->midiGate->load() > 0.5f ? crack::MidiGate::oneShot : crack::MidiGate::gated;
    opt.midiBank = juce::jlimit (0, crack::numBanks, (int) p->midiTarget->load()) - 1;

    crack::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const auto ppq = pos->getPpqPosition();
            const auto bpm = pos->getBpm();
            tr.playing = pos->getIsPlaying();
            tr.hasPosition = ppq.hasValue() && bpm.hasValue() && *bpm > 0;
            if (tr.hasPosition) { tr.ppq = *ppq; tr.bpm = *bpm; }
        }

    // Sub-divide very large host blocks so the engine's scratch buffers suffice.
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int done = 0;
    auto it = midi.begin();
    while (done < n)
    {
        const int chunk = juce::jmin (n - done, preparedBlock);
        midiEvents.clear();
        for (; it != midi.end(); ++it)
        {
            const auto meta = *it;
            if (meta.samplePosition >= done + chunk) break;
            const auto m = meta.getMessage();
            const int off = juce::jmax (0, meta.samplePosition - done);
            if (midiEvents.size() >= midiEvents.capacity()) continue;
            if (m.isNoteOn()) midiEvents.push_back ({ crack::MidiEvent::noteOn, off, m.getNoteNumber(), m.getFloatVelocity() });
            else if (m.isNoteOff()) midiEvents.push_back ({ crack::MidiEvent::noteOff, off, m.getNoteNumber(), 0 });
            else if (m.isSustainPedalOn()) midiEvents.push_back ({ crack::MidiEvent::sustain, off, 64, 1 });
            else if (m.isSustainPedalOff()) midiEvents.push_back ({ crack::MidiEvent::sustain, off, 64, 0 });
            else if (m.isAllNotesOff() || m.isAllSoundOff()) midiEvents.push_back ({ crack::MidiEvent::allNotesOff, off, 0, 0 });
        }
        float* l = L + done;
        float* r = R != nullptr ? R + done : nullptr;
        if (r != nullptr)
            engine.process (l, r, chunk, lp, opt, tr, midiEvents.data(), (int) midiEvents.size());
        else
        {
            engine.process (l, monoScratch.data(), chunk, lp, opt, tr, midiEvents.data(), (int) midiEvents.size());
            for (int i = 0; i < chunk; ++i) l[i] = 0.5f * (l[i] + monoScratch[(size_t) i]);
        }
        if (tr.hasPosition) tr.ppq += chunk * tr.bpm / 60.0 / preparedRate;
        done += chunk;
    }
    // A browser clamps its output to +/-1 at the audio device; by default the plugin does the
    // same so a scene sounds as it does in the web app (and cannot send overs into the host).
    if (p->outputClip->load() > 0.5f)
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
            juce::FloatVectorOperations::clip (buffer.getWritePointer (c), buffer.getReadPointer (c), -1.0f, 1.0f, n);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

// ───────────────────────────────────────────────────────────── patches
void ContinuumRackProcessor::publish (std::unique_ptr<crack::Program> prog)
{
    // caller holds programLock
    auto* raw = prog.get();
    programs.push_back (std::move (prog));
    auto* old = live.exchange (raw, std::memory_order_acq_rel);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (auto& up : programs)
        if (up.get() == old)
        {
            retired.push_back ({ std::move (up), now });
            break;
        }
    programs.erase (std::remove_if (programs.begin(), programs.end(), [] (auto& u) { return u == nullptr; }), programs.end());
    patchSerial.fetch_add (1);
}

void ContinuumRackProcessor::timerCallback()
{
    const std::lock_guard<std::mutex> g (programLock);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    retired.erase (std::remove_if (retired.begin(), retired.end(), [&] (const Retired& r) { return now - r.when > retireAfterSeconds; }),
                   retired.end());
}

void ContinuumRackProcessor::loadState (const crack::RackState& s, const juce::String& name, const juce::String& desc, int sceneIndex, bool updateParams)
{
    {
        const std::lock_guard<std::mutex> g (programLock);
        currentState = s;
        crack::normalise (currentState);
        patchName = name;
        patchDesc = desc;
        currentScene.store (sceneIndex);
        publish (crack::buildProgram (currentState, library));
    }
    if (updateParams) pushParamsFromState (currentState);
}

void ContinuumRackProcessor::pushParamsFromState (const crack::RackState& s)
{
    auto set = [this] (const juce::String& id, float v) {
        if (auto* prm = parameters.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set ("master", s.master); set ("drive", s.drive); set ("delayMix", s.delayMix);
    set ("delayTime", s.delayTime); set ("feedback", s.feedback); set ("lfoRate", s.lfoRate);
    set ("lfoDepth", s.lfoDepth); set ("lfoDest", (float) (int) s.lfoDest); set ("lfoWave", (float) lfoWaveIndex (s.lfoWave));
    set ("sendLevel", s.sendLevel); set ("root", s.root); set ("tempo", s.tempo);
    for (int b = 0; b < crack::numBanks; ++b)
    {
        set (bankParamID (b, "level"), s.banks[(size_t) b].level);
        set (bankParamID (b, "pan"), s.banks[(size_t) b].pan);
        set (bankParamID (b, "cutoff"), s.banks[(size_t) b].cutoff);
        set (bankParamID (b, "mute"), s.patterns[(size_t) b].mute ? 1.0f : 0.0f);
    }
}

void ContinuumRackProcessor::foldParams (crack::RackState& s) const
{
    auto v = [this] (const juce::String& id) { return parameters.getRawParameterValue (id)->load(); };
    s.master = v ("master"); s.drive = v ("drive"); s.delayMix = v ("delayMix"); s.delayTime = v ("delayTime");
    s.feedback = v ("feedback"); s.lfoRate = v ("lfoRate"); s.lfoDepth = v ("lfoDepth");
    s.lfoDest = (crack::LfoDest) juce::jlimit (0, 2, (int) v ("lfoDest"));
    s.lfoWave = lfoWaves[juce::jlimit (0, 3, (int) v ("lfoWave"))];
    s.sendLevel = v ("sendLevel"); s.root = v ("root"); s.tempo = v ("tempo");
    for (int b = 0; b < crack::numBanks; ++b)
    {
        s.banks[(size_t) b].level = v (bankParamID (b, "level"));
        s.banks[(size_t) b].pan = v (bankParamID (b, "pan"));
        s.banks[(size_t) b].cutoff = v (bankParamID (b, "cutoff"));
        s.patterns[(size_t) b].mute = v (bankParamID (b, "mute")) > 0.5f;
    }
}

crack::RackState ContinuumRackProcessor::getRackState() const
{
    crack::RackState s;
    {
        const std::lock_guard<std::mutex> g (programLock);
        s = currentState;
    }
    foldParams (s);
    return s;
}

juce::String ContinuumRackProcessor::getPatchName() const
{
    const std::lock_guard<std::mutex> g (programLock);
    return patchName;
}

juce::String ContinuumRackProcessor::getPatchDescription() const
{
    const std::lock_guard<std::mutex> g (programLock);
    return patchDesc;
}

void ContinuumRackProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) scenes.size()) return;
    const auto& sc = scenes[(size_t) index];
    loadState (sc.state, sc.name, sc.desc, index, true);
}

const juce::String ContinuumRackProcessor::getProgramName (int index)
{
    if (index < 0 || index >= (int) scenes.size()) return {};
    return scenes[(size_t) index].name;
}

bool ContinuumRackProcessor::importRackPatch (const juce::File& file, juce::String& error)
{
    if (file.getSize() > 16 * 1024 * 1024) { error = "file is too large to be a rack patch"; return false; }
    crack::RackState s;
    if (! crack::readRackPatch (file.loadFileAsString(), s, error)) return false;
    loadState (s, file.getFileNameWithoutExtension(), "Imported rack patch", -1, true);
    return true;
}

bool ContinuumRackProcessor::exportRackPatch (const juce::File& file, juce::String& error) const
{
    if (! file.replaceWithText (crack::writeRackPatch (getRackState())))
    {
        error = "could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

void ContinuumRackProcessor::setSlotEnabled (int bank, int slot, bool on)
{
    if (bank < 0 || bank >= crack::numBanks || slot < 0 || slot >= crack::numSlots) return;
    const std::lock_guard<std::mutex> g (programLock);
    currentState.banks[(size_t) bank].slots[(size_t) slot].on = on;
    publish (crack::buildProgram (currentState, library));
}

void ContinuumRackProcessor::setSlotType (int bank, int slot, int type)
{
    if (bank < 0 || bank >= crack::numBanks || slot < 0 || slot >= crack::numSlots || type < 0 || type > 7) return;
    const std::lock_guard<std::mutex> g (programLock);
    currentState.banks[(size_t) bank].slots[(size_t) slot].type = type;
    publish (crack::buildProgram (currentState, library));
}

// ───────────────────────────────────────────────────────────── host state
void ContinuumRackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("ContinuumRackState");
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty ("scene", currentScene.load(), nullptr);
    root.setProperty ("name", getPatchName(), nullptr);
    root.setProperty ("desc", getPatchDescription(), nullptr);
    root.setProperty ("rack", crack::writeRackPatch (getRackState()), nullptr);
    root.appendChild (parameters.copyState(), nullptr);
    juce::MemoryOutputStream out (dest, false);
    root.writeToStream (out);
}

void ContinuumRackProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    const auto root = juce::ValueTree::readFromData (data, (size_t) size);
    if (! root.isValid() || root.getType().toString() != "ContinuumRackState") return;
    if ((int) root.getProperty ("version", 0) != stateVersion) return;

    crack::RackState s;
    juce::String error;
    if (! crack::readRackPatch (root.getProperty ("rack").toString(), s, error)) return;
    const auto params = root.getChildWithName (parameters.state.getType());
    if (! params.isValid()) return;

    loadState (s, root.getProperty ("name").toString(), root.getProperty ("desc").toString(),
               (int) root.getProperty ("scene", -1), false);
    parameters.replaceState (params);
}

juce::AudioProcessorEditor* ContinuumRackProcessor::createEditor()
{
    return new ContinuumRackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ContinuumRackProcessor();
}
