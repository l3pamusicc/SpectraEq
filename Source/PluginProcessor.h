#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <complex>

namespace eq
{
constexpr int numBands = 24;
constexpr int maxStages = 8;
constexpr int numTypes = 9;
constexpr int numChans = 5;
constexpr int numSlopes = 9;

enum Type { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt };
enum Chan { Stereo, Left, Right, Mid, Side };

inline const char* const typeNames[numTypes] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                                 "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
inline const char* const chanNames[numChans] = { "Stereo", "Left", "Right", "Mid", "Side" };
inline const char* const slopeNames[numSlopes] = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
                                                   "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
inline const int slopeOrders[numSlopes] = { 1, 2, 3, 4, 5, 6, 8, 12, 16 };

inline juce::String pid (int band, const char* name) { return "b" + juce::String (band) + "_" + name; }
inline bool typeHasGain (int t) { return t == Bell || t == LowShelf || t == HighShelf || t == TiltShelf || t == FlatTilt; }
inline bool typeIsCut (int t) { return t == LowCut || t == HighCut; }

struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct Design { std::array<Coef, maxStages> st {}; int n = 0; };

Design design (int type, double freq, double gainDb, double q, int slopeIdx, double fs);
double magnitudeDb (const Design& d, double freq, double fs);
}

struct BandParams
{
    std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *ch, *dyn, *thr, *range, *dir;
};

class SpectraEQAudioProcessor : public juce::AudioProcessor
{
public:
    SpectraEQAudioProcessor();
    ~SpectraEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SpectraEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // shared with editor
    eq::Design makeDesign (int band, double extraGainDb) const;
    void readRing (bool post, float* dest, int n) const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<BandParams, eq::numBands> bp {};
    std::array<std::atomic<float>, eq::numBands> dynOffset;

    static constexpr int ringSize = 16384;

private:
    struct Runtime
    {
        double st[2][eq::maxStages * 2] {};
        eq::Design d;
        std::array<float, 8> key {};
        bool keyValid = false, wasOn = false;
        eq::Coef det;
        double dz[2] {};
        double env = 0, dynAmt = 0, detF = -1, detQ = -1;
        void reset() { for (auto& s : st) for (auto& v : s) v = 0; dz[0] = dz[1] = 0; env = 0; dynAmt = 0; keyValid = false; detF = -1; }
    };

    void processBand (int i, float* l, float* r, int n, double attC, double relC);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<Runtime, eq::numBands> rt;
    std::atomic<float> *outGain, *outPan, *gainScale, *attack, *release, *invert, *bypass;
    std::array<float, ringSize> ring[2];
    std::atomic<int> ringPos { 0 };
    double fs = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraEQAudioProcessor)
};
