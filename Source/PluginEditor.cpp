#include "PluginEditor.h"

using namespace juce;

//==============================================================================
class SlotRow : public Component, public FileDragAndDropTarget
{
public:
    SlotRow (JingleTriggerProcessor& p, int idx) : proc (p), index (idx)
    {
        const auto n = String (idx + 1);

        addAndMakeVisible (noteLabel);
        noteLabel.setJustificationType (Justification::centredLeft);

        addAndMakeVisible (fileButton);
        fileButton.onClick = [this] { chooseFile(); };

        addAndMakeVisible (playButton);
        playButton.onClick = [this]
        {
            if (proc.isSlotPlaying (index)) proc.requestStop (index);
            else                            proc.requestTrigger (index);
        };

        addAndMakeVisible (clearButton);
        clearButton.setButtonText (String::fromUTF8 ("\xc3\x97"));
        clearButton.onClick = [this] { proc.clearSlot (index); refresh (true); };

        addAndMakeVisible (modeBox);
        modeBox.addItemList ({ "One-shot", "Gate", "Loop", "Toggle" }, 1);
        modeAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode" + n, modeBox);

        addAndMakeVisible (gainSlider);
        gainSlider.setSliderStyle (Slider::LinearHorizontal);
        gainSlider.setTextBoxStyle (Slider::TextBoxRight, false, 56, 20);
        gainSlider.setTextValueSuffix (" dB");
        gainAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "gain" + n, gainSlider);

        refresh (true);
    }

    void refresh (bool force)
    {
        const int base = (int) *proc.apvts.getRawParameterValue ("basenote");
        const bool pl = proc.isSlotPlaying (index);
        const float pr = proc.getSlotProgress (index);
        const bool loaded = proc.isSlotLoaded (index);
        const bool missing = proc.isSlotMissing (index);

        if (force || base != lastBase)
        {
            lastBase = base;
            noteLabel.setText (String (index + 1) + "  " + MidiMessage::getMidiNoteName (base + index, true, true, 3)
                               + " (" + String (base + index) + ")", dontSendNotification);
        }
        if (force || pl != lastPlaying || loaded != lastLoaded || missing != lastMissing)
        {
            lastPlaying = pl; lastLoaded = loaded; lastMissing = missing;
            playButton.setButtonText (pl ? "Stop" : "Play");
            playButton.setEnabled (loaded);
            clearButton.setEnabled (loaded || missing);
            const auto path = proc.getSlotPath (index);
            fileButton.setButtonText (path.isEmpty() ? "Click or drop an audio file..."
                                                     : (missing ? "MISSING: " : "") + File (path).getFileName());
            fileButton.setColour (TextButton::textColourOffId, missing ? Colours::orangered : Colours::white);
            repaint();
        }
        if (force || std::abs (pr - lastProgress) > 0.002f)
        {
            lastProgress = pr;
            repaint (getLocalBounds().removeFromBottom (6));
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds();
        g.setColour (Colour (0xff2a2d33));
        g.fillRoundedRectangle (b.reduced (2, 1).toFloat(), 4.0f);
        if (lastPlaying)
        {
            g.setColour (Colour (0xff3fb27f));
            g.fillRect (Rectangle<float> (6.0f, (float) getHeight() - 6.0f,
                                          jmax (0.0f, (getWidth() - 12.0f) * lastProgress), 3.0f));
        }
        if (dragOver)
        {
            g.setColour (Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (b.reduced (2, 1).toFloat(), 4.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        r.removeFromBottom (4);
        noteLabel.setBounds (r.removeFromLeft (110));
        clearButton.setBounds (r.removeFromRight (28));
        r.removeFromRight (4);
        gainSlider.setBounds (r.removeFromRight (190));
        r.removeFromRight (4);
        modeBox.setBounds (r.removeFromRight (90));
        r.removeFromRight (4);
        playButton.setBounds (r.removeFromLeft (56));
        r.removeFromLeft (4);
        fileButton.setBounds (r);
    }

    bool isInterestedInFileDrag (const StringArray&) override { return true; }
    void fileDragEnter (const StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const StringArray& files, int, int) override
    {
        dragOver = false;
        if (files.size() > 0) proc.loadFile (index, File (files[0]));
        refresh (true);
    }

private:
    void chooseFile()
    {
        chooser = std::make_unique<FileChooser> ("Select an audio file", File(),
                                                 proc.getFormatManager().getWildcardForAllFormats());
        Component::SafePointer<SlotRow> safe (this);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [safe] (const FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile()) safe->proc.loadFile (safe->index, f);
            safe->refresh (true);
        });
    }

    JingleTriggerProcessor& proc;
    int index;
    Label noteLabel;
    TextButton fileButton, playButton, clearButton;
    ComboBox modeBox;
    Slider gainSlider;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> gainAtt;
    std::unique_ptr<FileChooser> chooser;

    int lastBase = -1;
    bool lastPlaying = false, lastLoaded = false, lastMissing = false, dragOver = false;
    float lastProgress = 0.0f;
};

//==============================================================================
JingleTriggerEditor::JingleTriggerEditor (JingleTriggerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    masterLabel.setText ("Master", dontSendNotification);
    baseLabel.setText ("Base note", dontSendNotification);
    addAndMakeVisible (masterLabel);
    addAndMakeVisible (baseLabel);

    for (auto* s : { &masterSlider, &baseSlider })
    {
        addAndMakeVisible (s);
        s->setSliderStyle (Slider::LinearHorizontal);
        s->setTextBoxStyle (Slider::TextBoxRight, false, 56, 20);
    }
    masterSlider.setTextValueSuffix (" dB");
    masterAtt = std::make_unique<SA> (proc.apvts, "master", masterSlider);
    baseAtt   = std::make_unique<SA> (proc.apvts, "basenote", baseSlider);

    addAndMakeVisible (stopAllButton);
    stopAllButton.onClick = [this] { proc.requestStopAll(); };

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    for (int i = 0; i < kNumSlots; ++i)
    {
        rows.push_back (std::make_unique<SlotRow> (proc, i));
        rowHolder.addAndMakeVisible (*rows.back());
    }

    setResizable (true, true);
    setResizeLimits (620, 300, 1100, 900);
    setSize (760, 520);
    startTimerHz (15);          // only runs while the window is open
}

JingleTriggerEditor::~JingleTriggerEditor() { stopTimer(); }

void JingleTriggerEditor::timerCallback()
{
    for (auto& r : rows) r->refresh (false);
}

void JingleTriggerEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1c1e22));
}

void JingleTriggerEditor::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto top = r.removeFromTop (28);
    stopAllButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (10);
    baseSlider.setBounds (top.removeFromRight (200));
    baseLabel.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    masterLabel.setBounds (top.removeFromLeft (50));
    masterSlider.setBounds (top.removeFromLeft (220));

    r.removeFromTop (6);
    viewport.setBounds (r);

    const int rowH = 40;
    const int w = viewport.getMaximumVisibleWidth();
    rowHolder.setSize (w, rowH * kNumSlots);
    for (int i = 0; i < kNumSlots; ++i)
        rows[(size_t) i]->setBounds (0, i * rowH, w, rowH);
}
