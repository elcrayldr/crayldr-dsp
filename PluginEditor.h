#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class VoxLevelerEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoxLevelerEditor(VoxLevelerProcessor&);
    ~VoxLevelerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class CurveView final : public juce::Component
    {
    public:
        explicit CurveView(VoxLevelerProcessor& p) : proc(p) {}
        void paint(juce::Graphics&) override;
    private:
        VoxLevelerProcessor& proc;
    };

    void timerCallback() override;

    VoxLevelerProcessor& proc;
    CurveView curve;
    juce::ComboBox modeBox;
    juce::Slider amountS, rangeS, ceilS;
    juce::Label amountL, rangeL, ceilL, status;
    juce::ToggleButton applyBtn { "APLICAR CURVA" };
    juce::TextButton analyzeBtn { "ANALIZAR" }, finishBtn { "FINALIZAR" }, clearBtn { "BORRAR" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> amountA, rangeA, ceilA;
    std::unique_ptr<CA> modeA;
    std::unique_ptr<BA> applyA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoxLevelerEditor)
};
