#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VoxLevelerCore.h"

class VoxLevelerProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::Timer
{
public:
    VoxLevelerProcessor();
    ~VoxLevelerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CRAY LDR VOX LEVELER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- UI actions (message thread) ----
    void startAnalysis();
    void finishAnalysis();
    void clearAnalysis();
    juce::String statusText() const;
    crayldr::VoxLevelerCore& core() noexcept { return core_; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    crayldr::Settings makeSettings() const;
    void rebuild();
    void parameterChanged(const juce::String&, float) override { paramsDirty_.store(true); }
    void timerCallback() override;

    crayldr::VoxLevelerCore core_;
    std::atomic<float>* applyParam_ = nullptr;
    std::atomic<bool> paramsDirty_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoxLevelerProcessor)
};
