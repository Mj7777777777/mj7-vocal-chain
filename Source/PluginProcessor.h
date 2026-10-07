// MJ7 Vocal Chain - coeur du plugin : parametres, chaine de traitement, analyse, prereglages.
#pragma once
#include "Params.h"
#include "dsp/Denoise.h"
#include "dsp/Dynamics.h"
#include "dsp/Effects.h"
#include "dsp/Pitch.h"
#include <juce_dsp/juce_dsp.h>

class MJ7Processor final : public juce::AudioProcessor,
                           private juce::Timer,
                           private juce::Thread,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    enum AnalysisState { idle = 0, waiting, listening, computing, done, failed };

    MJ7Processor();
    ~MJ7Processor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MJ7 Vocal Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- interface (fil des messages) ---
    juce::AudioProcessorValueTreeState apvts;

    void startAnalysis();                       // bouton ANALYSER
    void undoAnalysis();                        // "Annuler l'analyse"
    AnalysisState analysisState() const noexcept { return (AnalysisState) state.load(); }
    float analysisProgress() const noexcept { return captureLen > 0 ? (float) capturePos.load() / (float) captureLen : 0.0f; }
    float analysisSeconds() const noexcept { return 12.0f; }
    bool hasAnalysis() const noexcept { return ! adjustments.empty(); }
    juce::StringArray summary;                  // resume lisible de la derniere analyse

    int currentPreset() const noexcept { return presetIndex; }
    void loadFactoryPreset (int index);
    bool savePresetFile (const juce::File&);
    bool loadPresetFile (const juce::File&);
    static juce::File presetFolder();
    void toggleAB();
    int abSlot() const noexcept { return abIndex; }

    // --- mesures lues par l'interface ---
    std::atomic<float> meter[mj7::kNumMeters];  // reduction de gain par module (dB, <= 0)
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, outLufs { -120.0f }, outRms { -120.0f };
    float detectedNote() const noexcept { return tune.detectedMidi.load(); }
    float targetNote() const noexcept { return tune.targetMidi.load(); }
    void readScope (float* dest, int n, bool input = false) const noexcept;

private:
    void timerCallback() override;
    void run() override;
    void parameterChanged (const juce::String&, float) override;
    void processChunk (float* L, float* R, int n);
    void applyAnalysis();
    void applyConfig (bool force);
    void pushNoiseProfile();
    void writeExtras (juce::ValueTree&) const;
    void readExtras (const juce::ValueTree&);
    void applyIntensity();
    void setParam (int pid, float realValue);
    float v (int pid) const noexcept { return raw[pid]->load (std::memory_order_relaxed); }
    bool on (int pid) const noexcept { return v (pid) > 0.5f; }

    std::atomic<float>* raw[mj7::kNumParams] = {};
    float sr = 48000.0f; double tempo = 120.0; int latency = 0, osLatency = 0;

    // --- chaine ---
    mj7::Smooth inGainSm, outGainSm, dlyWetSm, revWetSm;
    mj7::StereoBiquad hpf, eq[4], hicut, toneLow, toneMid, tonePres, toneAir, dcBlock, fltHp, fltLp;
    mj7::NoiseReducer denoise; mj7::Saturator satDirect;
    bool trackingNow = false, nrNow = false; std::atomic<int> latencyNow { 0 };
    std::vector<float> noiseDensity; float noiseBinHz = 0.0f;
    mj7::Gate gate; mj7::PitchCorrector tune;
    mj7::DynamicBand deEss, dyn1, dyn2;
    mj7::Compressor comp1, comp2, punch;
    mj7::Saturator sat; juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    mj7::FixedDelay satBypassL, satBypassR, bypassL, bypassR;
    mj7::Doubler doubler; mj7::StereoDelay delay; mj7::PlateReverb reverb;
    mj7::Limiter limiter; mj7::LevelMeter outMeter;
    float duckEnv = 0.0f; bool wasBypassed = false, dlyWasOn = true, revWasOn = true;
    static constexpr int chunk = 512;
    float tmpL[chunk] = {}, tmpR[chunk] = {}, wetL[chunk] = {}, wetR[chunk] = {}, fxL[chunk] = {}, fxR[chunk] = {};
    std::vector<float> monoR;

    static constexpr int scopeSize = 4096;
    float scope[scopeSize] = {}, scopeIn[scopeSize] = {}; std::atomic<int> scopePos { 0 }, scopeInPos { 0 };

    // --- analyse ---
    std::atomic<int> state { idle }, capturePos { 0 };
    std::atomic<bool> resultReady { false }, intensityDirty { false };
    std::vector<float> capture; int captureLen = 0, waited = 0; bool captureValid = false;
    mj7::AnalysisResult result; mj7::StyleTarget analysisTarget; float analysisRatio = 4.0f, analysisMakeup = 3.0f;
    struct Adjustment { int pid; float base, target; };
    std::vector<Adjustment> adjustments;

    int presetIndex = 0, abIndex = 0, prevKey = -1, prevScale = -1; juce::ValueTree abState[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MJ7Processor)
};
