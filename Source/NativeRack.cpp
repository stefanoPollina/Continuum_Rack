#include "NativeRack.h"

#include <cmath>

using namespace juce;

namespace
{
String U (const char* s) { return String::fromUTF8 (s); }   // literals below contain · ◂ ▸ ▶ ■ ↓ ↑

enum ModuleKind { mCore = 1, mMidi, mMaster, mDelay, mLfo, mScope, mOcto, mRouter, mScenes, mSeq, mEditor, mStrip, mEngine, mKeys, mAtlas, mFactory, mMutator };

struct Skin { Colour a, b, ink, sub, rule; bool dark; };
Skin skinFor (const String& n)
{
    if (n == "light")  return { Colour (0xffeeeeee), Colour (0xffdcdcdc), Colour (0xff1b1b1b), Colour (0xff5c5c5c), Colour (0xffa7a7a7), false };
    if (n == "white")  return { Colour (0xfff7f7f4), Colour (0xffecece8), Colour (0xff1b1b1b), Colour (0xff5c5c5c), Colour (0xffb1b1ad), false };
    if (n == "silver") return { Colour (0xffdadde0), Colour (0xffb8bbbf), Colour (0xff17181a), Colour (0xff45474b), Colour (0xff888b90), false };
    if (n == "beige")  return { Colour (0xffebe7de), Colour (0xffdcd7cb), Colour (0xff2a2723), Colour (0xff6b665d), Colour (0xffa8a296), false };
    if (n == "black")  return { Colour (0xff181818), Colour (0xff0e0e0e), Colour (0xfff2f2f2), Colour (0xff9a9a9a), Colour (0xff3a3a3a), true };
    if (n == "navy")   return { Colour (0xff202b46), Colour (0xff151d31), Colour (0xffe8eefc), Colour (0xff8fa0c4), Colour (0xff34446a), true };
    if (n == "maroon") return { Colour (0xff34191c), Colour (0xff231012), Colour (0xfff6e9e9), Colour (0xffc49ea1), Colour (0xff5c3135), true };
    if (n == "surge")  return { Colour (0xff33353a), Colour (0xff25272b), Colour (0xffececec), Colour (0xffa3a6ad), Colour (0xff4b4e55), true };
    return { Colour (0xff2c2e32), Colour (0xff202226), Colour (0xffededed), Colour (0xffa0a3a9), Colour (0xff46494f), true };   // dark
}

struct KnobCols { Colour cc, lo, ind; };
KnobCols knobColours (const String& n, Colour bank)
{
    if (n == "grey")   return { Colour (0xff75787e), Colour (0xff3a3c40), Colours::white };
    if (n == "white")  return { Colour (0xffececea), Colour (0xffa3a4a2), Colour (0xff1d1d1d) };
    if (n == "red")    return { Colour (0xffda3b35), Colour (0xff741410), Colours::white };
    if (n == "teal")   return { Colour (0xff1fa99f), Colour (0xff0b5853), Colours::white };
    if (n == "orange") return { Colour (0xfff48b1d), Colour (0xff8a4300), Colour (0xff1a1a1a) };
    if (n == "yellow") return { Colour (0xfff4c632), Colour (0xff8c6a00), Colour (0xff1a1a1a) };
    if (n == "blue")   return { Colour (0xff3579e1), Colour (0xff12397a), Colours::white };
    if (n == "cyan")   return { Colour (0xff2cc8e7), Colour (0xff0b5d70), Colour (0xff0b1a22) };
    if (n == "bank")   return { bank, bank.interpolatedWith (Colours::black, 0.6f), Colour (0xff111111) };
    return { Colour (0xff27282b), Colour (0xff0b0b0c), Colour (0xfff2f2f2) };   // black
}

const uint32 bankHex[] = { 0xffdeae62, 0xfff59d87, 0xffed9ac1, 0xffc8a5f0, 0xff87bafd, 0xff54c9e3, 0xff5fceb3, 0xffa7c474 };
const char* const engineSkin[] = { "silver", "light", "dark", "black", "navy", "beige", "white", "maroon" };
const char* const engineKnob[] = { "red", "black", "teal", "orange", "cyan", "white", "blue", "yellow" };
const char* const typeNames[] = { "ACID / 303-INSPIRED", "HYBRID SUBTRACTIVE", "FM MATRIX", "MACRO WAVEFOLD", "SPECTRAL WAVETABLE", "PHYSICAL RESONATOR", "ADDITIVE HARMONICS", "CROSS MODULATION" };
const char* const cableColours[] = { "#f3374b", "#ffb437", "#3695ef", "#8b4ade" };
const Colour amber (0xffffb437);

Colour bankColour (int b) { return Colour (bankHex[b & 7]); }
Font mono (float size, bool bold = true) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), size, bold ? Font::bold : Font::plain)); }
Font sans (float size, bool bold = true) { return Font (FontOptions (size, bold ? Font::bold : Font::plain)); }
String two (int n) { return String (n).paddedLeft ('0', 2); }
String noteName (int m)
{
    static const char* const n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (n[((m % 12) + 12) % 12]) + String (m / 12 - 1);
}
const char* waveShort (crack::Wave w)
{
    switch (w) { case crack::Wave::sine: return "SINE"; case crack::Wave::triangle: return "TRI"; case crack::Wave::sawtooth: return "SAW"; case crack::Wave::square: return "SQR"; }
    return "SINE";
}
const char* filterShort (crack::VoiceFilter f)
{
    switch (f) { case crack::VoiceFilter::lowpass: return "LP"; case crack::VoiceFilter::highpass: return "HP"; case crack::VoiceFilter::bandpass: return "BP"; case crack::VoiceFilter::notch: return "NOTCH"; case crack::VoiceFilter::comb: return "COMB"; }
    return "LP";
}
Colour colourFromHex (const std::string& h)
{
    if (h.size() == 7 && h[0] == '#') return Colour ((uint32) (0xff000000u | (uint32) std::stoul (h.substr (1), nullptr, 16)));
    return Colour (0xfff3374b);
}
template <typename E> E cycleEnum (E v, int dir, int n) { return (E) ((((int) v + dir) % n + n) % n); }
double now() { return Time::getMillisecondCounterHiRes(); }

void lcdBox (Graphics& g, Rectangle<float> r)
{
    g.setColour (Colour (0xff08090a));
    g.fillRoundedRectangle (r, 3);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3, 1);
}
void lcdText (Graphics& g, Rectangle<float> r, const String& t, Colour c, float size, Justification j = Justification::centred)
{
    g.setFont (mono (size));
    g.setColour (c.withAlpha (0.25f));
    g.drawText (t, r.translated (0, 0.6f), j, true);
    g.setColour (c);
    g.drawText (t, r, j, true);
}
} // namespace

// ═════════════════════════════════════════════════════════════ construction
NativeRackCanvas::NativeRackCanvas (ContinuumRackProcessor& p) : proc (p)
{
    setSize (rackWidth, rackHeight);
    setOpaque (true);
    status = U ("PLUGIN ENGINE \xc2\xb7 DAW TRANSPORT DRIVES THE SEQUENCER");
    masterWave.assign (1024, 0.0f);
    octoWave.assign (8 * 512, 0.0f);
    lastStep.fill (-1);
    hitTime.fill (-1e9);
    refreshState();
    startTimerHz (30);
}

NativeRackCanvas::~NativeRackCanvas() { stopTimer(); }

void NativeRackCanvas::say (const String& s)
{
    status = s;
    repaint (6, 44, 168, 46);
}

void NativeRackCanvas::refreshState()
{
    serial = proc.getPatchSerial();
    rs = proc.getRackState();
    if (proc.getSceneIndex() >= 0) scenePick = proc.getSceneIndex();
    rebuild();
    repaint();
}

float NativeRackCanvas::paramValue (const String& pid) const
{
    return proc.parameters.getRawParameterValue (pid)->load();
}

void NativeRackCanvas::setParam (const String& pid, float v)
{
    if (auto* prm = proc.parameters.getParameter (pid))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void NativeRackCanvas::editSlot (int b, int s, const std::function<void (crack::Slot&)>& fn)
{
    fn (rs.banks[(size_t) b].slots[(size_t) s]);
    proc.editRack ([b, s, fn] (crack::RackState& st) { fn (st.banks[(size_t) b].slots[(size_t) s]); });
    serial = proc.getPatchSerial();
}

void NativeRackCanvas::playNote (int midi, double duration, int bank, int slot)
{
    crack::UiNote n;
    n.midi = jlimit (0, 127, midi);
    n.duration = duration;
    n.bank = bank;
    n.slot = slot;
    proc.queueUiNote (n);
}

// ═════════════════════════════════════════════════════════════ control builders
NativeRackCanvas::Ctl& NativeRackCanvas::add (Ctl c)
{
    ctls.push_back (std::move (c));
    return ctls.back();
}

NativeRackCanvas::Ctl& NativeRackCanvas::paramKnob (const String& pid, float cx, float cy, float radius, const String& colour, const String& label, Colour bank)
{
    Ctl c;
    c.kind = Ctl::knob;
    c.id = pid;
    c.label = label;
    c.r = Rectangle<float> (cx - radius, cy - radius, radius * 2, radius * 2);
    c.knobColour = colour;
    c.accent = bank;
    auto* prm = proc.parameters.getParameter (pid);
    c.get01 = [prm] { return prm->getValue(); };
    c.set01 = [prm] (float v) { prm->setValueNotifyingHost (v); };
    c.text = [prm] { return prm->getCurrentValueAsText() + (prm->getLabel().isNotEmpty() ? " " + prm->getLabel() : String()); };
    c.begin = [prm] { prm->beginChangeGesture(); };
    c.end = [prm] { prm->endChangeGesture(); };
    c.def01 = prm->getDefaultValue();
    return add (std::move (c));
}

NativeRackCanvas::Ctl& NativeRackCanvas::editKnob (int b, int s, float crack::EngineEdit::* field, float lo, float hi, float stp,
                                                   float cx, float cy, float radius, const String& colour, const String& label)
{
    Ctl c;
    c.kind = Ctl::knob;
    c.id = "e:" + String (b) + ":" + String (s) + ":" + label;
    c.label = label;
    c.r = Rectangle<float> (cx - radius, cy - radius, radius * 2, radius * 2);
    c.knobColour = colour;
    c.get01 = [this, b, s, field, lo, hi] { return jlimit (0.0f, 1.0f, (rs.banks[(size_t) b].slots[(size_t) s].edit.*field - lo) / (hi - lo)); };
    c.set01 = [this, b, s, field, lo, hi, stp] (float v) {
        float x = lo + v * (hi - lo);
        x = jlimit (lo, hi, lo + std::round ((x - lo) / stp) * stp);
        if (std::abs (rs.banks[(size_t) b].slots[(size_t) s].edit.*field - x) < 1e-6f) return;
        editSlot (b, s, [field, x] (crack::Slot& sl) { sl.edit.*field = x; });
    };
    c.text = [this, b, s, field, stp] {
        const float v = rs.banks[(size_t) b].slots[(size_t) s].edit.*field;
        return String (v, stp < 1 ? 2 : 0);
    };
    c.def01 = jlimit (0.0f, 1.0f, (crack::EngineEdit {}.*field - lo) / (hi - lo));
    c.bank = b;
    c.index = s;
    return add (std::move (c));
}

NativeRackCanvas::Ctl& NativeRackCanvas::select (const String& id, Rectangle<float> r, const String& label, std::function<String()> text, std::function<void (int)> step)
{
    Ctl c;
    c.kind = Ctl::select;
    c.id = id;
    c.r = r;
    c.label = label;
    c.text = std::move (text);
    c.step = std::move (step);
    c.accent = amber;
    return add (std::move (c));
}

NativeRackCanvas::Ctl& NativeRackCanvas::button (const String& id, Rectangle<float> r, const String& text, const String& label, std::function<void()> click, Colour accent)
{
    Ctl c;
    c.kind = Ctl::button;
    c.id = id;
    c.r = r;
    c.label = label;
    const String t = text;
    c.text = [t] { return t; };
    c.click = std::move (click);
    c.accent = accent;
    return add (std::move (c));
}

NativeRackCanvas::Ctl& NativeRackCanvas::ledButton (const String& id, Rectangle<float> r, const String& label, Colour led, std::function<bool()> lit, std::function<void()> click)
{
    Ctl c;
    c.kind = Ctl::ledButton;
    c.id = id;
    c.r = r;
    c.label = label;
    c.ledColour = led;
    c.lit = std::move (lit);
    c.click = std::move (click);
    return add (std::move (c));
}

// ═════════════════════════════════════════════════════════════ layout (same as the web rack)
void NativeRackCanvas::rebuild()
{
    modules.clear();
    ctls.clear();
    lastVal.clear();
    lastText.clear();
    ctls.reserve (900);
    buildTopRow();
    modules.push_back ({ { 0, 380, 960, 380 }, "light", U ("SEQ \xc2\xb7 8\xc3\x97" "64"), U ("PERFORMANCE SEQUENCER \xc2\xb7 EIGHT INDEPENDENT BANK LANES"), {}, false, false, mSeq });
    buildSeq ({ 0, 380, 960, 380 });
    modules.push_back ({ { 960, 380, 780, 380 }, "surge", "DSP EDITOR", U ("LIVE ENGINE PARAMETERS \xc2\xb7 OSC \xe2\x86\x92 FM/FOLD \xe2\x86\x92 FILTER \xe2\x86\x92 ADSR \xe2\x86\x92 BANK BUS"), {}, false, false, mEditor });
    buildEditor ({ 960, 380, 780, 380 });
    for (int b = 0; b < crack::numBanks; ++b)
    {
        const int x0 = (b % 2) * 870, y0 = 760 + (b / 2) * 380;
        buildStrip (b, { x0, y0, 150, 380 });
        for (int s = 0; s < crack::numSlots; ++s)
            buildEngine (b, s, { x0 + 150 + s * 90, y0, 90, 380 });
    }
    buildLibraryRow();
}

void NativeRackCanvas::buildTopRow()
{
    // CORE
    modules.push_back ({ { 0, 0, 180, 380 }, "surge", "CORE", U ("TRANSPORT \xc2\xb7 PATCH I/O"), {}, false, false, mCore });
    {
        Ctl l; l.kind = Ctl::lcd; l.id = "status"; l.r = { 6, 44, 168, 46 }; l.accent = Colour (0xff3cff6e);
        l.text = [this] { return status; };
        add (std::move (l));
        ledButton ("power", { 16, 98, 22, 22 }, "POWER", Colour (0xff3cff6e), [] { return true; }, [this] { say (U ("ENGINE RUNS IN THE PLUGIN \xc2\xb7 ALWAYS ON")); });
        button ("stop", { 60, 100, 48, 18 }, U ("\xe2\x96\xa0 STOP"), "PANIC", [this] { proc.requestPanic(); say ("ALL VOICES STOPPED"); }, Colour (0xffe0342b));
        button ("kill", { 122, 100, 48, 18 }, U ("\xe2\x8f\xbb KILL"), "ENGINE", [this] { setParam ("run", 0); proc.requestPanic(); say (U ("SEQUENCER STOPPED \xc2\xb7 VOICES CLEARED")); });
        paramKnob ("tempo", 50, 168, 28, "orange", "TEMPO");
        Ctl bpm; bpm.kind = Ctl::lcd; bpm.id = "bpm"; bpm.r = { 112, 132, 46, 18 }; bpm.label = "BPM"; bpm.accent = amber;
        bpm.text = [this] { return proc.getEngine().usingHostClock.load() && proc.getEngine().seqRunning.load() ? String ("HOST") : String (roundToInt (paramValue ("tempo"))); };
        add (std::move (bpm));
        ledButton ("run", { 124, 176, 22, 22 }, "RUN SEQ", amber, [this] { return proc.getEngine().seqRunning.load(); }, [this] {
            const bool run = paramValue ("run") < 0.5f;
            if (run && paramValue ("seqMode") > 1.5f) setParam ("seqMode", 0);
            setParam ("run", run ? 1.0f : 0.0f);
            say (run ? U ("SEQUENCER RUNNING \xc2\xb7 DAW PLAY ALSO RUNS IT") : "SEQUENCER STOPPED");
        });
        button ("save", { 24, 222, 50, 18 }, U ("\xe2\x86\x93 SAVE"), "", [this] {
            chooser = std::make_unique<FileChooser> ("Save rack patch", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Continuum_Synth_8x8_Patch.json"), "*.json");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc) {
                const auto f = fc.getResult();
                String err;
                if (f != File()) say (proc.exportRackPatch (f.withFileExtension ("json"), err) ? "RACK PATCH SAVED" : U ("SAVE FAILED \xc2\xb7 ") + err.toUpperCase());
            });
        });
        button ("load", { 106, 222, 50, 18 }, U ("\xe2\x86\x91 LOAD"), "", [this] {
            chooser = std::make_unique<FileChooser> ("Load a Continuum Rack patch", File(), "*.json");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc) {
                const auto f = fc.getResult();
                String err;
                if (f != File()) say (proc.importRackPatch (f, err) ? U ("RACK PATCH LOADED \xc2\xb7 ") + f.getFileName().toUpperCase() : U ("LOAD FAILED \xc2\xb7 ") + err.toUpperCase());
            });
        });
        Ctl clock; clock.kind = Ctl::select; clock.id = "clock"; clock.r = { 30, 262, 120, 18 }; clock.label = "CLOCK"; clock.accent = amber;
        clock.text = [this] { const int m = roundToInt (paramValue ("seqMode")); return String (m == 0 ? "HOST" : m == 1 ? "INTERNAL" : "OFF"); };
        clock.step = [this] (int d) { setParam ("seqMode", (float) ((roundToInt (paramValue ("seqMode")) + d + 3) % 3)); };
        add (std::move (clock));
        Ctl notes; notes.kind = Ctl::select; notes.id = "midiGate"; notes.r = { 30, 304, 120, 18 }; notes.label = "MIDI NOTES"; notes.accent = amber;
        notes.text = [this] { return String (paramValue ("midiGate") > 0.5f ? "ONE-SHOT" : "GATED"); };
        notes.step = [this] (int) { setParam ("midiGate", paramValue ("midiGate") > 0.5f ? 0.0f : 1.0f); };
        add (std::move (notes));
    }
    // MIDI IN
    modules.push_back ({ { 180, 0, 120, 380 }, "white", U ("MIDI\xc2\xb7IN"), "HOST MIDI", Colour (0xff3695ef), true, false, mMidi });
    {
        Ctl dev; dev.kind = Ctl::lcd; dev.id = "midiDev"; dev.r = { 186, 54, 108, 20 }; dev.accent = Colour (0xff7ec8ff);
        dev.text = [] { return String ("HOST MIDI"); };
        add (std::move (dev));
        Ctl tgt; tgt.kind = Ctl::select; tgt.id = "midiTarget"; tgt.r = { 200, 82, 80, 18 }; tgt.label = "TARGET"; tgt.accent = Colour (0xff7ec8ff);
        tgt.text = [this] { const int t = roundToInt (paramValue ("midiTarget")); return t == 0 ? String ("ALL") : "BANK " + String (t); };
        tgt.step = [this] (int d) { setParam ("midiTarget", (float) ((roundToInt (paramValue ("midiTarget")) + d + 9) % 9)); };
        add (std::move (tgt));
        Ctl led; led.kind = Ctl::led; led.id = "midiLed"; led.r = { 236, 120, 8, 8 }; led.label = "NOTE ON"; led.ledColour = Colour (0xff3695ef);
        led.lit = [this] { return now() - midiHitTime < 200; };
        add (std::move (led));
        Ctl note; note.kind = Ctl::lcd; note.id = "midiNote"; note.r = { 217, 142, 46, 24 }; note.accent = Colour (0xff7ec8ff); note.small = false;
        note.text = [this] { return midiNote >= 0 ? noteName (midiNote) : U ("\xe2\x80\x94"); };
        add (std::move (note));
    }
    // MASTER
    modules.push_back ({ { 300, 0, 150, 380 }, "light", "MASTER", U ("DRIVE \xc2\xb7 OUTPUT"), {}, false, false, mMaster });
    paramKnob ("master", 348, 156, 28, "black", "LEVEL");
    { Ctl v; v.kind = Ctl::vu; v.id = "vuM"; v.r = { 412, 104, 16, 114 }; v.index = 8; add (std::move (v)); }
    paramKnob ("drive", 375, 298, 16, "black", "DRIVE");
    { Ctl clip; clip.kind = Ctl::ledButton; clip.id = "outputClip"; clip.r = { 413, 290, 14, 14 }; clip.ledColour = Colour (0xffff3b30);
      clip.lit = [this] { return paramValue ("outputClip") > 0.5f; };
      clip.click = [this] { setParam ("outputClip", paramValue ("outputClip") > 0.5f ? 0.0f : 1.0f); };
      clip.label = "CLIP";
      add (std::move (clip)); }
    // DELAY
    modules.push_back ({ { 450, 0, 120, 380 }, "black", "DELAY", "MASTER ECHO", {}, false, false, mDelay });
    paramKnob ("delayTime", 510, 150, 21, "orange", "TIME");
    paramKnob ("feedback", 510, 204, 16, "white", "FEEDBACK");
    paramKnob ("delayMix", 510, 251, 16, "white", "MIX");
    // LFO
    modules.push_back ({ { 570, 0, 150, 380 }, "dark", "LFO", "GLOBAL MODULATION", {}, false, false, mLfo });
    paramKnob ("lfoRate", 610, 64, 21, "teal", "RATE");
    { Ctl ph; ph.kind = Ctl::led; ph.id = "lfoLed"; ph.r = { 678, 60, 11, 11 }; ph.label = "PHASE"; ph.ledColour = Colour (0xff2cc8e7); add (std::move (ph)); }
    paramKnob ("lfoDepth", 645, 117, 16, "white", "DEPTH");
    select ("lfoWave", { 588, 150, 114, 20 }, "WAVE",
            [this] { static const char* n[] = { "SINE", "TRI", "SQR", "SAW" }; return String (n[jlimit (0, 3, roundToInt (paramValue ("lfoWave")))]); },
            [this] (int d) { setParam ("lfoWave", (float) ((roundToInt (paramValue ("lfoWave")) + d + 4) % 4)); });
    select ("lfoDest", { 588, 182, 114, 20 }, "DESTINATION",
            [this] { static const char* n[] = { "FILTER", "AMP", "PAN" }; return String (n[jlimit (0, 2, roundToInt (paramValue ("lfoDest")))]); },
            [this] (int d) { setParam ("lfoDest", (float) ((roundToInt (paramValue ("lfoDest")) + d + 3) % 3)); });
    {
        Ctl info; info.kind = Ctl::lcd; info.id = "lfoInfo"; info.r = { 575, 216, 140, 30 }; info.accent = Colour (0xff2cc8e7); info.small = true;
        info.text = [this] {
            static const char* n[] = { "FILTER", "AMP", "PAN" };
            String t = String (n[jlimit (0, 2, roundToInt (paramValue ("lfoDest")))]) + U (" \xe2\x96\xb8 ");
            const auto rt = crack::computeRouting (rs);
            bool patched = false;
            for (auto& c : rs.cables) patched |= c.from == "lfo";
            if (! patched) return t + "ALL BANKS\n" + U ("NORMALLED \xc2\xb7 UNPATCHED");
            for (int b = 0; b < 8; ++b) if (rt.lfoTarget[(size_t) b]) t << "B" << (b + 1) << " ";
            return t.trimEnd() + "\nPATCHED VIA MOD";
        };
        add (std::move (info));
        Ctl j; j.kind = Ctl::jack; j.id = "jack:lfo"; j.jackId = "lfo"; j.r = { 635, 255, 20, 20 }; j.label = "OUT"; j.small = true; add (std::move (j));
    }
    // SCOPE
    modules.push_back ({ { 720, 0, 300, 380 }, "dark", "SCOPE", "MASTER OSCILLOSCOPE", {}, false, false, mScope });
    { Ctl sc; sc.kind = Ctl::scope; sc.id = "scope"; sc.r = { 726, 44, 288, 262 }; add (std::move (sc)); }
    // OCTOSCOPE
    modules.push_back ({ { 1020, 0, 420, 380 }, "navy", "OCTOSCOPE", U ("8 ENGINE SCOPES \xc2\xb7 SELECTED BANK"), {}, false, false, mOcto });
    for (int b = 0; b < 8; ++b)
    {
        auto& c = ledButton ("octB" + String (b), { 1114.0f + b * 24, 44, 16, 16 }, "B" + String (b + 1), bankColour (b),
                             [this, b] { return octoBank == b; }, [this, b] { octoBank = b; octoFollow = false; proc.setScopeBank (b); repaint (1020, 0, 420, 380); });
        c.small = true;
    }
    {
        auto& f = ledButton ("octFollow", { 1320, 44, 16, 16 }, "FOLLOW", Colours::white, [this] { return octoFollow; },
                             [this] { octoFollow = ! octoFollow; if (octoFollow) { octoBank = focusBank; proc.setScopeBank (octoBank); } repaint (1020, 0, 420, 380); });
        f.small = true;
        Ctl o; o.kind = Ctl::octo; o.id = "octo"; o.r = { 1026, 77, 408, 232 }; add (std::move (o));
    }
    // ROUTER
    modules.push_back ({ { 1440, 0, 150, 380 }, "light", "ROUTER", "SYNTHESIS MATRIX", {}, false, false, mRouter });
    paramKnob ("sendLevel", 1515, 64, 21, "red", "SEND");
    {
        Ctl l; l.kind = Ctl::lcd; l.id = "routes"; l.r = { 1446, 102, 138, 150 }; l.accent = Colour (0xffff6a5c); l.small = true;
        l.text = [this] {
            String t;
            const int send = roundToInt (paramValue ("sendLevel"));
            for (auto& c : rs.cables)
                if (c.from.rfind ("bo:", 0) == 0) t << "B" << (c.from[3] - '0' + 1) << U (" \xe2\x96\xb8 B") << (c.to[3] - '0' + 1) << U (" \xc2\xb7 ") << send << "%\n";
            for (auto& c : rs.cables)
                if (c.from == "lfo") t << U ("LFO \xe2\x96\xb8 B") << (c.to[3] - '0' + 1) << " MOD\n";
            return t.isEmpty() ? String ("NO CABLES") : t.trimEnd();
        };
        add (std::move (l));
        button ("clearRoutes", { 1478, 257, 74, 18 }, "CLEAR SENDS", "", [this] {
            proc.editRack ([] (crack::RackState& st) { st.cables.erase (std::remove_if (st.cables.begin(), st.cables.end(), [] (const crack::Cable& c) { return c.from != "lfo"; }), st.cables.end()); });
            say (U ("AUDIO SENDS CLEARED \xc2\xb7 DIRECT PATHS RETAINED"));
        });
    }
    // SCENES
    modules.push_back ({ { 1590, 0, 150, 380 }, "black", "SCENES", "FULL-RACK PRESETS", {}, false, false, mScenes });
    {
        Ctl l; l.kind = Ctl::list; l.id = "scnList"; l.r = { 1596, 46, 138, 160 }; l.accent = Colour (0xffff5fd2);
        l.step = [this] (int d) { const int n = (int) proc.getScenes().size(); sceneTop = jlimit (0, jmax (0, n - 11), sceneTop + d); repaint (1590, 0, 150, 380); };
        add (std::move (l));
        Ctl info; info.kind = Ctl::lcd; info.id = "scnInfo"; info.r = { 1596, 210, 138, 66 }; info.accent = Colour (0xffff5fd2); info.small = true;
        info.text = [this] {
            const auto& sc = proc.getScenes();
            if (scenePick < 0 || scenePick >= (int) sc.size()) return String();
            return sc[(size_t) scenePick].tag + "\n" + sc[(size_t) scenePick].desc;
        };
        add (std::move (info));
        button ("scnLoad", { 1626, 281, 78, 18 }, U ("\xe2\x96\xb6 LOAD SCENE"), "", [this] { proc.setCurrentProgram (scenePick); say (U ("SCENE \xe2\x96\xb8 ") + proc.getPatchName().toUpperCase()); }, Colour (0xffff9f1a));
        button ("scnPrev", { 1620, 306, 18, 18 }, U ("\xe2\x97\x82"), "", [this] { const int n = (int) proc.getScenes().size(); scenePick = (scenePick + n - 1) % n; proc.setCurrentProgram (scenePick); });
        button ("scnNext", { 1692, 306, 18, 18 }, U ("\xe2\x96\xb8"), "", [this] { const int n = (int) proc.getScenes().size(); scenePick = (scenePick + 1) % n; proc.setCurrentProgram (scenePick); });
    }
}

void NativeRackCanvas::buildSeq (Rectangle<int> m)
{
    const float x = (float) m.getX(), y = (float) m.getY();
    ledButton ("seqRun", { x + 16, y + 52, 22, 22 }, "RUN", Colour (0xff3cff6e), [this] { return proc.getEngine().seqRunning.load(); }, [this] {
        const bool run = paramValue ("run") < 0.5f;
        if (run && paramValue ("seqMode") > 1.5f) setParam ("seqMode", 0);
        setParam ("run", run ? 1.0f : 0.0f);
    });
    paramKnob ("tempo", x + 68, y + 63, 15, "black", "TEMPO");
    { Ctl c; c.kind = Ctl::lcd; c.id = "seqBpm"; c.r = { x + 96, y + 56, 46, 18 }; c.label = "BPM"; c.accent = amber;
      c.text = [this] { return String (roundToInt (paramValue ("tempo"))); }; add (std::move (c)); }
    paramKnob ("root", x + 170, y + 63, 15, "black", "ROOT");
    { Ctl c; c.kind = Ctl::lcd; c.id = "seqNote"; c.r = { x + 198, y + 56, 46, 18 }; c.label = "NOTE"; c.accent = amber;
      c.text = [this] { return noteName (roundToInt (paramValue ("root"))); }; add (std::move (c)); }
    select ("seqLen", { x + 272, y + 56, 54, 18 }, "LENGTH", [this] { return String (rs.patterns[(size_t) editBank].length) + " STP"; }, [this] (int d) {
        static const int L[] = { 8, 16, 32, 64 };
        int i = 0;
        while (i < 3 && L[i] != rs.patterns[(size_t) editBank].length) ++i;
        const int len = L[(i + d + 4) % 4], b = editBank;
        proc.editRack ([b, len] (crack::RackState& st) { st.patterns[(size_t) b].length = len; });
        editStep = jmin (editStep, len - 1);
    });
    select ("seqArp", { x + 364, y + 56, 46, 18 }, "ARP", [this] {
        static const char* n[] = { "OFF", "UP", "DOWN", "UP/DN", "RAND" };
        return String (n[(int) rs.patterns[(size_t) editBank].arp]);
    }, [this] (int d) {
        const int b = editBank;
        proc.editRack ([b, d] (crack::RackState& st) { auto& p = st.patterns[(size_t) b]; p.arp = cycleEnum (p.arp, d, 5); });
    });
    button ("seqTest", { x + 434, y + 55, 46, 18 }, U ("\xe2\x9c\xa6 TEST"), "PATTERN", [this] {
        const int b = editBank;
        proc.editRack ([b] (crack::RackState& st) {
            auto& p = st.patterns[(size_t) b];
            static const float pitch[] = { 0, 0, 7, 12, 5, 3, 7, -5 }, vel[] = { 110, 78, 94, 70 }, filt[] = { 30, 60, 80, 45 };
            for (int i = 0; i < crack::maxSteps; ++i)
                p.steps[(size_t) i] = { i < p.length && i % 4 != 3, pitch[i % 8], vel[i % 4], 55, filt[i % 4] };
        });
        say (U ("TEST PATTERN LOADED \xc2\xb7 BANK ") + String (b + 1));
    });
    button ("seqClear", { x + 494, y + 55, 44, 18 }, "CLEAR", "BANK / LANE", [this] {
        const int b = editBank;
        proc.editRack ([b] (crack::RackState& st) { for (auto& s : st.patterns[(size_t) b].steps) s.on = false; });
        say (U ("BANK ") + String (b + 1) + " PATTERN CLEARED");
    });
    { Ctl c; c.kind = Ctl::lcd; c.id = "seqLane"; c.r = { x + 876, y + 56, 54, 18 }; c.label = "STEP LANE"; c.accent = amber;
      c.text = [] { return String ("ALL"); }; add (std::move (c)); }

    for (int b = 0; b < 8; ++b)
    {
        ledButton ("seqMute" + String (b), { x + 16, y + 134 + b * 19.0f, 13, 13 }, "", Colour (0xffff3b30),
                   [this, b] { return paramValue (ContinuumRackProcessor::bankParamID (b, "mute")) > 0.5f; },
                   [this, b] { const auto id = ContinuumRackProcessor::bankParamID (b, "mute"); setParam (id, paramValue (id) > 0.5f ? 0.0f : 1.0f); });
        auto& bb = button ("seqBank" + String (b), { x + 34, y + 133 + b * 19.0f, 28, 15 }, "B" + String (b + 1), "", [this, b] {
            editBank = b; editStep = jmin (editStep, rs.patterns[(size_t) b].length - 1);
            if (octoFollow) { octoBank = b; proc.setScopeBank (b); }
            rebuild(); repaint();
        }, bankColour (b));
        bb.bank = b;
    }
    { Ctl g; g.kind = Ctl::stepGrid; g.id = "seqGrid"; g.r = { x + 82, y + 118, 862, 168 }; add (std::move (g)); }

    { Ctl c; c.kind = Ctl::lcd; c.id = "seqSel"; c.r = { x + 16, y + 326, 118, 34 }; c.accent = Colour (0xff3cff6e);
      c.text = [this] { const auto& st = rs.patterns[(size_t) editBank].steps[(size_t) editStep];
          return "B" + String (editBank + 1) + U (" \xc2\xb7 STEP ") + two (editStep + 1) + "\n" + noteName (roundToInt (paramValue ("root") + st.pitch)) + U (" \xc2\xb7 ") + (st.on ? "ON" : "OFF"); };
      add (std::move (c)); }
    struct StepKnob { const char* label; float crack::Step::* f; float lo, hi, stp; const char* col; float cx; };
    const StepKnob sk[] = { { "PITCH", &crack::Step::pitch, -24, 24, 1, "red", 162 }, { "VEL", &crack::Step::velocity, 1, 127, 1, "black", 206 },
                            { "GATE", &crack::Step::gate, 5, 3200, 5, "black", 250 }, { "FILTER", &crack::Step::filter, 5, 100, 1, "black", 294 } };
    for (const auto& k : sk)
    {
        Ctl c; c.kind = Ctl::knob; c.id = String ("q:") + k.label; c.label = k.label; c.knobColour = k.col;
        c.r = { x + k.cx - 16, y + 320, 32, 32 };
        const auto f = k.f; const float lo = k.lo, hi = k.hi, stp = k.stp;
        c.get01 = [this, f, lo, hi] { return (rs.patterns[(size_t) editBank].steps[(size_t) editStep].*f - lo) / (hi - lo); };
        c.set01 = [this, f, lo, hi, stp] (float v) {
            const float val = jlimit (lo, hi, lo + std::round (v * (hi - lo) / stp) * stp);
            const int b = editBank, s = editStep;
            rs.patterns[(size_t) b].steps[(size_t) s].*f = val;
            proc.editRack ([b, s, f, val] (crack::RackState& st) { st.patterns[(size_t) b].steps[(size_t) s].*f = val; });
            serial = proc.getPatchSerial();
        };
        c.text = [this, f] { return String (rs.patterns[(size_t) editBank].steps[(size_t) editStep].*f, 0); };
        c.def01 = (crack::Step {}.*f - lo) / (hi - lo);
        add (std::move (c));
    }
    for (int s = 0; s < 8; ++s)
    {
        auto& t = ledButton ("tgt" + String (s), { x + 714 + s * 25.0f, y + 335, 16, 16 }, String (s + 1), amber,
                             [this, s] { return rs.engineSequence[(size_t) editBank].targets[(size_t) s]; }, [this, s] {
                                 const int b = editBank;
                                 proc.editRack ([b, s] (crack::RackState& st) { auto& on = st.engineSequence[(size_t) b].targets[(size_t) s]; on = ! on; });
                             });
        t.small = true;
    }
    button ("tgtAll", { x + 917, y + 332, 24, 13 }, "ALL", "", [this] { const int b = editBank; proc.editRack ([b] (crack::RackState& st) { st.engineSequence[(size_t) b].targets.fill (true); }); });
    button ("tgtNone", { x + 913, y + 349, 30, 13 }, "NONE", "", [this] { const int b = editBank; proc.editRack ([b] (crack::RackState& st) { st.engineSequence[(size_t) b].targets.fill (false); }); });
}

void NativeRackCanvas::buildEditor (Rectangle<int> m)
{
    const float x = (float) m.getX(), y = (float) m.getY();
    const int b = focusBank, s = focusSlot;
    const auto& sl = rs.banks[(size_t) b].slots[(size_t) s];
    { Ctl h; h.kind = Ctl::lcd; h.id = "edHead"; h.r = { x + 12, y + 44, 434, 20 }; h.accent = amber;
      h.text = [this] { const auto& q = rs.banks[(size_t) focusBank].slots[(size_t) focusSlot];
          return "BANK " + two (focusBank + 1) + U (" \xc2\xb7 ENGINE ") + two (focusSlot + 1) + U (" \xc2\xb7 ") + typeNames[q.type] + (q.on ? "" : U (" \xc2\xb7 OFF")); };
      add (std::move (h)); }
    select ("edType", { x + 466, y + 45, 66, 18 }, "", [this] { return String (crack::engineShortName (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].type)); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.type = (q.type + d + 8) % 8; }); rebuild(); repaint(); });
    button ("edPrev", { x + 556, y + 45, 40, 18 }, U ("\xe2\x97\x82PREV"), "", [this] { const int i = (focusBank * 8 + focusSlot + 63) % 64; setFocus (i / 8, i % 8); });
    button ("edNext", { x + 600, y + 45, 40, 18 }, U ("NEXT\xe2\x96\xb8"), "", [this] { const int i = (focusBank * 8 + focusSlot + 1) % 64; setFocus (i / 8, i % 8); });
    ledButton ("edOn", { x + 720, y + 45, 18, 18 }, "", Colour (0xff3cff6e), [this] { return rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].on; },
               [this, b, s] { editSlot (b, s, [] (crack::Slot& q) { q.on = ! q.on; }); });
    { Ctl t; t.kind = Ctl::button; t.id = "edTrig"; t.r = { x + 748, y + 45, 18, 18 }; t.small = true; t.text = [] { return String(); };
      t.click = [this] { playNote (48, 0.42, focusBank, focusSlot); }; add (std::move (t)); }

    using E = crack::EngineEdit;
    // OSCILLATORS
    select ("edW1", { x + 40, y + 101, 44, 18 }, "OSC 1", [this] { return String (waveShort (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wave1)); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.edit.wave1 = cycleEnum (q.edit.wave1, d, 4); }); });
    select ("edW2", { x + 130, y + 101, 44, 18 }, "OSC 2", [this] { return String (waveShort (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wave2)); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.edit.wave2 = cycleEnum (q.edit.wave2, d, 4); }); });
    editKnob (b, s, &E::mix, 0, 100, 1, x + 46, y + 166, 16, "white", "MIX");
    editKnob (b, s, &E::detune, -100, 100, 1, x + 106, y + 166, 16, "white", "DETUNE");
    editKnob (b, s, &E::transpose, -24, 24, 1, x + 166, y + 166, 16, "white", "PITCH");
    // FM · PHASE FOLD
    editKnob (b, s, &E::ratio, 0.25f, 12, 0.25f, x + 46, y + 292, 16, "orange", "RATIO");
    editKnob (b, s, &E::index, 0, 100, 1, x + 106, y + 292, 16, "orange", "INDEX");
    editKnob (b, s, &E::pd, 0, 100, 1, x + 166, y + 292, 16, "orange", "FOLD");
    // MULTIMODE FILTER
    select ("edFilt", { x + 254, y + 101, 44, 18 }, "MODE", [this] { return String (filterShort (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.filter)); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.edit.filter = cycleEnum (q.edit.filter, d, 5); }); });
    editKnob (b, s, &E::cutoff, 0, 100, 1, x + 276, y + 178, 28, "orange", "CUTOFF");
    editKnob (b, s, &E::resonance, 0, 100, 1, x + 242, y + 258, 16, "white", "RES");
    editKnob (b, s, &E::envDepth, -100, 100, 1, x + 308, y + 258, 16, "white", "ENV AMT");
    editKnob (b, s, &E::combFeedback, 0, 90, 1, x + 276, y + 320, 12, "white", "COMB FB");
    // VOICE LFO
    editKnob (b, s, &E::lfoRate, 0.05f, 20, 0.05f, x + 395, y + 118, 16, "teal", "RATE");
    editKnob (b, s, &E::lfoDepth, 0, 100, 1, x + 472, y + 118, 16, "teal", "DEPTH");
    select ("edLfoT", { x + 411, y + 166, 44, 18 }, "ROUTE", [this] {
        static const char* n[] = { "FILTER", "PITCH", "AMP" }; return String (n[(int) rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.lfoTarget]); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.edit.lfoTarget = cycleEnum (q.edit.lfoTarget, d, 3); }); });
    // AMP ENVELOPE (vertical sliders)
    struct Fader { float E::* f; const char* label; float cx; };
    const Fader adsr[] = { { &E::attack, "A", 375 }, { &E::decay, "D", 414 }, { &E::sustain, "S", 452 }, { &E::release, "R", 491 } };
    for (const auto& fd : adsr)
    {
        auto& k = editKnob (b, s, fd.f, 0, 100, 1, x + fd.cx, y + 292, 8, "white", fd.label);
        k.kind = Ctl::slider;
        k.r = { x + fd.cx - 8, y + 252, 16, 80 };
    }
    // SPECTRAL WAVETABLE
    select ("edWt", { x + 548, y + 101, 44, 18 }, "ENGAGE", [this] { return String (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wtEnabled ? "ON" : "OFF"); },
            [this, b, s] (int) { editSlot (b, s, [] (crack::Slot& q) { q.edit.wtEnabled = ! q.edit.wtEnabled; }); });
    editKnob (b, s, &E::wtMorph, 0, 100, 1, x + 640, y + 105, 12, "cyan", "MORPH");
    editKnob (b, s, &E::wtTilt, 0, 100, 1, x + 686, y + 105, 12, "cyan", "TILT");
    editKnob (b, s, &E::wtOdd, 0, 100, 1, x + 736, y + 105, 12, "cyan", "ODD/EVN");
    editKnob (b, s, &E::wtScan, 0, 100, 1, x + 666, y + 152, 12, "cyan", "SCAN");
    editKnob (b, s, &E::wtSweep, -100, 100, 1, x + 730, y + 152, 12, "cyan", "SWEEP");
    {
        auto& sel = select ("edTable", { x + 557, y + 148, 44, 18 }, "TABLE", [this] {
            const auto& t = rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wtTable;
            return t.empty() ? String ("8 PAR..") : String (t); },
            nullptr);   // opens as a menu (popup below)
        sel.popup = [this, b, s] (const MouseEvent&) {
            PopupMenu menu;
            menu.addItem (1, "8 PARTIALS (no table)");
            const auto& infos = proc.getWaveTableInfos();
            String group;
            PopupMenu sub;
            int id = 2;
            for (const auto& t : infos)
            {
                if (t.bank != group.toStdString())
                {
                    if (sub.getNumItems() > 0) menu.addSubMenu (group, sub);
                    sub = PopupMenu();
                    group = t.bank;
                }
                sub.addItem (id++, String (t.id) + U (" \xc2\xb7 ") + String (t.name));
            }
            if (sub.getNumItems() > 0) menu.addSubMenu (group, sub);
            menu.showMenuAsync (PopupMenu::Options(), [this, b, s] (int r) {
                if (r <= 0) return;
                const auto& infos2 = proc.getWaveTableInfos();
                const std::string id2 = r == 1 ? std::string() : infos2[(size_t) (r - 2)].id;
                editSlot (b, s, [id2] (crack::Slot& q) { q.edit.wtTable = id2; if (! id2.empty()) q.edit.wtEnabled = true; });
                repaint();
            });
        };
        Ctl dd; dd.kind = Ctl::lcd; dd.id = "edTableName"; dd.r = { x + 528, y + 190, 232, 18 }; dd.accent = Colour (0xff7ec8ff);
        dd.text = [this] {
            const auto& t = rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wtTable;
            if (t.empty()) return String ("8 PARTIALS (no table)");
            for (const auto& i : proc.getWaveTableInfos()) if (i.id == t) return String (i.id) + U (" \xc2\xb7 ") + String (i.name);
            return String (t);
        };
        add (std::move (dd));
    }
    for (int h = 0; h < 8; ++h)
    {
        Ctl c; c.kind = Ctl::slider; c.id = "wtH" + String (h + 1); c.label = "H" + String (h + 1); c.knobColour = "cyan";
        c.r = { x + 532 + h * 29.7f, y + 222, 16, 112 };
        c.get01 = [this, h] { return rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wtH[(size_t) h] / 100.0f; };
        c.set01 = [this, b, s, h] (float v) { const float val = std::round (v * 100); editSlot (b, s, [h, val] (crack::Slot& q) { q.edit.wtH[(size_t) h] = val; }); };
        c.text = [this, h] { return String (rs.banks[(size_t) focusBank].slots[(size_t) focusSlot].edit.wtH[(size_t) h], 0); };
        c.def01 = crack::EngineEdit {}.wtH[(size_t) h] / 100.0f;
        add (std::move (c));
    }
    juce::ignoreUnused (sl);
}

void NativeRackCanvas::buildStrip (int b, Rectangle<int> m)
{
    const float x = (float) m.getX(), y = (float) m.getY();
    modules.push_back ({ m, "dark", "BANK " + two (b + 1), U ("BUS \xc2\xb7 CHANNEL STRIP"), bankColour (b), true, false, mStrip });
    paramKnob (ContinuumRackProcessor::bankParamID (b, "level"), x + 60, y + 74, 22, "bank", "LEVEL", bankColour (b));
    { Ctl v; v.kind = Ctl::vu; v.id = "vu" + String (b); v.r = { x + 122, y + 84, 11, 96 }; v.index = b; add (std::move (v)); }
    paramKnob (ContinuumRackProcessor::bankParamID (b, "pan"), x + 60, y + 130, 16, "white", "PAN");
    paramKnob (ContinuumRackProcessor::bankParamID (b, "cutoff"), x + 60, y + 183, 16, "white", "FILTER");
    auto& ins = select ("ins" + String (b), { x + 18, y + 228, 114, 18 }, "", [this, b] { return String (crack::insertLabel (rs.banks[(size_t) b].insert)); },
                        [this, b] (int d) { proc.editRack ([b, d] (crack::RackState& st) { st.banks[(size_t) b].insert = cycleEnum (st.banks[(size_t) b].insert, d, 5); }); });
    ins.accent = amber;
    button ("play" + String (b), { x + 16, y + 250, 48, 18 }, U ("\xe2\x96\xb6 PLAY"), "BANK", [this, b] { playNote (48, 0.42, b, -1); });
    ledButton ("mute" + String (b), { x + 104, y + 251, 15, 15 }, "SEQ MUTE", Colour (0xffff3b30),
               [this, b] { return paramValue (ContinuumRackProcessor::bankParamID (b, "mute")) > 0.5f; },
               [this, b] { const auto id = ContinuumRackProcessor::bankParamID (b, "mute"); setParam (id, paramValue (id) > 0.5f ? 0.0f : 1.0f); });
    const char* ids[] = { "bi:", "bm:", "bo:" };
    const char* labels[] = { "IN", "MOD", "OUT" };
    const float xs[] = { 28, 75, 120 };
    for (int j = 0; j < 3; ++j)
    {
        Ctl c; c.kind = Ctl::jack; c.jackId = String (ids[j]) + String (b); c.id = "jack:" + c.jackId; c.label = labels[j];
        c.r = { x + xs[j] - 10, y + 290, 20, 20 }; c.small = j == 2; c.bank = b;
        add (std::move (c));
    }
}

void NativeRackCanvas::buildEngine (int b, int s, Rectangle<int> m)
{
    const auto& sl = rs.banks[(size_t) b].slots[(size_t) s];
    const int t = sl.type;
    const float x = (float) m.getX(), y = (float) m.getY();
    modules.push_back ({ m, engineSkin[t], crack::engineTitle (t), "B" + String (b + 1) + U (" \xc2\xb7 E") + String (s + 1), bankColour (b), true,
                         focusBank == b && focusSlot == s, mEngine });
    ledButton ("on" + String (b * 8 + s), { x + 14, y + 83, 18, 18 }, "ON", Colour (0xff3cff6e), [this, b, s] { return rs.banks[(size_t) b].slots[(size_t) s].on; },
               [this, b, s] { editSlot (b, s, [] (crack::Slot& q) { q.on = ! q.on; }); repaint(); });
    { Ctl g; g.kind = Ctl::led; g.id = "gate" + String (b * 8 + s); g.r = { x + 61, y + 87, 8, 8 }; g.label = "GATE"; g.ledColour = bankColour (b); g.index = b * 8 + s;
      add (std::move (g)); }
    select ("type" + String (b * 8 + s), { x + 20, y + 128, 50, 18 }, "", [t] { return String (crack::engineShortName (t)); },
            [this, b, s] (int d) { editSlot (b, s, [d] (crack::Slot& q) { q.type = (q.type + d + 8) % 8; }); rebuild(); repaint(); });
    const String small = skinFor (engineSkin[t]).dark ? "grey" : "black";
    using E = crack::EngineEdit;
    editKnob (b, s, &E::cutoff, 0, 100, 1, x + 45, y + 184, 22, engineKnob[t], "CUTOFF");
    editKnob (b, s, &E::resonance, 0, 100, 1, x + 25, y + 242, 11, small, "RES");
    editKnob (b, s, &E::envDepth, -100, 100, 1, x + 65, y + 242, 11, small, "ENV");
    editKnob (b, s, &E::attack, 0, 100, 1, x + 25, y + 288, 11, small, "ATK");
    editKnob (b, s, &E::release, 0, 100, 1, x + 65, y + 288, 11, small, "REL");
    { Ctl tr; tr.kind = Ctl::button; tr.id = "trig" + String (b * 8 + s); tr.r = { x + 16, y + 333, 18, 18 }; tr.small = true; tr.label = "TRIG";
      tr.text = [] { return String(); }; tr.click = [this, b, s] { playNote (48, 0.42, b, s); }; add (std::move (tr)); }
    ledButton ("edit" + String (b * 8 + s), { x + 57, y + 334, 16, 16 }, "EDIT", amber, [this, b, s] { return focusBank == b && focusSlot == s; }, [this, b, s] { setFocus (b, s); });
}

void NativeRackCanvas::buildLibraryRow()
{
    modules.push_back ({ { 0, 2280, 840, 380 }, "light", "KEYS", "4-OCTAVE PERFORMANCE KEYBOARD", {}, false, false, mKeys });
    select ("octave", { 30, 2452, 34, 18 }, "OCTAVE", [this] { return "C" + String (octave); }, [this] (int d) { octave = jlimit (0, 5, octave + d); repaint (0, 2280, 840, 380); });
    { Ctl k; k.kind = Ctl::keys; k.id = "keys"; k.r = { 16, 2487, 808, 155 }; add (std::move (k)); }

    modules.push_back ({ { 840, 2280, 300, 380 }, "navy", "ENGINE ATLAS", String ((int) proc.getEnginePatches().size()) + U (" ENGINE PATCHES \xc2\xb7 FACTORY"), {}, false, false, mAtlas });
    {
        Ctl l; l.kind = Ctl::list; l.id = "libList"; l.r = { 846, 2326, 288, 118 }; l.accent = Colour (0xff7ec8ff);
        l.step = [this] (int d) { libTop = jlimit (0, jmax (0, (int) proc.getEnginePatches().size() - 9), libTop + d); repaint (840, 2280, 300, 380); };
        add (std::move (l));
        select ("libBank", { 864, 2472, 104, 18 }, "TARGET BANK", [this] { return "BANK " + String (libBank + 1); }, [this] (int d) { libBank = (libBank + d + 8) % 8; repaint (840, 2280, 300, 380); });
        select ("libSlot", { 1010, 2472, 104, 18 }, "TARGET ENGINE", [this] { return "ENG " + String (libSlot + 1); }, [this] (int d) { libSlot = (libSlot + d + 8) % 8; repaint (840, 2280, 300, 380); });
        button ("libLoad", { 868, 2508, 46, 16 }, U ("\xe2\x96\xb6 LOAD"), "", [this] {
            const auto& pats = proc.getEnginePatches();
            if (libPick < 0 || libPick >= (int) pats.size()) return;
            const auto p = pats[(size_t) libPick];
            editSlot (libBank, libSlot, [p] (crack::Slot& q) { q.type = p.type; q.edit = p.edit; q.on = true; });
            rebuild(); repaint();
            say ("LOADED " + p.name.toUpperCase() + U (" \xe2\x96\xb8 B") + String (libBank + 1) + U ("\xc2\xb7") + "E" + String (libSlot + 1));
        }, Colour (0xffff9f1a));
        Ctl info; info.kind = Ctl::lcd; info.id = "libInfo"; info.r = { 846, 2536, 288, 116 }; info.accent = Colour (0xff7ec8ff); info.small = true;
        info.text = [this] {
            const auto& pats = proc.getEnginePatches();
            if (libPick < 0 || libPick >= (int) pats.size()) return String();
            const auto& p = pats[(size_t) libPick];
            return p.name.toUpperCase() + "\n" + p.group.toUpperCase() + "\nENGINE " + typeNames[p.type]
                 + "\nFILTER " + filterShort (p.edit.filter) + U (" \xc2\xb7 CUT ") + String (p.edit.cutoff, 0) + U (" \xc2\xb7 RES ") + String (p.edit.resonance, 0)
                 + "\nADSR " + String (p.edit.attack, 0) + "/" + String (p.edit.decay, 0) + "/" + String (p.edit.sustain, 0) + "/" + String (p.edit.release, 0)
                 + (p.edit.wtEnabled ? "\nWAVETABLE " + (p.edit.wtTable.empty() ? String ("8 PARTIALS") : String (p.edit.wtTable)) : String());
        };
        add (std::move (info));
    }
    modules.push_back ({ { 1140, 2280, 390, 380 }, "black", "FACTORY", U ("32 STARTING POINTS \xc2\xb7 8 FAMILIES"), {}, false, false, mFactory });
    { Ctl l; l.kind = Ctl::lcd; l.id = "facNote"; l.r = { 1146, 2326, 378, 168 }; l.accent = Colour (0xff7dffa1); l.small = true;
      l.text = [] { return String ("FACTORY RECIPES, PLAY / ARP / DRONE\nMODES AND THE RECIPE BUILDER LIVE\nIN THE WEB RACK VIEW.\n\nSWITCH WITH THE BUTTON BOTTOM-RIGHT;\nANYTHING YOU BUILD THERE PLAYS HERE."); };
      add (std::move (l)); }
    modules.push_back ({ { 1530, 2280, 210, 380 }, "maroon", "MUTATOR", "EVOLUTIONARY LAB", {}, false, false, mMutator });
    { Ctl l; l.kind = Ctl::lcd; l.id = "mutNote"; l.r = { 1536, 2326, 198, 90 }; l.accent = Colour (0xffffd36b); l.small = true;
      l.text = [] { return String ("DISCOVER, AUDITION AND\nVARIATIONS LIVE IN THE\nWEB RACK VIEW."); };
      add (std::move (l)); }
}

void NativeRackCanvas::setFocus (int b, int s)
{
    focusBank = b; focusSlot = s; libBank = b; libSlot = s;
    if (octoFollow) { octoBank = b; proc.setScopeBank (b); }
    rebuild();
    repaint();
}

// ═════════════════════════════════════════════════════════════ painting
void NativeRackCanvas::paint (Graphics& g)
{
    g.fillAll (Colour (0xff202020));
    const auto clip = g.getClipBounds();
    for (const auto& m : modules)
        if (m.r.intersects (clip)) paintModule (g, m);
    for (const auto& c : ctls)
        if (c.r.expanded (40, 24).toNearestInt().intersects (clip)) paintCtl (g, c);
    paintCables (g);

    // value tooltip for the knob being dragged or hovered
    const int show = dragCtl >= 0 ? dragCtl : hoverCtl;
    if (show >= 0 && show < (int) ctls.size())
    {
        const auto& c = ctls[(size_t) show];
        if ((c.kind == Ctl::knob || c.kind == Ctl::slider) && c.text)
        {
            const auto t = (c.label.isNotEmpty() ? c.label + " " : String()) + c.text();
            const float w = jmax (40.0f, GlyphArrangement::getStringWidth (mono (9), t) + 12);
            Rectangle<float> r (c.r.getCentreX() - w / 2, c.r.getY() - 20, w, 16);
            lcdBox (g, r);
            lcdText (g, r, t, amber, 9);
        }
    }
}

void NativeRackCanvas::paintModule (Graphics& g, const Module& m)
{
    const auto sk = skinFor (m.skin);
    const auto r = m.r.toFloat();
    if (m.skin == "silver")
    {
        ColourGradient grad (Colour (0xffb8bbbf), r.getX(), 0, Colour (0xffb6b9bd), r.getRight(), 0, false);
        grad.addColour (0.22, Colour (0xffdadde0)); grad.addColour (0.5, Colour (0xffc3c6ca)); grad.addColour (0.78, Colour (0xffdbdee1));
        g.setGradientFill (grad);
    }
    else
        g.setGradientFill (ColourGradient (sk.a, 0, r.getY(), sk.b, 0, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawRect (r, 1.0f);
    g.setColour (Colours::white.withAlpha (sk.dark ? 0.06f : 0.5f));
    g.drawLine (r.getX() + 1, r.getY() + 1, r.getRight() - 1, r.getY() + 1, 1);

    // screws
    for (auto p : { Point<float> (r.getX() + 8, r.getY() + 8), Point<float> (r.getRight() - 8, r.getY() + 8),
                    Point<float> (r.getX() + 8, r.getBottom() - 8), Point<float> (r.getRight() - 8, r.getBottom() - 8) })
    {
        if (m.r.getWidth() <= 90 && p.y < r.getY() + 20 && p.x < r.getCentreX()) continue;
        if (m.r.getWidth() <= 90 && p.y > r.getY() + 20 && p.x > r.getCentreX()) continue;
        g.setGradientFill (ColourGradient (Colour (0xfff6f6f6), p.x - 2, p.y - 2, Colour (0xff6a6d72), p.x + 4, p.y + 4, true));
        g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
        g.setColour (Colour (0xff3a3c40));
        g.drawLine (p.x - 3.5f, p.y + 1.5f, p.x + 3.5f, p.y - 1.5f, 1.4f);
    }

    // header
    const bool bankTitle = m.kind == mStrip;
    g.setColour (bankTitle ? m.bank : sk.ink);
    g.setFont (Font (FontOptions (m.kind == mEngine ? 13.0f : 12.5f, Font::bold)).withHorizontalScale (0.92f).withExtraKerningFactor (0.06f));
    g.drawText (m.title, Rectangle<float> (r.getX(), r.getY() + 13, r.getWidth(), 16), Justification::centred, false);
    g.setColour (sk.sub);
    g.setFont (sans (6.8f).withExtraKerningFactor (0.15f));
    g.drawText (m.sub, Rectangle<float> (r.getX() + 2, r.getY() + 31, r.getWidth() - 4, 9), Justification::centred, true);
    if (m.stripe)
    {
        g.setColour (m.bank);
        g.fillRect (r.getX() + 1, r.getY() + 43, r.getWidth() - 2, 3.0f);
    }
    if (m.kind == mMaster)
    {
        g.setColour (sk.sub);
        g.setFont (sans (7).withExtraKerningFactor (0.08f));
        g.drawText (U ("COMPRESSOR \xc2\xb7 LIMITER"), Rectangle<float> (r.getX(), r.getY() + 332, r.getWidth(), 9), Justification::centred, false);
        auto plate = Rectangle<float> (r.getCentreX() - 35, r.getY() + 344, 70, 18);
        g.setColour (Colour (0xff262626));
        g.fillRoundedRectangle (plate, 3);
        g.setColour (Colour (0xfff2f2f2));
        g.setFont (sans (7.5f));
        g.drawText (U ("AUDIO OUT \xe2\x96\xb8"), plate, Justification::centred, false);
    }
    if (m.kind == mEditor)
    {
        struct Sec { float x, y, w, h; const char* t; };
        const Sec secs[] = { { 12, 72, 188, 142, "OSCILLATORS" }, { 12, 222, 188, 140, "FM \xc2\xb7 PHASE FOLD" }, { 206, 72, 140, 290, "MULTIMODE FILTER" },
                             { 352, 72, 162, 142, "VOICE LFO" }, { 352, 222, 162, 140, "AMP ENVELOPE" },
                             { 520, 72, 248, 290, "SPECTRAL WAVETABLE \xc2\xb7 8 PARTIALS \xc2\xb7 MOSAIC TABLES" } };
        for (const auto& sc : secs)
        {
            const auto box = Rectangle<float> (r.getX() + sc.x, r.getY() + sc.y, sc.w, sc.h);
            g.setColour (Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (box, 5);
            g.setColour (sk.rule);
            g.drawRoundedRectangle (box, 5, 1);
            g.setColour (Colour (0xffff9000));
            g.setFont (sans (7).withExtraKerningFactor (0.1f));
            g.drawText (U (sc.t), box.reduced (8, 6).removeFromTop (9), Justification::left, true);
        }
    }
    if (m.focused)
    {
        g.setColour (amber);
        g.drawRect (r.reduced (0.5f), 2.0f);
    }
    g.setColour (sk.sub);
    g.setFont (sans (6.5f).withExtraKerningFactor (0.28f));
    g.drawText ("CONTINUUM", Rectangle<float> (r.getX(), r.getBottom() - 12, r.getWidth(), 8), Justification::centred, false);
}

void NativeRackCanvas::paintKnob (Graphics& g, const Ctl& c)
{
    const auto kc = knobColours (c.knobColour, c.accent);
    const auto r = c.r;
    const float rad = r.getWidth() * 0.5f;
    const auto ctr = r.getCentre();
    const float v = c.get01 ? c.get01() : 0;
    const float a0 = -MathConstants<float>::pi * 0.75f, a1 = MathConstants<float>::pi * 0.75f, ang = a0 + v * (a1 - a0);
    // tick marks for the larger knobs
    if (rad >= 18)
    {
        g.setColour (Colours::grey.withAlpha (0.8f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + i * (a1 - a0) / 10;
            const Point<float> p1 = ctr.getPointOnCircumference (rad + 3, a), p2 = ctr.getPointOnCircumference (rad + 6, a);
            g.drawLine ({ p1, p2 }, 1.0f);
        }
    }
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (r.translated (0, 1.5f).expanded (1));
    g.setColour (kc.lo);
    g.fillEllipse (r);
    g.setGradientFill (ColourGradient (kc.cc.brighter (0.25f), ctr.x - rad * 0.5f, ctr.y - rad * 0.6f, kc.cc.darker (0.15f), ctr.x + rad * 0.5f, ctr.y + rad * 0.7f, false));
    g.fillEllipse (r.reduced (rad * 0.12f));
    // knurled rim
    g.setColour (kc.lo.withAlpha (0.6f));
    for (int i = 0; i < 24; ++i)
    {
        const float a = i * MathConstants<float>::twoPi / 24;
        g.drawLine ({ ctr.getPointOnCircumference (rad * 0.88f, a), ctr.getPointOnCircumference (rad, a) }, 0.8f);
    }
    g.setColour (kc.ind);
    g.drawLine ({ ctr.getPointOnCircumference (rad * 0.25f, ang), ctr.getPointOnCircumference (rad * 0.82f, ang) }, jmax (1.6f, rad * 0.1f));
}

void NativeRackCanvas::paintCtl (Graphics& g, const Ctl& c)
{
    // the skin under this control decides label colours
    const Module* mod = nullptr;
    for (const auto& m : modules) if (m.r.toFloat().contains (c.r.getCentre())) { mod = &m; break; }
    const auto sk = skinFor (mod != nullptr ? mod->skin : "dark");
    auto label = [&] (float yOff = 9.0f) {
        if (c.label.isEmpty()) return;
        g.setColour (sk.ink);
        g.setFont (sans (7.0f).withExtraKerningFactor (0.08f));
        g.drawText (c.label, Rectangle<float> (c.r.getCentreX() - 40, c.r.getBottom() + yOff - 6, 80, 9), Justification::centred, false);
    };
    switch (c.kind)
    {
        case Ctl::knob:
            paintKnob (g, c);
            label (c.r.getWidth() >= 40 ? 12.0f : 9.0f);
            break;
        case Ctl::slider:
        {
            const auto tr = Rectangle<float> (c.r.getCentreX() - 2, c.r.getY(), 4, c.r.getHeight());
            g.setColour (Colour (0xff0b0b0c));
            g.fillRoundedRectangle (tr, 2);
            const float v = c.get01 ? jlimit (0.0f, 1.0f, c.get01()) : 0;
            const float hy = c.r.getBottom() - v * c.r.getHeight();
            auto handle = Rectangle<float> (c.r.getCentreX() - 8, hy - 5, 16, 10);
            const bool wt = c.knobColour == "cyan" && v >= 0.99f;
            g.setGradientFill (ColourGradient (Colour (0xfff4f4f4), 0, handle.getY(), Colour (0xffbdbdbd), 0, handle.getBottom(), false));
            g.fillRoundedRectangle (handle, 2);
            g.setColour (wt ? Colour (0xff2cc8e7) : Colour (0xff333333));
            g.fillRect (handle.reduced (2, 4));
            label (12.0f);
            break;
        }
        case Ctl::select:
        {
            lcdBox (g, c.r);
            lcdText (g, c.r, c.text ? c.text() : String(), c.accent, c.r.getHeight() > 19 ? 10.0f : 9.5f);
            g.setColour (sk.sub);
            g.setFont (sans (8));
            g.drawText (U ("\xe2\x97\x82"), Rectangle<float> (c.r.getX() - 12, c.r.getY(), 10, c.r.getHeight()), Justification::centred, false);
            g.drawText (U ("\xe2\x96\xb8"), Rectangle<float> (c.r.getRight() + 2, c.r.getY(), 10, c.r.getHeight()), Justification::centred, false);
            label (10.0f);
            break;
        }
        case Ctl::button:
        {
            if (c.small)   // round trigger button
            {
                g.setGradientFill (ColourGradient (Colours::white, c.r.getX(), c.r.getY(), Colour (0xffb4b4b4), c.r.getRight(), c.r.getBottom(), false));
                g.fillEllipse (c.r);
                g.setColour (Colour (0xff333333));
                g.drawEllipse (c.r, 1.0f);
                label (10.0f);
                break;
            }
            Colour bg = sk.dark ? Colour (0xff3a3d42) : Colour (0xfff4f4f2), fg = sk.dark ? Colours::white : Colour (0xff1b1b1b);
            if (! c.accent.isTransparent())
            {
                bg = c.accent;
                fg = c.accent.getPerceivedBrightness() > 0.6f ? Colour (0xff141414) : Colours::white;
            }
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (c.r.translated (0, 1), 3);
            g.setGradientFill (ColourGradient (bg.brighter (0.08f), 0, c.r.getY(), bg.darker (0.1f), 0, c.r.getBottom(), false));
            g.fillRoundedRectangle (c.r, 3);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.drawRoundedRectangle (c.r, 3, 1);
            g.setColour (fg);
            g.setFont (sans (c.r.getHeight() < 15 ? 6.5f : 7.6f));
            g.drawText (c.text ? c.text() : String(), c.r, Justification::centred, false);
            label (10.0f);
            break;
        }
        case Ctl::ledButton:
        case Ctl::led:
        {
            bool on = c.lit && c.lit();
            float level = on ? 1.0f : 0.0f;
            if (c.kind == Ctl::led && c.index >= 0)
            {
                const double age = now() - hitTime[(size_t) c.index];
                level = age >= 0 ? (float) std::exp (-age / 170.0) : 0;
                if (level < 0.02f) level = 0;
            }
            else if (c.id == "lfoLed")
            {
                const double ph = std::fmod (now() / 1000.0 * paramValue ("lfoRate"), 1.0);
                level = (float) ((std::sin (ph * MathConstants<double>::twoPi) + 1) / 2 * (0.25 + 0.75 * paramValue ("lfoDepth") / 100.0));
            }
            if (c.kind == Ctl::ledButton)
            {
                g.setGradientFill (ColourGradient (Colour (0xff3a3c40), 0, c.r.getY(), Colour (0xff141517), 0, c.r.getBottom(), false));
                g.fillRoundedRectangle (c.r, 3);
                g.setColour (Colours::black);
                g.drawRoundedRectangle (c.r, 3, 1);
            }
            const auto led = c.kind == Ctl::ledButton ? c.r.reduced (c.r.getWidth() * 0.24f) : c.r;
            g.setColour (c.ledColour.withMultipliedBrightness (0.25f).withAlpha (0.9f));
            g.fillRoundedRectangle (led, 1.5f);
            if (level > 0)
            {
                g.setColour (c.ledColour.withAlpha (0.35f * level));
                g.fillRoundedRectangle (led.expanded (3), 3);
                g.setColour (c.ledColour.withAlpha (level));
                g.fillRoundedRectangle (led, 1.5f);
            }
            if (c.small && c.kind == Ctl::ledButton && c.label.isNotEmpty())
            {
                g.setColour (sk.ink);
                g.setFont (sans (6.5f));
                g.drawText (c.label, Rectangle<float> (c.r.getCentreX() - 20, c.r.getBottom() + 1, 40, 8), Justification::centred, false);
            }
            else
                label (c.kind == Ctl::led ? 12.0f : 10.0f);
            break;
        }
        case Ctl::jack:
        {
            const auto ctr = c.r.getCentre();
            if (c.small)   // OUT jacks sit on a white plate
            {
                g.setColour (Colour (0xffe8e8e8));
                g.fillRoundedRectangle (c.r.expanded (5, 5).withHeight (c.r.getHeight() + 20), 3);
            }
            Path hex;
            for (int i = 0; i < 6; ++i)
            {
                const auto p = ctr.getPointOnCircumference (10.5f, i * MathConstants<float>::pi / 3);
                if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
            }
            hex.closeSubPath();
            g.setGradientFill (ColourGradient (Colour (0xffe9eaec), ctr.x - 6, ctr.y - 6, Colour (0xff8c8f94), ctr.x + 8, ctr.y + 8, false));
            g.fillPath (hex);
            g.setColour (Colour (0xff4a4c50));
            g.strokePath (hex, PathStrokeType (0.8f));
            g.setColour (Colour (0xff050505));
            g.fillEllipse (ctr.x - 4.5f, ctr.y - 4.5f, 9, 9);
            g.setColour (c.small ? Colour (0xff141414) : sk.ink);
            g.setFont (sans (7));
            g.drawText (c.label, Rectangle<float> (ctr.x - 20, c.r.getBottom() + 3, 40, 9), Justification::centred, false);
            break;
        }
        case Ctl::lcd:
        {
            lcdBox (g, c.r);
            const auto txt = c.text ? c.text() : String();
            if (txt.containsChar ('\n') || c.small)
            {
                AttributedString as;
                as.setWordWrap (AttributedString::byWord);
                as.append (txt, mono (c.small ? 8.0f : 9.5f), c.accent);
                as.setLineSpacing (1.5f);
                TextLayout tl;
                const auto area = c.r.reduced (5, 4);
                tl.createLayout (as, area.getWidth());
                g.saveState();
                g.reduceClipRegion (area.toNearestInt());
                tl.draw (g, area.withHeight (jmax (area.getHeight(), tl.getHeight())));
                g.restoreState();
            }
            else
            {
                const float fs = c.id == "midiNote" ? 14.0f : c.id == "status" ? 9.5f : 9.5f;
                if (c.id == "status")
                {
                    g.setFont (mono (9.5f));
                    g.setColour (c.accent);
                    g.drawFittedText (txt, c.r.reduced (5, 4).toNearestInt(), Justification::topLeft, 3);
                }
                else
                    lcdText (g, c.r, txt, c.accent, fs);
            }
            label (10.0f);
            break;
        }
        case Ctl::list:
        {
            lcdBox (g, c.r);
            const float lh = 14.5f;
            if (c.id == "scnList")
            {
                const auto& sc = proc.getScenes();
                for (int i = sceneTop, row = 0; i < (int) sc.size() && row < 11; ++i, ++row)
                {
                    auto rr = Rectangle<float> (c.r.getX() + 2, c.r.getY() + 3 + row * lh, c.r.getWidth() - 4, lh);
                    if (i == scenePick) { g.setColour (c.accent); g.fillRect (rr); }
                    const bool cur = i == proc.getSceneIndex();
                    lcdText (g, rr.withTrimmedLeft (3), (cur ? U ("\xe2\x96\xb6 ") : U ("\xc2\xb7 ")) + sc[(size_t) i].name,
                             i == scenePick ? Colour (0xff111111) : c.accent, 9.0f, Justification::centredLeft);
                }
            }
            else
            {
                const auto& pats = proc.getEnginePatches();
                String group;
                if (libTop > 0 && libTop - 1 < (int) pats.size()) group = pats[(size_t) libTop - 1].group;
                float yy = c.r.getY() + 3;
                for (int i = libTop; i < (int) pats.size() && yy + lh <= c.r.getBottom(); ++i)
                {
                    if (pats[(size_t) i].group != group)
                    {
                        group = pats[(size_t) i].group;
                        lcdText (g, { c.r.getX() + 5, yy, c.r.getWidth() - 8, lh - 2 }, group.toUpperCase(), c.accent.withAlpha (0.5f), 7.0f, Justification::centredLeft);
                        yy += lh - 2;
                        if (yy + lh > c.r.getBottom()) break;
                    }
                    auto rr = Rectangle<float> (c.r.getX() + 2, yy, c.r.getWidth() - 4, lh);
                    if (i == libPick) { g.setColour (c.accent); g.fillRect (rr); }
                    lcdText (g, rr.withTrimmedLeft (3), U ("\xe2\x97\x87 ") + pats[(size_t) i].name, i == libPick ? Colour (0xff111111) : c.accent, 9.0f, Justification::centredLeft);
                    yy += lh;
                }
            }
            break;
        }
        case Ctl::vu:
        {
            g.setColour (Colour (0xff0b0c0a));
            g.fillRoundedRectangle (c.r.expanded (2), 2);
            const int n = c.index == 8 ? 12 : 10;
            const float db = Decibels::gainToDecibels (vu[(size_t) c.index] + 1e-9f);
            const int lit = jlimit (0, n, roundToInt ((db + 50) / 50 * n));
            const float sh = c.r.getHeight() / n;
            for (int i = 0; i < n; ++i)
            {
                const auto seg = Rectangle<float> (c.r.getX(), c.r.getBottom() - (i + 1) * sh + 1, c.r.getWidth(), sh - 2);
                const int ci = (int) std::round (i * (12.0 / n));
                const Colour col = ci >= 8 ? Colour (0xffff3b30) : ci >= 6 ? Colour (0xffffd23c) : Colour (0xff3cff6e);
                g.setColour (i < lit ? col : col.withAlpha (0.1f));
                g.fillRoundedRectangle (seg, 1);
            }
            break;
        }
        case Ctl::scope:
        {
            g.setColour (Colour (0xff030303));
            g.fillRect (c.r);
            g.setColour (Colour (0xff1a1a1a));
            for (int i = 1; i < 10; ++i) g.drawVerticalLine ((int) (c.r.getX() + i * c.r.getWidth() / 10), c.r.getY(), c.r.getBottom());
            for (int i = 1; i < 8; ++i) g.drawHorizontalLine ((int) (c.r.getY() + i * c.r.getHeight() / 8), c.r.getX(), c.r.getRight());
            paintScope (g, c.r, masterWave.data(), (int) masterWave.size(), amber, 1.1f);
            g.setColour (skinFor ("dark").sub);
            g.setFont (sans (7));
            g.drawText (U ("MASTER BUS \xc2\xb7 TRIGGERED"), Rectangle<float> (c.r.getX(), c.r.getBottom() + 6, 180, 9), Justification::left, false);
            g.drawText ("1024 SMP", Rectangle<float> (c.r.getRight() - 80, c.r.getBottom() + 6, 80, 9), Justification::right, false);
            break;
        }
        case Ctl::octo:
        {
            const auto col = bankColour (octoBank);
            const auto& bk = rs.banks[(size_t) octoBank];
            int on = 0;
            for (int i = 0; i < 8; ++i)
            {
                const auto cell = Rectangle<float> (c.r.getX() + (i % 4) * 104, c.r.getY() + (i / 4) * 120, 98, 104);
                g.setColour (Colour (0xff02050b));
                g.fillRect (cell);
                g.setColour (Colour (0xff15213b));
                for (int x = 1; x < 4; ++x) g.drawVerticalLine ((int) (cell.getX() + x * cell.getWidth() / 4), cell.getY(), cell.getBottom());
                g.drawHorizontalLine ((int) cell.getCentreY(), cell.getX(), cell.getRight());
                const float* w = octoWave.data() + i * 512;
                float pk = 0; for (int k = 0; k < 512; ++k) pk = jmax (pk, std::abs (w[k]));
                paintScope (g, cell, w, 512, col, 0.42f / jmax (pk, 0.015f));
                const auto& sl = bk.slots[(size_t) i];
                on += sl.on ? 1 : 0;
                if (! sl.on)
                {
                    g.setColour (Colour (0xff02050b).withAlpha (0.55f));
                    g.fillRect (cell);
                    g.setColour (Colour (0xff5b6a8c));
                    g.setFont (mono (9));
                    g.drawText ("ENGINE OFF", cell.withTrimmedBottom (12), Justification::centred, false);
                }
                g.setColour (sl.on ? Colour (0xffe8eefc) : Colour (0xff5b6a8c));
                g.setFont (sans (7));
                g.drawText ("E" + two (i + 1) + U (" \xc2\xb7 ") + (sl.on ? String (crack::engineShortName (sl.type)) : String ("OFF")),
                            Rectangle<float> (cell.getX(), cell.getBottom() + 3, cell.getWidth(), 9), Justification::left, false);
            }
            g.setColour (skinFor ("navy").sub);
            g.setFont (sans (7));
            g.drawText ("BANK " + two (octoBank + 1) + U (" \xc2\xb7 ") + String (on) + "/8 ON" + (octoFollow ? U (" \xc2\xb7 FOLLOWING EDITS") : String()),
                        Rectangle<float> (c.r.getX(), c.r.getBottom() + 6, c.r.getWidth(), 9), Justification::centred, false);
            break;
        }
        case Ctl::stepGrid: paintStepGrid (g, c); break;
        case Ctl::keys: paintKeys (g, c); break;
        case Ctl::targetToggle: break;
    }
}

void NativeRackCanvas::paintScope (Graphics& g, Rectangle<float> r, const float* d, int n, Colour col, float gain)
{
    // trigger on a rising zero crossing in the first half so the trace stands still
    int start = 0;
    for (int i = 1; i < n / 2; ++i) if (d[i - 1] <= 0 && d[i] > 0) { start = i; break; }
    const int len = n / 2;
    Path p;
    for (int i = 0; i < len; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / (len - 1);
        const float y = r.getCentreY() - jlimit (-1.0f, 1.0f, d[start + i] * gain) * r.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col.withAlpha (0.25f));
    g.strokePath (p, PathStrokeType (4.0f));
    g.setColour (col);
    g.strokePath (p, PathStrokeType (1.4f));
}

void NativeRackCanvas::paintStepGrid (Graphics& g, const Ctl& c)
{
    int maxLen = 16;
    for (auto& p : rs.patterns) maxLen = jmax (maxLen, p.length);
    const float cw = c.r.getWidth() / maxLen, top = c.r.getY() + 15, lh = 19;
    g.setFont (sans (6.5f));
    for (int i = 0; i < maxLen; ++i)
    {
        g.setColour (Colour (0xff333333));
        if (maxLen <= 32 || i % 2 == 0)
            g.drawText (String (i + 1), Rectangle<float> (c.r.getX() + i * cw, c.r.getY(), cw, 10), Justification::centred, false);
    }
    for (int b = 0; b < 8; ++b)
    {
        const auto& p = rs.patterns[(size_t) b];
        const bool muted = paramValue (ContinuumRackProcessor::bankParamID (b, "mute")) > 0.5f;
        const int play = lastStep[(size_t) b];
        for (int i = 0; i < maxLen; ++i)
        {
            auto cell = Rectangle<float> (c.r.getX() + i * cw + 1, top + b * lh, cw - 2, lh - 2);
            const bool inLen = i < p.length;
            Colour col = inLen ? ((i / 4) % 2 ? Colour (0xff222222) : Colour (0xff2b2b2b)) : Colour (0xffc9c9c9);
            if (inLen && p.steps[(size_t) i].on) col = muted ? bankColour (b).withSaturation (0.15f) : bankColour (b);
            g.setColour (col);
            g.fillRoundedRectangle (cell, 2);
            if (b == editBank && i == editStep)
            {
                g.setColour (Colour (0xff111111));
                g.drawRoundedRectangle (cell.expanded (1), 2, 2.0f);
                g.setColour (amber);
                g.drawRoundedRectangle (cell, 2, 1.2f);
            }
            if (play >= 0 && i == play)
            {
                g.setColour (Colours::white.withAlpha (0.85f));
                g.drawRoundedRectangle (cell, 2, 1.5f);
            }
        }
    }
}

void NativeRackCanvas::paintKeys (Graphics& g, const Ctl& c)
{
    const int whites = 29;
    const float ww = c.r.getWidth() / whites;
    static const int whiteSemis[] = { 0, 2, 4, 5, 7, 9, 11 };
    const int base = 12 * (octave + 1);
    for (int i = 0; i < whites; ++i)
    {
        auto k = Rectangle<float> (c.r.getX() + i * ww, c.r.getY(), ww - 1, c.r.getHeight());
        const int midi = base + (i / 7) * 12 + whiteSemis[i % 7];
        g.setGradientFill (ColourGradient (Colours::white, 0, k.getY(), midi == heldKey ? Colour (0xffc8c8c8) : Colour (0xffe9e9e9), 0, k.getBottom(), false));
        g.fillRect (k);
        g.setColour (Colour (0xff8a8a8a));
        g.drawRect (k, 0.8f);
        if (i % 7 == 0)
        {
            g.setColour (Colour (0xff777777));
            g.setFont (sans (6.5f));
            g.drawText ("C" + String (octave + i / 7), k.removeFromBottom (12), Justification::centred, false);
        }
    }
    static const int blackAfter[] = { 0, 1, 3, 4, 5 };
    static const int blackSemis[] = { 1, 3, 6, 8, 10 };
    for (int o = 0; o < 4; ++o)
        for (int j = 0; j < 5; ++j)
        {
            const float x = c.r.getX() + (o * 7 + blackAfter[j] + 1) * ww - ww * 0.3f;
            auto k = Rectangle<float> (x, c.r.getY(), ww * 0.6f, c.r.getHeight() * 0.62f);
            const int midi = base + o * 12 + blackSemis[j];
            g.setGradientFill (ColourGradient (Colour (0xff3a3a3a), 0, k.getY(), midi == heldKey ? Colour (0xff555555) : Colour (0xff050505), 0, k.getBottom(), false));
            g.fillRoundedRectangle (k, 2);
        }
    g.setColour (skinFor ("light").sub);
    g.setFont (sans (7.5f, false));
    g.drawText ("Click or drag across the keys for glissando. Plays every enabled engine through its bank bus.",
                Rectangle<float> (c.r.getX() + 78, c.r.getY() - 30, 600, 12), Justification::left, false);
}

Point<float> NativeRackCanvas::jackPos (const String& id) const
{
    for (const auto& c : ctls)
        if (c.kind == Ctl::jack && c.jackId == id) return c.r.getCentre();
    return {};
}

void NativeRackCanvas::paintCables (Graphics& g)
{
    auto drawCable = [&g] (Point<float> a, Point<float> b, Colour col) {
        const float dist = a.getDistanceFrom (b);
        const float sag = 30.0f + dist * 0.25f;
        Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.translated (0, sag), b.translated (0, sag), b);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.strokePath (p, PathStrokeType (6.5f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (1.5f, 2.5f));
        g.setColour (col.darker (0.4f));
        g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.85f));
        g.strokePath (p, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        for (auto e : { a, b })
        {
            g.setColour (col.darker (0.3f));
            g.fillEllipse (e.x - 7, e.y - 7, 14, 14);
            g.setColour (col.brighter (0.3f));
            g.drawEllipse (e.x - 7, e.y - 7, 14, 14, 1.5f);
        }
    };
    for (const auto& c : rs.cables)
    {
        const auto a = jackPos (c.from), b = jackPos (c.to);
        if (a.isOrigin() || b.isOrigin()) continue;
        drawCable (a, b, colourFromHex (c.color).withAlpha (0.9f));
    }
    if (cableFrom.isNotEmpty())
        drawCable (jackPos (cableFrom), cableTo, cableFrom == "lfo" ? Colour (0xff00b56e) : Colour (0xff3695ef));
}

// ═════════════════════════════════════════════════════════════ live updates
void NativeRackCanvas::timerCallback()
{
    if (proc.getPatchSerial() != serial) refreshState();
    const auto& eng = proc.getEngine();

    // engine gate LEDs
    const double t = now();
    for (int b = 0; b < 8; ++b)
        for (int s = 0; s < 8; ++s)
        {
            const auto h = eng.slotHits[(size_t) b][(size_t) s].load();
            if (h != lastHits[(size_t) (b * 8 + s)]) { lastHits[(size_t) (b * 8 + s)] = h; hitTime[(size_t) (b * 8 + s)] = t; }
        }
    // meters
    for (int b = 0; b < 8; ++b) vu[(size_t) b] = jmax (eng.bankPeak[(size_t) b].load() * 0.6f * 2.2f, vu[(size_t) b] * 0.86f);
    vu[8] = jmax (eng.masterPeak.load() * 0.6f * 1.6f, vu[8] * 0.86f);
    // scopes
    eng.scopes.readMaster (masterWave.data(), (int) masterWave.size());
    for (int s = 0; s < 8; ++s) eng.scopes.readSlot (s, octoWave.data() + s * 512, 512);
    // playhead
    bool stepMoved = false;
    for (int b = 0; b < 8; ++b)
    {
        const int st = eng.playStep[(size_t) b].load();
        if (st != lastStep[(size_t) b]) { lastStep[(size_t) b] = st; stepMoved = true; }
    }
    // MIDI
    const auto mc = eng.midiNoteCount.load();
    if (mc != lastMidiCount) { lastMidiCount = mc; midiHitTime = t; midiNote = eng.lastMidiNote.load(); }

    // repaint only what changed
    if (lastVal.size() != ctls.size()) { lastVal.assign (ctls.size(), -1.0f); lastText.assign (ctls.size(), {}); }
    for (size_t i = 0; i < ctls.size(); ++i)
    {
        const auto& c = ctls[i];
        bool dirty = false;
        switch (c.kind)
        {
            case Ctl::vu: case Ctl::scope: case Ctl::octo: dirty = true; break;
            case Ctl::stepGrid: dirty = stepMoved; break;
            case Ctl::led:
            {
                float lv = c.lit && c.lit() ? 1.0f : 0.0f;
                if (c.index >= 0) lv = (float) std::exp (-(t - hitTime[(size_t) c.index]) / 170.0);
                if (c.id == "lfoLed") lv = (float) t;   // always moving
                dirty = std::abs (lv - lastVal[i]) > 0.02f;
                if (dirty) lastVal[i] = lv;
                break;
            }
            case Ctl::knob: case Ctl::slider: case Ctl::ledButton:
            {
                const float v = c.kind == Ctl::ledButton ? (c.lit && c.lit() ? 1.0f : 0.0f) : (c.get01 ? c.get01() : 0.0f);
                dirty = std::abs (v - lastVal[i]) > 1e-4f;
                if (dirty) lastVal[i] = v;
                break;
            }
            case Ctl::lcd: case Ctl::select:
            {
                const auto txt = c.text ? c.text() : String();
                dirty = txt != lastText[i];
                if (dirty) lastText[i] = txt;
                break;
            }
            default: break;
        }
        if (dirty) repaint (c.r.expanded (c.kind == Ctl::knob ? 8.0f : 4.0f).toNearestInt());
    }
}

// ═════════════════════════════════════════════════════════════ interaction
int NativeRackCanvas::hit (Point<float> p) const
{
    for (int i = (int) ctls.size() - 1; i >= 0; --i)
    {
        const auto& c = ctls[(size_t) i];
        auto r = c.r;
        if (c.kind == Ctl::select) r = r.expanded (12, 2);
        if (c.kind == Ctl::knob) r = r.expanded (3);
        if (c.kind == Ctl::jack) r = r.expanded (4);
        if (c.kind == Ctl::slider) r = r.expanded (6, 6);
        if (r.contains (p)) return i;
    }
    return -1;
}

int NativeRackCanvas::keyAt (Point<float> p, const Ctl& c) const
{
    const int whites = 29;
    const float ww = c.r.getWidth() / whites;
    const int base = 12 * (octave + 1);
    static const int blackAfter[] = { 0, 1, 3, 4, 5 };
    static const int blackSemis[] = { 1, 3, 6, 8, 10 };
    static const int whiteSemis[] = { 0, 2, 4, 5, 7, 9, 11 };
    if (p.y < c.r.getY() + c.r.getHeight() * 0.62f)
        for (int o = 0; o < 4; ++o)
            for (int j = 0; j < 5; ++j)
            {
                const float x = c.r.getX() + (o * 7 + blackAfter[j] + 1) * ww - ww * 0.3f;
                if (p.x >= x && p.x < x + ww * 0.6f) return base + o * 12 + blackSemis[j];
            }
    const int i = jlimit (0, whites - 1, (int) ((p.x - c.r.getX()) / ww));
    return base + (i / 7) * 12 + whiteSemis[i % 7];
}

void NativeRackCanvas::stepAt (const Ctl& c, Point<float> p, int& lane, int& step) const
{
    int maxLen = 16;
    for (auto& pt : rs.patterns) maxLen = jmax (maxLen, pt.length);
    lane = (int) ((p.y - (c.r.getY() + 15)) / 19);
    step = (int) ((p.x - c.r.getX()) / (c.r.getWidth() / maxLen));
}

bool NativeRackCanvas::connect (const String& fromJack, const String& toJack)
{
    const auto from = fromJack.toStdString(), to = toJack.toStdString();
    const auto before = proc.getRackState().cables;
    const bool lfo = from == "lfo";
    const std::string colour = lfo ? "#00b56e" : cableColours[before.size() % 4];
    proc.editRack ([from, to, colour] (crack::RackState& st) { st.cables.push_back ({ from, to, colour }); });
    const auto after = proc.getRackState().cables;
    bool made = false;
    for (const auto& c : after) made |= c.from == from && c.to == to;
    if (made)
        say (lfo ? U ("LFO \xe2\x96\xb8 ") + toJack.toUpperCase() + " MOD" : fromJack.toUpperCase() + U (" \xe2\x96\xb8 ") + toJack.toUpperCase());
    else
        say (U ("THAT PATCH IS NOT ALLOWED \xc2\xb7 SENDS GO FORWARD TO A LATER BANK IN"));
    refreshState();
    return made;
}

bool NativeRackCanvas::tapStep (int lane, int step)
{
    if (lane < 0 || lane > 7 || step < 0 || step >= rs.patterns[(size_t) lane].length) return false;
    editBank = lane;
    editStep = step;
    proc.editRack ([lane, step] (crack::RackState& st) { auto& s = st.patterns[(size_t) lane].steps[(size_t) step]; s.on = ! s.on; });
    rebuild();
    repaint();
    return true;
}

void NativeRackCanvas::mouseMove (const MouseEvent& e)
{
    const int h = hit (e.position);
    const int k = (h >= 0 && (ctls[(size_t) h].kind == Ctl::knob || ctls[(size_t) h].kind == Ctl::slider)) ? h : -1;
    if (k != hoverCtl)
    {
        for (int i : { hoverCtl, k })
            if (i >= 0) repaint (ctls[(size_t) i].r.expanded (60, 26).toNearestInt());
        hoverCtl = k;
    }
}

void NativeRackCanvas::mouseDown (const MouseEvent& e)
{
    const int i = hit (e.position);
    dragCtl = -1;
    if (i < 0) return;
    auto& c = ctls[(size_t) i];
    if (e.mods.isPopupMenu())
    {
        if (c.kind == Ctl::jack)
        {
            const auto j = c.jackId.toStdString();
            proc.editRack ([j] (crack::RackState& st) {
                st.cables.erase (std::remove_if (st.cables.begin(), st.cables.end(), [&j] (const crack::Cable& cb) { return cb.from == j || cb.to == j; }), st.cables.end());
            });
            say ("CABLES CLEARED AT " + c.jackId.toUpperCase());
        }
        else if (c.popup) c.popup (e);
        return;
    }
    switch (c.kind)
    {
        case Ctl::knob:
        case Ctl::slider:
            dragCtl = i;
            dragStart01 = c.get01 ? c.get01() : 0;
            dragFrom = e.position;
            if (c.begin) c.begin();
            if (c.kind == Ctl::slider && c.set01)   // faders jump to the click
            {
                c.set01 (jlimit (0.0f, 1.0f, (c.r.getBottom() - e.position.y) / c.r.getHeight()));
                dragStart01 = c.get01();
            }
            break;
        case Ctl::select:
            if (c.popup) { c.popup (e); break; }
            if (c.step) c.step (e.position.x < c.r.getX() ? -1 : 1);
            repaint (c.r.expanded (20, 4).toNearestInt());
            break;
        case Ctl::button:
        case Ctl::ledButton:
            if (c.click) { const auto fn = c.click; fn(); }
            repaint();
            break;
        case Ctl::jack:
        {
            if (c.jackId.startsWith ("bo:") || c.jackId == "lfo")
            {
                cableFrom = c.jackId;
                cableTo = e.position;
            }
            else
            {
                // pull an existing plug off this input and carry it
                for (const auto& cb : rs.cables)
                    if (cb.to == c.jackId.toStdString())
                    {
                        cableFrom = cb.from;
                        cableTo = e.position;
                        const auto to = cb.to;
                        proc.editRack ([to] (crack::RackState& st) {
                            st.cables.erase (std::remove_if (st.cables.begin(), st.cables.end(), [&to] (const crack::Cable& x) { return x.to == to; }), st.cables.end());
                        });
                        break;
                    }
            }
            break;
        }
        case Ctl::list:
        {
            const float lh = 14.5f;
            if (c.id == "scnList")
            {
                const int row = (int) ((e.position.y - c.r.getY() - 3) / lh);
                const int idx = sceneTop + row;
                if (idx >= 0 && idx < (int) proc.getScenes().size())
                {
                    scenePick = idx;
                    if (e.getNumberOfClicks() > 1) proc.setCurrentProgram (idx);
                }
            }
            else
            {
                // walk the same layout as paint (group headers take a row)
                const auto& pats = proc.getEnginePatches();
                String group;
                if (libTop > 0 && libTop - 1 < (int) pats.size()) group = pats[(size_t) libTop - 1].group;
                float yy = c.r.getY() + 3;
                for (int k = libTop; k < (int) pats.size() && yy + lh <= c.r.getBottom(); ++k)
                {
                    if (pats[(size_t) k].group != group) { group = pats[(size_t) k].group; yy += lh - 2; }
                    if (e.position.y >= yy && e.position.y < yy + lh) { libPick = k; break; }
                    yy += lh;
                }
            }
            repaint (c.r.expanded (4).withHeight (c.r.getHeight() + 120).toNearestInt());
            break;
        }
        case Ctl::stepGrid:
        {
            int lane, st;
            stepAt (c, e.position, lane, st);
            tapStep (lane, st);
            break;
        }
        case Ctl::keys:
            heldKey = keyAt (e.position, c);
            playNote (heldKey, 0.42);
            repaint (c.r.toNearestInt());
            break;
        default: break;
    }
}

void NativeRackCanvas::mouseDrag (const MouseEvent& e)
{
    if (cableFrom.isNotEmpty())
    {
        cableTo = e.position;
        repaint();
        return;
    }
    if (dragCtl >= 0)
    {
        auto& c = ctls[(size_t) dragCtl];
        float v;
        if (c.kind == Ctl::slider)
            v = jlimit (0.0f, 1.0f, (c.r.getBottom() - e.position.y) / c.r.getHeight());
        else
        {
            const float sens = e.mods.isShiftDown() ? 1000.0f : 200.0f;
            v = jlimit (0.0f, 1.0f, dragStart01 + (dragFrom.y - e.position.y) / sens);
        }
        if (c.set01) c.set01 (v);
        repaint (c.r.expanded (60, 28).toNearestInt());
        return;
    }
    const int i = hit (e.position);
    if (i >= 0 && ctls[(size_t) i].kind == Ctl::keys)
    {
        const int k = keyAt (e.position, ctls[(size_t) i]);
        if (k != heldKey) { heldKey = k; playNote (k, 0.42); repaint (ctls[(size_t) i].r.toNearestInt()); }
    }
}

void NativeRackCanvas::mouseUp (const MouseEvent& e)
{
    if (cableFrom.isNotEmpty())
    {
        const int i = hit (e.position);
        if (i >= 0 && ctls[(size_t) i].kind == Ctl::jack)
            connect (cableFrom, ctls[(size_t) i].jackId);
        cableFrom.clear();
        refreshState();
        return;
    }
    if (dragCtl >= 0)
    {
        auto& c = ctls[(size_t) dragCtl];
        if (c.end) c.end();
        repaint (c.r.expanded (60, 28).toNearestInt());
        dragCtl = -1;
    }
    if (heldKey >= 0) { heldKey = -1; repaint (0, 2280, 840, 380); }
}

void NativeRackCanvas::mouseDoubleClick (const MouseEvent& e)
{
    const int i = hit (e.position);
    if (i < 0) return;
    auto& c = ctls[(size_t) i];
    if ((c.kind == Ctl::knob || c.kind == Ctl::slider) && c.set01)
    {
        if (c.begin) c.begin();
        c.set01 (c.def01);
        if (c.end) c.end();
        repaint (c.r.expanded (60, 28).toNearestInt());
    }
}

void NativeRackCanvas::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (auto* v = findParentComponentOfClass<NativeRackView>())
            v->setZoom (v->getZoom() * (w.deltaY > 0 ? 1.1f : 1.0f / 1.1f), true);
        return;
    }
    const int i = hit (e.position);
    if (i >= 0 && ctls[(size_t) i].kind == Ctl::list && ctls[(size_t) i].step)
    {
        ctls[(size_t) i].step (w.deltaY > 0 ? -2 : 2);
        return;
    }
    Component::mouseWheelMove (e, w);   // scrolls the viewport
}

bool NativeRackCanvas::clickControl (const String& id, bool)
{
    for (auto& c : ctls)
        if (c.id == id)
        {
            if (c.kind == Ctl::select && c.step) { c.step (1); return true; }
            if (c.click) { const auto fn = c.click; fn(); return true; }
            return false;
        }
    return false;
}

bool NativeRackCanvas::dragKnob (const String& id, float delta01)
{
    for (auto& c : ctls)
        if (c.id == id && c.set01 && c.get01)
        {
            if (c.begin) c.begin();
            c.set01 (jlimit (0.0f, 1.0f, c.get01() + delta01));
            if (c.end) c.end();
            return true;
        }
    return false;
}

// ═════════════════════════════════════════════════════════════ view
NativeRackView::NativeRackView (ContinuumRackProcessor& p) : canvas (p)
{
    holder.addAndMakeVisible (canvas);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

void NativeRackView::setZoom (float z, bool fromUser)
{
    userZoomed |= fromUser;
    const float fit = getWidth() > 0 ? (float) getWidth() / NativeRackCanvas::rackWidth : 0.8f;
    zoom = jlimit (jmin (0.3f, fit), 2.0f, z);
    canvas.setTransform (AffineTransform::scale (zoom));
    holder.setSize (roundToInt (NativeRackCanvas::rackWidth * zoom), roundToInt (NativeRackCanvas::rackHeight * zoom));
}

void NativeRackView::resized()
{
    viewport.setBounds (getLocalBounds());
    if (! userZoomed && getWidth() > 0) setZoom ((float) (getWidth() - viewport.getScrollBarThickness()) / NativeRackCanvas::rackWidth);
    else setZoom (zoom);
}
