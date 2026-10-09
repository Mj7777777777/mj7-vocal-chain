#include "PluginEditor.h"

using namespace mj7;
namespace mj7ui
{
static const float twoPi = juce::MathConstants<float>::twoPi;

juce::Font font (float size, bool bold, float kerning)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}

/** Majuscules avec les accents francais (toUpperCase ne les gere pas sur tous les systemes). */
static juce::String upper (const juce::String& s)
{
    static const char* pairs[][2] = { { "é", "É" }, { "è", "È" }, { "ê", "Ê" }, { "à", "À" }, { "â", "Â" }, { "ç", "Ç" }, { "î", "Î" }, { "ô", "Ô" }, { "û", "Û" }, { "ù", "Ù" } };
    auto out = s.toUpperCase();
    for (const auto& p : pairs) out = out.replace (U8 (p[0]), U8 (p[1]));
    return out;
}

static juce::String noteName (float midi)
{
    static const char* names[12] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    const int n = juce::roundToInt (midi);
    return U8 (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);
}

// ================================================================================================
Look::Look()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #elif JUCE_MAC
    setDefaultSansSerifTypefaceName ("Avenir Next");
   #endif
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::gold.withAlpha (0.9f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::bg0);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bg0);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::gold.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, col::bg0);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::gold.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::gold);
    setColour (juce::CaretComponent::caretColourId, col::gold);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (area.getWidth(), area.getHeight()) - 6.0f;
    const auto c = area.getCentre();
    const float r = size * 0.5f, track = juce::jmax (3.0f, size * 0.075f), angle = start + pos * (end - start);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, start, end, true);
    g.setColour (col::line); g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // l'arc part du centre pour les reglages bipolaires (gain +/-), du debut sinon
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-6;
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    valArc.addCentredArc (c.x, c.y, r - track * 0.5f, r - track * 0.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    const bool enabled = s.isEnabled();
    g.setColour (accent.withAlpha (enabled ? 0.22f : 0.08f)); g.strokePath (valArc, juce::PathStrokeType (track + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent.withAlpha (enabled ? 1.0f : 0.35f));  g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = r - track - 4.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2932), c.x, c.y - br, juce::Colour (0xff121116), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (s.isMouseOverOrDragging() ? 0.22f : 0.09f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.6f, -br + 3.0f, 3.2f, br * 0.46f, 1.6f);
    g.setColour (enabled ? col::text : col::dim);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    const bool lit = b.getToggleState() || down;
    g.setColour (lit ? accent : col::panel.brighter (over ? 0.12f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (lit ? accent : (over ? accent.withAlpha (0.8f) : col::line.brighter (0.25f)));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (font (juce::jmin (15.0f, (float) b.getHeight() * 0.5f), true, 0.04f));
    g.setColour ((b.getToggleState() || down) ? col::bg0 : col::text.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 2);
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    const juce::Rectangle<float> pill ((float) b.getWidth() * 0.5f - 21.0f, 2.0f, 42.0f, 22.0f);
    g.setColour (on ? accent : col::line.brighter (over ? 0.2f : 0.0f)); g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (on ? col::bg0 : col::dim); g.fillEllipse (on ? pill.getRight() - 20.0f : pill.getX() + 2.0f, pill.getY() + 2.0f, 18.0f, 18.0f);
    g.setColour (col::text); g.setFont (font (13.0f));
    g.drawFittedText (on ? "Oui" : "Non", 0, 26, b.getWidth(), 18, juce::Justification::centred, 1);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> r (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (col::panel); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.isMouseOver (true) ? col::gold.withAlpha (0.8f) : col::line.brighter (0.25f)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow; const float ax = (float) w - 15.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f); arrow.lineTo (ax, ay + 2.5f); arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (col::dim); g.strokePath (arrow, juce::PathStrokeType (1.6f));
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

// ================================================================================================
bool AnalyseButton::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) < 80.0f;
}

void AnalyseButton::mouseUp (const juce::MouseEvent& e)
{
    if (hitTest (e.x, e.y)) proc.startAnalysis();
}

void AnalyseButton::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = 76.0f;
    const auto st = proc.analysisState();
    const bool busy = st == MJ7Processor::waiting || st == MJ7Processor::listening || st == MJ7Processor::computing;
    const float breathe = 0.5f + 0.5f * std::sin (phase);

    // spectre circulaire de la voix (symetrique gauche / droite)
    for (int i = 0; i < numBands; ++i)
    {
        const float len = 3.0f + 40.0f * bands[i];
        for (int side = 0; side < 2; ++side)
        {
            const float t = ((float) i + 0.5f) / (float) numBands;
            const float a = (side == 0 ? 1.0f : -1.0f) * (0.04f + t * 0.92f) * juce::MathConstants<float>::pi;   // 0 = bas, pi = haut
            const float sx = std::sin (a), cy2 = std::cos (a);
            const float r0 = R + 17.0f, r1 = r0 + len;
            g.setColour (col::gold.interpolatedWith (col::amber, bands[i]).withAlpha (0.25f + 0.75f * bands[i]));
            g.drawLine (c.x + sx * r0, c.y + cy2 * r0, c.x + sx * r1, c.y + cy2 * r1, 2.6f);
        }
    }

    // halo
    const float glow = busy ? 0.9f : (st == MJ7Processor::done ? 0.6f : 0.25f + 0.25f * breathe) + (hover ? 0.2f : 0.0f);
    for (int i = 6; i >= 1; --i)
    {
        const float rr = R + (float) i * 3.0f;
        g.setColour (col::gold.withAlpha (0.035f * glow * (float) (7 - i)));
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
    }

    // disque
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff25232b), c.x, c.y - R, juce::Colour (0xff0c0c0f), c.x, c.y + R, false));
    g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour (col::gold.withAlpha (hover ? 0.95f : 0.55f)); g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.4f);

    // anneau de progression
    const float rr = R + 9.0f;
    juce::Path ring; ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi, true);
    g.setColour (col::line); g.strokePath (ring, juce::PathStrokeType (3.0f));
    juce::Path prog;
    if (st == MJ7Processor::listening)      prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi * juce::jlimit (0.005f, 1.0f, proc.analysisProgress()), true);
    else if (st == MJ7Processor::done)      prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.0f, twoPi, true);
    else if (st == MJ7Processor::waiting || st == MJ7Processor::computing) prog.addCentredArc (c.x, c.y, rr, rr, 0.0f, phase * 2.0f, phase * 2.0f + 1.2f, true);
    g.setColour (st == MJ7Processor::failed ? col::family (2) : col::gold);
    g.strokePath (prog, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::String title = "ANALYSER", sub = U8 ("lancez la lecture\npuis appuyez");
    switch (st)
    {
        case MJ7Processor::waiting:   title = "EN ATTENTE"; sub = U8 ("lancez la lecture\nde la voix"); break;
        case MJ7Processor::listening: title = U8 ("ÉCOUTE");  sub = juce::String (juce::jmax (0, juce::roundToInt (proc.analysisSeconds() * (1.0f - proc.analysisProgress())))) + " s"; break;
        case MJ7Processor::computing: title = "CALCUL";     sub = U8 ("réglage en cours"); break;
        case MJ7Processor::done:      title = U8 ("TRAITÉ");  sub = U8 ("appuyez pour\nréanalyser"); break;
        case MJ7Processor::failed:    title = U8 ("RÉESSAYER"); sub = U8 ("aucune voix\ndétectée"); break;
        case MJ7Processor::idle: default: break;
    }
    g.setColour (col::gold); g.setFont (font (20.0f, true, 0.12f));
    g.drawText (title, juce::Rectangle<float> (c.x - R, c.y - 30.0f, R * 2.0f, 26.0f), juce::Justification::centred);
    g.setColour (col::text.withAlpha (0.82f)); g.setFont (font (st == MJ7Processor::listening ? 20.0f : 13.0f));
    g.drawFittedText (sub, juce::Rectangle<float> (c.x - R + 8.0f, c.y - 2.0f, R * 2.0f - 16.0f, 40.0f).toNearestInt(), juce::Justification::centredTop, 2);
}

// ================================================================================================
void MeterBar::push (float peakLinear)
{
    const float db = juce::Decibels::gainToDecibels (peakLinear, -90.0f);
    level = db > level ? db : level - 1.2f;
    if (db >= hold) { hold = db; holdCount = 60; } else if (--holdCount < 0) hold -= 0.6f;
    repaint();
}

void MeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto lab = r.removeFromBottom (18.0f);
    g.setColour (col::dim); g.setFont (font (12.0f, true, 0.06f)); g.drawText (label, lab.expanded (10.0f, 0.0f), juce::Justification::centred);
    r = r.withSizeKeepingCentre (9.0f, r.getHeight());
    g.setColour (col::line); g.fillRoundedRectangle (r, 4.0f);
    auto yFor = [&r] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    const float top = yFor (level);
    if (top < r.getBottom() - 1.0f)
    {
        g.setGradientFill (juce::ColourGradient (col::family (2), r.getX(), r.getY(), col::gold, r.getX(), r.getY() + r.getHeight() * 0.2f, false));
        g.fillRoundedRectangle (r.withTop (top), 4.0f);
    }
    g.setColour (hold > -1.0f ? col::family (2) : col::text); g.fillRect (r.getX() - 2.0f, yFor (hold) - 1.0f, r.getWidth() + 4.0f, 2.0f);
}

void ChainTile::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    if (hasPower && e.x < 30) { if (onPower) onPower(); }
    else if (onSelect) onSelect();
}

void ChainTile::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto accent = col::family (familyIndex);
    g.setColour (selected ? accent.withAlpha (0.16f) : col::panel.withAlpha (isMouseOver() ? 1.0f : 0.8f)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (selected ? accent : col::line.brighter (isMouseOver() ? 0.5f : 0.1f)); g.drawRoundedRectangle (r, 6.0f, selected ? 1.5f : 1.0f);

    float textX = 10.0f;
    if (hasPower)
    {
        const juce::Rectangle<float> dot (10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
        if (active) { g.setColour (accent.withAlpha (0.25f)); g.fillEllipse (dot.expanded (3.0f)); g.setColour (accent); g.fillEllipse (dot); }
        else        { g.setColour (col::dim.withAlpha (0.7f)); g.drawEllipse (dot, 1.3f); }
        textX = 28.0f;
    }
    g.setColour (active ? col::text : col::dim.withAlpha (0.75f)); g.setFont (font (13.5f, selected || ! hasPower));
    g.drawText (name, juce::Rectangle<float> (textX, 0.0f, r.getWidth() - textX - 4.0f, r.getHeight()), hasPower ? juce::Justification::centredLeft : juce::Justification::centred);
    if (gr < -0.3f && active)       // reduction de gain en cours
    {
        const float wBar = (r.getWidth() - 12.0f) * juce::jlimit (0.0f, 1.0f, -gr / 12.0f);
        g.setColour (accent); g.fillRoundedRectangle (r.getX() + 6.0f, r.getBottom() - 4.5f, wBar, 2.5f, 1.2f);
    }
}

// ================================================================================================
MainView::MainView (MJ7Processor& p) : proc (p), analyse (p)
{
    setLookAndFeel (&look);
    setOpaque (true);
    setSize (W, H);
    fftData.assign (4096, 0.0f); specIn.assign (1024, 0.0f); specOut.assign (1024, 0.0f);

    grain = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Random rnd (7);
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) grain.setPixelAt (x, y, juce::Colours::white.withAlpha (rnd.nextFloat() * 0.035f));

    // --- barre du haut ---
    int id = 1;
    presetBox.addSectionHeading (U8 ("RAP / TRAP / MÉLODIQUE"));
    for (const auto& f : factoryPresets())
    {
        if (id - 1 == kV3PresetStart) presetBox.addSectionHeading (U8 ("AFRO / AMAPIANO / POP / HOUSE"));
        presetBox.addItem (U8 (f.name), id++);
    }
    presetBox.setTooltip (U8 ("Style de voix. Il fixe les effets et la couleur visée par le bouton ANALYSER."));
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) { userPresetName.clear(); proc.loadFactoryPreset (idx); shownPreset = idx; }
    };
    addAndMakeVisible (presetBox);

    saveBtn.setButtonText ("Sauver"); loadBtn.setButtonText ("Ouvrir"); abBtn.setButtonText ("A"); undoBtn.setButtonText ("Annuler");
    tabAnalyse.setButtonText ("ANALYSE"); tabSpectrum.setButtonText ("SPECTRE");
    for (auto* b : { &saveBtn, &loadBtn, &abBtn, &undoBtn, &tabAnalyse, &tabSpectrum }) { b->setColour (juce::TextButton::buttonOnColourId, col::gold); addAndMakeVisible (b); }
    undoBtn.setTooltip (U8 ("Annuler l'analyse : revient aux réglages du style seul."));
    tabAnalyse.setTooltip (U8 ("Résumé de ce que le bouton ANALYSER a mesuré et réglé."));
    tabSpectrum.setTooltip (U8 ("Spectre de la voix avant et après traitement, avec la courbe d'égalisation."));
    tabAnalyse.onClick = [this] { tab = 0; tabAnalyse.setToggleState (true, juce::dontSendNotification); tabSpectrum.setToggleState (false, juce::dontSendNotification); repaint(); };
    tabSpectrum.onClick = [this] { tab = 1; tabAnalyse.setToggleState (false, juce::dontSendNotification); tabSpectrum.setToggleState (true, juce::dontSendNotification); repaint(); };
    tabAnalyse.setToggleState (true, juce::dontSendNotification);
    tabAnalyse.setBounds (48, 64, 88, 24); tabSpectrum.setBounds (140, 64, 88, 24);
    saveBtn.setTooltip (U8 ("Enregistrer tous les réglages dans un fichier de preset."));
    loadBtn.setTooltip (U8 ("Ouvrir un preset enregistré."));
    abBtn.setTooltip (U8 ("Comparer deux versions des réglages (A et B)."));
    saveBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer le preset", MJ7Processor::presetFolder().getChildFile ("Mon preset.mj7preset"), "*.mj7preset");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (proc.savePresetFile (f.withFileExtension ("mj7preset"))) { userPresetName = f.getFileNameWithoutExtension(); presetBox.setText (userPresetName, juce::dontSendNotification); }
            });
    };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Ouvrir un preset", MJ7Processor::presetFolder(), "*.mj7preset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile() && proc.loadPresetFile (f)) { userPresetName = f.getFileNameWithoutExtension(); presetBox.setText (userPresetName, juce::dontSendNotification); }
            });
    };
    abBtn.onClick = [this] { proc.toggleAB(); abBtn.setButtonText (proc.abSlot() == 0 ? "A" : "B"); };
    undoBtn.onClick = [this] { proc.undoAnalysis(); };
    undoBtn.setVisible (false);

    keyBox.addItemList (proc.apvts.getParameter ("tune_key")->getAllValueStrings(), 1);
    scaleBox.addItemList (proc.apvts.getParameter ("tune_scale")->getAllValueStrings(), 1);
    keyBox.setTooltip (U8 ("Tonalité du morceau, utilisée par l'autotune."));
    scaleBox.setTooltip (U8 ("Gamme du morceau. Chromatique = recale sur la note la plus proche."));
    keyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "tune_key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "tune_scale", scaleBox);
    addAndMakeVisible (keyBox); addAndMakeVisible (scaleBox);
    modeBox.addItemList (proc.apvts.getParameter ("mode")->getAllValueStrings(), 1);
    modeBox.setTooltip (U8 ("Mix : qualité maximale. Tracking : latence réduite à 5 ms pour enregistrer avec l'autotune dans le casque (sans réduction de bruit ni suréchantillonnage)."));
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    addAndMakeVisible (modeBox);

    bypassBtn.setButtonText ("BYPASS"); bypassBtn.setClickingTogglesState (true);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, col::family (2));
    bypassBtn.setTooltip (U8 ("Bypass : écouter la voix sans le plugin."));
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassBtn);
    addAndMakeVisible (bypassBtn);

    presetBox.setBounds (236, 12, 204, 30); saveBtn.setBounds (446, 12, 62, 30); loadBtn.setBounds (512, 12, 62, 30);
    keyBox.setBounds (584, 12, 64, 30); scaleBox.setBounds (652, 12, 124, 30); modeBox.setBounds (784, 12, 92, 30);
    abBtn.setBounds (882, 12, 28, 30); bypassBtn.setBounds (914, 12, 68, 30);

    // --- centre ---
    inMeter.label = "IN"; outMeter.label = "OUT";
    addAndMakeVisible (inMeter); addAndMakeVisible (outMeter); addAndMakeVisible (analyse);
    inMeter.setBounds (12, 66, 24, 268); outMeter.setBounds (W - 36, 66, 24, 268);
    analyse.setBounds (360, 58, 280, 280);
    undoBtn.setBounds (268, 64, 80, 24);

    // --- chaine : Essentiel + 16 modules sur deux rangees ---
    essentialTile.name = "ESSENTIEL"; essentialTile.hasPower = false; essentialTile.familyIndex = Neutre;
    essentialTile.onSelect = [this] { showModule (-1); };
    essentialTile.setBounds (20, 346, 96, 62);
    addAndMakeVisible (essentialTile);
    const auto& mods = modules();
    for (int i = 0; i < (int) mods.size(); ++i)
    {
        auto* t = tiles.add (new ChainTile());
        t->name = U8 (mods[(size_t) i].name); t->familyIndex = mods[(size_t) i].family;
        t->onSelect = [this, i] { showModule (i); };
        t->onPower = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (paramDef (modules()[(size_t) i].bypass).id))
            { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f); prm->endChangeGesture(); }
        };
        t->setBounds (juce::roundToInt (122.0f + (float) (i % 9) * 95.5f), 346 + (i / 9) * 33, 92, 29);
        t->setTooltip (U8 (mods[(size_t) i].help));
        addAndMakeVisible (t);
    }

    showModule (-1);
    startTimerHz (60);
}

MainView::~MainView() { stopTimer(); setLookAndFeel (nullptr); }

std::unique_ptr<Control> MainView::makeControl (int pid, const juce::String& labelText, juce::Colour accent)
{
    auto c = std::make_unique<Control>();
    const auto& d = paramDef (pid);
    c->label = std::make_unique<juce::Label> (juce::String(), labelText);
    c->label->setJustificationType (juce::Justification::centred);
    c->label->setColour (juce::Label::textColourId, col::dim.brighter (0.25f));
    c->label->setMinimumHorizontalScale (0.75f);
    addAndMakeVisible (*c->label);
    if (d.type == PType::tC)
    {
        c->combo = std::make_unique<juce::ComboBox>();
        c->combo->addItemList (proc.apvts.getParameter (d.id)->getAllValueStrings(), 1);
        c->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, d.id, *c->combo);
        addAndMakeVisible (*c->combo);
    }
    else if (d.type == PType::tB)
    {
        c->toggle = std::make_unique<juce::ToggleButton>();
        c->toggle->setColour (juce::ToggleButton::tickColourId, accent);
        c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, d.id, *c->toggle);
        addAndMakeVisible (*c->toggle);
    }
    else
    {
        c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        c->slider->setColour (juce::Slider::rotarySliderFillColourId, accent);
        c->slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, *c->slider);
        addAndMakeVisible (*c->slider);
    }
    return c;
}

void MainView::showModule (int index)
{
    controls.clear(); throwDelay.reset(); throwReverb.reset();
    selected = index;
    if (index >= 0)                                             // les modules d'egalisation ouvrent le spectre
    {
        const juce::String n (modules()[(size_t) index].name);
        const bool eqLike = n == "EQ" || n == "Ton" || n == "EQ dyn" || n == "De-ess" || n == "Filtre" || n == "Bruit";
        if (eqLike && tab == 0 && tabSpectrum.onClick) tabSpectrum.onClick();
    }
    essentialTile.selected = index < 0; essentialTile.repaint();
    for (int i = 0; i < tiles.size(); ++i) { tiles[i]->selected = i == index; tiles[i]->repaint(); }

    const int top = 452, height = 152;
    auto place = [&] (Control& c, juce::Rectangle<int> cell)
    {
        c.label->setBounds (cell.removeFromTop (20));
        if (c.slider != nullptr) { c.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (cell.getWidth(), 84), 20); c.slider->setBounds (cell.reduced (2, 0)); }
        if (c.combo != nullptr)  c.combo->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth() - 6, 140), 30));
        if (c.toggle != nullptr) c.toggle->setBounds (cell.withSizeKeepingCentre (60, 46));
    };

    if (index < 0)
    {
        struct Macro { int pid; const char* label; int family; const char* tip; };
        static const Macro macros[] = {
            { tune_speed,  "VITESSE TUNE", Correction, "0 ms = autotune dur. 20 à 60 ms = correction naturelle." },
            { macro_comp,  "COMPRESS +",   Dynamique,  "Ajoute de la compression sur les deux compresseurs à la fois." },
            { tone_pres_g, "PRÉSENCE",     Couleur,    "Fait avancer la voix dans le mix (autour de 4 kHz)." },
            { tone_air_g,  "AIR",          Couleur,    "Brillance et souffle dans les aigus (au-dessus de 10 kHz)." },
            { sat_drive,   "DIRT",         Couleur,    "Quantité de saturation : chaleur, puis distorsion." },
            { dbl_mix,     "LARGEUR",      Espace,     "Niveau du doubleur stéréo." },
            { dly_mix,     "ÉCHO",         Espace,     "Niveau du delay." },
            { rev_mix,     "ESPACE",       Espace,     "Niveau de la réverbe." } };
        int x = 20;
        for (const auto& m : macros)
        {
            auto c = makeControl (m.pid, U8 (m.label), col::family (m.family));
            c->label->setFont (font (12.5f, true, 0.08f)); c->label->setColour (juce::Label::textColourId, col::text);
            c->slider->setTooltip (U8 (m.tip));
            place (*c, { x, top - 6, 86, height + 6 }); x += 88;
            controls.push_back (std::move (c));
        }
        auto c = makeControl (intensity, U8 ("INTENSITÉ"), col::gold);
        c->label->setFont (font (12.5f, true, 0.08f)); c->label->setColour (juce::Label::textColourId, col::gold);
        c->slider->setTooltip (U8 ("Dose toutes les corrections trouvées par ANALYSER : 0 % = réglages du style seul, 100 % = correction complète."));
        place (*c, { 736, top - 6, 96, height + 6 });
        controls.push_back (std::move (c));

        throwDelay = std::make_unique<ThrowButton> (proc.apvts.getParameter ("dly_throw"), "THROW DELAY");
        throwReverb = std::make_unique<ThrowButton> (proc.apvts.getParameter ("rev_throw"), U8 ("THROW RÉVERBE"));
        for (auto* b : { throwDelay.get(), throwReverb.get() }) { b->setColour (juce::TextButton::buttonOnColourId, col::family (Espace)); addAndMakeVisible (b); }
        throwDelay->setTooltip (U8 ("Maintenez pendant une fin de phrase : seule cette partie part dans le delay. Automatisable dans FL Studio."));
        throwReverb->setTooltip (U8 ("Maintenez pendant une fin de phrase : seule cette partie part dans la réverbe. Automatisable dans FL Studio."));
        throwDelay->setBounds (846, top + 14, 134, 50); throwReverb->setBounds (846, top + 74, 134, 50);
    }
    else
    {
        const auto& mod = modules()[(size_t) index];
        const int count = (int) mod.params.size();
        const int cellW = juce::jmin (112, 960 / count);
        int x = 20 + (960 - cellW * count) / 2;
        for (int pid : mod.params)
        {
            auto name = U8 (paramDef (pid).name);
            const auto prefix = U8 (mod.name) + " ";                       // "Comp 1 seuil" -> "Seuil"
            for (const auto& pre : { prefix, juce::String ("Saturation "), juce::String ("De-esser "), U8 ("Réverbe "), juce::String ("Doubleur "), juce::String ("Ducking "), juce::String ("Filtre "), juce::String ("Limiteur "), juce::String ("EQ dyn "), U8 ("Réduction bruit "), juce::String ("Harmonie ") })
                if (name.startsWith (pre) && name.length() > pre.length()) { name = name.substring (pre.length()); break; }
            name = name.substring (0, 1).toUpperCase() + name.substring (1);
            auto c = makeControl (pid, name, col::family (mod.family));
            place (*c, { x, top, cellW, height }); x += cellW;
            controls.push_back (std::move (c));
        }
    }
    repaint();
}

void MainView::refreshPresetBox()
{
    if (userPresetName.isEmpty() && shownPreset != proc.currentPreset())
    {
        shownPreset = proc.currentPreset();
        presetBox.setSelectedId (shownPreset + 1, juce::dontSendNotification);
    }
}

void MainView::updateSpectrum()
{
    const int N = 2048;
    if (tab == 1)                                               // spectre d'entree (apres le gain d'entree)
    {
        proc.readScope (fftData.data(), N, true);
        for (int i = 0; i < N; ++i) fftData[(size_t) i] *= 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (int k = 0; k < N / 2; ++k) { const float m = fftData[(size_t) k] / (float) (N / 4); specIn[(size_t) k] = m > specIn[(size_t) k] ? m : specIn[(size_t) k] * 0.9f + m * 0.1f; }
    }
    proc.readScope (fftData.data(), N);
    for (int i = 0; i < N; ++i) fftData[(size_t) i] *= 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) N);
    std::fill (fftData.begin() + N, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int k = 0; k < N / 2; ++k) { const float m = fftData[(size_t) k] / (float) (N / 4); specOut[(size_t) k] = m > specOut[(size_t) k] ? m : specOut[(size_t) k] * 0.9f + m * 0.1f; }
    const float sr = (float) juce::jmax (8000.0, proc.getSampleRate()), binHz = sr / (float) N;
    for (int b = 0; b < AnalyseButton::numBands; ++b)
    {
        const float f0 = 80.0f * std::pow (200.0f, (float) b / (float) AnalyseButton::numBands);
        const float f1 = 80.0f * std::pow (200.0f, (float) (b + 1) / (float) AnalyseButton::numBands);
        const int k0 = juce::jlimit (1, N / 2 - 1, (int) (f0 / binHz)), k1 = juce::jlimit (k0, N / 2 - 1, (int) (f1 / binHz));
        float mx = 0.0f; for (int k = k0; k <= k1; ++k) mx = juce::jmax (mx, fftData[(size_t) k]);
        const float db = juce::Decibels::gainToDecibels (mx / (float) (N / 4), -100.0f) + 4.5f * std::log2 (f0 / 1000.0f);   // pente pour equilibrer l'affichage
        const float val = juce::jlimit (0.0f, 1.0f, (db + 62.0f) / 50.0f);
        analyse.bands[b] = val > analyse.bands[b] ? val : analyse.bands[b] * 0.88f;
    }
}

void MainView::timerCallback()
{
    ++tick;
    analyse.phase = std::fmod (analyse.phase + 0.045f, twoPi * 4.0f);
    updateSpectrum();
    analyse.repaint();
    const float outPk = proc.outPeak.exchange (0.0f);
    inMeter.push (proc.inPeak.exchange (0.0f)); outMeter.push (outPk);
    const float outDb = juce::Decibels::gainToDecibels (outPk, -90.0f);
    peakHoldDb = outDb > peakHoldDb ? outDb : peakHoldDb - 0.05f;

    if (tick % 4 != 0) return;
    refreshPresetBox();
    const auto& mods = modules();
    for (int i = 0; i < tiles.size(); ++i)
    {
        const auto& m = mods[(size_t) i];
        const bool act = proc.apvts.getRawParameterValue (paramDef (m.bypass).id)->load() > 0.5f;
        const float gr = m.meter >= 0 ? proc.meter[m.meter].load() : 0.0f;
        if (act != tiles[i]->active || std::abs (gr - tiles[i]->gr) > 0.2f) { tiles[i]->active = act; tiles[i]->gr = gr; tiles[i]->repaint(); }
    }
    const int st = (int) proc.analysisState();
    const float note = proc.detectedNote(), target = proc.targetNote(), lufs = proc.outLufs.load();
    if (tab == 1) repaint (40, 90, 320, 252);
    const bool changed = st != shownState || proc.summary != shownSummary || std::abs (note - shownNote) > 0.02f
                       || target != shownTarget || std::abs (lufs - shownLufs) > 0.1f || tick % 16 == 0;
    undoBtn.setVisible (proc.hasAnalysis());
    if (changed)
    {
        shownState = st; shownSummary = proc.summary; shownNote = note; shownTarget = target; shownLufs = lufs;
        repaint (40, 58, 320, 284); repaint (640, 58, 320, 284);
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (col::bg1, (float) W * 0.5f, 0.0f, col::bg0, (float) W * 0.5f, (float) H, false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (col::gold.withAlpha (0.07f), 500.0f, 198.0f, col::gold.withAlpha (0.0f), 500.0f, 480.0f, true));
    g.fillRect (150, 0, 700, 420);
    g.setTiledImageFill (grain, 0, 0, 1.0f); g.fillAll();

    // --- barre du haut ---
    g.setColour (col::gold); g.setFont (font (21.0f, true, 0.16f)); g.drawText ("MJ7", 20, 10, 60, 34, juce::Justification::centredLeft);
    g.setColour (col::text); g.setFont (font (15.0f, false, 0.22f)); g.drawText ("VOCAL CHAIN", 72, 10, 170, 34, juce::Justification::centredLeft);
    g.setColour (col::line); g.fillRect (0, 53, W, 1);

    // --- panneau gauche : analyse ---
    const juce::Rectangle<int> left (48, 62, 300, 276);
    auto rows = left.withTrimmedTop (34);
    if (tab == 1) paintSpectrum (g, rows);
    else if (proc.summary.isEmpty())
    {
        static const char* steps[3] = { "Choisissez un style en haut.", "Lancez la lecture de la voix dans FL Studio.", "Appuyez sur ANALYSER : le plugin écoute 12 secondes, puis règle la chaîne." };
        for (int i = 0; i < 3; ++i)
        {
            auto row = rows.removeFromTop (i == 2 ? 62 : 40);
            g.setColour (col::gold); g.setFont (font (22.0f, true)); g.drawText (juce::String (i + 1), row.removeFromLeft (28), juce::Justification::topLeft);
            g.setColour (col::text.withAlpha (0.9f)); g.setFont (font (14.0f)); g.drawFittedText (U8 (steps[i]), row.withTrimmedTop (4), juce::Justification::topLeft, 3);
        }
        g.setColour (col::dim); g.setFont (font (13.0f));
        g.drawFittedText (U8 ("Tout reste réglable à la main ensuite. Le bouton INTENSITÉ dose l'ensemble des corrections."), rows.removeFromTop (60), juce::Justification::topLeft, 3);
    }
    else
    {
        const int rowH = juce::jmin (33, rows.getHeight() / juce::jmax (1, proc.summary.size()));
        for (const auto& line : proc.summary)
        {
            auto row = rows.removeFromTop (rowH);
            const int colon = line.indexOf (" : ");
            g.setColour (col::gold); g.fillEllipse ((float) row.getX(), (float) row.getY() + 6.0f, 5.0f, 5.0f);
            g.setColour (col::text.withAlpha (0.92f)); g.setFont (font (13.0f));
            g.drawFittedText (colon > 0 ? upper (line.substring (0, colon)) + " : " + line.substring (colon + 3) : line, row.withTrimmedLeft (13), juce::Justification::topLeft, 2, 0.85f);
        }
    }

    // --- panneau droit : note et niveaux ---
    const juce::Rectangle<int> right (656, 62, 296, 276);
    g.setColour (col::dim); g.setFont (font (12.0f, true, 0.14f)); g.drawText ("NOTE", right.withHeight (26), juce::Justification::centredLeft);
    const float note = proc.detectedNote(), target = proc.targetNote();
    const bool tuneOn = proc.apvts.getRawParameterValue ("tune_on")->load() > 0.5f;
    auto noteRow = right.withTrimmedTop (30).withHeight (56);
    if (note > 0.0f)
    {
        g.setColour (col::text); g.setFont (font (40.0f, true)); g.drawText (noteName (note), noteRow.withWidth (110), juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (22.0f)); g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), noteRow.withX (noteRow.getX() + 108).withWidth (34), juce::Justification::centred);
        g.setColour (tuneOn ? col::family (Correction) : col::dim); g.setFont (font (40.0f, true));
        g.drawText (tuneOn ? noteName (target) : juce::String ("--"), noteRow.withX (noteRow.getX() + 146).withWidth (130), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (col::dim.withAlpha (0.6f)); g.setFont (font (40.0f, true)); g.drawText ("--", noteRow, juce::Justification::centredLeft);
    }
    // ecart en cents par rapport a la note visee
    auto bar = right.withTrimmedTop (94).withHeight (6).toFloat();
    g.setColour (col::line); g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (col::dim); g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 3.0f, 1.0f, 12.0f);
    if (note > 0.0f)
    {
        const float centsOff = juce::jlimit (-50.0f, 50.0f, (note - target) * 100.0f);
        const float xPos = bar.getCentreX() + centsOff / 50.0f * bar.getWidth() * 0.5f;
        g.setColour (std::abs (centsOff) < 10.0f ? col::family (Correction) : col::gold); g.fillEllipse (xPos - 6.0f, bar.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    g.setColour (col::dim); g.setFont (font (12.0f));
    g.drawText (U8 ("chanté  ·  écart en cents  ·  corrigé"), right.withTrimmedTop (106).withHeight (18), juce::Justification::centred);

    auto stats = right.withTrimmedTop (140);
    auto stat = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        auto row = stats.removeFromTop (32);
        g.setColour (col::line); g.fillRect (row.getX(), row.getY(), row.getWidth(), 1);
        g.setColour (col::dim); g.setFont (font (12.0f, true, 0.12f)); g.drawText (label, row, juce::Justification::centredLeft);
        g.setColour (c); g.setFont (font (17.0f, true)); g.drawText (value, row, juce::Justification::centredRight);
    };
    const float lufs = proc.outLufs.load();
    stat ("NIVEAU DE SORTIE", lufs > -70.0f ? juce::String (lufs, 1) + " LUFS" : juce::String ("--"), col::text);
    stat (U8 ("CRÊTE"), peakHoldDb > -80.0f ? juce::String (peakHoldDb, 1) + " dB" : juce::String ("--"), peakHoldDb > -0.5f ? col::family (2) : col::text);
    const float gr = proc.meter[mComp1].load() + proc.meter[mComp2].load();
    stat ("COMPRESSION", juce::String (gr, 1) + " dB", col::family (Dynamique));
    stat ("LATENCE", juce::String (proc.getSampleRate() > 0.0 ? 1000.0 * proc.getLatencySamples() / proc.getSampleRate() : 0.0, 1) + " ms", col::dim);

    // --- chaine ---
    g.setColour (col::line); g.fillRect (20, 340, W - 40, 1);

    // --- panneau du bas ---
    g.setColour (col::panel.withAlpha (0.55f)); g.fillRoundedRectangle (12.0f, 416.0f, (float) W - 24.0f, 196.0f, 10.0f);
    g.setColour (col::line); g.drawRoundedRectangle (12.0f, 416.0f, (float) W - 24.0f, 196.0f, 10.0f, 1.0f);
    if (selected < 0)
    {
        g.setColour (col::gold); g.setFont (font (13.0f, true, 0.14f)); g.drawText ("ESSENTIEL", 28, 420, 120, 24, juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (13.0f));
        g.drawText (U8 ("Les réglages qui comptent le plus. Cliquez sur un module de la chaîne pour le détail. Double-clic sur un bouton : valeur par défaut."), 128, 420, 850, 24, juce::Justification::centredLeft);
    }
    else
    {
        const auto& mod = modules()[(size_t) selected];
        const auto title = upper (U8 (mod.title));
        g.setColour (col::family (mod.family)); g.setFont (font (13.0f, true, 0.14f));
        const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 16;
        g.drawText (title, 28, 420, tw, 24, juce::Justification::centredLeft);
        g.setColour (col::dim); g.setFont (font (13.0f)); g.drawText (U8 (mod.help), 28 + tw, 420, W - 56 - tw, 24, juce::Justification::centredLeft);
    }
}
/** Spectre avant / apres et courbe d'egalisation (coupe-bas, EQ, ton, filtre). */
void MainView::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto legend = area.removeFromBottom (20);
    const auto r = area.toFloat();
    const float sr = (float) juce::jmax (8000.0, proc.getSampleRate()), fMin = 40.0f, fMax = juce::jmin (20000.0f, 0.48f * sr);
    auto xFor = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yDb = [&] (float db, float lo, float hi) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo)); };

    g.setColour (col::bg0.withAlpha (0.55f)); g.fillRoundedRectangle (r, 6.0f);
    g.setFont (font (11.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (col::line); g.fillRect (xFor (f), r.getY(), 1.0f, r.getHeight());
        g.setColour (col::dim.withAlpha (0.8f)); g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + " k" : juce::String ((int) f), (int) xFor (f) + 3, (int) r.getBottom() - 15, 30, 13, juce::Justification::left);
    }
    g.setColour (col::line.brighter (0.3f)); g.fillRect (r.getX(), yDb (0.0f, -18.0f, 18.0f), r.getWidth(), 1.0f);

    const int pts = 120; const float binHz = sr / 2048.0f;
    auto curve = [&] (const std::vector<float>& mag, bool close)
    {
        juce::Path p;
        for (int i = 0; i < pts; ++i)
        {
            const float f0 = fMin * std::pow (fMax / fMin, (float) i / (float) pts), f1 = fMin * std::pow (fMax / fMin, (float) (i + 1) / (float) pts);
            const int k0 = juce::jlimit (1, 1023, (int) (f0 / binHz)), k1 = juce::jlimit (k0, 1023, (int) (f1 / binHz));
            float mx = 0.0f; for (int k = k0; k <= k1; ++k) mx = juce::jmax (mx, mag[(size_t) k]);
            const float db = juce::Decibels::gainToDecibels (mx, -120.0f) + 4.5f * std::log2 (f0 / 1000.0f);
            const float x = xFor (f0), y = yDb (db, -90.0f, -15.0f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (close) { p.lineTo (r.getRight(), r.getBottom()); p.lineTo (r.getX(), r.getBottom()); p.closeSubPath(); }
        return p;
    };
    g.setColour (col::dim.withAlpha (0.28f)); g.fillPath (curve (specIn, true));
    g.setColour (col::gold); g.strokePath (curve (specOut, false), juce::PathStrokeType (1.6f));

    // courbe d'egalisation statique, calculee a partir des reglages
    auto val = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    std::vector<mj7::Biquad> filters; mj7::Biquad b;
    b.setHighpass (val ("hpf_freq"), 0.707f, sr); filters.push_back (b);
    if (val ("eq_on") > 0.5f)
    {
        static const char* ids[4][3] = { { "eq1_f", "eq1_g", "eq1_q" }, { "eq2_f", "eq2_g", "eq2_q" }, { "eq3_f", "eq3_g", "eq3_q" }, { "eq4_f", "eq4_g", "eq4_q" } };
        for (const auto& e : ids) { b.setPeak (val (e[0]), val (e[2]), val (e[1]), sr); filters.push_back (b); }
        if (val ("eq_hicut") < 19500.0f) { b.setLowpass (val ("eq_hicut"), 0.707f, sr); filters.push_back (b); }
    }
    if (val ("tone_on") > 0.5f)
    {
        b.setLowShelf (val ("tone_low_f"), val ("tone_low_g"), sr); filters.push_back (b);
        b.setPeak (val ("tone_mid_f"), 1.0f, val ("tone_mid_g"), sr); filters.push_back (b);
        b.setPeak (val ("tone_pres_f"), 0.9f, val ("tone_pres_g"), sr); filters.push_back (b);
        b.setHighShelf (val ("tone_air_f"), val ("tone_air_g"), sr); filters.push_back (b);
    }
    if (val ("flt_on") > 0.5f)
    {
        b.setHighpass (val ("flt_hp"), val ("flt_res"), sr); filters.push_back (b);
        b.setLowpass (val ("flt_lp"), val ("flt_res"), sr); filters.push_back (b);
    }
    juce::Path eq;
    for (int i = 0; i <= pts; ++i)
    {
        const float f = fMin * std::pow (fMax / fMin, (float) i / (float) pts);
        float db = 0.0f; for (const auto& flt : filters) db += flt.magnitudeDb (f, sr);
        const float x = xFor (f), y = yDb (db, -18.0f, 18.0f);
        if (i == 0) eq.startNewSubPath (x, y); else eq.lineTo (x, y);
    }
    g.saveState(); g.reduceClipRegion (area);
    g.setColour (col::family (mj7::Correction)); g.strokePath (eq, juce::PathStrokeType (2.0f));
    g.restoreState();

    g.setFont (font (12.0f));
    auto item = [&] (const juce::String& text, juce::Colour c, int w)
    {
        auto cell = legend.removeFromLeft (w);
        g.setColour (c); g.fillRoundedRectangle ((float) cell.getX(), (float) cell.getCentreY() - 2.0f, 14.0f, 4.0f, 2.0f);
        g.setColour (col::dim.brighter (0.2f)); g.drawText (text, cell.withTrimmedLeft (19), juce::Justification::centredLeft);
    };
    item ("avant", col::dim.withAlpha (0.6f), 70); item (U8 ("après"), col::gold, 70); item (U8 ("égalisation (±18 dB)"), col::family (mj7::Correction), 160);
}
} // namespace mj7ui

// ================================================================================================
MJ7Editor::MJ7Editor (MJ7Processor& p) : juce::AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) mj7ui::MainView::W / (double) mj7ui::MainView::H);
    setResizeLimits (900, 558, 1800, 1116);
    setSize (mj7ui::MainView::W, mj7ui::MainView::H);
}

void MJ7Editor::resized()
{
    const float scale = (float) getWidth() / (float) mj7ui::MainView::W;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, mj7ui::MainView::W, mj7ui::MainView::H);
}
