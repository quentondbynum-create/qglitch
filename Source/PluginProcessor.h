#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>

//==============================================================================
/*
    QGlitch — live glitch/stutter FX for Headroom Audio.

    Signal path (per sample):
      stereo in -> 2 s circular buffer (always recording)
      wet = [reverse] -> [tape stop] -> [stutter] -> [crush]   (stutter wins ties)
      out = dry * (1 - mix) + wet * mix
      (no effect engaged => wet == dry, true bypass of the mangling)
*/
class QGlitchProcessor : public juce::AudioProcessor
{
public:
    QGlitchProcessor();
    ~QGlitchProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters ----
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr const char* kStutter     = "stutter";     // bool
    static constexpr const char* kReverse     = "reverse";     // bool
    static constexpr const char* kTapeStop    = "tapestop";     // bool
    static constexpr const char* kCrush       = "crush";       // bool
    static constexpr const char* kStutterRate = "stutterRate"; // choice 1/4..1/64
    static constexpr const char* kMix         = "mix";         // %, 0..100
    static constexpr const char* kCrushBits   = "crushBits";   // 2..16
    static constexpr const char* kCrushRate   = "crushRate";   // 1..40 (SR divider)

    // Beats per stutter-rate choice index (1/4, 1/8, 1/16, 1/32, 1/64).
    static constexpr float kRateBeats[5] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };

    // Lightweight scope feed for the UI (mono wet output, lock-free-ish).
    static constexpr int scopeSize = 512;
    float scopeData[scopeSize] = {};
    std::atomic<int> scopeWritePos { 0 };

private:
    // Circular capture buffer (2 s, stereo). writePosU is monotonic; the
    // actual index is writePosU % bufSize.
    juce::AudioBuffer<float> circBuf;
    int64_t writePosU = 0;
    int     bufSize   = 0;
    double  curSampleRate = 44100.0;

    // Stutter state
    bool    stutWasOn = false;
    int64_t stutStartU = 0;
    int     stutLen = 0;
    int     stutPos = 0;

    // Reverse state
    double  revPosU = 0.0;
    bool    revInit = false;

    // Tape-stop state
    double  tapeSpeed = 1.0;
    double  tapePosU  = 0.0;
    bool    tapeWasOn = false;
    bool    tapeResyncPending = false;

    // Bitcrusher state (per channel sample-and-hold)
    int   crushHoldCount[2] = { 0, 0 };
    float crushHeld[2]      = { 0.0f, 0.0f };

    float readInterp (int ch, double posU) const;
    float getHostBpm();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QGlitchProcessor)
};
