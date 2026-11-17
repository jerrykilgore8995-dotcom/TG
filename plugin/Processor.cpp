#include "Processor.h"
#include "Editor.h"
#include "MatchStimulus.h"
#include <sstream>

namespace {
constexpr int oversamplingStages = 2;
constexpr float voltsPerDbuPeak = 0.775f * 1.41421356237f;

float peakVolts(float dbu)
{
    return voltsPerDbuPeak * tg::linear(dbu);
}

constexpr char profileKey[] = "matchCoefficients";
constexpr char rateKey[] = "matchRate";
constexpr char zeroKey[] = "matchZeroDbu";
constexpr char testPadKey[] = "matchTestPadDb";
}

TGProcessor::TGProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      tree(*this, nullptr, "TG", createLayout()),
      parameters {tree.getRawParameterValue("coarse"), tree.getRawParameterValue("fine"),
                  tree.getRawParameterValue("output"), tree.getRawParameterValue("line"),
                  tree.getRawParameterValue("impedance"), tree.getRawParameterValue("phase"),
                  tree.getRawParameterValue("match")}
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TGProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;
    using ID = juce::ParameterID;
    return {
        std::make_unique<Float>(ID{"coarse", 1}, "Coarse Gain", juce::NormalisableRange<float>(20, 50, 5), 35),
        std::make_unique<Float>(ID{"fine", 1}, "Fine Gain", juce::NormalisableRange<float>(-10, 10, 0.1f), 0),
        std::make_unique<Float>(ID{"output", 1}, "Output", juce::NormalisableRange<float>(0, 10, 0.01f), 10),
        std::make_unique<Bool>(ID{"line", 1}, "Line", false),
        std::make_unique<Bool>(ID{"impedance", 1}, "300 Ohm Position", false),
        std::make_unique<Bool>(ID{"phase", 1}, "Polarity", false),
        std::make_unique<Bool>(ID{"match", 1}, "Match", false)
    };
}

bool TGProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return layout.getMainInputChannelSet() == output
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void TGProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    sessionRate.store(sampleRate);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(getTotalNumInputChannels()), oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    sliceSize = std::max(1, maximumBlockSize);
    oversampling->initProcessing(static_cast<size_t>(sliceSize));
    oversampling->reset();
    const auto fastRate = sampleRate * static_cast<double>(oversampling->getOversamplingFactor());
    for (auto& stage : stages)
        stage.prepare(fastRate);
    for (auto& channel : correction)
        channel.reset();
    const auto factor = static_cast<int>(oversampling->getOversamplingFactor());
    const auto inherentDelay = juce::roundToInt(oversampling->getLatencyInSamples() * static_cast<float>(factor));
    // Pad at the oversampled rate to avoid a phase-warping fractional delay
    // at the host rate. Both half-band FIR stages then retain linear phase.
    fastDelayLength = (factor - inherentDelay % factor) % factor;
    fastDelayOffset = 0;
    oversamplingPad = {};
    const auto latency = tg::matchDelay + (inherentDelay + fastDelayLength) / factor;
    setLatencySamples(latency);
    dryHistory.setSize(getTotalNumInputChannels(), latency);
    dryHistory.clear();
    historyOffset = 0;
    finePosition.reset(fastRate, 0.012);
    finePosition.setCurrentAndTargetValue(2.0f + *parameters.fine / 5.0f);
    impedanceBlend.reset(fastRate, 0.012);
    impedanceBlend.setCurrentAndTargetValue(*parameters.line < 0.5f ? parameters.impedance->load() : 0.0f);
    outputGain.reset(fastRate, 0.012);
    outputGain.setCurrentAndTargetValue(std::pow(*parameters.output / 10.0f, 1.34f));
    inputGain.reset(sampleRate, 0.012);
    inputGain.setCurrentAndTargetValue(0.0f);
    sourceBlend.reset(sampleRate, 0.012);
    sourceBlend.setCurrentAndTargetValue(0.0f);
    audioProfileVersion = 0;
    pullProfile();
}

void TGProcessor::releaseResources()
{
    oversampling.reset();
    sessionRate.store(0.0);
}

void TGProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (oversampling == nullptr || buffer.getNumSamples() == 0)
        return;
    pullProfile();
    auto block = juce::dsp::AudioBlock<float>(buffer);
    trackDry(block, false);
    for (int start = 0; start < buffer.getNumSamples(); start += sliceSize)
    {
        auto length = std::min(sliceSize, buffer.getNumSamples() - start);
        processSlice(block.getSubBlock(static_cast<size_t>(start), static_cast<size_t>(length)));
    }
}

void TGProcessor::processSlice(juce::dsp::AudioBlock<float> block)
{
    const auto line = *parameters.line > 0.5f;
    const auto match = !line && *parameters.match > 0.5f && activeProfile.usable(sessionRate.load());
    const auto fine = parameters.fine->load();
    const auto position = 2.0f + fine / 5.0f;
    const auto impedance = line ? 0.0f : parameters.impedance->load();
    const auto inputZero = line ? tg::nominalDigitalZeroDbu
                                : (match ? static_cast<double>(activeProfile.zeroDbu) : tg::nominalMicZeroDbu);
    const auto gain = tg::linear(*parameters.coarse + fine - (line ? 20.0f : 0.0f));
    const auto polarity = *parameters.phase > 0.5f ? -1.0f : 1.0f;
    // Panel gain is normalized at 1 kHz. The resistor-dependent transfer and
    // distortion still follow the two nearest physical reference settings.
    inputGain.setTargetValue(polarity * peakVolts(static_cast<float>(inputZero))
                             * gain / tg::TypeD::naturalGain(position, impedance));
    sourceBlend.setTargetValue(match ? 1.0f : 0.0f);
    finePosition.setTargetValue(position);
    impedanceBlend.setTargetValue(impedance);
    outputGain.setTargetValue(std::pow(*parameters.output / 10.0f, 1.34f));

    for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
    {
        const auto mix = sourceBlend.getNextValue();
        const auto gainIn = inputGain.getNextValue();
        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getChannelPointer(channel);
            const auto input = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            samples[sample] = correction[channel].process(input, mix, activeProfile) * gainIn;
        }
    }

    auto fast = oversampling->processSamplesUp(block);
    constexpr float fsVolts = voltsPerDbuPeak * 12.5892541179f;
    for (size_t sample = 0; sample < fast.getNumSamples(); ++sample)
    {
        const auto fineMix = finePosition.getNextValue();
        const auto inputZ = impedanceBlend.getNextValue();
        const auto level = outputGain.getNextValue() / fsVolts;
        for (size_t channel = 0; channel < fast.getNumChannels(); ++channel)
        {
            auto* samples = fast.getChannelPointer(channel);
            auto value = stages[channel].process(samples[sample], fineMix, inputZ) * level;
            if (fastDelayLength > 0)
                std::swap(value, oversamplingPad[channel][static_cast<size_t>(fastDelayOffset)]);
            samples[sample] = value;
        }
        if (++fastDelayOffset >= fastDelayLength)
            fastDelayOffset = 0;
    }
    oversampling->processSamplesDown(block);
}

void TGProcessor::trackDry(juce::dsp::AudioBlock<float> block, bool writeDelayed)
{
    const auto length = dryHistory.getNumSamples();
    if (length <= 0)
        return;
    for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
    {
        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto& history = dryHistory.getWritePointer(static_cast<int>(channel))[historyOffset];
            auto& input = block.getChannelPointer(channel)[sample];
            const auto delayed = history;
            history = input;
            if (writeDelayed)
                input = delayed;
        }
        historyOffset = (historyOffset + 1) % length;
    }
}

void TGProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    trackDry(juce::dsp::AudioBlock<float>(buffer), true);
}

void TGProcessor::setProfile(const tg::MatchProfile& profile)
{
    const juce::SpinLock::ScopedLockType lock(profileLock);
    savedProfile = profile;
    profileVersion.fetch_add(1, std::memory_order_release);
}

void TGProcessor::pullProfile()
{
    const auto version = profileVersion.load(std::memory_order_acquire);
    if (version == audioProfileVersion)
        return;
    const juce::SpinLock::ScopedTryLockType lock(profileLock);
    if (lock.isLocked())
    {
        activeProfile = savedProfile;
        audioProfileVersion = profileVersion.load(std::memory_order_relaxed);
    }
}

bool TGProcessor::hasMatch() const
{
    const juce::SpinLock::ScopedLockType lock(profileLock);
    return savedProfile.rate > 0;
}

bool TGProcessor::matchAtSessionRate() const
{
    const juce::SpinLock::ScopedLockType lock(profileLock);
    const auto rate = sessionRate.load();
    return rate == 0.0 ? savedProfile.rate > 0 : savedProfile.usable(rate);
}

juce::String TGProcessor::loadMatch(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 32000)
        return "The selected file is missing or too large.";
    std::istringstream reader(file.loadFileAsString().toStdString());
    reader.imbue(std::locale::classic());
    tg::MatchProfile profile;
    std::string format;
    int taps = 0, delay = 0;
    if (!(reader >> format >> profile.rate >> taps >> delay >> profile.zeroDbu)
        || format != "TG_MATCH_V1" || taps != tg::matchTaps || delay != tg::matchDelay)
        return "This file is not a TG Match V1 profile.";
    for (auto& tap : profile.kernel)
        if (!(reader >> tap))
            return "The profile is incomplete.";
    std::string extra;
    if ((reader >> extra) || !profile.valid())
        return "This profile contains invalid calibration values.";
    const auto rate = sessionRate.load();
    if (rate > 0.0 && !profile.usable(rate))
        return "Capture a profile at this session’s sample rate.";
    setProfile(profile);
    return {};
}

void TGProcessor::setTestAttenuation(float decibels)
{
    testPad.store(std::isfinite(decibels) ? std::clamp(decibels, tg::minTestPadDb, tg::maxTestPadDb)
                                          : tg::defaultTestPadDb);
}

void TGProcessor::getStateInformation(juce::MemoryBlock& data)
{
    auto state = tree.copyState();
    state.setProperty(testPadKey, testAttenuation(), nullptr);
    const juce::SpinLock::ScopedLockType lock(profileLock);
    if (savedProfile.valid())
    {
        state.setProperty(profileKey, juce::var(juce::MemoryBlock(savedProfile.kernel.data(),
                            sizeof(savedProfile.kernel))), nullptr);
        state.setProperty(rateKey, savedProfile.rate, nullptr);
        state.setProperty(zeroKey, savedProfile.zeroDbu, nullptr);
    }
    juce::MemoryOutputStream stream(data, false);
    state.writeToStream(stream);
}

void TGProcessor::setStateInformation(const void* data, int size)
{
    if (size <= 0 || size > 2 * 1024 * 1024)
        return;
    auto state = juce::ValueTree::readFromData(data, static_cast<size_t>(size));
    if (!state.hasType(tree.state.getType()))
        return;
    tg::MatchProfile profile;
    auto coefficients = state.getProperty(profileKey);
    auto* memory = coefficients.getBinaryData();
    if (memory != nullptr && memory->getSize() == sizeof(profile.kernel))
    {
        profile.rate = static_cast<int>(state.getProperty(rateKey));
        profile.zeroDbu = static_cast<float>(state.getProperty(zeroKey));
        memory->copyTo(profile.kernel.data(), 0, sizeof(profile.kernel));
    }
    setTestAttenuation(static_cast<float>(state.getProperty(testPadKey, tg::defaultTestPadDb)));
    state.removeProperty(testPadKey, nullptr);
    state.removeProperty(profileKey, nullptr);
    state.removeProperty(rateKey, nullptr);
    state.removeProperty(zeroKey, nullptr);
    setProfile(profile.valid() ? profile : tg::MatchProfile{});
    tree.replaceState(state);
}

juce::AudioProcessorEditor* TGProcessor::createEditor()
{
    return new TGEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TGProcessor;
}
