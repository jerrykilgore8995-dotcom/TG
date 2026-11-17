#pragma once

#include "TypeD.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class TGProcessor final : public juce::AudioProcessor
{
public:
    TGProcessor();
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "TG Type D Preview"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::String loadMatch(const juce::File&);
    bool hasMatch() const;
    bool matchAtSessionRate() const;
    juce::AudioProcessorValueTreeState tree;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setProfile(const tg::MatchProfile&);
    void pullProfile();
    void processSlice(juce::dsp::AudioBlock<float>);
    void trackDry(juce::dsp::AudioBlock<float>, bool writeDelayed);

    struct Parameters
    {
        std::atomic<float>* coarse;
        std::atomic<float>* fine;
        std::atomic<float>* output;
        std::atomic<float>* line;
        std::atomic<float>* impedance;
        std::atomic<float>* phase;
        std::atomic<float>* match;
    } parameters;
    mutable juce::SpinLock profileLock;
    tg::MatchProfile savedProfile;
    tg::MatchProfile activeProfile;
    std::atomic<unsigned int> profileVersion {1};
    unsigned int audioProfileVersion = 0;
    std::atomic<double> sessionRate {0.0};
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<tg::TypeD, 2> stages;
    std::array<tg::SourceCorrection, 2> correction;
    juce::AudioBuffer<float> dryHistory;
    int historyOffset = 0;
    int sliceSize = 0;
    juce::SmoothedValue<float> finePosition, outputGain, inputGain, sourceBlend, impedanceBlend;
};
