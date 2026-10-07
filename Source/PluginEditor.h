// MJ7 Vocal Chain - interface.
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace mj7ui
{
namespace col
{
    const juce::Colour bg0 { 0xff09090b }, bg1 { 0xff16151b }, panel { 0xff1b1a21 }, line { 0xff2c2a33 };
    const juce::Colour text { 0xfff0e9da }, dim { 0xffa19a8c }, gold { 0xffe9b44c }, amber { 0xffff9a3c };
    inline juce::Colour family (int f)
    {
        static const juce::Colour c[] = { juce::Colour (0xff5fd0c5), juce::Colour (0xfff2a63a), juce::Colour (0xffec6f5e), juce::Colour (0xff93a0ff), juce::Colour (0xffe9b44c) };
        return c[juce::jlimit (0, 4, f)];
    }
}
juce::Font font (float size, bool bold = false, float kerning = 0.0f);

class Look final : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (14.0f); }
    juce::Font getPopupMenuFont() override { return font (14.5f); }
    juce::Font getLabelFont (juce::Label&) override { return font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

/** Le grand bouton circulaire ANALYSER et sa visualisation. */
class AnalyseButton final : public juce::Component
{
public:
    explicit AnalyseButton (MJ7Processor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }
    bool hitTest (int x, int y) override;
    static constexpr int numBands = 40;
    float bands[numBands] = {};
    float phase = 0.0f;
private:
    MJ7Processor& proc; bool hover = false;
};

class MeterBar final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void push (float peakLinear);
    juce::String label;
private:
    float level = -90.0f, hold = -90.0f; int holdCount = 0;
};

/** Une tuile de la chaine : nom du module, marche/arret, reduction de gain. */
class ChainTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String name; int familyIndex = 4; bool active = true, selected = false, hasPower = true; float gr = 0.0f;
    std::function<void()> onSelect, onPower;
};

class ThrowButton final : public juce::TextButton
{
public:
    ThrowButton (juce::RangedAudioParameter* p, const juce::String& label) : juce::TextButton (label), param (p) {}
    void mouseDown (const juce::MouseEvent& e) override { juce::TextButton::mouseDown (e); set (true); }
    void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); set (false); }
private:
    void set (bool on) { if (param != nullptr) { param->beginChangeGesture(); param->setValueNotifyingHost (on ? 1.0f : 0.0f); param->endChangeGesture(); } }
    juce::RangedAudioParameter* param;
};

/** Un controle genere depuis la liste des parametres : bouton rotatif, liste ou interrupteur. */
struct Control
{
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
};

class MainView final : public juce::Component, private juce::Timer
{
public:
    explicit MainView (MJ7Processor&);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
    static constexpr int W = 1000, H = 620;
private:
    void timerCallback() override;
    void showModule (int index);                 // -1 = vue Essentiel
    std::unique_ptr<Control> makeControl (int pid, const juce::String& labelText, juce::Colour accent);
    void updateSpectrum();
    void refreshPresetBox();

    MJ7Processor& proc; Look look;
    juce::Image grain;
    juce::ComboBox presetBox, keyBox, scaleBox, modeBox;
    juce::TextButton tabAnalyse, tabSpectrum; int tab = 0;
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    std::vector<float> specIn, specOut;
    juce::TextButton saveBtn, loadBtn, abBtn, undoBtn, bypassBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;
    AnalyseButton analyse; MeterBar inMeter, outMeter;
    ChainTile essentialTile; juce::OwnedArray<ChainTile> tiles;
    std::vector<std::unique_ptr<Control>> controls;
    std::unique_ptr<ThrowButton> throwDelay, throwReverb;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::dsp::FFT fft { 11 }; std::vector<float> fftData;
    int selected = -1, tick = 0, shownPreset = -1; juce::String userPresetName;
    juce::StringArray shownSummary; int shownState = -1; float shownNote = -1.0f, shownTarget = -1.0f, shownLufs = 0.0f, peakHoldDb = -90.0f;
    juce::TooltipWindow tooltips { this, 600 };
};
} // namespace mj7ui

class MJ7Editor final : public juce::AudioProcessorEditor
{
public:
    explicit MJ7Editor (MJ7Processor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (mj7ui::col::bg0); }
private:
    mj7ui::MainView view;
};
