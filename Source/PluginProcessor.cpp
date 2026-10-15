#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
QGlitchProcessor::QGlitchProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QGlitchParams", createParameterLayout())
{
}

QGlitchProcessor::~QGlitchProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout QGlitchProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterBool> (kStutter,  "Stutter",  false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (kReverse,  "Reverse",  false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (kTapeStop, "Tape Stop", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (kCrush,    "Crush",    false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        kStutterRate, "Stutter Rate",
        juce::StringArray ({ "1/4", "1/8", "1/16", "1/32", "1/64" }), 2));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kMix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kCrushBits, "Crush Bits", juce::NormalisableRange<float> (2.0f, 16.0f, 1.0f), 8.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kCrushRate, "Crush Rate", juce::NormalisableRange<float> (1.0f, 40.0f, 1.0f), 12.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
void QGlitchProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    curSampleRate = sampleRate;
    bufSize = (int) (2.0 * sampleRate) + 64;
    circBuf.setSize (2, bufSize);
    circBuf.clear();

    writePosU = 0;
    stutWasOn = false; stutStartU = 0; stutLen = 0; stutPos = 0;
    revInit = false; revPosU = 0.0;
    tapeSpeed = 1.0; tapePosU = 0.0; tapeWasOn = false; tapeResyncPending = false;
    crushHoldCount[0] = crushHoldCount[1] = 0;
    crushHeld[0] = crushHeld[1] = 0.0f;

    for (int i = 0; i < scopeSize; ++i)
        scopeData[i] = 0.0f;
    scopeWritePos.store (0);
}

void QGlitchProcessor::releaseResources() {}

bool QGlitchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float QGlitchProcessor::readInterp (int ch, double posU) const
{
    if (bufSize <= 0)
        return 0.0f;

    auto* d = circBuf.getReadPointer (ch);
    double p = std::fmod (posU, (double) bufSize);
    if (p < 0.0) p += bufSize;

    int i0 = (int) p;
    int i1 = i0 + 1;
    if (i1 >= bufSize) i1 -= bufSize;
    float frac = (float) (p - i0);
    return d[i0] * (1.0f - frac) + d[i1] * frac;
}

float QGlitchProcessor::getHostBpm()
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getBpm().hasValue())
                return (float) *pos->getBpm();
    return 120.0f;
}

void QGlitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh  = juce::jmin (2, buffer.getNumChannels());
    const int nSmp = buffer.getNumSamples();

    auto* stutterP = apvts.getRawParameterValue (kStutter);
    auto* reverseP = apvts.getRawParameterValue (kReverse);
    auto* tapeP    = apvts.getRawParameterValue (kTapeStop);
    auto* crushP   = apvts.getRawParameterValue (kCrush);
    auto* mixP     = apvts.getRawParameterValue (kMix);
    auto* bitsP    = apvts.getRawParameterValue (kCrushBits);
    auto* rateP    = apvts.getRawParameterValue (kCrushRate);

    auto* rateChoice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (kStutterRate));
    const int rateIdx = rateChoice != nullptr ? rateChoice->getIndex() : 2;

    const float bpm = getHostBpm();
    const float sliceBeats = kRateBeats[juce::jlimit (0, 4, rateIdx)];
    const int sliceLen = juce::jmax (64, (int) (60.0f / bpm * sliceBeats * (float) curSampleRate));

    const float mix   = juce::jlimit (0.0f, 1.0f, *mixP / 100.0f);
    const float bits  = *bitsP;
    const int   crushDiv = juce::jmax (1, (int) std::round (*rateP));
    const float levels = std::pow (2.0f, bits);

    auto* left  = circBuf.getWritePointer (0);
    auto* right = circBuf.getNumChannels() > 1 ? circBuf.getWritePointer (1) : nullptr;

    for (int s = 0; s < nSmp; ++s)
    {
        const float inL = buffer.getSample (0, s);
        const float inR = nCh > 1 ? buffer.getSample (1, s) : inL;

        // 1) Record into the circular buffer.
        const int wIdx = (int) (writePosU % bufSize);
        left[wIdx] = inL;
        if (right != nullptr) right[wIdx] = inR;

        const bool stutOn = *stutterP > 0.5f;
        const bool revOn  = *reverseP  > 0.5f;
        const bool tapeOn = *tapeP     > 0.5f;
        const bool crshOn = *crushP    > 0.5f;

        float wetL = inL, wetR = inR;

        // ---- Reverse (continuous backwards read, 1.5 s window) ----
        if (revOn && ! stutOn && ! tapeOn)
        {
            if (! revInit) { revPosU = (double) writePosU - 64.0; revInit = true; }
            revPosU -= 1.0;

            const double winStart = (double) writePosU - 1.5 * curSampleRate;
            double r = revPosU;
            float wRev = 1.0f;

            if (r < winStart + 64.0 && r >= winStart)
            {
                // Raised-cosine crossfade into the jump-back point.
                const double rem = r - winStart; // 0..64
                const float w = 0.5f + 0.5f * std::cos ((float) (juce::MathConstants<double>::pi * rem / 64.0));
                const double jumpPos = (double) writePosU - rem;
                wetL = readInterp (0, r) * (1.0f - w) + readInterp (0, jumpPos) * w;
                wetR = readInterp (nCh > 1 ? 1 : 0, r) * (1.0f - w)
                     + readInterp (nCh > 1 ? 1 : 0, jumpPos) * w;
                wRev = 0.0f; // handled
            }
            if (r < winStart)
            {
                revPosU = (double) writePosU - 64.0;
                r = revPosU;
            }
            if (wRev > 0.5f)
            {
                wetL = readInterp (0, r);
                wetR = readInterp (nCh > 1 ? 1 : 0, r);
            }
        }
        else
        {
            revInit = false;
        }

        // ---- Tape stop (speed ramp 1 -> 0, fade to silence) ----
        if (tapeOn && ! stutOn)
        {
            if (! tapeWasOn) { tapePosU = (double) writePosU - 128.0; }
            // Move speed toward 0 with ~0.7 s time constant.
            const double coeff = std::exp (-1.0 / (0.7 * curSampleRate));
            tapeSpeed = tapeSpeed * coeff;
            if (tapeSpeed < 0.0005) tapeSpeed = 0.0;
            tapePosU += tapeSpeed;

            const float fade = juce::jmin (1.0f, (float) (tapeSpeed * 4.0));
            wetL = readInterp (0, tapePosU) * fade;
            wetR = readInterp (nCh > 1 ? 1 : 0, tapePosU) * fade;
        }
        else if (tapeWasOn && ! tapeOn)
        {
            // Released: snap speed back and re-sync near the write head.
            tapeSpeed = 1.0;
            tapeResyncPending = true;
        }
        if (tapeResyncPending && ! tapeOn)
        {
            tapePosU = (double) writePosU - 128.0;
            tapeResyncPending = false;
        }
        tapeWasOn = tapeOn;

        // ---- Stutter (loop a tempo-synced slice; wins over reverse/tape) ----
        if (stutOn)
        {
            if (! stutWasOn || stutLen != sliceLen)
            {
                stutLen   = sliceLen;
                stutStartU = writePosU - stutLen;
                stutPos   = 0;
            }
            constexpr int xf = 24;
            float sL, sR;
            if (stutPos >= stutLen - xf)
            {
                const float t = (float) (stutPos - (stutLen - xf)) / (float) xf;
                const float w = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
                sL = readInterp (0, (double) (stutStartU + stutPos)) * (1.0f - w)
                   + readInterp (0, (double) (stutStartU + stutPos - (stutLen - xf))) * w;
                sR = readInterp (nCh > 1 ? 1 : 0, (double) (stutStartU + stutPos)) * (1.0f - w)
                   + readInterp (nCh > 1 ? 1 : 0, (double) (stutStartU + stutPos - (stutLen - xf))) * w;
            }
            else
            {
                sL = readInterp (0, (double) (stutStartU + stutPos));
                sR = readInterp (nCh > 1 ? 1 : 0, (double) (stutStartU + stutPos));
            }
            wetL = sL; wetR = sR;
            if (++stutPos >= stutLen) stutPos = 0;
        }
        stutWasOn = stutOn;

        // ---- Bitcrusher ----
        if (crshOn)
        {
            for (int ch = 0; ch < nCh; ++ch)
            {
                float x = (ch == 0) ? wetL : wetR;
                float q = std::floor (x * levels + 0.5f) / levels;
                if (++crushHoldCount[ch] >= crushDiv)
                {
                    crushHoldCount[ch] = 0;
                    crushHeld[ch] = q;
                }
                if (ch == 0) wetL = crushHeld[ch]; else wetR = crushHeld[ch];
            }
        }
        else
        {
            crushHoldCount[0] = crushHoldCount[1] = 0;
        }

        // ---- Mix ----
        const float outL = inL * (1.0f - mix) + wetL * mix;
        const float outR = inR * (1.0f - mix) + wetR * mix;
        buffer.setSample (0, s, outL);
        if (nCh > 1) buffer.setSample (1, s, outR);

        // Scope feed (mono wet, decimated).
        if ((s & 7) == 0)
        {
            const int wp = scopeWritePos.load();
            scopeData[wp] = 0.5f * (wetL + wetR);
            scopeWritePos.store ((wp + 1) % scopeSize);
        }

        ++writePosU;
    }
}

//==============================================================================
juce::AudioProcessorEditor* QGlitchProcessor::createEditor() { return new QGlitchEditor (*this); }
bool QGlitchProcessor::hasEditor() const { return true; }
const juce::String QGlitchProcessor::getName() const { return "QGlitch"; }
bool QGlitchProcessor::acceptsMidi() const { return false; }
bool QGlitchProcessor::producesMidi() const { return false; }
bool QGlitchProcessor::isMidiEffect() const { return false; }
double QGlitchProcessor::getTailLengthSeconds() const { return 0.0; }
int QGlitchProcessor::getNumPrograms() { return 1; }
int QGlitchProcessor::getCurrentProgram() { return 0; }
void QGlitchProcessor::setCurrentProgram (int) {}
const juce::String QGlitchProcessor::getProgramName (int) { return {}; }
void QGlitchProcessor::changeProgramName (int, const juce::String&) {}

void QGlitchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (state.isValid())
    {
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void QGlitchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QGlitchProcessor();
}
