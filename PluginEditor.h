#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Look & feel: neon-on-dark "glass" style (rounded cards, gradient knobs, pill buttons).
class CrayLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CrayLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
};

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

    CrayLookAndFeel laf;                       // declared first: destroyed last
    VoxLevelerProcessor& proc;
    CurveView curve;
    juce::ComboBox modeBox;
    juce::Slider amountS, rangeS, ceilS;
    juce::Label amountL, rangeL, ceilL;
    juce::ToggleButton applyBtn { "APLICAR CURVA" };
    juce::TextButton analyzeBtn { "ANALIZAR" }, finishBtn { "FINALIZAR" }, clearBtn { "BORRAR" };

    juce::Rectangle<int> curveCard, knobCard, statusCard;
    juce::String statusMsg;
    enum class Led { Idle, Analyzing, Ready, Stale } led = Led::Idle;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> amountA, rangeA, ceilA;
    std::unique_ptr<CA> modeA;
    std::unique_ptr<BA> applyA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoxLevelerEditor)
};
