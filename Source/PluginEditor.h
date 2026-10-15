#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Chaos palette (QGlitch identity — neon green / magenta digital decay)
namespace GlitchUI
{
    const juce::Colour bg      = juce::Colour (0xff0a0a0d);
    const juce::Colour panel   = juce::Colour (0xff121216);
    const juce::Colour neonG   = juce::Colour (0xff39ff14);
    const juce::Colour neonM   = juce::Colour (0xffff2fb3);
    const juce::Colour danger  = juce::Colour (0xffff4d00);
    const juce::Colour text    = juce::Colour (0xfff2f2f5);
    const juce::Colour textDim = juce::Colour (0xff8a8a95);

    inline void drawGlitchText (juce::Graphics& g, const juce::String& label,
                                juce::Rectangle<int> area, const juce::Font& font,
                                juce::Justification just = juce::Justification::centred)
    {
        // Chromatic-aberration "broken signal" type.
        g.setFont (font);
        g.setColour (neonM.withAlpha (0.85f));
        g.drawText (label, area.translated (-2, 1), just, true);
        g.setColour (neonG.withAlpha (0.85f));
        g.drawText (label, area.translated (2, -1), just, true);
        g.setColour (text);
        g.drawText (label, area, just, true);
    }
}

//==============================================================================
// Big trigger pad — the hero of the interface. Looks like pressing it will
// break something (in a good way).
class GlitchPad : public juce::TextButton
{
public:
    GlitchPad (const juce::String& label, juce::Colour neon)
        : juce::TextButton (label), neonColour (neon) {}

    void setFlash (float f) { flash = f; }
    float getFlash() const  { return flash; }

    void paintButton (juce::Graphics& g, bool /*isHover*/, bool /*isDown*/) override
    {
        auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // Base
        g.setColour (GlitchUI::panel);
        g.fillRoundedRectangle (r, 6.0f);

        // Hazard stripe along the top edge
        auto stripe = r.removeFromTop (8.0f);
        g.saveState();
        g.reduceClipRegion (stripe.toType<int>());
        g.setColour (neonColour.withAlpha (on ? 0.9f : 0.35f));
        const float sw = 14.0f;
        for (float x = stripe.getX() - sw; x < stripe.getRight() + sw; x += sw * 2.0f)
        {
            juce::Path tri;
            tri.addQuadrilateral (x, stripe.getBottom(), x + sw, stripe.getBottom(),
                                  x + sw * 0.5f, stripe.getY(), x - sw * 0.5f, stripe.getY());
            g.fillPath (tri);
        }
        g.restoreState();

        // Active glow
        if (on)
        {
            const float glow = 0.25f + 0.55f * flash;
            g.setColour (neonColour.withAlpha (glow * 0.35f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            g.setColour (neonColour.withAlpha (juce::jmin (1.0f, 0.6f + flash)));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 2.5f + 2.0f * flash);
        }
        else
        {
            g.setColour (neonColour.withAlpha (0.28f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 1.5f);
        }

        // Label — distorted
        auto labelArea = getLocalBounds().reduced (8);
        labelArea.removeFromTop (10);
        GlitchUI::drawGlitchText (g, getButtonText(), labelArea,
                                  juce::Font (30.0f, juce::Font::bold));

        // Sub-caption
        g.setFont (11.0f);
        g.setColour (on ? neonColour : GlitchUI::textDim);
        g.drawText (on ? "ENGAGED" : "ARMED", getLocalBounds().reduced (8).removeFromBottom (22),
                    juce::Justification::centred, true);
    }

private:
    juce::Colour neonColour;
    float flash = 0.0f;
};

//==============================================================================
// Scanline / CRT decay overlay (non-interactive, sits on top).
class ScanlineOverlay : public juce::Component
{
public:
    ScanlineOverlay() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        for (int y = 0; y < r.getHeight(); y += 3)
            g.fillRect (0, y, r.getWidth(), 1);
        // Occasional tear line drifting down
        const int tearY = (int) (tearPhase * r.getHeight()) % r.getHeight();
        g.setColour (GlitchUI::neonG.withAlpha (0.10f));
        g.fillRect (0, tearY, r.getWidth(), 2);
        g.setColour (GlitchUI::neonM.withAlpha (0.08f));
        g.fillRect (0, (tearY + 40) % r.getHeight(), r.getWidth(), 1);
    }
    void advance() { tearPhase += 0.011; if (tearPhase >= 1.0) tearPhase -= 1.0; }
private:
    double tearPhase = 0.0;
};

//==============================================================================
// Thin live scope of the wet output.
class GlitchScope : public juce::Component
{
public:
    explicit GlitchScope (QGlitchProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, false);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff08080a));
        g.fillRect (r);
        g.setColour (GlitchUI::neonG.withAlpha (0.9f));

        const int N = QGlitchProcessor::scopeSize;
        const int wp = processor.scopeWritePos.load();
        juce::Path path;
        const float midY = r.getCentreY();
        const float amp  = r.getHeight() * 0.46f;
        for (int i = 0; i < N; ++i)
        {
            const int idx = (wp + i) % N;
            const float x = r.getX() + (float) i / (float) (N - 1) * r.getWidth();
            const float y = midY - juce::jlimit (-1.0f, 1.0f, processor.scopeData[idx]) * amp;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
private:
    QGlitchProcessor& processor;
};

//==============================================================================
struct QGlitchFactoryPreset
{
    const char* name;
    bool stutter, reverse, tape, crush;
    int  rate; // choice index
    float mix, bits, sr;
};

//==============================================================================
class QGlitchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QGlitchEditor (QGlitchProcessor&);
    ~QGlitchEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Presets
    void refreshPresetList();
    void applyPreset (const QGlitchFactoryPreset& p);
    void applyUserPreset (const juce::File& f);
    void saveUserPreset();
    juce::File getPresetDir();
    juce::File getFavoritesFile();
    juce::StringArray getFavorites();
    void setFavorite (const juce::String& name, bool fav);
    bool isFavorite (const juce::String& name);
    void onPresetChosen();

    QGlitchProcessor& processor;

    // Hero pads
    GlitchPad padStutter  { "STUTTER",  GlitchUI::neonG };
    GlitchPad padReverse  { "REVERSE",  GlitchUI::neonM };
    GlitchPad padTape     { "TAPE STOP", GlitchUI::danger };
    GlitchPad padCrush    { "CRUSH",    GlitchUI::neonG };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attStutter, attReverse, attTape, attCrush;

    // Minimal knobs
    juce::Slider knobMix, knobBits, knobSr;
    juce::ComboBox comboRate;
    juce::Label lblRate, lblMix, lblBits, lblSr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attMix, attBits, attSr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attRate;

    // Preset bar
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" };
    juce::TextButton heartBtn { "\u2665" };
    juce::ImageComponent logoImg;
    juce::String currentPresetName = "Init";

    GlitchScope scope;
    ScanlineOverlay scanlines;

    struct PresetEntry { juce::String name; int factoryIdx; juce::File file; };
    std::vector<PresetEntry> presetEntries;
    int presetIdBase = 1;

    static constexpr int numFactoryPresets = 6;
    static const QGlitchFactoryPreset factoryPresets[numFactoryPresets];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QGlitchEditor)
};
