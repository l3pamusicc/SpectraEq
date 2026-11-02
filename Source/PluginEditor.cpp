#include "PluginEditor.h"

using namespace eq;

//==============================================================================
EQDisplay::EQDisplay (SpectraEQAudioProcessor& p) : proc (p)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    fftIn.assign ((size_t) fftSize, 0.0f);
    preSmooth.assign ((size_t) fftSize / 2, -120.0f);
    postSmooth.assign ((size_t) fftSize / 2, -120.0f);
}

juce::Colour EQDisplay::bandColour (int i) { return juce::Colour::fromHSV ((float) i / (float) numBands, 0.65f, 0.95f, 1.0f); }
float EQDisplay::freqToX (double f) const { return (float) getWidth() * (float) (std::log (f / 10.0) / std::log (3000.0)); }
double EQDisplay::xToFreq (float x) const { return 10.0 * std::pow (3000.0, (double) x / (double) getWidth()); }
float EQDisplay::dbToY (double db) const { return (float) getHeight() * 0.5f - (float) (db / rangeDb()) * (float) getHeight() * 0.5f; }
double EQDisplay::yToDb (float y) const { return ((double) getHeight() * 0.5 - y) / ((double) getHeight() * 0.5) * rangeDb(); }
double EQDisplay::rangeDb() const
{
    static const double r[] = { 3.0, 6.0, 12.0, 30.0 };
    return r[juce::jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("disp_range")->load())];
}
bool EQDisplay::isOn (int i) const { return proc.bp[(size_t) i].on->load() > 0.5f; }

juce::Point<float> EQDisplay::nodePos (int i) const
{
    const auto& p = proc.bp[(size_t) i];
    const int type = (int) p.type->load();
    double db = typeHasGain (type) ? (double) p.gain->load() * (double) proc.apvts.getRawParameterValue ("gain_scale")->load() * 0.01 : 0.0;
    return { freqToX (p.freq->load()), dbToY (db) };
}

int EQDisplay::hitNode (juce::Point<float> p) const
{
    int best = -1;
    float bd = 14.0f * 14.0f;
    for (int i = 0; i < numBands; ++i)
    {
        if (! isOn (i)) continue;
        const auto n = nodePos (i);
        const float dx = n.x - p.x, dy = n.y - p.y, d = dx * dx + dy * dy;
        if (d <= bd) { bd = d; best = i; }
    }
    return best;
}

void EQDisplay::setReal (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void EQDisplay::updateAnalyzer()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
    if (mode == 0) return;
    for (int which = 0; which < 2; ++which)
    {
        const bool post = which == 1;
        if ((post && mode < 2) || (! post && mode != 1 && mode != 3)) continue;
        proc.readRing (post, fftIn.data(), fftSize);
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        std::copy (fftIn.begin(), fftIn.end(), fftData.begin());
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        auto& sm = post ? postSmooth : preSmooth;
        for (size_t b = 0; b < sm.size(); ++b)
        {
            const float db = 20.0f * std::log10 (fftData[b] / ((float) fftSize / 4.0f) + 1e-9f);
            sm[b] = db > sm[b] ? sm[b] + (db - sm[b]) * 0.5f : std::max (db, sm[b] - 0.8f);
        }
    }
}

void EQDisplay::drawAnalyzer (juce::Graphics& g, const std::vector<float>& sm, juce::Colour c)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const double binHz = proc.getSampleRate() / (double) fftSize;
    juce::Path path;
    bool started = false;
    for (float x = 0; x <= w; x += 2.0f)
    {
        const double f0 = xToFreq (x), f1 = xToFreq (x + 2.0f);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float db;
        if (b1 - b0 < 1.0)
        {
            const double bc = 0.5 * (b0 + b1);
            const int i0 = juce::jlimit (0, (int) sm.size() - 2, (int) bc);
            const float fr = (float) (bc - i0);
            db = sm[(size_t) i0] * (1 - fr) + sm[(size_t) i0 + 1] * fr;
        }
        else
        {
            db = -120.0f;
            for (int b = juce::jmax (0, (int) b0); b <= juce::jmin ((int) sm.size() - 1, (int) b1); ++b)
                db = std::max (db, sm[(size_t) b]);
        }
        db += 4.5f * (float) std::log2 (0.5 * (f0 + f1) / 1000.0);
        const float y = h * (1.0f - (db + 90.0f) / 90.0f);
        if (! started) { path.startNewSubPath (x, h); path.lineTo (x, y); started = true; }
        else path.lineTo (x, y);
    }
    path.lineTo (w, h);
    path.closeSubPath();
    g.setColour (c.withAlpha (0.28f));
    g.fillPath (path);
    g.setColour (c.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void EQDisplay::paint (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    g.fillAll (juce::Colour (0xff12161c));

    // grid
    g.setFont (11.0f);
    const double fl[] = { 20, 30, 50, 100, 200, 300, 500, 1000, 2000, 3000, 5000, 10000, 20000 };
    for (double f : fl)
    {
        const float x = freqToX (f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) x, 0.0f, (float) h);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, f == 1000 ? 0 : 0) + "k" : juce::String ((int) f),
                    (int) x + 2, h - 16, 40, 14, juce::Justification::left);
    }
    const double R = rangeDb();
    const double step = R / 4.0;
    for (int k = -4; k <= 4; ++k)
    {
        const double db = k * step;
        const float y = dbToY (db);
        g.setColour (juce::Colours::white.withAlpha (k == 0 ? 0.22f : 0.07f));
        g.drawHorizontalLine ((int) y, 0.0f, (float) w);
        if (k != 0 && k != 4 && k != -4)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String (db, db == std::floor (db) ? 0 : 1), w - 40, (int) y - 7, 36, 14, juce::Justification::right);
        }
    }

    // analyzer
    const int mode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
    if (mode == 1 || mode == 3) drawAnalyzer (g, preSmooth, juce::Colours::grey);
    if (mode >= 2) drawAnalyzer (g, postSmooth, juce::Colour (0xff4aa8ff));

    // designs
    std::array<eq::Design, numBands> ds;
    std::array<int, numBands> chs {};
    bool anyMS = false;
    for (int i = 0; i < numBands; ++i)
    {
        if (! isOn (i)) continue;
        ds[(size_t) i] = proc.makeDesign (i, proc.dynOffset[(size_t) i].load());
        chs[(size_t) i] = (int) proc.bp[(size_t) i].ch->load();
        if (chs[(size_t) i] == Mid || chs[(size_t) i] == Side) anyMS = true;
    }

    auto curve = [&] (auto&& dbAt)
    {
        juce::Path p;
        for (int x = 0; x <= w; x += 2)
        {
            const float y = dbToY (juce::jlimit (-R * 1.5, R * 1.5, dbAt (xToFreq ((float) x))));
            if (x == 0) p.startNewSubPath ((float) x, y); else p.lineTo ((float) x, y);
        }
        return p;
    };

    // selected / hovered band fill
    for (int i : { selected, hoverBand })
    {
        if (i < 0 || ! isOn (i)) continue;
        auto p = curve ([&] (double f) { return magnitudeDb (ds[(size_t) i], f, fs); });
        juce::Path fill (p);
        fill.lineTo ((float) w, dbToY (0)); fill.lineTo (0.0f, dbToY (0)); fill.closeSubPath();
        g.setColour (bandColour (i).withAlpha (i == selected ? 0.22f : 0.12f));
        g.fillPath (fill);
        g.setColour (bandColour (i).withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    if (anyMS)
    {
        auto sum = [&] (bool mid, double f)
        {
            double db = 0;
            for (int i = 0; i < numBands; ++i)
                if (isOn (i) && (chs[(size_t) i] == Stereo || chs[(size_t) i] == (mid ? Mid : Side)))
                    db += magnitudeDb (ds[(size_t) i], f, fs);
            return db;
        };
        g.setColour (juce::Colour (0xff3de0d0).withAlpha (0.8f));
        g.strokePath (curve ([&] (double f) { return sum (true, f); }), juce::PathStrokeType (1.2f));
        g.setColour (juce::Colour (0xffffa040).withAlpha (0.8f));
        g.strokePath (curve ([&] (double f) { return sum (false, f); }), juce::PathStrokeType (1.2f));
        g.setFont (12.0f);
        g.setColour (juce::Colour (0xff3de0d0)); g.drawText ("Mid", 8, 6, 40, 14, juce::Justification::left);
        g.setColour (juce::Colour (0xffffa040)); g.drawText ("Side", 48, 6, 40, 14, juce::Justification::left);
    }

    auto total = curve ([&] (double f)
    {
        double db = 0;
        for (int i = 0; i < numBands; ++i) if (isOn (i)) db += magnitudeDb (ds[(size_t) i], f, fs);
        return db;
    });
    g.setColour (juce::Colours::white);
    g.strokePath (total, juce::PathStrokeType (2.0f));

    // nodes
    for (int i = 0; i < numBands; ++i)
    {
        if (! isOn (i)) continue;
        const auto n = nodePos (i);
        const auto c = bandColour (i);
        const bool sel = i == selected;
        const bool dyn = typeHasGain ((int) proc.bp[(size_t) i].type->load()) && proc.bp[(size_t) i].dyn->load() > 0.5f;
        g.setColour (c);
        g.fillEllipse (n.x - 8, n.y - 8, 16, 16);
        if (dyn) { g.setColour (juce::Colours::white); g.drawEllipse (n.x - 11, n.y - 11, 22, 22, 1.0f); }
        if (sel) { g.setColour (juce::Colours::white); g.drawEllipse (n.x - 8, n.y - 8, 16, 16, 2.0f); }
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (i + 1), (int) n.x - 8, (int) n.y - 8, 16, 16, juce::Justification::centred);
        const int ch = chs[(size_t) i];
        if (ch != Stereo)
        {
            g.setColour (c);
            g.setFont (10.0f);
            g.drawText (juce::String (chanNames[ch]).substring (0, 1), (int) n.x + 9, (int) n.y - 16, 12, 12, juce::Justification::left);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (getLocalBounds());
}

void EQDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hoverBand) { hoverBand = h; repaint(); }
}

void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 0 && onSelect) onSelect (h);
    if (e.mods.isPopupMenu()) { if (h >= 0) showMenu (h); return; }
    dragBand = h;
    if (h >= 0)
    {
        dragStartMouse = e.position;
        dragStartNode = nodePos (h);
        for (auto* id : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (pid (h, id))) p->beginChangeGesture();
    }
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || e.mods.isPopupMenu()) return;
    const float k = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const float x = juce::jlimit (0.0f, (float) getWidth(), dragStartNode.x + (e.position.x - dragStartMouse.x) * k);
    const float y = dragStartNode.y + (e.position.y - dragStartMouse.y) * k;
    auto setNoGesture = [this] (const juce::String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    setNoGesture (pid (dragBand, "freq"), (float) juce::jlimit (10.0, 30000.0, xToFreq (x)));
    if (typeHasGain ((int) proc.bp[(size_t) dragBand].type->load()))
    {
        const double scale = std::max (1.0f, proc.apvts.getRawParameterValue ("gain_scale")->load()) * 0.01;
        setNoGesture (pid (dragBand, "gain"), (float) juce::jlimit (-30.0, 30.0, yToDb (y) / scale));
    }
    repaint();
}

void EQDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (auto* id : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (pid (dragBand, id))) p->endChangeGesture();
    dragBand = -1;
}

void EQDisplay::addBand (juce::Point<float> p)
{
    for (int i = 0; i < numBands; ++i)
    {
        if (isOn (i)) continue;
        setReal (pid (i, "type"), (float) Bell);
        setReal (pid (i, "ch"), (float) Stereo);
        setReal (pid (i, "dyn"), 0.0f);
        setReal (pid (i, "q"), 1.0f);
        setReal (pid (i, "slope"), 1.0f);
        setReal (pid (i, "freq"), (float) juce::jlimit (10.0, 30000.0, xToFreq (p.x)));
        setReal (pid (i, "gain"), (float) juce::jlimit (-30.0, 30.0, yToDb (p.y)));
        setReal (pid (i, "on"), 1.0f);
        if (onSelect) onSelect (i);
        repaint();
        return;
    }
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h >= 0) { setReal (pid (h, "on"), 0.0f); repaint(); }
    else addBand (e.position);
}

void EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd)
{
    const int h = hitNode (e.position);
    if (h < 0) return;
    const float q = proc.bp[(size_t) h].q->load();
    setReal (pid (h, "q"), juce::jlimit (0.025f, 40.0f, q * std::exp (wd.deltaY * 0.8f)));
    repaint();
}

void EQDisplay::showMenu (int band)
{
    const int type = (int) proc.bp[(size_t) band].type->load();
    const int ch = (int) proc.bp[(size_t) band].ch->load();
    const int slope = (int) proc.bp[(size_t) band].slope->load();
    const bool dyn = proc.bp[(size_t) band].dyn->load() > 0.5f;

    juce::PopupMenu m, tm, cm, sm;
    for (int t = 0; t < numTypes; ++t) tm.addItem (100 + t, typeNames[t], true, t == type);
    for (int c = 0; c < numChans; ++c) cm.addItem (200 + c, chanNames[c], true, c == ch);
    for (int s = 0; s < numSlopes; ++s) sm.addItem (300 + s, slopeNames[s], true, s == slope);
    m.addSubMenu ("Shape", tm);
    m.addSubMenu ("Stereo placement", cm);
    if (typeIsCut (type)) m.addSubMenu ("Slope", sm);
    m.addItem (400, "Dynamic", typeHasGain (type), dyn);
    m.addSeparator();
    m.addItem (500, "Delete band");

    juce::Component::SafePointer<EQDisplay> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [sp, band, dyn] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r >= 100 && r < 200) sp->setReal (pid (band, "type"), (float) (r - 100));
        else if (r >= 200 && r < 300) sp->setReal (pid (band, "ch"), (float) (r - 200));
        else if (r >= 300 && r < 400) sp->setReal (pid (band, "slope"), (float) (r - 300));
        else if (r == 400) sp->setReal (pid (band, "dyn"), dyn ? 0.0f : 1.0f);
        else if (r == 500) sp->setReal (pid (band, "on"), 0.0f);
        sp->repaint();
    });
}

//==============================================================================
SpectraEQAudioProcessorEditor::SpectraEQAudioProcessorEditor (SpectraEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setResizable (true, true);
    setResizeLimits (900, 560, 1800, 1200);
    setSize (1100, 720);

    addAndMakeVisible (display);
    display.onSelect = [this] (int i) { bandC.setSelectedItemIndex (i, juce::dontSendNotification); selectBand (i); };

    for (int i = 0; i < numBands; ++i) bandC.addItem ("Band " + juce::String (i + 1), i + 1);
    bandC.setSelectedItemIndex (0, juce::dontSendNotification);
    bandC.onChange = [this] { selectBand (bandC.getSelectedItemIndex()); };
    addAndMakeVisible (bandC);

    setupCombo (typeC, typeNames, numTypes);
    setupCombo (slopeC, slopeNames, numSlopes);
    setupCombo (chC, chanNames, numChans);
    static const char* const dirs[] = { "Above", "Below" };
    static const char* const anas[] = { "Off", "Pre", "Post", "Pre + Post" };
    static const char* const rngs[] = { "3 dB", "6 dB", "12 dB", "30 dB" };
    setupCombo (dirC, dirs, 2);
    setupCombo (anaC, anas, 4);
    setupCombo (rangeC, rngs, 4);

    for (auto* b : { &onB, &dynB, &invB, &bypB }) addAndMakeVisible (*b);

    setupKnob (freqK, "Frequency"); setupKnob (gainK, "Gain"); setupKnob (qK, "Q");
    setupKnob (thrK, "Threshold"); setupKnob (rangeK, "Dyn Range");
    setupKnob (outK, "Output"); setupKnob (panK, "Pan"); setupKnob (scaleK, "Gain Scale");
    setupKnob (attK, "Attack"); setupKnob (relK, "Release");

    auto& v = proc.apvts;
    aOut = std::make_unique<SA> (v, "out_gain", outK.s);
    aPan = std::make_unique<SA> (v, "out_pan", panK.s);
    aScale = std::make_unique<SA> (v, "gain_scale", scaleK.s);
    aAtt = std::make_unique<SA> (v, "dyn_attack", attK.s);
    aRel = std::make_unique<SA> (v, "dyn_release", relK.s);
    aAna = std::make_unique<CA> (v, "analyzer", anaC);
    aRng = std::make_unique<CA> (v, "disp_range", rangeC);
    aInv = std::make_unique<BA> (v, "invert", invB);
    aByp = std::make_unique<BA> (v, "bypass", bypB);

    selectBand (0);
    startTimerHz (30);
}

SpectraEQAudioProcessorEditor::~SpectraEQAudioProcessorEditor() { stopTimer(); }

void SpectraEQAudioProcessorEditor::setupKnob (Knob& k, const juce::String& name)
{
    k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    k.l.setText (name, juce::dontSendNotification);
    k.l.setJustificationType (juce::Justification::centred);
    k.l.setFont (12.0f);
    addAndMakeVisible (k.s);
    addAndMakeVisible (k.l);
}

void SpectraEQAudioProcessorEditor::setupCombo (juce::ComboBox& c, const char* const* items, int n)
{
    for (int i = 0; i < n; ++i) c.addItem (items[i], i + 1);
    addAndMakeVisible (c);
}

void SpectraEQAudioProcessorEditor::selectBand (int i)
{
    selected = i;
    display.selected = i;
    for (auto* a : { &aFreq, &aGain, &aQ, &aThr, &aRange }) a->reset();
    aType.reset(); aSlope.reset(); aCh.reset(); aDir.reset();
    aOn.reset(); aDyn.reset();

    auto& v = proc.apvts;
    aFreq = std::make_unique<SA> (v, pid (i, "freq"), freqK.s);
    aGain = std::make_unique<SA> (v, pid (i, "gain"), gainK.s);
    aQ = std::make_unique<SA> (v, pid (i, "q"), qK.s);
    aThr = std::make_unique<SA> (v, pid (i, "thr"), thrK.s);
    aRange = std::make_unique<SA> (v, pid (i, "range"), rangeK.s);
    aType = std::make_unique<CA> (v, pid (i, "type"), typeC);
    aSlope = std::make_unique<CA> (v, pid (i, "slope"), slopeC);
    aCh = std::make_unique<CA> (v, pid (i, "ch"), chC);
    aDir = std::make_unique<CA> (v, pid (i, "dir"), dirC);
    aOn = std::make_unique<BA> (v, pid (i, "on"), onB);
    aDyn = std::make_unique<BA> (v, pid (i, "dyn"), dynB);
    display.repaint();
}

void SpectraEQAudioProcessorEditor::timerCallback()
{
    display.updateAnalyzer();
    display.repaint();

    const auto& p = proc.bp[(size_t) selected];
    const int type = (int) p.type->load();
    const bool on = p.on->load() > 0.5f;
    const bool hasGain = typeHasGain (type);
    const bool dyn = hasGain && p.dyn->load() > 0.5f;
    gainK.s.setEnabled (hasGain);
    slopeC.setEnabled (typeIsCut (type));
    dynB.setEnabled (hasGain);
    thrK.s.setEnabled (dyn); rangeK.s.setEnabled (dyn); dirC.setEnabled (dyn);
    for (auto* c : { &freqK.s, &qK.s }) c->setEnabled (on);
}

void SpectraEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b2028));
    auto r = getLocalBounds().removeFromBottom (270);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (r.getY() + 135, 10.0f, (float) getWidth() - 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (11.0f);
    g.drawText ("Double-click: add/delete band   |   Drag: freq/gain (Shift = fine)   |   Wheel: Q   |   Right-click: menu",
                10, getHeight() - 16, getWidth() - 20, 14, juce::Justification::left);
}

void SpectraEQAudioProcessorEditor::placeKnob (Knob& k, juce::Rectangle<int> r)
{
    k.l.setBounds (r.removeFromTop (16));
    k.s.setBounds (r);
}

void SpectraEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto panel = area.removeFromBottom (270);
    display.setBounds (area);

    auto row1 = panel.removeFromTop (135).reduced (10, 6);
    auto row2 = panel.removeFromTop (125).reduced (10, 6);

    auto a = row1.removeFromLeft (130);
    bandC.setBounds (a.removeFromTop (28)); a.removeFromTop (8);
    onB.setBounds (a.removeFromTop (28));
    row1.removeFromLeft (10);

    auto b = row1.removeFromLeft (140);
    typeC.setBounds (b.removeFromTop (28)); b.removeFromTop (6);
    slopeC.setBounds (b.removeFromTop (28)); b.removeFromTop (6);
    chC.setBounds (b.removeFromTop (28));

    placeKnob (freqK, row1.removeFromLeft (105));
    placeKnob (gainK, row1.removeFromLeft (105));
    placeKnob (qK, row1.removeFromLeft (105));
    row1.removeFromLeft (14);

    auto d = row1.removeFromLeft (130);
    dynB.setBounds (d.removeFromTop (28)); d.removeFromTop (8);
    dirC.setBounds (d.removeFromTop (28));
    placeKnob (thrK, row1.removeFromLeft (105));
    placeKnob (rangeK, row1.removeFromLeft (105));

    placeKnob (outK, row2.removeFromLeft (105));
    placeKnob (panK, row2.removeFromLeft (105));
    placeKnob (scaleK, row2.removeFromLeft (105));
    row2.removeFromLeft (14);
    placeKnob (attK, row2.removeFromLeft (105));
    placeKnob (relK, row2.removeFromLeft (105));
    row2.removeFromLeft (14);
    auto e = row2.removeFromLeft (130);
    anaC.setBounds (e.removeFromTop (28)); e.removeFromTop (8);
    rangeC.setBounds (e.removeFromTop (28));
    row2.removeFromLeft (10);
    auto f = row2.removeFromLeft (110);
    invB.setBounds (f.removeFromTop (28)); f.removeFromTop (8);
    bypB.setBounds (f.removeFromTop (28));
}
