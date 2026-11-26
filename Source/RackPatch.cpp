#include "RackPatch.h"

namespace crack
{
namespace
{
float num (const juce::var& v, float def)
{
    if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
    {
        const double d = (double) v;
        return std::isfinite (d) ? (float) d : def;
    }
    if (v.isString())
    {
        const auto s = v.toString().trim();
        if (s.containsOnly ("0123456789.-+eE") && s.isNotEmpty()) return s.getFloatValue();
    }
    return def;
}

std::string str (const juce::var& v) { return v.isString() ? v.toString().toStdString() : std::string(); }

const juce::var& member (const juce::var& o, const char* name)
{
    static const juce::var none;
    if (auto* d = o.getDynamicObject())
        if (d->hasProperty (name)) return d->getProperty (name);
    return none;
}

template <typename E, typename F>
void readEnum (const juce::var& o, const char* name, E& target, F parse)
{
    E v;
    if (parse (str (member (o, name)), v)) target = v;
}

void readEdit (const juce::var& o, EngineEdit& e)
{
    if (! o.isObject()) return;
    readEnum (o, "wave1", e.wave1, parseWave);
    readEnum (o, "wave2", e.wave2, parseWave);
    readEnum (o, "filter", e.filter, parseVoiceFilter);
    readEnum (o, "lfoTarget", e.lfoTarget, parseLfoTarget);
#define CR_NUM(f) e.f = num (member (o, #f), e.f);
    CR_NUM (mix) CR_NUM (detune) CR_NUM (transpose) CR_NUM (ratio) CR_NUM (index) CR_NUM (pd)
    CR_NUM (cutoff) CR_NUM (resonance) CR_NUM (envDepth) CR_NUM (lfoRate) CR_NUM (lfoDepth)
    CR_NUM (attack) CR_NUM (decay) CR_NUM (sustain) CR_NUM (release) CR_NUM (combFeedback)
    CR_NUM (wtMorph) CR_NUM (wtTilt) CR_NUM (wtOdd) CR_NUM (wtScan) CR_NUM (wtSweep)
#undef CR_NUM
    for (int h = 0; h < 8; ++h)
        e.wtH[(size_t) h] = num (member (o, ("wtH" + std::to_string (h + 1)).c_str()), e.wtH[(size_t) h]);
    const auto& wt = member (o, "wtEnabled");
    if (wt.isString()) e.wtEnabled = wt.toString() == "on";
    else if (wt.isBool()) e.wtEnabled = (bool) wt;
    const auto& table = member (o, "wtTable");
    if (table.isString()) e.wtTable = table.toString().substring (0, 64).toStdString();
}

juce::var editToVar (const EngineEdit& e)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("wave1", waveName (e.wave1));
    o->setProperty ("wave2", waveName (e.wave2));
#define CR_SET(f) o->setProperty (#f, (double) e.f);
    CR_SET (mix) CR_SET (detune) CR_SET (transpose) CR_SET (ratio) CR_SET (index) CR_SET (pd)
    CR_SET (cutoff) CR_SET (resonance)
#undef CR_SET
    o->setProperty ("filter", voiceFilterName (e.filter));
    o->setProperty ("envDepth", (double) e.envDepth);
    o->setProperty ("lfoRate", (double) e.lfoRate);
    o->setProperty ("lfoDepth", (double) e.lfoDepth);
    o->setProperty ("lfoTarget", lfoTargetName (e.lfoTarget));
#define CR_SET(f) o->setProperty (#f, (double) e.f);
    CR_SET (attack) CR_SET (decay) CR_SET (sustain) CR_SET (release) CR_SET (combFeedback)
#undef CR_SET
    o->setProperty ("wtEnabled", e.wtEnabled ? "on" : "off");
    o->setProperty ("wtMorph", (double) e.wtMorph);
    o->setProperty ("wtTilt", (double) e.wtTilt);
    o->setProperty ("wtOdd", (double) e.wtOdd);
    for (int h = 0; h < 8; ++h) o->setProperty (juce::Identifier ("wtH" + juce::String (h + 1)), (double) e.wtH[(size_t) h]);
    o->setProperty ("wtTable", juce::String (e.wtTable));
    o->setProperty ("wtScan", (double) e.wtScan);
    o->setProperty ("wtSweep", (double) e.wtSweep);
    return juce::var (o);
}
} // namespace

bool stateFromVar (const juce::var& v, RackState& out, juce::String& error)
{
    if (! v.isObject()) { error = "patch state is not an object"; return false; }
    const auto* banks = member (v, "banks").getArray();
    if (banks == nullptr || banks->size() != numBanks) { error = "patch must have 8 banks"; return false; }

    RackState s = makeDefaultRack();
    for (int b = 0; b < numBanks; ++b)
    {
        const auto& bv = (*banks)[b];
        auto& bank = s.banks[(size_t) b];
        bank.level = num (member (bv, "level"), 70);
        bank.pan = num (member (bv, "pan"), 50);
        bank.cutoff = num (member (bv, "cutoff"), 78);
        if (! parseInsert (str (member (bv, "insert")), bank.insert)) bank.insert = Insert::none;
        if (const auto* slots = member (bv, "slots").getArray(); slots != nullptr && slots->size() == numSlots)
        {
            for (int i = 0; i < numSlots; ++i)
            {
                const auto& sv = (*slots)[i];
                auto& slot = bank.slots[(size_t) i];
                slot.on = (bool) member (sv, "on");
                const float t = num (member (sv, "type"), 0);
                slot.type = (t >= 0 && t < 8 && juce::exactlyEqual (t, std::floor (t))) ? (int) t : 0;
                slot.edit = EngineEdit {};
                readEdit (member (sv, "edit"), slot.edit);
            }
        }
    }

    s.master = num (member (v, "master"), 55);
    s.drive = num (member (v, "drive"), 0);
    s.delayMix = num (member (v, "delayMix"), 12);
    s.delayTime = num (member (v, "delayTime"), 0.25f);
    s.feedback = num (member (v, "feedback"), 12);
    s.lfoRate = num (member (v, "lfoRate"), 2);
    s.lfoDepth = num (member (v, "lfoDepth"), 20);
    s.tempo = num (member (v, "tempo"), 120);
    s.root = num (member (v, "root"), 48);
    s.sendLevel = num (member (v, "sendLevel"), 8);
    if (! parseLfoDest (str (member (v, "lfoDest")), s.lfoDest)) s.lfoDest = LfoDest::filter;
    if (! parseWave (str (member (v, "lfoWave")), s.lfoWave)) s.lfoWave = Wave::sine;

    if (const auto* pats = member (v, "bankPatterns").getArray(); pats != nullptr && pats->size() == numBanks)
    {
        for (int b = 0; b < numBanks; ++b)
        {
            const auto& pv = (*pats)[b];
            auto& p = s.patterns[(size_t) b];
            p = Pattern {};
            p.length = (int) num (member (pv, "length"), 16);
            p.mute = (bool) member (pv, "mute");
            if (! parseArp (str (member (pv, "arp")), p.arp)) p.arp = Arp::off;
            const auto* steps = member (pv, "steps").getArray();
            for (int i = 0; i < maxSteps; ++i)
            {
                auto& st = p.steps[(size_t) i];
                if (steps == nullptr || i >= steps->size()) continue;
                const auto& sv = (*steps)[i];
                st.on = (bool) member (sv, "on");
                // the web app's  +x || default  rule: zero and missing both mean the default
                auto orDef = [] (float x, float d) { return ! juce::exactlyEqual (x, 0.0f) ? x : d; };
                st.pitch = num (member (sv, "pitch"), 0);
                st.velocity = orDef (num (member (sv, "velocity"), 0), 100);
                st.gate = orDef (num (member (sv, "gate"), 0), 55);
                st.filter = orDef (num (member (sv, "filter"), 0), 78);
            }
        }
    }

    if (const auto* seq = member (v, "engineSequence").getArray(); seq != nullptr && seq->size() == numBanks)
    {
        for (int b = 0; b < numBanks; ++b)
        {
            const auto& sv = (*seq)[b];
            auto& es = s.engineSequence[(size_t) b];
            const auto* targets = member (sv, "targets").getArray();
            const auto* masks = member (sv, "masks").getArray();
            for (int i = 0; i < numSlots; ++i)
            {
                es.targets[(size_t) i] = ! (targets && i < targets->size() && (*targets)[i].isBool() && ! (bool) (*targets)[i]);
                const auto* m = (masks && i < masks->size()) ? (*masks)[i].getArray() : nullptr;
                for (int j = 0; j < maxSteps; ++j)
                    es.masks[(size_t) i][(size_t) j] = ! (m && j < m->size() && (*m)[j].isBool() && ! (bool) (*m)[j]);
            }
        }
    }

    s.cables.clear();
    if (const auto* cables = member (v, "cables").getArray())
        for (const auto& c : *cables)
            s.cables.push_back ({ str (member (c, "from")), str (member (c, "to")), str (member (c, "color")) });

    normalise (s);
    out = std::move (s);
    return true;
}

juce::var stateToVar (const RackState& s)
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> banks;
    for (const auto& b : s.banks)
    {
        auto* bo = new juce::DynamicObject();
        bo->setProperty ("level", (double) b.level);
        bo->setProperty ("pan", (double) b.pan);
        bo->setProperty ("cutoff", (double) b.cutoff);
        bo->setProperty ("insert", insertName (b.insert));
        juce::Array<juce::var> slots;
        for (const auto& sl : b.slots)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("on", sl.on);
            so->setProperty ("type", sl.type);
            so->setProperty ("edit", editToVar (sl.edit));
            slots.add (juce::var (so));
        }
        bo->setProperty ("slots", slots);
        banks.add (juce::var (bo));
    }
    root->setProperty ("banks", banks);

    juce::Array<juce::var> legacy;   // the web app still carries its first 16-step lane
    for (int i = 0; i < 16; ++i) legacy.add (s.patterns[0].steps[(size_t) i].on);
    root->setProperty ("steps", legacy);
    root->setProperty ("master", (double) s.master);
    root->setProperty ("tempo", (double) s.tempo);
    root->setProperty ("lfoRate", (double) s.lfoRate);
    root->setProperty ("lfoDepth", (double) s.lfoDepth);
    root->setProperty ("lfoDest", lfoDestName (s.lfoDest));
    root->setProperty ("lfoWave", waveName (s.lfoWave));
    root->setProperty ("drive", (double) s.drive);
    root->setProperty ("delayMix", (double) s.delayMix);
    root->setProperty ("delayTime", (double) s.delayTime);
    root->setProperty ("feedback", (double) s.feedback);
    root->setProperty ("gate", 55);
    root->setProperty ("root", (double) s.root);
    root->setProperty ("sendLevel", (double) s.sendLevel);

    juce::Array<juce::var> cables;
    for (const auto& c : s.cables)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("from", juce::String (c.from));
        co->setProperty ("to", juce::String (c.to));
        co->setProperty ("color", juce::String (c.color));
        cables.add (juce::var (co));
    }
    root->setProperty ("cables", cables);

    juce::Array<juce::var> pats;
    for (const auto& p : s.patterns)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("length", p.length);
        po->setProperty ("mute", p.mute);
        po->setProperty ("arp", arpName (p.arp));
        juce::Array<juce::var> steps;
        for (const auto& st : p.steps)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("on", st.on);
            so->setProperty ("pitch", (double) st.pitch);
            so->setProperty ("velocity", (double) st.velocity);
            so->setProperty ("gate", (double) st.gate);
            so->setProperty ("filter", (double) st.filter);
            steps.add (juce::var (so));
        }
        po->setProperty ("steps", steps);
        pats.add (juce::var (po));
    }
    root->setProperty ("bankPatterns", pats);

    juce::Array<juce::var> seq;
    for (const auto& es : s.engineSequence)
    {
        auto* eo = new juce::DynamicObject();
        juce::Array<juce::var> targets, masks;
        for (bool t : es.targets) targets.add (t);
        for (const auto& m : es.masks)
        {
            juce::Array<juce::var> row;
            for (bool x : m) row.add (x);
            masks.add (row);
        }
        eo->setProperty ("targets", targets);
        eo->setProperty ("masks", masks);
        seq.add (juce::var (eo));
    }
    root->setProperty ("engineSequence", seq);
    return juce::var (root);
}

void editFromVar (const juce::var& edit, EngineEdit& out)
{
    out = EngineEdit {};
    readEdit (edit, out);
}

std::vector<EnginePatch> parseEnginePatches (const juce::String& json)
{
    std::vector<EnginePatch> out;
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed()) return out;
    if (const auto* arr = member (parsed, "patches").getArray())
        for (const auto& pv : *arr)
        {
            EnginePatch p;
            p.name = member (pv, "name").toString();
            p.group = member (pv, "group").toString();
            const float t = num (member (pv, "type"), 0);
            p.type = juce::jlimit (0, 7, (int) t);
            editFromVar (member (pv, "edit"), p.edit);
            out.push_back (std::move (p));
        }
    return out;
}

bool readRackPatch (const juce::String& json, RackState& out, juce::String& error)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed()) { error = "not valid JSON: " + result.getErrorMessage(); return false; }
    if (member (parsed, "format").toString() != rackPatchFormat)
    {
        error = "not a Continuum Rack patch (format must be " + juce::String (rackPatchFormat) + ")";
        return false;
    }
    return stateFromVar (member (parsed, "state"), out, error);
}

juce::String writeRackPatch (const RackState& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", rackPatchFormat);
    o->setProperty ("state", stateToVar (s));
    return juce::JSON::toString (juce::var (o), true);
}

std::vector<Scene> parseScenes (const juce::String& json, juce::String& error)
{
    std::vector<Scene> scenes;
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed() || member (parsed, "format").toString() != "continuum-rack-scenes-1")
    {
        error = "factory scene data is damaged";
        return scenes;
    }
    if (const auto* arr = member (parsed, "scenes").getArray())
        for (const auto& sv : *arr)
        {
            Scene sc;
            sc.name = member (sv, "name").toString();
            sc.tag = member (sv, "tag").toString();
            sc.desc = member (sv, "desc").toString();
            if (! stateFromVar (member (sv, "state"), sc.state, error)) { scenes.clear(); return scenes; }
            scenes.push_back (std::move (sc));
        }
    return scenes;
}
} // namespace crack
