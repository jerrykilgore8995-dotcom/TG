#pragma once

#include "Processor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TGEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TGEditor(TGProcessor&);
    ~TGEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class FaceLook final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float,
                              float, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                                  bool, bool) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    };

    class MatchButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void()> onImport;
        void mouseDown(const juce::MouseEvent&) override;
    };

    void timerCallback() override;
    void chooseMatch();
    void setMatch(bool);

    TGProcessor& owner;
    FaceLook face;
    juce::Slider coarse, fine, output;
    juce::TextButton line, impedance, phantom, phase;
    MatchButton match {"Match"};
    juce::TooltipWindow tooltips;
    std::unique_ptr<juce::FileChooser> chooser;
    using SliderLink = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonLink = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderLink> coarseLink, fineLink, outputLink;
    std::unique_ptr<ButtonLink> lineLink, impedanceLink, phaseLink;
};
