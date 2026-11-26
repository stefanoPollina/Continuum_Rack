#include "WebBridge.h"

namespace
{
const char* const lfoDestNames[] = { "filter", "amp", "pan" };
const char* const lfoWaveNames[] = { "sine", "triangle", "square", "sawtooth" };   // parameter choice order

int intOf (const juce::var& v, int def)
{
    return (v.isInt() || v.isInt64() || v.isDouble() || v.isBool()) ? (int) v : def;
}
double numOf (const juce::var& v, double def)
{
    return (v.isInt() || v.isInt64() || v.isDouble()) ? (double) v : def;
}
} // namespace

RackWebBridge::RackWebBridge (ContinuumRackProcessor& p) : proc (p)
{
    onSave = [this] (const juce::String& name, const juce::String& json) {
        auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (name));
        chooser = std::make_unique<juce::FileChooser> ("Save from Continuum Rack", start, "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [json] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (f != juce::File()) f.withFileExtension ("json").replaceWithText (json);
                              });
    };
}

const juce::StringArray& RackWebBridge::pageEvents()
{
    static const juce::StringArray ids { "rackHello", "rackState", "rackNote", "rackStopNotes", "rackRun", "rackPanic", "rackSave", "rackScope" };
    return ids;
}

void RackWebBridge::handle (const juce::Identifier& event, const juce::var& d)
{
    const auto id = event.toString();
    if (id == "rackHello")
    {
        ready = true;
        sendLoad();
    }
    else if (id == "rackState")
    {
        juce::var parsed;
        if (juce::JSON::parse (d["stateJson"].toString(), parsed).failed()) return;
        crack::RackState s;
        juce::String error;
        if (! crack::stateFromVar (parsed, s, error)) return;
        seenSerial = proc.applyEditorState (s, intOf (d["sceneIndex"], -1), havePageState ? &lastPageState : nullptr);
        lastPageState = s;
        havePageState = true;
        lastParams = juce::JSON::toString (paramSnapshot(), true);   // these values came from the page
    }
    else if (id == "rackNote")
    {
        crack::UiNote n;
        n.midi = juce::jlimit (0, 127, intOf (d["midi"], 48));
        n.duration = juce::jlimit (0.005, 600.0, numOf (d["duration"], 0.42));
        n.velocity = (float) juce::jlimit (0.0, 1.0, numOf (d["velocity"], 1.0));
        n.bank = juce::jlimit (-1, crack::numBanks - 1, intOf (d["bank"], -1));
        n.slot = juce::jlimit (-1, crack::numSlots - 1, intOf (d["slot"], -1));
        n.force = (bool) d["force"];
        proc.queueUiNote (n);
    }
    else if (id == "rackStopNotes") proc.requestStopUiNotes();
    else if (id == "rackPanic") proc.requestPanic();
    else if (id == "rackRun")
    {
        const bool run = (bool) d["run"];
        if (run)
            if (auto* mode = proc.parameters.getParameter ("seqMode"))
                if (mode->getValue() > 0.75f) mode->setValueNotifyingHost (0.0f);   // clock was Off: back to Host
        if (auto* r = proc.parameters.getParameter ("run"))
            r->setValueNotifyingHost (run ? 1.0f : 0.0f);
    }
    else if (id == "rackSave")
    {
        const auto json = d["json"].toString();
        if (json.isNotEmpty() && json.length() < 16 * 1024 * 1024 && onSave)
            onSave (d["name"].toString().isNotEmpty() ? d["name"].toString() : juce::String ("Continuum_Synth_8x8_Patch.json"), json);
    }
    else if (id == "rackScope")
        proc.setScopeBank (juce::jlimit (0, crack::numBanks - 1, intOf (d["bank"], 0)));
}

juce::var RackWebBridge::paramSnapshot() const
{
    auto v = [this] (const juce::String& pid) { return (double) proc.parameters.getRawParameterValue (pid)->load(); };
    auto* o = new juce::DynamicObject();
    for (auto* k : { "master", "drive", "delayMix", "delayTime", "feedback", "lfoRate", "lfoDepth", "sendLevel", "tempo", "root" })
        o->setProperty (k, v (k));
    o->setProperty ("lfoDest", lfoDestNames[juce::jlimit (0, 2, (int) v ("lfoDest"))]);
    o->setProperty ("lfoWave", lfoWaveNames[juce::jlimit (0, 3, (int) v ("lfoWave"))]);
    juce::Array<juce::var> banks;
    for (int b = 0; b < crack::numBanks; ++b)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("level", v (ContinuumRackProcessor::bankParamID (b, "level")));
        bo->setProperty ("pan", v (ContinuumRackProcessor::bankParamID (b, "pan")));
        bo->setProperty ("cutoff", v (ContinuumRackProcessor::bankParamID (b, "cutoff")));
        bo->setProperty ("mute", v (ContinuumRackProcessor::bankParamID (b, "mute")) > 0.5);
        banks.add (juce::var (bo));
    }
    o->setProperty ("banks", banks);
    return juce::var (o);
}

void RackWebBridge::sendLoad()
{
    seenSerial = proc.getPatchSerial();
    const auto s = proc.getRackState();
    lastPageState = s;
    havePageState = true;
    auto* o = new juce::DynamicObject();
    o->setProperty ("stateJson", juce::JSON::toString (crack::stateToVar (s), true));
    o->setProperty ("sceneIndex", proc.getSceneIndex());
    o->setProperty ("name", proc.getPatchName());
    lastParams = juce::JSON::toString (paramSnapshot(), true);
    if (send) send ("rackLoad", juce::var (o));
}

void RackWebBridge::tick()
{
    if (! ready || ! send) return;

    // A patch change that did not come from the page (host preset, project recall, the
    // native view, LOAD PATCH): show it.
    if (proc.getPatchSerial() != seenSerial) sendLoad();

    // Parameter changes from host automation or the native view.
    const auto params = paramSnapshot();
    const auto ptxt = juce::JSON::toString (params, true);
    if (ptxt != lastParams)
    {
        lastParams = ptxt;
        send ("rackParams", params);
    }

    // Meters, scopes, playhead, engine LEDs.
    const auto& eng = proc.getEngine();
    auto* st = new juce::DynamicObject();
    st->setProperty ("sr", proc.getPreparedSampleRate());
    const bool run = eng.seqRunning.load();
    st->setProperty ("run", run);
    st->setProperty ("tick", (juce::int64) eng.lastTick.load());
    juce::Array<juce::var> hits, banks, masterWave, octo;
    for (int b = 0; b < crack::numBanks; ++b)
        for (int s = 0; s < crack::numSlots; ++s)
            hits.add ((juce::int64) eng.slotHits[(size_t) b][(size_t) s].load());
    for (int b = 0; b < crack::numBanks; ++b)
        banks.add (juce::roundToInt (eng.bankPeak[(size_t) b].load() * 0.6 * 10000) / 10000.0);   // the page reads it as an RMS level
    float buf[512];
    eng.scopes.readMaster (buf, 512);
    for (int i = 0; i < 512; i += 2) masterWave.add (juce::roundToInt (buf[i] * 10000) / 10000.0);
    for (int s = 0; s < crack::numSlots; ++s)
    {
        eng.scopes.readSlot (s, buf, 512);
        juce::Array<juce::var> w;
        for (int i = 0; i < 512; i += 4) w.add (juce::roundToInt (buf[i] * 100000) / 100000.0);
        octo.add (w);
    }
    st->setProperty ("hits", hits);
    st->setProperty ("banks", banks);
    st->setProperty ("master", masterWave);
    st->setProperty ("octoBank", eng.scopeBank.load());
    st->setProperty ("octo", octo);
    const auto mc = eng.midiNoteCount.load();
    st->setProperty ("midiNote", mc != lastMidiCount ? eng.lastMidiNote.load() : -1);
    lastMidiCount = mc;
    send ("rackStatus", juce::var (st));
}
