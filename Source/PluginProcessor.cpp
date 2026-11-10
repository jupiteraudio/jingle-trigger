#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout JingleTriggerProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master Gain",
                    NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "basenote", 1 }, "Base Note", 0, 111, 36));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stopall", 1 }, "Stop All", false));

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto n = String (i + 1);
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "trig" + n, 1 }, "Trigger " + n, false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain" + n, 1 }, "Gain " + n,
                        NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
                        AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode" + n, 1 }, "Mode " + n,
                        StringArray { "One-shot", "Gate", "Loop", "Toggle" }, 0));
    }
    return layout;
}

JingleTriggerProcessor::JingleTriggerProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), false)   // optional: lets hosts that open plugins as stereo-in/stereo-out load it
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.registerBasicFormats();

    masterP   = apvts.getRawParameterValue ("master");
    baseNoteP = apvts.getRawParameterValue ("basenote");
    stopAllP  = apvts.getRawParameterValue ("stopall");
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto n = String (i + 1);
        trigP[(size_t) i] = apvts.getRawParameterValue ("trig" + n);
        gainP[(size_t) i] = apvts.getRawParameterValue ("gain" + n);
        modeP[(size_t) i] = apvts.getRawParameterValue ("mode" + n);
    }
}

bool JingleTriggerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet();
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo()
        && (in.isDisabled() || in == AudioChannelSet::stereo());
}

void JingleTriggerProcessor::prepareToPlay (double sr, int maxBlock)
{
    sampleRate  = sr;
    attackStep  = (float) (1.0 / (0.003 * sr));   // 3 ms fade in
    releaseStep = (float) (1.0 / (0.020 * sr));   // 20 ms fade out
    gainScratch.assign ((size_t) jmax (maxBlock, 64), 1.0f);

    for (auto& s : slots)
    {
        const SpinLock::ScopedLockType sl (s.lock);
        s.gain.reset (sr, 0.02);
        s.gain.setCurrentAndTargetValue (1.0f);
        for (auto& v : s.voices) v = {};
        s.anyActive = false;
        s.playing = false;
    }
}

//==============================================================================
PlayMode JingleTriggerProcessor::getMode (int i) const
{
    return (PlayMode) jlimit (0, 3, (int) modeP[(size_t) i]->load());
}

float JingleTriggerProcessor::slotGain (int i) const
{
    const float db = gainP[(size_t) i]->load() + masterP->load();
    return db <= -59.9f ? 0.0f : Decibels::decibelsToGain (db);
}

void JingleTriggerProcessor::startSlot (int i)
{
    auto& s = slots[(size_t) i];
    const SpinLock::ScopedTryLockType sl (s.lock);
    if (! sl.isLocked() || s.data == nullptr) return;

    if (getMode (i) == PlayMode::Toggle)
    {
        bool running = false;
        for (auto& v : s.voices) running |= (v.active && ! v.releasing);
        if (running)
        {
            for (auto& v : s.voices) if (v.active) v.releasing = true;
            return;
        }
    }

    // Fade out whatever is playing (retrigger = short crossfade, no click)
    for (auto& v : s.voices) if (v.active) v.releasing = true;

    Voice* target = nullptr;
    for (auto& v : s.voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
    {
        target = &s.voices[0];
        for (auto& v : s.voices) if (v.env < target->env) target = &v;
    }

    if (! s.anyActive)
        s.gain.setCurrentAndTargetValue (slotGain (i));

    *target = {};
    target->active = true;
    s.anyActive = true;
    s.playing = true;
}

void JingleTriggerProcessor::stopSlot (int i)
{
    auto& s = slots[(size_t) i];
    if (! s.anyActive) return;
    const SpinLock::ScopedTryLockType sl (s.lock);
    if (! sl.isLocked()) return;
    for (auto& v : s.voices) if (v.active) v.releasing = true;
}

void JingleTriggerProcessor::stopAll()
{
    for (int i = 0; i < kNumSlots; ++i) stopSlot (i);
}

void JingleTriggerProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int i = m.getNoteNumber() - (int) baseNoteP->load();
        if (isPositiveAndBelow (i, kNumSlots)) startSlot (i);
    }
    else if (m.isNoteOff())
    {
        const int i = m.getNoteNumber() - (int) baseNoteP->load();
        if (isPositiveAndBelow (i, kNumSlots))
        {
            const auto mode = getMode (i);
            if (mode == PlayMode::Gate || mode == PlayMode::Loop) stopSlot (i);
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        stopAll();
}

//==============================================================================
void JingleTriggerProcessor::renderSlot (int i, float* outL, float* outR, int num)
{
    auto& s = slots[(size_t) i];
    const SpinLock::ScopedTryLockType sl (s.lock);
    if (! sl.isLocked() || s.data == nullptr) return;

    const float* src0 = s.data->getReadPointer (0);
    const float* src1 = s.data->getNumChannels() > 1 ? s.data->getReadPointer (1) : src0;
    const int len = s.data->getNumSamples();
    const double inc = s.fileRate / sampleRate;
    const bool exact = std::abs (inc - 1.0) < 1.0e-9;
    const bool loop  = getMode (i) == PlayMode::Loop;

    s.gain.setTargetValue (slotGain (i));

    int done = 0;
    while (done < num)
    {
        const int chunk = jmin (num - done, (int) gainScratch.size());
        const bool smoothing = s.gain.isSmoothing();
        float constGain = 1.0f;
        if (smoothing) { for (int k = 0; k < chunk; ++k) gainScratch[(size_t) k] = s.gain.getNextValue(); }
        else           { constGain = s.gain.getTargetValue(); }

        float* l = outL + done;
        float* r = outR + done;

        for (auto& v : s.voices)
        {
            if (! v.active) continue;
            for (int k = 0; k < chunk; ++k)
            {
                if (v.pos >= (double) len)
                {
                    if (loop) { v.pos = std::fmod (v.pos, (double) len); }
                    else      { v.active = false; break; }
                }

                const int idx = (int) v.pos;
                float a, b;
                if (exact) { a = src0[idx]; b = src1[idx]; }
                else
                {
                    const float fr = (float) (v.pos - idx);
                    int j = idx + 1;
                    if (j >= len) j = loop ? 0 : idx;
                    a = src0[idx] + fr * (src0[j] - src0[idx]);
                    b = src1[idx] + fr * (src1[j] - src1[idx]);
                }

                if (v.releasing)
                {
                    v.env -= releaseStep;
                    if (v.env <= 0.0f) { v.active = false; break; }
                }
                else if (v.env < 1.0f)
                    v.env = jmin (1.0f, v.env + attackStep);

                const float g = v.env * (smoothing ? gainScratch[(size_t) k] : constGain);
                l[k] += a * g;
                r[k] += b * g;
                v.pos += inc;
            }
        }
        done += chunk;
    }

    bool any = false;
    float prog = 0.0f;
    for (auto& v : s.voices)
        if (v.active)
        {
            any = true;
            if (! v.releasing || prog == 0.0f) prog = (float) (v.pos / (double) len);
        }
    s.anyActive = any;
    s.playing.store (any, std::memory_order_relaxed);
    s.progress.store (any ? jlimit (0.0f, 1.0f, prog) : 0.0f, std::memory_order_relaxed);
}

void JingleTriggerProcessor::render (AudioBuffer<float>& buffer, int start, int num)
{
    float* outL = buffer.getWritePointer (0) + start;
    float* outR = (buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0)) + start;
    for (int i = 0; i < kNumSlots; ++i)
        if (slots[(size_t) i].anyActive)
            renderSlot (i, outL, outR, num);
}

void JingleTriggerProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // If the host feeds us audio (plugin used inline in a chain) pass it through and add on top;
    // otherwise start from silence.
    const bool hasInput = getBusCount (true) > 0 && getBus (true, 0)->isEnabled();
    if (! hasInput)
        buffer.clear();

    // ---- block-start events: parameter edges + GUI buttons (16 cheap atomic loads)
    const bool stopNow = stopAllP->load() >= 0.5f;
    if ((stopNow && ! prevStopAll) || stopAllRequest.exchange (false)) stopAll();
    prevStopAll = stopNow;

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        const bool trig = trigP[(size_t) i]->load() >= 0.5f;
        if (trig && ! s.prevTrig)       startSlot (i);
        else if (! trig && s.prevTrig)
        {
            const auto m = getMode (i);
            if (m == PlayMode::Gate || m == PlayMode::Loop) stopSlot (i);
        }
        s.prevTrig = trig;

        if (const int cmd = s.uiCmd.exchange (0))
        {
            if (cmd == 1) startSlot (i);
            else          stopSlot (i);
        }
    }

    // ---- sample-accurate MIDI + rendering. Idle = nothing below does any work.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = jlimit (0, n, meta.samplePosition);
        if (t > pos) { render (buffer, pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) render (buffer, pos, n - pos);
}

//==============================================================================
bool JingleTriggerProcessor::loadFile (int slot, const File& file)
{
    if (! isPositiveAndBelow (slot, kNumSlots)) return false;
    auto& s = slots[(size_t) slot];

    std::unique_ptr<AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0
         || reader->lengthInSamples > (int64) (reader->sampleRate * 600.0))   // refuse > 10 min
        return false;

    const int len = (int) reader->lengthInSamples;
    auto buf = std::make_unique<AudioBuffer<float>> (jmin (2, (int) reader->numChannels), len);
    reader->read (buf.get(), 0, len, 0, true, true);

    std::unique_ptr<AudioBuffer<float>> old;
    {
        const SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.data);
        s.data = std::move (buf);
        s.fileRate = reader->sampleRate;
        for (auto& v : s.voices) v = {};
        s.anyActive = false;
    }
    s.path = file.getFullPathName();
    s.missing = false;
    s.playing = false;
    s.progress = 0.0f;
    s.loaded = true;
    return true;      // 'old' is freed here, on the message thread
}

void JingleTriggerProcessor::clearSlot (int slot)
{
    if (! isPositiveAndBelow (slot, kNumSlots)) return;
    auto& s = slots[(size_t) slot];
    std::unique_ptr<AudioBuffer<float>> old;
    {
        const SpinLock::ScopedLockType sl (s.lock);
        old = std::move (s.data);
        for (auto& v : s.voices) v = {};
        s.anyActive = false;
    }
    s.path = {};
    s.missing = false;
    s.playing = false;
    s.progress = 0.0f;
    s.loaded = false;
}

//==============================================================================
void JingleTriggerProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        for (int i = 0; i < kNumSlots; ++i)
            xml->setAttribute ("file" + String (i), slots[(size_t) i].path);
        copyXmlToBinary (*xml, dest);
    }
}

void JingleTriggerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (! xml->hasTagName (apvts.state.getType())) return;
        apvts.replaceState (ValueTree::fromXml (*xml));

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto path = xml->getStringAttribute ("file" + String (i));
            if (path.isEmpty()) { clearSlot (i); continue; }

            const File f (path);
            if (! (f.existsAsFile() && loadFile (i, f)))
            {
                clearSlot (i);
                slots[(size_t) i].path = path;       // keep the path so it is not lost on re-save
                slots[(size_t) i].missing = true;
            }
        }
    }
}

AudioProcessorEditor* JingleTriggerProcessor::createEditor()
{
    return new JingleTriggerEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JingleTriggerProcessor();
}
