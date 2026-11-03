#include "PluginEditor.h"

namespace {
const juce::Colour kBg(0xff0e1116), kAccent(0xff2dd4bf), kText(0xffe5e7eb);
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
}

void VoxLevelerEditor::CurveView::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff10141a));
    const float mid = r.getCentreY(), maxDb = 12.0f;
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawHorizontalLine((int) mid, 0.0f, r.getWidth());
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(13.0f);

    auto& core = proc.core();
    if (core.isAnalyzing()) { g.drawText(u8("Analizando..."), getLocalBounds(), juce::Justification::centred); return; }
    const auto* s = core.snapshotForUI();     // message thread only
    if (s == nullptr) { g.drawText(u8("Sin curva de ganancia"), getLocalBounds(), juce::Justification::centred); return; }

    const auto& d = s->gainDb();
    const int w = getWidth();
    g.setColour(kAccent);
    for (int x = 0; x < w; ++x)
    {
        const size_t i0 = (size_t) x * d.size() / (size_t) w;
        const size_t i1 = juce::jmax(i0 + 1, (size_t) (x + 1) * d.size() / (size_t) w);
        float mn = 1.0e9f, mx = -1.0e9f;
        for (size_t i = i0; i < i1 && i < d.size(); ++i) { mn = juce::jmin(mn, d[i]); mx = juce::jmax(mx, d[i]); }
        const float yTop = mid - juce::jlimit(-1.0f, 1.0f, mx / maxDb) * mid;
        const float yBot = mid - juce::jlimit(-1.0f, 1.0f, mn / maxDb) * mid;
        g.drawVerticalLine(x, juce::jmin(yTop, yBot), juce::jmax(yTop, yBot) + 1.0f);
    }
    const double sr = proc.getSampleRate();
    if (sr > 0.0)
    {
        const double hop = (double) core.lastPosition() / sr * crayldr::kHopsPerSecond - (double) s->firstHop();
        if (hop >= 0.0 && hop < (double) d.size())
        {
            g.setColour(juce::Colours::white);
            g.drawVerticalLine((int) (hop / (double) d.size() * w), 0.0f, (float) getHeight());
        }
    }
}

VoxLevelerEditor::VoxLevelerEditor(VoxLevelerProcessor& p)
    : AudioProcessorEditor(&p), proc(p), curve(p)
{
    setSize(600, 380);
    addAndMakeVisible(curve);

    modeBox.addItem("NATURAL", 1);
    modeBox.addItem("EXTREME", 2);
    addAndMakeVisible(modeBox);
    modeA = std::make_unique<CA>(proc.apvts, "mode", modeBox);

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, kText);
        addAndMakeVisible(l);
    };
    setup(amountS, amountL, "CANTIDAD");
    setup(rangeS, rangeL, "RANGO (dB)");
    setup(ceilS, ceilL, "TECHO PICO (dBFS)");
    amountA = std::make_unique<SA>(proc.apvts, "amount", amountS);
    rangeA  = std::make_unique<SA>(proc.apvts, "range", rangeS);
    ceilA   = std::make_unique<SA>(proc.apvts, "ceiling", ceilS);

    applyBtn.setColour(juce::ToggleButton::textColourId, kText);
    addAndMakeVisible(applyBtn);
    applyA = std::make_unique<BA>(proc.apvts, "apply", applyBtn);

    analyzeBtn.onClick = [this] { proc.startAnalysis(); };
    finishBtn.onClick  = [this] { proc.finishAnalysis(); };
    clearBtn.onClick   = [this] { proc.clearAnalysis(); };
    for (auto* b : { &analyzeBtn, &finishBtn, &clearBtn }) addAndMakeVisible(*b);

    status.setColour(juce::Label::textColourId, kText);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
    startTimerHz(10);
    timerCallback();
}

VoxLevelerEditor::~VoxLevelerEditor() { stopTimer(); }

void VoxLevelerEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("CRAY LDR  VOX LEVELER", 16, 8, 400, 28, juce::Justification::centredLeft);
}

void VoxLevelerEditor::resized()
{
    auto r = getLocalBounds().reduced(16);
    r.removeFromTop(32);
    auto top = r.removeFromTop(36);
    modeBox.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(12);
    analyzeBtn.setBounds(top.removeFromLeft(100)); top.removeFromLeft(8);
    finishBtn.setBounds(top.removeFromLeft(100));  top.removeFromLeft(8);
    clearBtn.setBounds(top.removeFromLeft(90));    top.removeFromLeft(12);
    applyBtn.setBounds(top);
    r.removeFromTop(10);
    status.setBounds(r.removeFromBottom(26));
    auto knobs = r.removeFromBottom(120);
    const int kw = knobs.getWidth() / 3;
    for (auto pair : { std::make_pair(&amountS, &amountL), std::make_pair(&rangeS, &rangeL), std::make_pair(&ceilS, &ceilL) })
    {
        auto c = knobs.removeFromLeft(kw);
        pair.second->setBounds(c.removeFromTop(18));
        pair.first->setBounds(c);
    }
    r.removeFromBottom(8);
    curve.setBounds(r);
}

void VoxLevelerEditor::timerCallback()
{
    status.setText(proc.statusText(), juce::dontSendNotification);
    const bool analyzing = proc.core().isAnalyzing();
    analyzeBtn.setEnabled(! analyzing);
    finishBtn.setEnabled(analyzing);
    curve.repaint();
}
