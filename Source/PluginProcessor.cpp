#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace eq
{
static Coef norm (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

static Coef rbj (int type, double f, double gDb, double q, double fs)
{
    f = juce::jlimit (5.0, fs * 0.49, f);
    q = std::max (q, 0.05);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gDb / 40.0);
    switch (type)
    {
        case Bell:
            return norm (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
        case LowShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            return norm (A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw),
                         A * ((A + 1) - (A - 1) * cw - s), (A + 1) + (A - 1) * cw + s,
                         -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s);
        }
        case HighShelf:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            return norm (A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw),
                         A * ((A + 1) + (A - 1) * cw - s), (A + 1) - (A - 1) * cw + s,
                         2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s);
        }
        case Notch:
            return norm (1, -2 * cw, 1, 1 + alpha, -2 * cw, 1 - alpha);
        case BandPass:
            return norm (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
        case LowCut:   // 2nd-order highpass
            return norm ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        case HighCut:  // 2nd-order lowpass
            return norm ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }
    return {};
}

static Coef firstOrder (bool hp, double f, double fs)
{
    f = juce::jlimit (5.0, fs * 0.49, f);
    const double K = std::tan (juce::MathConstants<double>::pi * f / fs);
    return hp ? norm (1, -1, 0, K + 1, K - 1, 0) : norm (K, K, 0, K + 1, K - 1, 0);
}

Design design (int type, double f, double g, double q, int slopeIdx, double fs)
{
    Design d;
    auto add = [&] (Coef c) { if (d.n < maxStages) d.st[(size_t) d.n++] = c; };
    switch (type)
    {
        case Bell:      add (rbj (Bell, f, g, q, fs)); break;
        case Notch:     add (rbj (Notch, f, 0, q, fs)); break;
        case BandPass:  add (rbj (BandPass, f, 0, q, fs)); break;
        case LowShelf:  add (rbj (LowShelf, f, g, juce::jlimit (0.1, 3.0, q * 0.7071), fs)); break;
        case HighShelf: add (rbj (HighShelf, f, g, juce::jlimit (0.1, 3.0, q * 0.7071), fs)); break;
        case TiltShelf:
            add (rbj (LowShelf, f, -g * 0.5, 0.5, fs));
            add (rbj (HighShelf, f, g * 0.5, 0.5, fs));
            break;
        case FlatTilt:
            add (rbj (LowShelf, f, -g * 0.5, 0.15, fs));
            add (rbj (HighShelf, f, g * 0.5, 0.15, fs));
            break;
        case LowCut:
        case HighCut:
        {
            const bool hp = type == LowCut;
            const int order = slopeOrders[juce::jlimit (0, numSlopes - 1, slopeIdx)];
            if (order & 1) add (firstOrder (hp, f, fs));
            for (int k = 0; k < order / 2; ++k)
            {
                double qk = 1.0 / (2.0 * std::sin (juce::MathConstants<double>::pi * (2 * k + 1) / (2.0 * order)));
                if (k == 0) qk *= std::max (1.0, q);   // user Q adds resonance above 1.0
                add (rbj (type, f, 0, qk, fs));
            }
            break;
        }
    }
    return d;
}

double magnitudeDb (const Design& d, double freq, double fs)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double mag = 1.0;
    for (int i = 0; i < d.n; ++i)
    {
        const auto& c = d.st[(size_t) i];
        mag *= std::abs (c.b0 + c.b1 * z1 + c.b2 * z2) / std::max (1e-12, std::abs (1.0 + c.a1 * z1 + c.a2 * z2));
    }
    return 20.0 * std::log10 (std::max (mag, 1e-9));
}
} // namespace eq

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SpectraEQAudioProcessor::createLayout()
{
    using namespace eq;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto fmtDb = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    auto fmtHz = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, 1) + " Hz"; };
    auto parseHz = [] (const juce::String& s) { auto t = s.trim().toLowerCase(); float v = t.getFloatValue(); if (t.contains ("k")) v *= 1000.0f; return v; };
    auto fmtQ = [] (float v, int) { return juce::String (v, 2); };
    auto fmtMs = [] (float v, int) { return juce::String (v, 0) + " ms"; };

    for (int i = 0; i < numBands; ++i)
    {
        const auto n = "Band " + juce::String (i + 1) + " ";
        const float defFreq = 20.0f * std::pow (1000.0f, (float) i / (float) (numBands - 1));

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (i, "on"), 1 }, n + "Enabled", false));

        juce::StringArray types, chans, slopes;
        for (auto* s : typeNames) types.add (s);
        for (auto* s : chanNames) chans.add (s);
        for (auto* s : slopeNames) slopes.add (s);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (i, "type"), 1 }, n + "Shape", types, 0));

        juce::NormalisableRange<float> fr (10.0f, 30000.0f, 0.01f);
        fr.setSkewForCentre (1000.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i, "freq"), 1 }, n + "Frequency", fr, defFreq,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i, "gain"), 1 }, n + "Gain",
            juce::NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));

        juce::NormalisableRange<float> qr (0.025f, 40.0f, 0.001f);
        qr.setSkewForCentre (1.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i, "q"), 1 }, n + "Q", qr, 1.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtQ)));

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (i, "slope"), 1 }, n + "Slope", slopes, 1));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (i, "ch"), 1 }, n + "Stereo Placement", chans, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (i, "dyn"), 1 }, n + "Dynamic", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i, "thr"), 1 }, n + "Threshold",
            juce::NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -30.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (i, "range"), 1 }, n + "Dynamic Range",
            juce::NormalisableRange<float> (-30.0f, 30.0f, 0.1f), -6.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid (i, "dir"), 1 }, n + "Dynamic Direction",
            juce::StringArray { "Above", "Below" }, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_gain", 1 }, "Output Level",
        juce::NormalisableRange<float> (-36.0f, 36.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtDb)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out_pan", 1 }, "Output Pan",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v == 0 ? juce::String ("C") : juce::String ((int) std::abs (v)) + (v < 0 ? " L" : " R"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain_scale", 1 }, "EQ Gain Scale",
        juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) v) + " %"; })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dyn_attack", 1 }, "Dynamic Attack",
        juce::NormalisableRange<float> (0.5f, 500.0f, 0.1f, 0.4f), 10.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtMs)));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dyn_release", 1 }, "Dynamic Release",
        juce::NormalisableRange<float> (5.0f, 3000.0f, 1.0f, 0.4f), 100.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmtMs)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "analyzer", 1 }, "Analyzer",
        juce::StringArray { "Off", "Pre", "Post", "Pre + Post" }, 3));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "disp_range", 1 }, "Display Range",
        juce::StringArray { "3 dB", "6 dB", "12 dB", "30 dB" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "invert", 1 }, "Phase Invert", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

SpectraEQAudioProcessor::SpectraEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    using namespace eq;
    for (int i = 0; i < numBands; ++i)
    {
        auto g = [&] (const char* n) { return apvts.getRawParameterValue (pid (i, n)); };
        bp[(size_t) i] = { g ("on"), g ("type"), g ("freq"), g ("gain"), g ("q"), g ("slope"), g ("ch"), g ("dyn"), g ("thr"), g ("range"), g ("dir") };
        dynOffset[(size_t) i].store (0.0f);
    }
    outGain = apvts.getRawParameterValue ("out_gain");
    outPan = apvts.getRawParameterValue ("out_pan");
    gainScale = apvts.getRawParameterValue ("gain_scale");
    attack = apvts.getRawParameterValue ("dyn_attack");
    release = apvts.getRawParameterValue ("dyn_release");
    invert = apvts.getRawParameterValue ("invert");
    bypass = apvts.getRawParameterValue ("bypass");
    ring[0].fill (0.0f);
    ring[1].fill (0.0f);
}

bool SpectraEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SpectraEQAudioProcessor::prepareToPlay (double sr, int)
{
    fs = sr;
    for (auto& r : rt) { r.reset(); r.wasOn = false; }
}

eq::Design SpectraEQAudioProcessor::makeDesign (int i, double extra) const
{
    const auto& p = bp[(size_t) i];
    const int type = (int) p.type->load();
    double g = 0.0;
    if (eq::typeHasGain (type))
        g = juce::jlimit (-60.0, 60.0, (double) p.gain->load() * (double) gainScale->load() * 0.01 + extra);
    return eq::design (type, p.freq->load(), g, p.q->load(), (int) p.slope->load(), getSampleRate() > 0 ? getSampleRate() : 44100.0);
}

void SpectraEQAudioProcessor::readRing (bool post, float* dest, int n) const
{
    const int pos = ringPos.load();
    const auto& r = ring[post ? 1 : 0];
    for (int i = 0; i < n; ++i)
        dest[i] = r[(size_t) ((pos - n + i) & (ringSize - 1))];
}

void SpectraEQAudioProcessor::processBand (int i, float* l, float* r, int n, double attC, double relC)
{
    using namespace eq;
    auto& p = bp[(size_t) i];
    auto& b = rt[(size_t) i];

    if (p.on->load() < 0.5f)
    {
        b.wasOn = false;
        dynOffset[(size_t) i].store (0.0f);
        return;
    }
    if (! b.wasOn) { b.reset(); b.wasOn = true; }

    const int type = (int) p.type->load();
    const int ch = (int) p.ch->load();
    const double freq = p.freq->load();
    const double q = p.q->load();
    const bool dyn = typeHasGain (type) && p.dyn->load() > 0.5f;
    double extra = 0.0;

    if (dyn)
    {
        if (b.detF != freq || b.detQ != q)
        {
            b.det = rbj (BandPass, freq, 0.0, juce::jlimit (0.7, 8.0, q), fs);
            b.detF = freq; b.detQ = q;
        }
        double env = b.env;
        for (int k = 0; k < n; ++k)
        {
            float x;
            switch (ch)
            {
                case Left:  x = l[k]; break;
                case Right: x = r[k]; break;
                case Side:  x = 0.5f * (l[k] - r[k]); break;
                default:    x = 0.5f * (l[k] + r[k]); break;
            }
            const auto& c = b.det;
            const double y = c.b0 * x + b.dz[0];
            b.dz[0] = c.b1 * x - c.a1 * y + b.dz[1];
            b.dz[1] = c.b2 * x - c.a2 * y;
            const double a = std::abs (y);
            env = a > env ? attC * env + (1.0 - attC) * a : relC * env + (1.0 - relC) * a;
        }
        b.env = env;
        const double level = 20.0 * std::log10 (env + 1e-9);
        const double thr = p.thr->load();
        const double over = p.dir->load() < 0.5f ? level - thr : thr - level;
        const double t = juce::jlimit (0.0, 1.0, over / 8.0);
        const double target = t * t * (3.0 - 2.0 * t);
        b.dynAmt += (target - b.dynAmt) * 0.35;
        extra = b.dynAmt * p.range->load();
    }
    else
        b.dynAmt = 0.0;

    dynOffset[(size_t) i].store ((float) extra);

    const std::array<float, 8> key { (float) type, (float) freq, p.gain->load(), (float) q, p.slope->load(), gainScale->load(), (float) extra, 0.0f };
    if (! b.keyValid || key != b.key)
    {
        b.d = makeDesign (i, extra);
        b.key = key;
        b.keyValid = true;
    }

    const auto& d = b.d;
    auto run = [&d] (double* z, double x)
    {
        for (int s = 0; s < d.n; ++s)
        {
            const auto& c = d.st[(size_t) s];
            double* zz = z + s * 2;
            const double y = c.b0 * x + zz[0];
            zz[0] = c.b1 * x - c.a1 * y + zz[1];
            zz[1] = c.b2 * x - c.a2 * y;
            x = y;
        }
        return x;
    };

    switch (ch)
    {
        case Stereo:
            for (int k = 0; k < n; ++k) { l[k] = (float) run (b.st[0], l[k]); r[k] = (float) run (b.st[1], r[k]); }
            break;
        case Left:
            for (int k = 0; k < n; ++k) l[k] = (float) run (b.st[0], l[k]);
            break;
        case Right:
            for (int k = 0; k < n; ++k) r[k] = (float) run (b.st[1], r[k]);
            break;
        case Mid:
            for (int k = 0; k < n; ++k)
            {
                const double m = 0.5 * ((double) l[k] + r[k]);
                const float dlt = (float) (run (b.st[0], m) - m);
                l[k] += dlt; r[k] += dlt;
            }
            break;
        case Side:
            for (int k = 0; k < n; ++k)
            {
                const double s = 0.5 * ((double) l[k] - r[k]);
                const float dlt = (float) (run (b.st[1], s) - s);
                l[k] += dlt; r[k] -= dlt;
            }
            break;
    }
}

void SpectraEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    auto pushRing = [this, n, l, r] (int which)
    {
        int pos = ringPos.load();
        for (int i = 0; i < n; ++i)
            ring[which][(size_t) ((pos + i) & (ringSize - 1))] = 0.5f * (l[i] + r[i]);
    };

    pushRing (0);

    if (bypass->load() < 0.5f)
    {
        const double attC = std::exp (-1.0 / (std::max (0.1f, attack->load()) * 0.001 * fs));
        const double relC = std::exp (-1.0 / (std::max (1.0f, release->load()) * 0.001 * fs));
        constexpr int chunk = 32;
        for (int s = 0; s < n; s += chunk)
        {
            const int len = std::min (chunk, n - s);
            for (int i = 0; i < eq::numBands; ++i)
                processBand (i, l + s, r + s, len, attC, relC);
        }

        const float g = juce::Decibels::decibelsToGain (outGain->load());
        const float pan = outPan->load() * 0.01f;
        const float pol = invert->load() > 0.5f ? -1.0f : 1.0f;
        const float gl = g * pol * (pan > 0 ? 1.0f - pan : 1.0f);
        const float gr = g * pol * (pan < 0 ? 1.0f + pan : 1.0f);
        for (int i = 0; i < n; ++i) { l[i] *= gl; r[i] *= gr; }
    }

    pushRing (1);
    ringPos.store ((ringPos.load() + n) & (ringSize - 1));
}

juce::AudioProcessorEditor* SpectraEQAudioProcessor::createEditor() { return new SpectraEQAudioProcessorEditor (*this); }

void SpectraEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SpectraEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectraEQAudioProcessor(); }
