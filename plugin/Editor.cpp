#include "Editor.h"
#include <numbers>

namespace {
const auto white = juce::Colour(0xfff5f1e6);
const auto yellow = juce::Colour(0xfff7e629);
constexpr float pi = std::numbers::pi_v<float>;

juce::Font panelFont(float size, bool bold = true)
{
    return juce::Font(juce::FontOptions("Arial Narrow", size, bold ? juce::Font::bold : juce::Font::plain));
}

void label(juce::Graphics& g, juce::String text, float x, float y, float width,
           float height, float size, juce::Colour ink)
{
    g.setFont(panelFont(size));
    g.setColour(ink);
    g.drawText(text, {x - width * .5f, y, width, height}, juce::Justification::centred);
}

void screw(juce::Graphics& g, float x, float y, float radius)
{
    auto r = juce::Rectangle<float>(2 * radius, 2 * radius).withCentre({x, y});
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff626267), x - 4, y - 6,
                                          juce::Colour(0xff212122), x + 4, y + 5, false));
    g.fillEllipse(r);
    g.setColour(juce::Colour(0xff171719));
    g.drawLine(x - radius * .54f, y, x + radius * .54f, y, 1.5f);
    g.drawLine(x, y - radius * .54f, x, y + radius * .54f, 1.5f);
}

void dial(juce::Graphics& g, float x, float y, float arc, int count, float radius,
          juce::Colour ink, const std::function<juce::String(int)>& numerals)
{
    for (int i = 0; i < count; ++i)
    {
        const auto angle = arc * (2.0f * static_cast<float>(i) / static_cast<float>(count - 1) - 1.0f);
        const auto s = std::sin(angle);
        const auto c = std::cos(angle);
        g.setColour(ink);
        g.drawLine(x + s * (radius - 8), y - c * (radius - 8), x + s * radius, y - c * radius, 1.65f);
        const auto text = numerals(i);
        if (text.isNotEmpty())
            label(g, text, x + s * (radius + 10), y - c * (radius + 10) - 7, 28, 14, 11.5f, ink);
    }
}
}

TGEditor::TGEditor(TGProcessor& p)
    : AudioProcessorEditor(p), owner(p), tooltips(this, 650)
{
    setLookAndFeel(&face);
    const auto slider = [this](juce::Slider& knob, const char* name, float angle,
                               juce::Colour colour, const char* help)
    {
        knob.setName(name);
        knob.setTooltip(help);
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters(2 * pi - angle, 2 * pi + angle, true);
        knob.setColour(juce::Slider::rotarySliderFillColourId, colour);
        addAndMakeVisible(knob);
    };
    slider(coarse, "Coarse Gain", pi * .5f, juce::Colour(0xffce2229), "Coarse gain in 5 dB steps.");
    slider(fine, "Fine Gain", pi * .78f, juce::Colour(0xff454548), "Fine gain changes feedback and drive.");
    slider(output, "Output", pi * .78f, juce::Colour(0xff73757a), "Post-stage output attenuation.");
    coarse.setDoubleClickReturnValue(true, 35);
    fine.setDoubleClickReturnValue(true, 0);
    output.setDoubleClickReturnValue(true, 10);
    coarseLink = std::make_unique<SliderLink>(p.tree, "coarse", coarse);
    fineLink = std::make_unique<SliderLink>(p.tree, "fine", fine);
    outputLink = std::make_unique<SliderLink>(p.tree, "output", output);

    line.setName("Mic or Line");
    line.setComponentID("line");
    line.setTooltip("Select Line for prerecorded tracks and line-level material.");
    impedance.setName("Input Impedance");
    impedance.setComponentID("impedance");
    impedance.setTooltip("Change simulated microphone-source loading in Mic mode.");
    phase.setName("Phase");
    phase.setTooltip("Reverse input polarity.");
    phantom.setName("48V");
    phantom.setTooltip("Phantom power is controlled on your audio interface.");
    phantom.setInterceptsMouseClicks(false, false);
    for (auto* control : {&line, &impedance, &phase})
    {
        control->setClickingTogglesState(true);
        addAndMakeVisible(*control);
    }
    addAndMakeVisible(phantom);
    lineLink = std::make_unique<ButtonLink>(p.tree, "line", line);
    impedanceLink = std::make_unique<ButtonLink>(p.tree, "impedance", impedance);
    phaseLink = std::make_unique<ButtonLink>(p.tree, "phase", phase);
    match.setComponentID("match");
    match.onImport = [this] { chooseMatch(); };
    match.onClick = [this]
    {
        if (!owner.hasMatch() || !owner.matchAtSessionRate())
            chooseMatch();
        else
            setMatch(owner.tree.getRawParameterValue("match")->load() < 0.5f);
    };
    addAndMakeVisible(match);
    timerCallback();
    startTimerHz(8);
    setSize(204, 728);
}

TGEditor::~TGEditor()
{
    setLookAndFeel(nullptr);
}

void TGEditor::paint(juce::Graphics& g)
{
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3b3c), 0, 0,
                                          juce::Colour(0xff262627), 204, 728, false));
    g.fillAll();
    g.setColour(juce::Colour(0xff69696a));
    g.drawRect(getLocalBounds().toFloat().reduced(.5f), 1.0f);
    g.setColour(white);
    g.drawRoundedRectangle(7.5f, 40.0f, 189.0f, 563.0f, 24.0f, 1.6f);
    screw(g, 102, 21, 12);
    screw(g, 12, 75, 10);
    screw(g, 192, 75, 10);
    screw(g, 12, 658, 10);
    screw(g, 192, 658, 10);
    screw(g, 102, 710, 12);

    dial(g, 102, 150, pi * (2.0f / 3.0f), 17, 68, white, [](int i)
    {
        return i >= 2 && i <= 14 && i % 2 == 0
            ? "+" + juce::String(15 + i * 2.5, 0) : juce::String();
    });
    label(g, "COARSE GAIN", 102, 240, 104, 17, 14, yellow);
    label(g, "MIC", 30, 209, 29, 13, 10, yellow);
    label(g, "LINE", 30, 278, 29, 13, 10, yellow);
    label(g, "IMPEDANCE", 171, 199, 52, 10, 7.8f, white);
    label(g, "300", 171, 209, 31, 13, 10, white);
    label(g, "1200", 171, 278, 31, 13, 10, white);

    dial(g, 102, 343, pi * .78f, 21, 49.5f, yellow, [](int i)
    {
        if (i == 0) return juce::String("-10");
        if (i == 10) return juce::String("0");
        if (i == 20) return juce::String("+10");
        return juce::String();
    });
    label(g, "FINE GAIN", 102, 412, 90, 17, 14, yellow);
    dial(g, 102, 508, pi * .78f, 21, 48, white, [](int i)
    {
        return i % 2 == 0 ? juce::String(i / 2) : juce::String();
    });
    label(g, "OUTPUT", 102, 580, 76, 16, 14, yellow);
    label(g, "48V", 26, 619, 28, 16, 9, yellow);
    label(g, "PHASE", 180, 619, 34, 16, 9, yellow);
}

void TGEditor::resized()
{
    coarse.setBounds(38, 78, 128, 150);
    fine.setBounds(52, 290, 100, 108);
    output.setBounds(50, 453, 104, 110);
    line.setBounds(17, 223, 26, 53);
    impedance.setBounds(158, 223, 26, 53);
    phantom.setBounds(43, 612, 59, 33);
    phase.setBounds(102, 612, 59, 33);
    match.setBounds(64, 655, 76, 27);
}

void TGEditor::FaceLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                         float pos, float begin, float end, juce::Slider& slider)
{
    const auto knobColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    auto centre = juce::Point<float>(static_cast<float>(x) + static_cast<float>(w) * .5f,
                                      static_cast<float>(y) + static_cast<float>(h) * .48f);
    auto radius = static_cast<float>(w) * .315f;
    const auto angle = begin + (end - begin) * pos;
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    juce::Path shadow;
    shadow.addEllipse(-radius, -radius + 3, radius * 2, radius * 2);
    shadow.addRoundedRectangle(-radius * .53f, 2, radius * 1.06f, radius * 1.71f, 2);
    g.setColour(juce::Colours::black.withAlpha(.64f));
    g.fillPath(shadow);
    juce::Path body;
    body.addEllipse(-radius, -radius, radius * 2, radius * 2);
    body.addTriangle(0, -1.53f * radius, -.57f * radius, -.57f * radius,
                     .57f * radius, -.57f * radius);
    body.addRoundedRectangle(-.53f * radius, .06f * radius, 1.06f * radius, 1.6f * radius, 2);
    g.setGradientFill(juce::ColourGradient(knobColour.brighter(.53f), -radius, -radius,
                                          knobColour.darker(.9f), radius, radius, false));
    g.fillPath(body);
    g.setColour(knobColour.darker(.66f));
    g.strokePath(body, juce::PathStrokeType(.8f));
    g.setColour(knobColour.brighter(.44f));
    g.drawEllipse(-radius * .76f, -radius * .76f, radius * 1.52f, radius * 1.52f, 2.0f);
    g.setGradientFill(juce::ColourGradient(knobColour.darker(.46f), -radius * .4f, -radius * .5f,
                                          knobColour.brighter(.65f), radius * .4f, radius * .5f, false));
    g.fillEllipse(-radius * .41f, -radius * .41f, radius * .82f, radius * .82f);
    g.setColour(white);
    g.drawLine(0, -radius * 1.43f, -radius * .41f, -radius * .73f, 1.8f);
    g.drawLine(0, -radius * 1.43f, radius * .41f, -radius * .73f, 1.8f);
    g.drawLine(-radius * .41f, -radius * .73f, radius * .41f, -radius * .73f, 1.5f);
}

void TGEditor::FaceLook::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool hover, bool down)
{
    auto bounds = button.getLocalBounds().toFloat();
    auto id = button.getComponentID();
    auto active = button.getToggleState();
    if (id == "line" || id == "impedance")
    {
        bounds = bounds.reduced(4.1f, 1.2f);
        g.setColour(juce::Colour(0xff111112));
        g.fillRoundedRectangle(bounds, 8);
        auto lever = bounds.reduced(2.8f, 3.5f).withHeight(26);
        const bool lower = id == "line" ? active : !active;
        if (lower)
            lever.translate(0, bounds.getHeight() - 33);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffc0c0bd), lever.getX(), lever.getY(),
                                              juce::Colour(0xff343638), lever.getRight(), lever.getBottom(), false));
        g.fillRoundedRectangle(lever, 8);
    }
    else
    {
        g.setColour(juce::Colour(0xff101010));
        g.fillRect(bounds.translated(0, 2));
        auto fill = active ? juce::Colour(0xffd3c762) : (id == "match" ? juce::Colour(0xff424142) : white);
        if (hover) fill = fill.brighter(.12f);
        if (down) fill = fill.darker(.24f);
        g.setGradientFill(juce::ColourGradient(fill, 0, 0, fill.darker(.26f),
                                              bounds.getRight(), bounds.getBottom(), false));
        g.fillRect(bounds.reduced(.5f, .5f));
    }
}

void TGEditor::FaceLook::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    if (button.getComponentID() == "match")
        label(g, button.getButtonText(), static_cast<float>(button.getWidth()) * .5f, 0,
              static_cast<float>(button.getWidth()), static_cast<float>(button.getHeight()),
              14, button.getToggleState() ? juce::Colours::black : yellow);
}

void TGEditor::MatchButton::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || event.mods.isShiftDown())
    {
        if (onImport) onImport();
        return;
    }
    juce::TextButton::mouseDown(event);
}

void TGEditor::timerCallback()
{
    const auto valid = owner.matchAtSessionRate();
    const auto isMic = owner.tree.getRawParameterValue("line")->load() < 0.5f;
    const auto enabled = owner.tree.getRawParameterValue("match")->load() > 0.5f;
    match.setToggleState(valid && isMic && enabled, juce::dontSendNotification);
    match.setTooltip(valid ? "Enable UMC1820 correction in Mic mode. Shift-click to replace the profile."
                           : "Import a UMC1820 input.tgmatch profile for this sample rate.");
}

void TGEditor::setMatch(bool enabled)
{
    auto* parameter = owner.tree.getParameter("match");
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
    parameter->endChangeGesture();
    timerCallback();
}

void TGEditor::chooseMatch()
{
    if (chooser != nullptr)
        return;
    chooser = std::make_unique<juce::FileChooser>("Load UMC1820 calibration", juce::File(), "*.tgmatch");
    auto safe = juce::Component::SafePointer<TGEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& selected)
        {
            if (safe == nullptr)
                return;
            const auto file = selected.getResult();
            safe->chooser.reset();
            if (file == juce::File())
                return;
            const auto problem = safe->owner.loadMatch(file);
            if (problem.isEmpty())
                safe->setMatch(true);
            else
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                      "Match profile", problem);
        });
}
