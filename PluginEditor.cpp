#include "PluginEditor.h"

namespace {
const juce::Colour kBgTop(0xff0a0b14), kBgBottom(0xff150a26);
const juce::Colour kCyan(0xff2de2e6), kViolet(0xff9b5cff), kMagenta(0xffff3d9a);
const juce::Colour kAmber(0xffffb02e), kGreen(0xff3dffa2);
const juce::Colour kText(0xfff1f3ff), kDim(0xff8f93b8);
juce::String u8(const char* s) { return juce::String::fromUTF8(s); }
juce::Font mkFont(float size, int style = juce::Font::plain) { return juce::Font(juce::FontOptions(size, style)); }

void drawCard(juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f)
{
    g.setColour(juce::Colour(0xff1b1d33).withAlpha(0.62f));
    g.fillRoundedRectangle(r, radius);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.10f), r.getX(), r.getY(),
                                           juce::Colours::white.withAlpha(0.02f), r.getX(), r.getBottom(), false));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}
} // namespace

// ------------------------------- look & feel -------------------------------

CrayLookAndFeel::CrayLookAndFeel()
{
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0c0d18));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x22ffffff));
    setColour(juce::Slider::textBoxHighlightColourId, kViolet.withAlpha(0.5f));
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff15172b));
    setColour(juce::PopupMenu::textColourId, kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kViolet.withAlpha(0.55f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

juce::Font CrayLookAndFeel::getComboBoxFont(juce::ComboBox&) { return mkFont(14.0f, juce::Font::bold); }
juce::Font CrayLookAndFeel::getLabelFont(juce::Label&) { return mkFont(12.0f, juce::Font::bold); }

void CrayLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(6.0f);
    const float radius = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float thick = 7.0f, arcR = radius - thick * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff252843));
    g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour(kViolet.withAlpha(0.20f));                                    // glow
        g.strokePath(val, juce::PathStrokeType(thick + 8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(kCyan, c.x - arcR, c.y, kMagenta, c.x + arcR, c.y, false));
        g.strokePath(val, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float faceR = arcR - thick - 5.0f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b2e4f), c.x, c.y - faceR,
                                           juce::Colour(0xff0f1020), c.x, c.y + faceR, false));
    g.fillEllipse(c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);

    const auto tip = c.getPointOnCircumference(faceR - 5.0f, angle);
    const auto base = c.getPointOnCircumference(faceR * 0.45f, angle);
    g.setColour(juce::Colours::white);
    g.drawLine(juce::Line<float>(base, tip), 2.5f);
}

void CrayLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool highlighted, bool down)
{
    auto r = bt.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = bt.isEnabled();
    const bool primary = bt.getButtonText() == "ANALIZAR";
    if (on && primary)
    {
        g.setColour(kViolet.withAlpha(highlighted ? 0.35f : 0.22f));
        g.fillRoundedRectangle(r.expanded(2.0f), 12.0f);
        g.setGradientFill(juce::ColourGradient(kCyan, r.getX(), r.getY(), kViolet, r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 10.0f);
    }
    else
    {
        g.setColour(juce::Colour(0xff1e2140).withAlpha(on ? (down ? 0.95f : highlighted ? 0.85f : 0.65f) : 0.35f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(on ? 0.16f : 0.06f));
        g.drawRoundedRectangle(r, 10.0f, 1.0f);
    }
}

void CrayLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& bt, bool, bool)
{
    const bool primary = bt.isEnabled() && bt.getButtonText() == "ANALIZAR";
    g.setFont(mkFont(13.0f, juce::Font::bold));
    g.setColour(primary ? juce::Colour(0xff06070f) : (bt.isEnabled() ? kText : kDim.withAlpha(0.6f)));
    g.drawText(bt.getButtonText(), bt.getLocalBounds(), juce::Justification::centred);
}

void CrayLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& bt, bool highlighted, bool)
{
    const auto r = bt.getLocalBounds().toFloat();
    const float tw = 40.0f, th = 22.0f;
    juce::Rectangle<float> sw(r.getX() + 2.0f, r.getCentreY() - th * 0.5f, tw, th);
    const bool on = bt.getToggleState();
    if (on) g.setGradientFill(juce::ColourGradient(kCyan, sw.getX(), sw.getY(), kViolet, sw.getRight(), sw.getY(), false));
    else    g.setColour(juce::Colour(0xff2a2d4d));
    g.fillRoundedRectangle(sw, th * 0.5f);
    g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.35f : 0.18f));
    g.drawRoundedRectangle(sw, th * 0.5f, 1.0f);
    const float kx = on ? sw.getRight() - th + 3.0f : sw.getX() + 3.0f;
    g.setColour(juce::Colours::white);
    g.fillEllipse(kx, sw.getY() + 3.0f, th - 6.0f, th - 6.0f);
    g.setFont(mkFont(12.0f, juce::Font::bold));
    g.setColour(on ? kText : kDim);
    g.drawText(bt.getButtonText(), r.withTrimmedLeft(tw + 12.0f).toNearestInt(), juce::Justification::centredLeft);
}

void CrayLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(1.0f);
    g.setColour(juce::Colour(0xff1e2140).withAlpha(0.85f));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(kViolet.withAlpha(0.7f));
    g.drawRoundedRectangle(r, 10.0f, 1.2f);
    juce::Path arrow;
    const float ax = (float) w - 20.0f, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 5.0f, ay - 2.5f, ax + 5.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(kCyan);
    g.fillPath(arrow);
}

// ------------------------------- curve view -------------------------------

void VoxLevelerEditor::CurveView::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float mid = r.getCentreY();
    const float maxDb = juce::jmax(6.0f, proc.apvts.getRawParameterValue("range")->load());

    g.setColour(juce::Colour(0xff0b0c17));
    g.fillRoundedRectangle(r, 12.0f);

    // dB grid
    g.setFont(mkFont(10.5f));
    for (int k = -2; k <= 2; ++k)
    {
        const float y = mid - (float) k * 0.5f * mid;
        g.setColour(juce::Colours::white.withAlpha(k == 0 ? 0.30f : 0.07f));
        g.drawHorizontalLine((int) y, 8.0f, r.getRight() - 8.0f);
        if (k != 0)
        {
            g.setColour(kDim);
            g.drawText((k > 0 ? "+" : "") + juce::String(juce::roundToInt((float) k * 0.5f * maxDb)) + " dB",
                       juce::Rectangle<int>(10, (int) y - 14, 60, 12), juce::Justification::left);
        }
    }

    auto& core = proc.core();
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.setFont(mkFont(13.0f));
    if (core.isAnalyzing()) { g.drawText(u8("Analizando... reproduce la toma completa"), getLocalBounds(), juce::Justification::centred); return; }
    const auto* s = core.snapshotForUI();     // message thread only
    if (s == nullptr) { g.drawText(u8("Sin curva de ganancia"), getLocalBounds(), juce::Justification::centred); return; }

    const auto& d = s->gainDb();
    const int w = getWidth();
    juce::Path fill, line;
    fill.startNewSubPath(0.0f, mid);
    for (int x = 0; x < w; ++x)
    {
        const size_t i0 = (size_t) x * d.size() / (size_t) w;
        const size_t i1 = juce::jmax(i0 + 1, (size_t) (x + 1) * d.size() / (size_t) w);
        float v = 0.0f;
        for (size_t i = i0; i < i1 && i < d.size(); ++i) if (std::fabs(d[i]) > std::fabs(v)) v = d[i];
        const float y = mid - juce::jlimit(-1.0f, 1.0f, v / maxDb) * mid;
        fill.lineTo((float) x, y);
        if (x == 0) line.startNewSubPath((float) x, y); else line.lineTo((float) x, y);
    }
    fill.lineTo((float) w, mid);
    fill.closeSubPath();

    {   // boosts (above 0 dB) in cyan, cuts (below) in magenta
        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, 0, w, (int) mid));
        g.setGradientFill(juce::ColourGradient(kCyan.withAlpha(0.65f), 0.0f, 0.0f, kCyan.withAlpha(0.05f), 0.0f, mid, false));
        g.fillPath(fill);
    }
    {
        juce::Graphics::ScopedSaveState st(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, (int) mid, w, getHeight() - (int) mid));
        g.setGradientFill(juce::ColourGradient(kMagenta.withAlpha(0.05f), 0.0f, mid, kMagenta.withAlpha(0.65f), 0.0f, r.getBottom(), false));
        g.fillPath(fill);
    }
    g.setColour(kViolet.withAlpha(0.35f));
    g.strokePath(line, juce::PathStrokeType(4.0f));
    g.setGradientFill(juce::ColourGradient(kCyan, 0.0f, 0.0f, kMagenta, (float) w, 0.0f, false));
    g.strokePath(line, juce::PathStrokeType(1.6f));

    const double sr = proc.getSampleRate();
    if (sr > 0.0)
    {
        const double hop = (double) core.lastPosition() / sr * crayldr::kHopsPerSecond - (double) s->firstHop();
        if (hop >= 0.0 && hop < (double) d.size())
        {
            const int px = (int) (hop / (double) d.size() * w);
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.fillRect(px - 2, 0, 5, getHeight());
            g.setColour(juce::Colours::white);
            g.drawVerticalLine(px, 0.0f, (float) getHeight());
        }
    }
}

// --------------------------------- editor ---------------------------------

VoxLevelerEditor::VoxLevelerEditor(VoxLevelerProcessor& p)
    : AudioProcessorEditor(&p), proc(p), curve(p)
{
    setLookAndFeel(&laf);
    setSize(720, 520);
    addAndMakeVisible(curve);

    modeBox.addItem("NATURAL", 1);
    modeBox.addItem("EXTREME", 2);
    addAndMakeVisible(modeBox);
    modeA = std::make_unique<CA>(proc.apvts, "mode", modeBox);

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 20);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(l);
    };
    setup(amountS, amountL, "CANTIDAD");
    setup(rangeS, rangeL, "RANGO (dB)");
    setup(ceilS, ceilL, "TECHO PICO (dBFS)");
    amountA = std::make_unique<SA>(proc.apvts, "amount", amountS);
    rangeA  = std::make_unique<SA>(proc.apvts, "range", rangeS);
    ceilA   = std::make_unique<SA>(proc.apvts, "ceiling", ceilS);

    addAndMakeVisible(applyBtn);
    applyA = std::make_unique<BA>(proc.apvts, "apply", applyBtn);

    analyzeBtn.onClick = [this] { proc.startAnalysis(); };
    finishBtn.onClick  = [this] { proc.finishAnalysis(); };
    clearBtn.onClick   = [this] { proc.clearAnalysis(); };
    for (auto* b : { &analyzeBtn, &finishBtn, &clearBtn }) addAndMakeVisible(*b);

    startTimerHz(15);
    timerCallback();
}

VoxLevelerEditor::~VoxLevelerEditor() { stopTimer(); setLookAndFeel(nullptr); }

void VoxLevelerEditor::paint(juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(kBgTop, 0.0f, 0.0f, kBgBottom, 0.0f, full.getBottom(), false));
    g.fillAll();
    g.setGradientFill(juce::ColourGradient(kViolet.withAlpha(0.28f), full.getRight(), 0.0f, juce::Colours::transparentBlack, full.getRight() - 360.0f, 200.0f, true));
    g.fillRect(full);
    g.setGradientFill(juce::ColourGradient(kCyan.withAlpha(0.14f), 0.0f, full.getBottom(), juce::Colours::transparentBlack, 300.0f, full.getBottom() - 240.0f, true));
    g.fillRect(full);

    // title
    g.setFont(mkFont(26.0f, juce::Font::bold));
    g.setColour(kText);
    g.drawText("CRAY LDR", 24, 14, 150, 34, juce::Justification::centredLeft);
    g.setGradientFill(juce::ColourGradient(kCyan, 160.0f, 0.0f, kMagenta, 360.0f, 0.0f, false));
    g.drawText("VOX LEVELER", 156, 14, 240, 34, juce::Justification::centredLeft);
    g.setFont(mkFont(11.0f));
    g.setColour(kDim);
    g.drawText(u8("Automatizacion de volumen silaba por silaba"), 26, 46, 380, 16, juce::Justification::centredLeft);

    drawCard(g, curveCard.toFloat());
    drawCard(g, knobCard.toFloat());
    drawCard(g, statusCard.toFloat(), 14.0f);

    // status LED + text
    const auto sc = statusCard.toFloat();
    juce::Colour lc = kDim;
    float pulse = 1.0f;
    switch (led)
    {
        case Led::Analyzing: lc = kAmber; pulse = 0.55f + 0.45f * std::sin((float) juce::Time::getMillisecondCounter() * 0.008f); break;
        case Led::Ready:     lc = kGreen; break;
        case Led::Stale:     lc = kMagenta; break;
        case Led::Idle:      break;
    }
    const float cx = sc.getX() + 20.0f, cy = sc.getCentreY();
    g.setColour(lc.withAlpha(0.25f * pulse));
    g.fillEllipse(cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
    g.setColour(lc.withAlpha(pulse));
    g.fillEllipse(cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
    g.setFont(mkFont(12.5f));
    g.setColour(kText);
    g.drawText(statusMsg, statusCard.withTrimmedLeft(38).withTrimmedRight(12), juce::Justification::centredLeft, true);
}

void VoxLevelerEditor::resized()
{
    auto r = getLocalBounds().reduced(20);
    r.removeFromTop(52);

    auto top = r.removeFromTop(40);
    modeBox.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(10);
    analyzeBtn.setBounds(top.removeFromLeft(112)); top.removeFromLeft(8);
    finishBtn.setBounds(top.removeFromLeft(112));  top.removeFromLeft(8);
    clearBtn.setBounds(top.removeFromLeft(92));
    applyBtn.setBounds(top.removeFromRight(160));
    r.removeFromTop(12);

    statusCard = r.removeFromBottom(36);
    r.removeFromBottom(12);
    knobCard = r.removeFromBottom(168);
    r.removeFromBottom(12);
    curveCard = r;
    curve.setBounds(curveCard.reduced(10));

    auto knobs = knobCard.reduced(10, 8);
    const int kw = knobs.getWidth() / 3;
    for (auto pair : { std::make_pair(&amountS, &amountL), std::make_pair(&rangeS, &rangeL), std::make_pair(&ceilS, &ceilL) })
    {
        auto c = knobs.removeFromLeft(kw);
        pair.second->setBounds(c.removeFromTop(18));
        pair.first->setBounds(c);
    }
}

void VoxLevelerEditor::timerCallback()
{
    statusMsg = proc.statusText();
    auto& core = proc.core();
    const bool analyzing = core.isAnalyzing();
    led = analyzing ? Led::Analyzing
        : core.hasCurve() ? (core.isStale() ? Led::Stale : Led::Ready)
        : Led::Idle;
    analyzeBtn.setEnabled(! analyzing);
    finishBtn.setEnabled(analyzing);
    curve.repaint();
    repaint(statusCard);
}
