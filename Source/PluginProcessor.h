#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

constexpr int kNumSlots = 16;
constexpr int kVoices   = 4;   // per slot, only used to crossfade retriggers

enum class PlayMode { OneShot = 0, Gate, Loop, Toggle };

class JingleTriggerProcessor : public juce::AudioProcessor
{
public:
    JingleTriggerProcessor();
    ~JingleTriggerProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Jingle Trigger"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Message-thread API (used by the editor)
    bool loadFile (int slot, const juce::File& file);
    void clearSlot (int slot);
    juce::String getSlotPath (int slot) const    { return slots[(size_t) slot].path; }
    bool isSlotMissing (int slot) const          { return slots[(size_t) slot].missing; }
    bool isSlotLoaded (int slot) const           { return slots[(size_t) slot].loaded.load(); }
    float getSlotProgress (int slot) const       { return slots[(size_t) slot].progress.load(); }
    bool isSlotPlaying (int slot) const          { return slots[(size_t) slot].playing.load(); }
    void requestTrigger (int slot)               { slots[(size_t) slot].uiCmd.store (1); }
    void requestStop (int slot)                  { slots[(size_t) slot].uiCmd.store (2); }
    void requestStopAll()                        { stopAllRequest.store (true); }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        double pos = 0.0;
        float env = 0.0f;
        bool active = false, releasing = false;
    };

    struct Slot
    {
        juce::SpinLock lock;                          // guards data/fileRate/voices
        std::unique_ptr<juce::AudioBuffer<float>> data;
        double fileRate = 44100.0;
        std::array<Voice, kVoices> voices;
        bool anyActive = false;                       // audio thread (and loader, under lock)
        juce::SmoothedValue<float> gain;
        bool prevTrig = false;                        // audio thread

        juce::String path;                            // message thread
        bool missing = false;                         // message thread
        std::atomic<bool>  loaded { false };
        std::atomic<float> progress { 0.0f };
        std::atomic<bool>  playing { false };
        std::atomic<int>   uiCmd { 0 };               // 0 none, 1 trigger, 2 stop
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    PlayMode getMode (int slot) const;
    float    slotGain (int slot) const;
    void startSlot (int slot);
    void stopSlot (int slot);
    void stopAll();
    void handleMidi (const juce::MidiMessage&);
    void render (juce::AudioBuffer<float>&, int start, int num);
    void renderSlot (int slot, float* outL, float* outR, int num);

    std::array<Slot, kNumSlots> slots;
    juce::AudioFormatManager formatManager;

    std::array<std::atomic<float>*, kNumSlots> trigP {}, gainP {}, modeP {};
    std::atomic<float>* masterP = nullptr;
    std::atomic<float>* baseNoteP = nullptr;
    std::atomic<float>* stopAllP = nullptr;
    bool prevStopAll = false;
    std::atomic<bool> stopAllRequest { false };

    double sampleRate = 44100.0;
    float attackStep = 0.01f, releaseStep = 0.002f;
    std::vector<float> gainScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JingleTriggerProcessor)
};
