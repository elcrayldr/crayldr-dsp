#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
constexpr int kStateMagic = 0x434C5653;   // 'CLVS'
constexpr int kStateVersion = 1;
}

juce::AudioProcessorValueTreeState::ParameterLayout VoxLevelerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ "mode", 1 }, "Mode", StringArray{ "Natural", "Extreme" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ "amount", 1 }, "Amount",
          NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ "range", 1 }, "Range",
          NormalisableRange<float>(0.0f, 24.0f, 0.1f), 12.0f, AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ "ceiling", 1 }, "Peak Ceiling",
          NormalisableRange<float>(-6.0f, 0.0f, 0.1f), -0.5f, AudioParameterFloatAttributes().withLabel("dBFS")));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ "apply", 1 }, "Apply Curve", true));
    return l;
}

VoxLevelerProcessor::VoxLevelerProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CRAYLDR_VOXLEVELER", createLayout())
{
    applyParam_ = apvts.getRawParameterValue("apply");
    for (const char* id : { "mode", "amount", "range", "ceiling" })
        apvts.addParameterListener(id, this);
    startTimerHz(10);
}

VoxLevelerProcessor::~VoxLevelerProcessor()
{
    stopTimer();
    for (const char* id : { "mode", "amount", "range", "ceiling" })
        apvts.removeParameterListener(id, this);
}

void VoxLevelerProcessor::prepareToPlay(double sampleRate, int)
{
    core_.prepare(sampleRate);
    setLatencySamples(0);   // the curve is precomputed: zero look-ahead latency
}

bool VoxLevelerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void VoxLevelerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    crayldr::TransportInfo t;
    // Some hosts report isPlaying=false while performing an offline bounce.
    // JUCE exposes this render mode separately; the timeline position remains
    // mandatory because the gain curve is indexed to absolute host samples.
    t.offlineRender = isNonRealtime();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (auto samples = pos->getTimeInSamples())
            {
                t.position = *samples;
                t.positionValid = true;
            }
            else if (auto seconds = pos->getTimeInSeconds())
            {
                t.position = (int64_t) std::llround(*seconds * getSampleRate());
                t.positionValid = true;
            }
        }
    }

    core_.processBlock(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(),
                       t, applyParam_->load() > 0.5f);
}

crayldr::Settings VoxLevelerProcessor::makeSettings() const
{
    crayldr::Settings s;
    s.mode      = ((int) apvts.getRawParameterValue("mode")->load() == 1) ? crayldr::Mode::Extreme : crayldr::Mode::Natural;
    s.amount    = apvts.getRawParameterValue("amount")->load();
    s.rangeDb   = apvts.getRawParameterValue("range")->load();
    s.ceilingDb = apvts.getRawParameterValue("ceiling")->load();
    return s;
}

void VoxLevelerProcessor::rebuild() { core_.rebuildCurve(makeSettings()); }

void VoxLevelerProcessor::startAnalysis() { core_.startAnalysis(); }
void VoxLevelerProcessor::finishAnalysis() { core_.stopAnalysis(); rebuild(); paramsDirty_.store(false); }
void VoxLevelerProcessor::clearAnalysis() { core_.clearAll(); }

void VoxLevelerProcessor::timerCallback()
{
    // Transport stopped while analysing -> finalise (needs >= 1 s of captured audio).
    if (core_.consumeStopRequest() && core_.isAnalyzing() && core_.coveredHops() >= 200)
        finishAnalysis();
    if (paramsDirty_.exchange(false) && ! core_.isAnalyzing() && core_.coveredHops() > 0)
        rebuild();
}

juce::String VoxLevelerProcessor::statusText() const
{
    if (core_.isAnalyzing())
    {
        auto s = u8("ANALIZANDO: ") + juce::String((double) core_.coveredHops() / 200.0, 1) + u8(" s capturados. Reproduce la toma completa y para el transporte.");
        if (core_.truncated()) s += u8("  (limite de 30 min alcanzado)");
        return s;
    }
    if (core_.hasCurve())
    {
        const auto r = core_.lastReport();
        auto s = u8("Curva lista: ") + juce::String(r.events) + u8(" eventos, ") + juce::String(r.groups) + u8(" grupos, ")
               + juce::String(r.phrases) + u8(" frases");
        if (core_.isStale()) s += u8("  |  AVISO: el audio no coincide con el analisis, vuelve a analizar");
        return s;
    }
    if (core_.coveredHops() > 0) return u8("Analisis guardado pero sin voz detectable. Ajusta o vuelve a analizar.");
    return u8("Sin analisis. Pulsa ANALIZAR y reproduce la toma completa.");
}

void VoxLevelerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(kStateMagic);
    out.writeInt(kStateVersion);
    if (auto xml = apvts.copyState().createXml()) out.writeString(xml->toString());
    else out.writeString({});
    const auto blob = core_.saveState();
    out.writeInt((int) blob.size());
    if (! blob.empty()) out.write(blob.data(), blob.size());
}

void VoxLevelerProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size < 12) return;
    juce::MemoryInputStream in(data, (size_t) size, false);
    if (in.readInt() != kStateMagic || in.readInt() != kStateVersion) return;
    const auto xmlText = in.readString();
    const int blobSize = in.readInt();
    if (auto xml = juce::XmlDocument::parse(xmlText))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid()) apvts.replaceState(tree);
    }
    if (blobSize > 0 && (juce::int64) blobSize <= in.getNumBytesRemaining())
    {
        std::vector<uint8_t> blob((size_t) blobSize);
        in.read(blob.data(), blobSize);
        core_.loadState(blob.data(), blob.size());
    }
    // analysis restored but no curve (e.g. older/foreign blob): rebuild from the stored analysis
    paramsDirty_.store(! core_.hasCurve() && core_.coveredHops() > 0);
}

juce::AudioProcessorEditor* VoxLevelerProcessor::createEditor() { return new VoxLevelerEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxLevelerProcessor(); }
