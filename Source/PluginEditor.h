#pragma once
#include "PluginProcessor.h"

//==============================================================================
class EQDisplay : public juce::Component
{
public:
    explicit EQDisplay (SpectraEQAudioProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void updateAnalyzer();

    int selected = 0;
    std::function<void (int)> onSelect;

private:
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;

    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    double rangeDb() const;
    bool isOn (int i) const;
    juce::Point<float> nodePos (int i) const;
    int hitNode (juce::Point<float> p) const;
    void setReal (const juce::String& id, float v);
    void addBand (juce::Point<float> p);
    void showMenu (int band);
    void drawAnalyzer (juce::Graphics&, const std::vector<float>&, juce::Colour);
    static juce::Colour bandColour (int i);

    SpectraEQAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftData, fftIn, preSmooth, postSmooth;

    int dragBand = -1, hoverBand = -1;
    juce::Point<float> dragStartMouse, dragStartNode;
};

//==============================================================================
class SpectraEQAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectraEQAudioProcessorEditor (SpectraEQAudioProcessor&);
    ~SpectraEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider s;
        juce::Label l;
    };
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void selectBand (int i);
    void setupKnob (Knob&, const juce::String& name);
    void setupCombo (juce::ComboBox&, const char* const* items, int n);
    static void placeKnob (Knob&, juce::Rectangle<int>);

    SpectraEQAudioProcessor& proc;
    EQDisplay display;
    int selected = 0;

    juce::ComboBox bandC, typeC, slopeC, chC, dirC, anaC, rangeC;
    juce::ToggleButton onB { "On" }, dynB { "Dynamic" }, invB { "Invert" }, bypB { "Bypass" };
    Knob freqK, gainK, qK, thrK, rangeK, outK, panK, scaleK, attK, relK;

    std::unique_ptr<SA> aFreq, aGain, aQ, aThr, aRange, aOut, aPan, aScale, aAtt, aRel;
    std::unique_ptr<CA> aType, aSlope, aCh, aDir, aAna, aRng;
    std::unique_ptr<BA> aOn, aDyn, aInv, aByp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraEQAudioProcessorEditor)
};
