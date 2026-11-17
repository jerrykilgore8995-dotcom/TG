#include "Editor.h"
#include "MatchStimulus.h"
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
    g.drawText(text, juce::Rectangle<float>(x - width * .5f, y, width, height),
               juce::Justification::centred);
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
    for (auto* control : {&line, &impedance, &phase, &pad312})
    {
        control->setClickingTogglesState(true);
        addAndMakeVisible(*control);
    }
    addAndMakeVisible(phantom);
    lineLink = std::make_unique<ButtonLink>(p.tree, "line", line);
    phaseLink = std::make_unique<ButtonLink>(p.tree, "phase", phase);
    padLink = std::make_unique<ButtonLink>(p.tree, "pad312", pad312);
    modeSelect.setComponentID("select");
    modeSelect.setTooltip("Select TG, 1290, 312 or V72");
    modeSelect.onClick = [this] { showPreampMenu(); };
    addAndMakeVisible(modeSelect);
    match.setComponentID("match");
    match.onImport = [this] { chooseMatch(); };
    match.onSettings = [this] { showMatchSettings(); };
    match.onClick = [this]
    {
        if (!owner.hasMatch() || !owner.matchAtSessionRate())
            showMatchSettings();
        else
            setMatch(owner.tree.getRawParameterValue("match")->load() < 0.5f);
    };
    addAndMakeVisible(match);
    timerCallback();
    startTimerHz(25);
    setSize(204, 728);
}

TGEditor::~TGEditor()
{
    setLookAndFeel(nullptr);
}

void TGEditor::paint(juce::Graphics& g)
{
    if (mode > 0)
    {
        paintAlternate(g);
        return;
    }
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
    modeSelect.setBounds(138, 698, 55, 23);
    if (mode == 1)
    {
        coarse.setBounds(53, 89, 98, 117);
        fine.setBounds(113, 328, 68, 72);
        impedance.setBounds(45, 290, 42, 30);
        phase.setBounds(45, 333, 42, 30);
        line.setBounds(45, 376, 42, 30);
        phantom.setBounds(45, 419, 42, 24);
        output.setBounds(57, 488, 90, 93);
        match.setBounds(63, 650, 78, 27);
        return;
    }
    if (mode == 2)
    {
        coarse.setBounds(53, 294, 98, 100);
        pad312.setBounds(62, 423, 34, 28);
        phase.setBounds(62, 464, 34, 28);
        impedance.setBounds(62, 505, 34, 28);
        phantom.setBounds(62, 546, 34, 28);
        line.setBounds(110, 595, 63, 23);
        output.setBounds(33, 584, 52, 47);
        match.setBounds(63, 650, 78, 27);
        return;
    }
    if (mode == 3)
    {
        coarse.setBounds(53, 290, 98, 100);
        output.setBounds(58, 448, 88, 94);
        phase.setBounds(43, 584, 47, 31);
        line.setBounds(114, 584, 47, 31);
        match.setBounds(63, 650, 78, 28);
        return;
    }
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
    if (slider.getComponentID() == "round")
    {
        auto outline = juce::Rectangle<float>(2 * radius, 2 * radius).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(.72f));
        g.fillEllipse(outline.expanded(3).translated(0, 2));
        g.setGradientFill(juce::ColourGradient(knobColour.brighter(.65f), centre.x, centre.y - radius,
                                              knobColour.darker(.65f), centre.x, centre.y + radius, false));
        g.fillEllipse(outline);
        g.setColour(knobColour.darker(.28f));
        g.drawEllipse(outline.reduced(2), 2);
        g.setColour(white);
        g.drawLine(centre.x + std::sin(angle) * radius * .55f,
                   centre.y - std::cos(angle) * radius * .55f,
                   centre.x + std::sin(angle) * radius * .91f,
                   centre.y - std::cos(angle) * radius * .91f, 2);
        return;
    }
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    juce::Path shadow;
    shadow.addEllipse(-radius, -radius + 3, radius * 2, radius * 2);
    shadow.addRoundedRectangle(-radius * .53f, 2, radius * 1.06f, radius * 1.71f, 2);
    g.setColour(juce::Colours::black.withAlpha(.64f));
    g.fillPath(shadow);
    juce::Path body;
    body.startNewSubPath(0, -1.53f * radius);
    body.lineTo(.48f * radius, -.86f * radius);
    body.cubicTo(1.11f * radius, -.62f * radius, 1.24f * radius, .43f * radius,
                 .53f * radius, .83f * radius);
    body.lineTo(.53f * radius, 1.63f * radius);
    body.lineTo(-.53f * radius, 1.63f * radius);
    body.lineTo(-.53f * radius, .83f * radius);
    body.cubicTo(-1.24f * radius, .43f * radius, -1.11f * radius, -.62f * radius,
                 -.48f * radius, -.86f * radius);
    body.closeSubPath();
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
        auto fill = active ? juce::Colour(0xffd3c762) : ((id == "match" || id == "select") ? juce::Colour(0xff424142) : white);
        if (hover) fill = fill.brighter(.12f);
        if (down) fill = fill.darker(.24f);
        g.setGradientFill(juce::ColourGradient(fill, 0, 0, fill.darker(.26f),
                                              bounds.getRight(), bounds.getBottom(), false));
        g.fillRect(bounds.reduced(.5f, .5f));
    }
}

void TGEditor::FaceLook::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    if (button.getComponentID() == "match" || button.getComponentID() == "select")
        label(g, button.getButtonText(), static_cast<float>(button.getWidth()) * .5f, 0,
              static_cast<float>(button.getWidth()), static_cast<float>(button.getHeight()),
              14, button.getToggleState() ? juce::Colours::black : yellow);
}

void TGEditor::MatchButton::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        if (onSettings) onSettings();
        return;
    }
    if (event.mods.isShiftDown())
    {
        if (onImport) onImport();
        return;
    }
    juce::TextButton::mouseDown(event);
}

void TGEditor::timerCallback()
{
    const auto selected = juce::roundToInt(owner.tree.getRawParameterValue("amp")->load());
    if (selected != mode)
        refreshPanel(selected);
    if (mode > 0)
        repaint();
    const auto valid = owner.matchAtSessionRate();
    const auto isMic = owner.tree.getRawParameterValue("line")->load() < 0.5f;
    const auto enabled = owner.tree.getRawParameterValue("match")->load() > 0.5f;
    match.setToggleState(valid && isMic && enabled, juce::dontSendNotification);
    match.setTooltip(valid ? "Toggle UMC1820 correction. Right-click for Match settings."
                           : "Open Match settings to export a test WAV or load an input profile.");
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

void TGEditor::showMatchSettings()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Test output attenuation...");
    menu.addItem(2, "Load Match profile...");
    auto safe = juce::Component::SafePointer<TGEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&match), [safe](int choice)
    {
        if (safe == nullptr)
            return;
        if (choice == 1)
            safe->showTestOutput();
        if (choice == 2)
            safe->chooseMatch();
    });
}

void TGEditor::showTestOutput()
{
    auto* dialog = new juce::AlertWindow("UMC1820 Test Output",
        "The UMC1820 line output is rated +16 dBu at full scale.\n"
        "The 50 dB default puts the hottest test tone near -40 dBu.\n"
        "Set a pad from 20 to 80 dB. Use a balanced cable and turn phantom power off.",
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("pad", juce::String(owner.testAttenuation(), 1), "Attenuation (dB)");
    dialog->getTextEditor("pad")->setInputRestrictions(5, "0123456789.");
    juce::StringArray rates;
    int selectedRate = 0;
    for (int rate : tg::testRates)
    {
        if (std::abs(owner.activeSampleRate() - rate) < 0.5)
            selectedRate = rates.size();
        rates.add(juce::String(rate) + " Hz");
    }
    dialog->addComboBox("rate", rates, "Sample Rate");
    dialog->getComboBoxComponent("rate")->setSelectedItemIndex(selectedRate);
    dialog->addButton("Export WAV...", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto safe = juce::Component::SafePointer<TGEditor>(this);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog](int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto pad = dialog->getTextEditorContents("pad").getFloatValue();
        if (pad < tg::minTestPadDb || pad > tg::maxTestPadDb)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                  "Test attenuation", "Enter a value from 20 to 80 dB.");
            return;
        }
        const int rateIndex = dialog->getComboBoxComponent("rate")->getSelectedItemIndex();
        const auto rate = tg::testRates[std::max(0, rateIndex)];
        safe->owner.setTestAttenuation(pad);
        safe->exportTestWav(rate, pad);
    }), true);
}

void TGEditor::exportTestWav(int rate, float attenuation)
{
    if (chooser != nullptr)
        return;
    auto file = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("UMC1820-match-test.wav");
    chooser = std::make_unique<juce::FileChooser>("Export UMC1820 Match test", file, "*.wav");
    auto safe = juce::Component::SafePointer<TGEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, rate, attenuation](const juce::FileChooser& selected)
        {
            if (safe == nullptr)
                return;
            const auto result = selected.getResult();
            safe->chooser.reset();
            if (result != juce::File() && !tg::writeMatchStimulus(result.withFileExtension("wav"), rate, attenuation))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                      "Test export", "The test WAV could not be saved.");
        });
}

void TGEditor::showPreampMenu()
{
    juce::PopupMenu menu;
    const juce::String names[] {"TG Type D", "1290", "312", "V72"};
    for (int index = 0; index < 4; ++index)
        menu.addItem(index + 1, names[index], true, mode == index);
    auto safe = juce::Component::SafePointer<TGEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&modeSelect), [safe](int selection)
    {
        if (safe == nullptr || selection <= 0)
            return;
        auto* parameter = safe->owner.tree.getParameter("amp");
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(static_cast<float>(selection - 1) / 3.0f);
        parameter->endChangeGesture();
        safe->refreshPanel(selection - 1);
    });
}

void TGEditor::refreshPanel(int selection)
{
    mode = selection;
    constexpr const char* gainIds[] {"coarse", "gain1290", "gain312", "driveV72"};
    constexpr const char* modeNames[] {"TG", "1290", "312", "V72"};
    constexpr float gainDefaults[] {35.0f, 35.0f, 34.0f, 0.0f};
    const auto arc = mode == 0 ? pi * .5f : pi * .78f;
    coarseLink.reset();
    coarse.setRotaryParameters(2 * pi - arc, 2 * pi + arc, true);
    coarse.setDoubleClickReturnValue(true, gainDefaults[mode]);
    coarse.setComponentID(mode == 0 ? "pointer" : "round");
    coarse.setName(mode == 3 ? "Drive" : "Gain");
    coarse.setTooltip(mode == 3 ? "Drive into the fixed-gain V72 circuit."
                                 : "Adjust microphone gain and circuit drive.");
    coarse.setColour(juce::Slider::rotarySliderFillColourId,
                     juce::Colour(mode == 2 ? 0xff1689d6 : mode == 3 ? 0xff303236 : 0xffba222a));
    coarseLink = std::make_unique<SliderLink>(owner.tree, gainIds[mode], coarse);
    fine.setVisible(mode < 2);
    fine.setComponentID(mode == 0 ? "pointer" : "round");
    output.setComponentID(mode == 0 ? "pointer" : "round");
    impedanceLink.reset();
    impedance.setVisible(mode != 3);
    if (mode != 3)
    {
        constexpr const char* switchIds[] {"impedance", "low1290", "tap312"};
        impedanceLink = std::make_unique<ButtonLink>(owner.tree, switchIds[mode], impedance);
    }
    impedance.setComponentID(mode == 0 ? "impedance" : "utility");
    impedance.setTooltip(mode == 2 ? "Switch between the 3:1 and 1:1 output transformer taps."
                                   : "Select the simulated low-impedance input.");
    line.setComponentID(mode == 0 ? "line" : "utility");
    phantom.setVisible(mode != 3);
    pad312.setVisible(mode == 2);
    pad312.setTooltip("20 dB microphone input pad");
    modeSelect.setButtonText(modeNames[mode]);
    resized();
    repaint();
}

void TGEditor::paintAlternate(juce::Graphics& g)
{
    const auto steel = juce::Colour(0xff929899);
    const auto panel = juce::Colour(mode == 1 ? 0xff242936 : mode == 2 ? 0xff151515 : 0xff555a5b);
    g.setGradientFill(juce::ColourGradient(panel.brighter(.10f), 0, 0, panel.darker(.45f), 204, 728, false));
    g.fillAll();
    g.setColour(panel.brighter(.35f));
    g.drawRect(getLocalBounds().toFloat().reduced(.5f), 1.0f);
    screw(g, 102, 20, 10);
    screw(g, 102, 709, 10);
    const auto level = owner.meterLevel();
    if (mode == 1)
    {
        const auto ink = juce::Colour(0xffe6ae54);
        g.setColour(level > .65f ? juce::Colours::red : level > .002f ? juce::Colours::limegreen
                                                                     : juce::Colour(0xff343836));
        g.fillEllipse(23, 58, 9, 9);
        label(g, "SIG", 27, 71, 29, 15, 9, ink);
        dial(g, 102, 145, pi * .78f, 13, 61, ink, [](int i)
        {
            return i % 2 == 0 ? juce::String(20 + i * 5) : juce::String();
        });
        label(g, "INPUT GAIN", 102, 233, 112, 16, 12, ink);
        label(g, "LO Z", 24, 297, 42, 16, 11, ink);
        label(g, "POL", 24, 341, 42, 16, 12, ink);
        label(g, "LINE", 24, 383, 42, 16, 11, ink);
        label(g, "48V", 23, 425, 41, 16, 11, ink);
        label(g, "TRIM", 146, 304, 80, 16, 11, ink);
        label(g, "OUTPUT", 102, 582, 90, 16, 12, ink);
        return;
    }
    if (mode == 2)
    {
        auto blue = juce::Colour(0xff38a4e4);
        auto bezel = juce::Rectangle<float>(29, 58, 146, 193);
        g.setColour(juce::Colour(0xff555656));
        g.fillRoundedRectangle(bezel, 7);
        g.setColour(juce::Colour(0xff090909));
        g.fillEllipse(33, 73, 138, 151);
        auto meter = juce::Rectangle<float>(39, 79, 126, 138);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffff0b0), 102, 93,
                                              juce::Colour(0xffa9935d), 102, 230, false));
        g.fillEllipse(meter);
        const auto origin = juce::Point<float>(102, 202);
        for (int i = 0; i < 14; ++i)
        {
            const auto angle = -.84f + static_cast<float>(i) / 13.0f * 1.68f;
            g.setColour(i > 9 ? juce::Colour(0xffbf2721) : juce::Colour(0xff332b18));
            g.drawLine(origin.x + std::sin(angle) * 67, origin.y - std::cos(angle) * 67,
                       origin.x + std::sin(angle) * 73, origin.y - std::cos(angle) * 73, 1);
        }
        label(g, "-20     -7     0  +3", 103, 116, 134, 15, 9, juce::Colour(0xff322418));
        label(g, "VU", 102, 156, 60, 18, 10, juce::Colour(0xff322418));
        const auto db = juce::Decibels::gainToDecibels(level) + 18.0f;
        const auto fraction = std::clamp((db + 20.0f) / 23.0f, 0.0f, 1.0f);
        const auto angle = -.86f + 1.72f * fraction;
        g.setColour(juce::Colour(0xff272019));
        g.drawLine(origin.x, origin.y, origin.x + std::sin(angle) * 68,
                   origin.y - std::cos(angle) * 68, 1.3f);
        for (float x : {37.0f, 167.0f})
        {
            screw(g, x, 65, 5);
            screw(g, x, 244, 5);
            screw(g, x, 670, 8);
        }
        g.setColour(level > .65f ? juce::Colours::red : juce::Colour(0xff442828));
        g.fillEllipse(51, 266, 8, 8);
        label(g, "PEAK", 31, 263, 34, 13, 9, white);
        dial(g, 102, 342, pi * .78f, 9, 47, blue, [](int) { return juce::String(); });
        label(g, "GAIN", 159, 335, 45, 16, 11, white);
        label(g, "PAD -20", 139, 430, 62, 16, 11, white);
        label(g, "POL", 133, 471, 56, 16, 11, white);
        label(g, "3:1", 133, 512, 56, 16, 11, white);
        label(g, "48V", 133, 553, 56, 16, 11, white);
        label(g, "LINE", 142, 573, 68, 18, 10, blue);
        label(g, "OUT", 60, 627, 62, 16, 10, blue);
        return;
    }
    g.setColour(juce::Colour(0xff171d21));
    for (int i = 0; i < 5; ++i)
        g.fillRoundedRectangle(58, 82.0f + static_cast<float>(i) * 14, 88, 6, 3);
    label(g, "V72", 102, 168, 136, 31, 27, white);
    g.setColour(juce::Colour(0xff292b2b));
    g.fillEllipse(91, 216, 22, 22);
    g.setColour(juce::Colour(0xffe78437).withAlpha(level > .002f ? 1.0f : .55f));
    g.fillEllipse(95, 220, 14, 14);
    dial(g, 102, 338, pi * .78f, 7, 52, steel.brighter(.5f), [](int i)
    {
        if (i == 0) return juce::String("-15");
        if (i == 3) return juce::String("0");
        if (i == 6) return juce::String("+15");
        return juce::String();
    });
    label(g, "DRIVE", 102, 406, 98, 19, 14, white);
    label(g, "OUTPUT", 102, 548, 98, 19, 14, white);
    label(g, "POL", 64, 563, 64, 18, 11, white);
    label(g, "LINE", 137, 563, 64, 18, 11, white);
}
