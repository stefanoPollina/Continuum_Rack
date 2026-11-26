#pragma once
// JSON conversion between RackState and the web app's patch files, plus the factory scenes.

#include "RackModel.h"

#include <juce_core/juce_core.h>

namespace crack
{
constexpr const char* rackPatchFormat = "continuum-synth-p01";

// Reads a web-app state object (the "state" member of a rack patch, or a scene's state).
// Missing fields take the web app's defaults; the result is normalised.
bool stateFromVar (const juce::var& state, RackState& out, juce::String& error);
juce::var stateToVar (const RackState&);

// Whole patch files: {"format":"continuum-synth-p01","state":{...}}
bool readRackPatch (const juce::String& json, RackState& out, juce::String& error);
juce::String writeRackPatch (const RackState&);

struct Scene
{
    juce::String name, tag, desc;
    RackState state;
};

// Decodes Resources/scenes.json.gz (already decompressed to JSON text).
std::vector<Scene> parseScenes (const juce::String& json, juce::String& error);
} // namespace crack
