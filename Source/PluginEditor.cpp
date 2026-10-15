#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

//==============================================================================
const QGlitchFactoryPreset QGlitchEditor::factoryPresets[numFactoryPresets] =
{
    { "Init",          false, false, false, false, 2, 100.0f,  8.0f, 12.0f },
    { "Stutter 16",    true,  false, false, false, 2, 100.0f,  8.0f, 12.0f },
    { "Reverse Wash",  false, true,  false, false, 2,  80.0f,  8.0f, 12.0f },
    { "Tape Stop Drop",false, false, true,  false, 2, 100.0f,  8.0f, 12.0f },
    { "Bit Destroyer", false, false, false, true,  2, 100.0f,  3.0f, 24.0f },
    { "Total Chaos",   true,  false, false, true,  3, 100.0f,  4.0f, 20.0f },
};

//==============================================================================
QGlitchEditor::QGlitchEditor (QGlitchProcessor& p)
    : AudioProcessorEditor (p), processor (p), scope (p)
{
    setSize (760, 580);

    // ---- Logo ----
    auto logo = juce::ImageCache::getFromMemory (BinaryData::headroomlogo_png,
                                                 BinaryData::headroomlogo_pngSize);
    logoImg.setImage (logo);
    addAndMakeVisible (logoImg);

    // ---- Hero pads ----
    for (auto* pad : { &padStutter, &padReverse, &padTape, &padCrush })
    {
        pad->setClickingTogglesState (true);
        addAndMakeVisible (pad);
    }
    attStutter = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, QGlitchProcessor::kStutter, padStutter);
    attReverse = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, QGlitchProcessor::kReverse, padReverse);
    attTape = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, QGlitchProcessor::kTapeStop, padTape);
    attCrush = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, QGlitchProcessor::kCrush, padCrush);

    // ---- Minimal knobs ----
    auto setupKnob = [this] (juce::Slider& s, const juce::String& id,
                             std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& att,
                             juce::Colour neon)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setColour (juce::Slider::rotarySliderFillColourId, neon);
        s.setColour (juce::Slider::thumbColourId, neon);
        s.setColour (juce::Slider::textBoxTextColourId, GlitchUI::text);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (s);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, id, s);
    };
    setupKnob (knobMix,  QGlitchProcessor::kMix,       attMix,  GlitchUI::neonG);
    setupKnob (knobBits, QGlitchProcessor::kCrushBits, attBits, GlitchUI::neonM);
    setupKnob (knobSr,   QGlitchProcessor::kCrushRate, attSr,   GlitchUI::neonM);

    comboRate.setColour (juce::ComboBox::backgroundColourId, GlitchUI::panel);
    comboRate.setColour (juce::ComboBox::textColourId, GlitchUI::text);
    comboRate.setColour (juce::ComboBox::arrowColourId, GlitchUI::neonG);
    addAndMakeVisible (comboRate);
    attRate = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, QGlitchProcessor::kStutterRate, comboRate);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, GlitchUI::textDim);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };
    setupLabel (lblRate, "RATE");
    setupLabel (lblMix,  "MIX");
    setupLabel (lblBits, "BITS");
    setupLabel (lblSr,   "S/R");

    // ---- Preset bar ----
    presetBox.setColour (juce::ComboBox::backgroundColourId, GlitchUI::panel);
    presetBox.setColour (juce::ComboBox::textColourId, GlitchUI::text);
    presetBox.setColour (juce::ComboBox::arrowColourId, GlitchUI::neonM);
    presetBox.onChange = [this] { onPresetChosen(); };
    addAndMakeVisible (presetBox);

    auto setupSmallBtn = [this] (juce::TextButton& b, const juce::String& t)
    {
        b.setButtonText (t);
        b.setColour (juce::TextButton::buttonColourId, GlitchUI::panel);
        b.setColour (juce::TextButton::textColourOffId, GlitchUI::text);
        addAndMakeVisible (b);
    };
    setupSmallBtn (prevBtn, "<");
    setupSmallBtn (nextBtn, ">");
    setupSmallBtn (saveBtn, "SAVE");
    prevBtn.onClick = [this]
    {
        const int cur = presetBox.getSelectedItemIndex();
        if (cur > 0) presetBox.setSelectedItemIndex (cur - 1);
    };
    nextBtn.onClick = [this]
    {
        const int cur = presetBox.getSelectedItemIndex();
        if (cur < presetBox.getNumItems() - 1) presetBox.setSelectedItemIndex (cur + 1);
    };
    saveBtn.onClick = [this] { saveUserPreset(); };

    heartBtn.setClickingTogglesState (true);
    heartBtn.setColour (juce::TextButton::buttonColourId, GlitchUI::panel);
    heartBtn.setColour (juce::TextButton::textColourOffId, GlitchUI::textDim);
    heartBtn.setColour (juce::TextButton::textColourOnId, GlitchUI::neonM);
    heartBtn.onClick = [this]
    {
        setFavorite (currentPresetName, heartBtn.getToggleState());
        refreshPresetList();
    };
    addAndMakeVisible (heartBtn);

    addAndMakeVisible (scope);
    addAndMakeVisible (scanlines); // last = on top

    refreshPresetList();
    startTimerHz (30);
}

QGlitchEditor::~QGlitchEditor()
{
    stopTimer();
}

//==============================================================================
void QGlitchEditor::paint (juce::Graphics& g)
{
    g.fillAll (GlitchUI::bg);

    // Distorted title next to the logo
    GlitchUI::drawGlitchText (g, "QGLITCH",
                              juce::Rectangle<int> (52, 10, 220, 44),
                              juce::Font (34.0f, juce::Font::bold),
                              juce::Justification::centredLeft);
    g.setFont (11.0f);
    g.setColour (GlitchUI::textDim);
    g.drawText ("HEADROOM AUDIO // SIGNAL DESTROYER",
                juce::Rectangle<int> (54, 44, 300, 14),
                juce::Justification::centredLeft, true);

    // Faint watermark behind the pads
    g.setFont (juce::Font (120.0f, juce::Font::bold));
    g.setColour (GlitchUI::neonG.withAlpha (0.04f));
    g.drawText ("GLITCH", getLocalBounds().withY (60).withHeight (360),
                juce::Justification::centred, true);
}

void QGlitchEditor::resized()
{
    logoImg.setBounds (12, 16, 32, 32);

    // Preset bar, right side of the top bar
    const int topY = 16;
    heartBtn.setBounds  (760 - 16 - 36, topY, 36, 32);
    saveBtn.setBounds   (760 - 16 - 36 - 8 - 64, topY, 64, 32);
    nextBtn.setBounds   (760 - 16 - 36 - 8 - 64 - 8 - 30, topY, 30, 32);
    prevBtn.setBounds   (760 - 16 - 36 - 8 - 64 - 8 - 30 - 8 - 30, topY, 30, 32);
    presetBox.setBounds (760 - 16 - 36 - 8 - 64 - 8 - 30 - 8 - 30 - 8 - 170, topY, 170, 32);

    // Hero pads: 2x2
    const int px = 12, py = 76, gap = 12;
    const int pw = (760 - px * 2 - gap) / 2;
    const int ph = 158;
    padStutter.setBounds (px, py, pw, ph);
    padReverse.setBounds (px + pw + gap, py, pw, ph);
    padTape.setBounds    (px, py + ph + gap, pw, ph);
    padCrush.setBounds   (px + pw + gap, py + ph + gap, pw, ph);

    // Knob strip
    const int ky = 412, kh = 88;
    const int colW = (760 - 24) / 4;
    auto placeKnob = [&] (juce::Component& knob, juce::Label& label, int col)
    {
        const int x = 12 + col * colW;
        label.setBounds (x, ky, colW, 14);
        knob.setBounds (x + (colW - 64) / 2, ky + 16, 64, kh - 16);
    };
    placeKnob (comboRate, lblRate, 0);
    placeKnob (knobMix,  lblMix,  1);
    placeKnob (knobBits, lblBits, 2);
    placeKnob (knobSr,   lblSr,   3);
    comboRate.setBounds (12 + (colW - 120) / 2, ky + 34, 120, 30);

    // Scope strip
    scope.setBounds (12, 508, 760 - 24, 60);

    // Scanlines over everything
    scanlines.setBounds (getLocalBounds());
}

//==============================================================================
void QGlitchEditor::timerCallback()
{
    bool anyFlash = false;
    for (auto* pad : { &padStutter, &padReverse, &padTape, &padCrush })
    {
        if (pad->getToggleState() && pad->getFlash() < 0.05f)
            pad->setFlash (1.0f); // rising edge kick
        if (! pad->getToggleState() && pad->getFlash() > 0.0f)
            pad->setFlash (0.0f);

        const float f = pad->getFlash();
        if (f > 0.01f)
        {
            pad->setFlash (f * 0.93f);
            pad->repaint();
            anyFlash = true;
        }
    }
    if (anyFlash) { /* pads repaint themselves */ }

    scope.repaint();
    scanlines.advance();
    scanlines.repaint();
}

//==============================================================================
// Presets
juce::File QGlitchEditor::getPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio").getChildFile ("QGlitch").getChildFile ("Presets");
}

juce::File QGlitchEditor::getFavoritesFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio").getChildFile ("QGlitch").getChildFile ("favorites.txt");
}

juce::StringArray QGlitchEditor::getFavorites()
{
    juce::StringArray favs;
    auto f = getFavoritesFile();
    if (f.existsAsFile())
        favs.addLines (f.loadFileAsString());
    favs.trim();
    favs.removeEmptyStrings();
    return favs;
}

bool QGlitchEditor::isFavorite (const juce::String& name)
{
    return getFavorites().contains (name);
}

void QGlitchEditor::setFavorite (const juce::String& name, bool fav)
{
    auto favs = getFavorites();
    favs.removeString (name);
    if (fav) favs.add (name);
    getFavoritesFile().getParentDirectory().createDirectory();
    getFavoritesFile().replaceWithText (favs.joinIntoString ("\n"));
}

void QGlitchEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);

    std::vector<PresetEntry> favs, facts, users;

    auto favorites = getFavorites();
    for (int i = 0; i < numFactoryPresets; ++i)
    {
        PresetEntry e { factoryPresets[i].name, i, {} };
        (favorites.contains (e.name) ? favs : facts).push_back (e);
    }

    auto dir = getPresetDir();
    if (dir.isDirectory())
    {
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.qglitch"))
        {
            PresetEntry e { f.getFileNameWithoutExtension(), -1, f };
            (favorites.contains (e.name) ? favs : users).push_back (e);
        }
    }

    int itemId = 1;
    std::vector<PresetEntry> ordered;
    auto addSection = [&] (const juce::String& heading, std::vector<PresetEntry>& list)
    {
        if (list.empty()) return;
        presetBox.addSectionHeading (heading);
        for (auto& e : list) { presetBox.addItem (e.name, itemId++); ordered.push_back (e); }
    };
    addSection ("♥ Favorites", favs);
    addSection ("Factory", facts);
    addSection ("User", users);

    presetEntries = std::move (ordered);
    presetIdBase = 1;

    // Reselect current
    for (size_t i = 0; i < presetEntries.size(); ++i)
        if (presetEntries[i].name == currentPresetName)
            { presetBox.setSelectedId ((int) i + presetIdBase, juce::dontSendNotification); break; }

    heartBtn.setToggleState (isFavorite (currentPresetName), juce::dontSendNotification);
}

void QGlitchEditor::onPresetChosen()
{
    const int selId = presetBox.getSelectedId();
    if (selId < presetIdBase) return;
    const size_t idx = (size_t) (selId - presetIdBase);
    if (idx >= presetEntries.size()) return;

    const auto& e = presetEntries[idx];
    currentPresetName = e.name;
    if (e.factoryIdx >= 0) applyPreset (factoryPresets[e.factoryIdx]);
    else                   applyUserPreset (e.file);
    heartBtn.setToggleState (isFavorite (currentPresetName), juce::dontSendNotification);
}

static void setParamNotifying (juce::AudioProcessorValueTreeState& apvts,
                               const char* id, float value01)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
}

void QGlitchEditor::applyPreset (const QGlitchFactoryPreset& p)
{
    auto& apvts = processor.apvts;
    setParamNotifying (apvts, QGlitchProcessor::kStutter,  p.stutter ? 1.0f : 0.0f);
    setParamNotifying (apvts, QGlitchProcessor::kReverse,  p.reverse ? 1.0f : 0.0f);
    setParamNotifying (apvts, QGlitchProcessor::kTapeStop, p.tape    ? 1.0f : 0.0f);
    setParamNotifying (apvts, QGlitchProcessor::kCrush,    p.crush   ? 1.0f : 0.0f);

    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (
            apvts.getParameter (QGlitchProcessor::kStutterRate)))
        ch->setValueNotifyingHost ((float) p.rate / (float) (ch->choices.size() - 1));

    auto setF = [&] (const char* id, float v)
    {
        if (auto* par = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id)))
            par->setValueNotifyingHost (par->convertTo0to1 (v));
    };
    setF (QGlitchProcessor::kMix,       p.mix);
    setF (QGlitchProcessor::kCrushBits, p.bits);
    setF (QGlitchProcessor::kCrushRate, p.sr);
}

void QGlitchEditor::applyUserPreset (const juce::File& f)
{
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (f));
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (tree.isValid())
        processor.apvts.replaceState (tree);
}

void QGlitchEditor::saveUserPreset()
{
    auto alert = std::make_shared<juce::AlertWindow> ("Save Preset", "Preset name:",
                                                      juce::AlertWindow::NoIcon);
    alert->addTextEditor ("name", currentPresetName, "");
    alert->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    alert->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, alert] (int result)
        {
            if (result != 1) return;
            auto name = alert->getTextEditorContents ("name").trim();
            if (name.isEmpty()) return;

            auto dir = getPresetDir();
            dir.createDirectory();
            auto f = dir.getChildFile (name + ".qglitch");
            auto state = processor.apvts.copyState();
            if (state.isValid())
                if (std::unique_ptr<juce::XmlElement> xml { state.createXml() })
                    xml->writeTo (f);

            currentPresetName = name;
            refreshPresetList();
        }));
}
