#pragma once
#include "PluginProcessor.h"
#include <memory>

class SlotRow;

class JingleTriggerEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit JingleTriggerEditor (JingleTriggerProcessor&);
    ~JingleTriggerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    JingleTriggerProcessor& proc;

    juce::Label masterLabel, baseLabel;
    juce::Slider masterSlider, baseSlider;
    juce::TextButton stopAllButton { "Stop All" };

    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<SlotRow>> rows;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> masterAtt, baseAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JingleTriggerEditor)
};
