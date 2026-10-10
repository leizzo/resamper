#include "TestPluginFormat.h"

#include <cstdlib>

namespace resamper::test
{

namespace
{
    /** What "plugin <name>" creates: a gain that can be told to crash. */
    class TestPlugin final : public juce::AudioPluginInstance
    {
    public:
        explicit TestPlugin (const juce::PluginDescription& d)
            : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                          .withOutput ("Output", juce::AudioChannelSet::stereo())),
              desc (d)
        {
            juce::AudioProcessor::addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID ("gain", 1), "Gain", 0.0f, 1.0f, 0.5f));
            juce::AudioProcessor::addParameter (crash = new juce::AudioParameterBool (juce::ParameterID ("crash", 1), "Crash", false));
            setLatencySamples (TestPluginFormat::pluginLatency);
        }

        void fillInPluginDescription (juce::PluginDescription& d) const override   { d = desc; }
        const juce::String getName() const override                              { return desc.name; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            // Dies as a crashing plug-in does (without leaving a crash report).
            if (crash->get())
                std::_Exit (134);

            // A plug-in named "Slow ..." takes 2 ms a block: a cost the CPU readout must show.
            if (desc.name.startsWith ("Slow "))
                juce::Thread::sleep (2);

            buffer.applyGain (gain->get());
        }

        double getTailLengthSeconds() const override                 { return 0; }
        bool acceptsMidi() const override                            { return false; }
        bool producesMidi() const override                           { return false; }
        juce::AudioProcessorEditor* createEditor() override          { return new Editor (*this); }
        bool hasEditor() const override                              { return true; }
        int getNumPrograms() override                                { return 1; }
        int getCurrentProgram() override                             { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override             { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& dest) override
        {
            juce::MemoryOutputStream (dest, false).writeFloat (gain->get());
        }

        void setStateInformation (const void* data, int size) override
        {
            juce::MemoryInputStream in (data, (size_t) size, false);

            if (size >= (int) sizeof (float))
                *gain = in.readFloat();
        }

    private:
        /** Up drags Gain up, as a mouse drag in it does: dragSteps steps of dragStep, one
            every dragStepMs, each from the value Gain has then, in one gesture. */
        struct Editor final : juce::AudioProcessorEditor, private juce::Timer
        {
            explicit Editor (TestPlugin& p) : juce::AudioProcessorEditor (p), plugin (p)
            {
                setSize (TestPluginFormat::editorWidth, TestPluginFormat::editorHeight);
            }

            ~Editor() override   { stopTimer(); }

            void paint (juce::Graphics& g) override   { g.fillAll (juce::Colours::darkorange); }

            bool keyPressed (const juce::KeyPress& key) override
            {
                if (key != juce::KeyPress::upKey)
                    return false;

                if (stepsLeft == 0)
                {
                    plugin.gain->beginChangeGesture();
                    stepsLeft = TestPluginFormat::dragSteps;
                    startTimer (TestPluginFormat::dragStepMs);
                }

                return true;
            }

            void timerCallback() override
            {
                auto& parameter = *plugin.gain;
                parameter = parameter.get() + TestPluginFormat::dragStep;

                if (--stepsLeft == 0)
                {
                    stopTimer();
                    parameter.endChangeGesture();
                }
            }

            TestPlugin& plugin;
            int stepsLeft = 0;
        };

        juce::PluginDescription desc;
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterBool* crash = nullptr;
    };

    /** The name after a creatable plug-in's keyword; empty if the text isn't one. */
    juce::String creatableName (const juce::String& text)
    {
        for (auto keyword : { "plugin ", "sandboxcrash " })
            if (text.startsWith (keyword))
                return text.fromFirstOccurrenceOf (keyword, false, false);

        return {};
    }
}

TestPluginFormat::TestPluginFormat (juce::File f) : folder (std::move (f)) {}

juce::File TestPluginFormat::scanFolder()
{
    static const auto folder = []
    {
        auto dir = juce::File::createTempFile ("resamper-test-plugins");
        dir.createDirectory();
        return dir;
    }();

    return folder;
}

void TestPluginFormat::registerWith (juce::AudioPluginFormatManager& manager)
{
    for (auto* format : manager.getFormats())
        if (format->getName() == formatName)
            return;

    manager.addFormat (std::make_unique<TestPluginFormat> (scanFolder()));
}

void TestPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier)
{
    const juce::File file (fileOrIdentifier);
    const auto text = file.loadFileAsString().trim();

    // Dies as a plug-in crashing the scanner does (without leaving a crash report).
    if (text == "crash")
        std::_Exit (134);

    if (text == "hang")
        for (;;)
            juce::Thread::sleep (100);

    const auto name = text.startsWith ("ok ") ? text.fromFirstOccurrenceOf ("ok ", false, false) : creatableName (text);

    if (name.isEmpty())
        return;

    auto desc = std::make_unique<juce::PluginDescription>();
    desc->name = desc->descriptiveName = name;
    desc->pluginFormatName = formatName;
    desc->manufacturerName = "Resamper Tests";
    desc->category = "Effect";
    desc->fileOrIdentifier = file.getFullPathName();
    desc->uniqueId = desc->deprecatedUid = file.getFileName().hashCode();
    desc->lastFileModTime = file.getLastModificationTime();
    desc->numInputChannels = desc->numOutputChannels = 2;
    results.add (desc.release());
}

bool TestPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.endsWithIgnoreCase (fileExtension);
}

juce::String TestPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool TestPluginFormat::doesPluginStillExist (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).existsAsFile();
}

juce::StringArray TestPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool)
{
    juce::StringArray found;

    for (int i = 0; i < paths.getNumPaths(); ++i)
        for (const auto& entry : juce::RangedDirectoryIterator (paths.getRawString (i), recursive,
                                                                juce::String ("*") + fileExtension))
            found.add (entry.getFile().getFullPathName());

    found.sort (true);
    return found;
}

void TestPluginFormat::createPluginInstance (const juce::PluginDescription& desc, double, int, PluginCreationCallback callback)
{
    const auto text = juce::File (desc.fileOrIdentifier).loadFileAsString().trim();

    if (creatableName (text).isEmpty())
    {
        callback (nullptr, "Test plug-ins can't be created");
        return;
    }

    if (inSandboxHost && text.startsWith ("sandboxcrash "))
        std::_Exit (134);

    // A plug-in named "Sluggish ..." takes a while to load in its sandbox: the app mustn't wait for it.
    if (inSandboxHost && desc.name.startsWith ("Sluggish "))
        juce::Thread::sleep (sluggishLoadMs);

    callback (std::make_unique<TestPlugin> (desc), {});
}

} // namespace resamper::test
