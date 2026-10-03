#include "PluginSandbox.h"
#include "SandboxDock.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#if JUCE_MAC || JUCE_LINUX
 #include <csignal>
 #include <fcntl.h>
 #include <semaphore.h>
 #include <sys/wait.h>
 #include <unistd.h>
 #define RESAMPER_SANDBOX 1
#else
 #define RESAMPER_SANDBOX 0
#endif

namespace resamper
{

namespace
{
    /** The memory both sides of a sandbox map: one audio block in flight, the
        parameter values the stand-in sets, and the block's MIDI. The stand-in
        fills a block in, then publishes requestSeq; the host processes it in
        place and answers with responseSeq. */
    struct SharedBlock
    {
        static constexpr int maxChannels = 32, maxSamples = 2048, maxParameters = 4096, midiBytes = 32768;

        std::atomic<juce::uint32> requestSeq, responseSeq, parameterSeq;
        std::atomic<float> hostLoad;   ///< the host's own time per block as a share of the block's length, smoothed
        juce::int32 numSamples, numChannels, midiInBytes, midiOutBytes;
        std::atomic<juce::uint32> parameterStamps[maxParameters];
        std::atomic<float> parameterValues[maxParameters];
        float audio[maxChannels][maxSamples];
        juce::uint8 midiIn[midiBytes], midiOut[midiBytes];
    };

    static_assert (std::atomic<juce::uint32>::is_always_lock_free && std::atomic<float>::is_always_lock_free,
                   "the shared block's atomics must work across processes");

    /** Message types and properties of the pipe between stand-in and host. */
    namespace msg
    {
        const juce::Identifier load ("load"), prepare ("prepare"), release ("release"), getState ("getState"),
                               setState ("setState"), program ("program"), openEditor ("openEditor"), dock ("dock"),
                               editorSize ("editorSize"), closeEditor ("closeEditor"), dockState ("dockState"),
                               clicked ("clicked"), key ("key"), pressKey ("pressKey"), reply ("reply"), hello ("hello"), parameters ("parameters"),
                               parameter ("p"), gesture ("gesture"), latency ("latency"), programName ("programName");

        const juce::Identifier id ("id"), error ("error"), description ("description"), shared ("shared"),
                               semaphore ("semaphore"), rate ("rate"), block ("block"), data ("data"), index ("i"),
                               value ("v"), text ("t"), fromPlugin ("plugin"), starting ("starting"), samples ("samples"),
                               name ("name"), label ("label"), defaultValue ("default"), steps ("steps"),
                               discrete ("discrete"), boolean ("boolean"), automatable ("automatable"),
                               parameterId ("parameterId"), inputs ("inputs"), outputs ("outputs"),
                               acceptsMidi ("acceptsMidi"), producesMidi ("producesMidi"), midiEffect ("midiEffect"),
                               tail ("tail"), current ("current"), pid ("pid"), hasEditor ("hasEditor"), resizable ("resizable"),
                               x ("x"), y ("y"), width ("w"), height ("h"), visible ("visible"), window ("window"),
                               level ("level"), scale ("scale"), code ("code"), modifiers ("modifiers"),
                               character ("character");
    }

    juce::MemoryBlock encode (const juce::ValueTree& message)
    {
        juce::MemoryOutputStream out;
        message.writeToStream (out);
        return out.getMemoryBlock();
    }

    juce::ValueTree decode (const juce::MemoryBlock& data)
    {
        return juce::ValueTree::readFromData (data.getData(), data.getSize());
    }

    juce::ValueTree messageFor (const juce::Identifier& type, const juce::KeyPress& key)
    {
        juce::ValueTree message (type);
        message.setProperty (msg::code, key.getKeyCode(), nullptr);
        message.setProperty (msg::modifiers, key.getModifiers().getRawFlags(), nullptr);
        message.setProperty (msg::character, (int) key.getTextCharacter(), nullptr);
        return message;
    }

    juce::KeyPress keyIn (const juce::ValueTree& message)
    {
        return { (int) message[msg::code], juce::ModifierKeys ((int) message[msg::modifiers]),
                 (juce::juce_wchar) (int) message[msg::character] };
    }

    juce::MemoryBlock binaryOf (const juce::var& value)
    {
        if (auto* block = value.getBinaryData())
            return *block;

        return {};
    }

    /** Writes the events of [start, start + length) to dest, positions made relative to start. Returns the bytes used. */
    int writeMidi (const juce::MidiBuffer& midi, int start, int length, juce::uint8* dest, int capacity)
    {
        int used = 0;

        for (const auto meta : midi)
        {
            // Its size goes in 16 bits: a longer event (a huge SysEx) can't cross.
            if (meta.samplePosition < start || meta.samplePosition >= start + length
                || meta.numBytes > std::numeric_limits<juce::uint16>::max())
                continue;

            const auto needed = (int) (sizeof (juce::int32) + sizeof (juce::uint16)) + meta.numBytes;

            if (used + needed > capacity)
                break;

            const auto position = (juce::int32) (meta.samplePosition - start);
            const auto size = (juce::uint16) meta.numBytes;
            std::memcpy (dest + used, &position, sizeof (position));
            std::memcpy (dest + used + sizeof (position), &size, sizeof (size));
            std::memcpy (dest + used + sizeof (position) + sizeof (size), meta.data, (size_t) meta.numBytes);
            used += needed;
        }

        return used;
    }

    /** Adds the events writeMidi wrote to out, offset by start. */
    void readMidi (const juce::uint8* source, int bytes, juce::MidiBuffer& out, int start)
    {
        constexpr auto header = (int) (sizeof (juce::int32) + sizeof (juce::uint16));
        int read = 0;

        while (read + header <= bytes)
        {
            juce::int32 position;
            juce::uint16 size;
            std::memcpy (&position, source + read, sizeof (position));
            std::memcpy (&size, source + read + sizeof (position), sizeof (size));

            if (read + header + size > bytes)
                break;

            out.addEvent (source + read + header, size, start + position);
            read += header + size;
        }
    }

   #if RESAMPER_SANDBOX
    /** A named POSIX semaphore: the stand-in posts it to wake the host's audio thread. */
    struct Semaphore
    {
        Semaphore() = default;
        ~Semaphore()   { if (handle != SEM_FAILED) sem_close (handle); }

        /** A name short enough for macOS (31 characters at most). */
        static juce::String createName()   { return "/rsmp" + juce::String::toHexString (juce::Random().nextInt()); }

        bool create (const juce::String& name)
        {
            handle = sem_open (name.toRawUTF8(), O_CREAT | O_EXCL, 0600, 0);
            return handle != SEM_FAILED;
        }

        bool open (const juce::String& name)
        {
            handle = sem_open (name.toRawUTF8(), 0);
            return handle != SEM_FAILED;
        }

        void post()   { sem_post (handle); }
        bool wait()   { return sem_wait (handle) == 0; }

        sem_t* handle = SEM_FAILED;
        JUCE_DECLARE_NON_COPYABLE (Semaphore)
    };
   #endif

    juce::File executable()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    }

    constexpr int stateTimeoutMs = 3000, prepareTimeoutMs = 5000, offlineDeadlineMs = 5000, reportMs = 100,
                  quitGraceMs = 2000, hostSetGraceMs = 300, dispatchMs = 50, maxLoaders = 8;

    /** Audio blocks the device has finished (PluginSandbox::audioBlockFinished). */
    std::atomic<juce::uint32> blocksFinished { 0 };

    /** When the host's answer to a block is due, on the audio thread: 75 % of the
        block's time from the first sandboxed plug-in this thread runs in the block.
        The plug-ins after it in a chain share what is left, so a chain waits for a
        block's time at most, not that much for each of its plug-ins (#135). A new
        block starts with the device's next one, or when a plug-in comes round again
        (each runs once a block), or once a block's time has passed. lastBlock is
        the caller's: the block it last waited in. */
    juce::int64 answerDeadline (int numSamples, double sampleRate, juce::uint64& lastBlock) noexcept
    {
        // Per thread: a chain runs in order on one thread, and that thread's time is what the block allows.
        // (A thread's first touch of its thread_locals may set them up, once.)
        thread_local juce::int64 started = 0;
        thread_local juce::uint32 deviceBlock = 0;
        thread_local juce::uint64 block = 0;

        const auto now = juce::Time::getHighResolutionTicks();
        const auto length = juce::Time::secondsToHighResolutionTicks (numSamples / sampleRate);
        const auto finished = blocksFinished.load (std::memory_order_relaxed);

        if (finished != deviceBlock || lastBlock == block || now - started >= length)
        {
            deviceBlock = finished;
            started = now;
            ++block;
        }

        lastBlock = block;
        return started + length * 3 / 4;
    }

    /** How long a stand-in spins for the host's answer before it polls it in short sleeps
        instead: most hosts answer a light plug-in's block within this. */
    constexpr double spinSeconds = 0.0002, pollSeconds = 0.00005;
}

#if RESAMPER_SANDBOX
namespace
{
/** The stand-in's end of one sandbox host: the process, the pipe, the shared block. */
class Remote final : public juce::ChildProcessCoordinator
{
public:
    Remote() = default;

    ~Remote() override
    {
        closing.store (true);
        killWorkerProcess();
        releaseNames();

        // Nothing of the host is worth waiting for: its plug-in's state lives in the Edit.
        if (const auto id = pid.load(); id > 0)
        {
            ::kill (id, SIGKILL);

            for (int i = 0; i < 50 && ::waitpid (id, nullptr, WNOHANG) == 0; ++i)
                juce::Thread::sleep (2);
        }
    }

    /** Creates the shared block and the semaphore. */
    bool createShared()
    {
        sharedFile = juce::File::createTempFile (".resampersandbox");
        juce::MemoryBlock zeros (sizeof (SharedBlock), true);

        if (! sharedFile.replaceWithData (zeros.getData(), zeros.getSize()))
            return false;

        mapped = std::make_unique<juce::MemoryMappedFile> (sharedFile, juce::MemoryMappedFile::readWrite);

        if (mapped->getData() == nullptr || mapped->getSize() < sizeof (SharedBlock))
            return false;

        shared = new (mapped->getData()) SharedBlock();
        semaphoreName = Semaphore::createName();
        return wake.create (semaphoreName);
    }

    /** Once the host has opened them, nobody else needs their names. */
    void releaseNames()
    {
        if (semaphoreName.isNotEmpty())
            sem_unlink (semaphoreName.toRawUTF8());

        semaphoreName.clear();
        sharedFile.deleteFile();
    }

    juce::File getSharedFile() const          { return sharedFile; }
    const juce::String& getSemaphoreName() const   { return semaphoreName; }
    SharedBlock& block() const noexcept       { return *shared; }
    void wakeHost()                           { wake.post(); }

    /** Sends a message and waits for its reply; invalid on timeout, or if the host died. */
    juce::ValueTree request (juce::ValueTree message, int timeoutMs)
    {
        if (dead.load())
            return {};

        auto waiter = std::make_shared<Waiter>();
        const auto id = nextId.fetch_add (1) + 1;

        {
            const std::scoped_lock lock (waitersLock);

            // Gone since the check above: nothing would wake this waiter.
            if (dead.load())
                return {};

            waiters[id] = waiter;
        }

        message.setProperty (msg::id, id, nullptr);
        const auto sent = sendMessageToWorker (encode (message));
        const auto answered = sent && waiter->done.wait (timeoutMs);

        const std::scoped_lock lock (waitersLock);
        waiters.erase (id);
        return answered ? waiter->reply : juce::ValueTree();
    }

    void post (const juce::ValueTree& message)
    {
        if (! dead.load())
            sendMessageToWorker (encode (message));
    }

    /** Messages that answer no request, on the pipe's thread; set once its receiver exists.
        A host that died before then is reported to onDied at once. */
    void setReceiver (std::function<void (const juce::ValueTree&)> onMessage, std::function<void()> onDied)
    {
        const std::scoped_lock lock (receiverLock);
        receiveMessage = std::move (onMessage);
        hostDied = std::move (onDied);

        if (dead.load() && ! closing.load())
            reportDeath();
    }

    /** Gives the host up, from any thread: a request waiting for it returns at once, and none is sent again. */
    void cancel()
    {
        closing.store (true);
        dead.store (true);

        const std::scoped_lock lock (waitersLock);

        for (auto& [id, waiter] : waiters)
            waiter->done.signal();
    }

    std::atomic<bool> dead { false };

private:
    struct Waiter
    {
        juce::WaitableEvent done;
        juce::ValueTree reply;
    };

    juce::File sharedFile;
    std::unique_ptr<juce::MemoryMappedFile> mapped;
    SharedBlock* shared = nullptr;
    Semaphore wake;
    juce::String semaphoreName;

    std::atomic<int> nextId { 0 }, pid { 0 };
    std::atomic<bool> closing { false };
    std::mutex waitersLock, receiverLock;
    std::map<int, std::shared_ptr<Waiter>> waiters;
    std::function<void (const juce::ValueTree&)> receiveMessage;
    std::function<void()> hostDied;
    bool deathReported = false;

    void handleMessageFromWorker (const juce::MemoryBlock& data) override
    {
        const auto message = decode (data);

        if (message.hasType (msg::hello))
        {
            pid.store ((int) message[msg::pid]);
            return;
        }

        if (message.hasType (msg::reply))
        {
            const std::scoped_lock lock (waitersLock);

            if (auto found = waiters.find ((int) message[msg::id]); found != waiters.end())
            {
                found->second->reply = message;
                found->second->done.signal();
            }

            return;
        }

        const std::scoped_lock lock (receiverLock);

        if (receiveMessage)
            receiveMessage (message);
    }

    void handleConnectionLost() override
    {
        dead.store (true);

        {
            const std::scoped_lock lock (waitersLock);

            for (auto& [id, waiter] : waiters)
                waiter->done.signal();
        }

        if (closing.load())
            return;

        const std::scoped_lock lock (receiverLock);
        reportDeath();
    }

    /** Once only, under receiverLock. */
    void reportDeath()
    {
        if (hostDied && ! std::exchange (deathReported, true))
            hostDied();
    }

    JUCE_DECLARE_NON_COPYABLE (Remote)
};

/** On a loader thread: starts the host and has it load the plug-in request
    describes. The host's answer, or invalid with error set. */
juce::ValueTree loadInHost (Remote& remote, juce::ValueTree request, const juce::String& name, juce::String& error)
{
    if (! remote.createShared())
    {
        error = "The sandbox couldn't share memory with its host";
        return {};
    }

    // No output streams: an unread pipe fills up and stalls the host.
    if (! remote.launchWorkerProcess (executable(), PluginSandbox::hostId, 0, 0))
    {
        error = "The plug-in's sandbox didn't start";
        return {};
    }

    request.setProperty (msg::shared, remote.getSharedFile().getFullPathName(), nullptr);
    request.setProperty (msg::semaphore, remote.getSemaphoreName(), nullptr);

    const auto loaded = remote.request (request, PluginSandbox::loadTimeoutMs);
    remote.releaseNames();

    if (! loaded.isValid())
    {
        error = remote.dead.load() ? name + " crashed while loading in its sandbox"
                                   : name + " didn't load in its sandbox within "
                                         + juce::String (PluginSandbox::loadTimeoutMs / 1000) + " s";
        return {};
    }

    if (loaded.hasProperty (msg::error))
    {
        error = loaded[msg::error].toString();
        return {};
    }

    return loaded;
}
} // namespace

/** A plug-in loading into its sandbox host, or loaded there and not yet taken. On the message thread. */
struct PluginSandbox::Load
{
    juce::String identifier;
    std::shared_ptr<Remote> remote;
    std::function<void()> onDone;
    bool done = false;
    juce::ValueTree loaded;
    juce::String error;
};

//==============================================================================
/** A sandboxed plug-in's stand-in: what the engine holds in its place. */
class PluginSandbox::Instance final : public juce::AudioPluginInstance
{
public:
    Instance (PluginSandbox& owner, const juce::PluginDescription& d, const juce::String& id,
              std::shared_ptr<Remote> r, const juce::ValueTree& loaded)
        : juce::AudioPluginInstance (busesFor (loaded)),
          sandbox (&owner), desc (d), pluginId (id), remote (std::move (r)),
          numIns ((int) loaded[msg::inputs]), numOuts ((int) loaded[msg::outputs]),
          midiIn ((bool) loaded[msg::acceptsMidi]), midiOut ((bool) loaded[msg::producesMidi]),
          midiEffect ((bool) loaded[msg::midiEffect]), ownEditor ((bool) loaded[msg::hasEditor]),
          tailSeconds ((double) loaded[msg::tail])
    {
        int index = 0;

        for (const auto& child : loaded)
        {
            if (child.hasType (msg::parameter) && index < SharedBlock::maxParameters)
            {
                auto parameter = std::make_unique<Parameter> (*this, index++, child);
                parameters.push_back (parameter.get());
                addHostedParameter (std::move (parameter));
            }
            else if (child.hasType (msg::programName))
            {
                programNames.add (child[msg::name].toString());
            }
        }

        currentProgram = (int) loaded[msg::current];
        setLatencySamples ((int) loaded[msg::latency]);

        // Created here, on the message thread, so later copies from other threads are only reference counts.
        self = this;

        remote->setReceiver ([this] (const juce::ValueTree& m) { received (m); }, [this] { hostDied(); });
    }

    ~Instance() override
    {
        remote->setReceiver ({}, {});
        remote.reset();
        masterReference.clear();
    }

    bool hasCrashed() const noexcept   { return remote->dead.load(); }

    /** The share of a block's time the host spends in the plug-in (not the round trip). */
    double getHostCpuLoad() const noexcept   { return (double) remote->block().hostLoad.load (std::memory_order_relaxed); }

    /** Tells the host something about the plug-in's own UI. */
    void post (const juce::ValueTree& message)   { remote->post (message); }

    /** Has the host's UI handle a key, as if typed in it; waits for it to. */
    void pressKeyInOwnEditor (const juce::KeyPress& key)
    {
        remote->request (messageFor (msg::pressKey, key), stateTimeoutMs);
    }

    /** Where the host shows the plug-in's own UI; empty while hidden, or if the host doesn't answer. */
    juce::Rectangle<int> getOwnEditorScreenBounds()
    {
        const auto state = remote->request (juce::ValueTree (msg::dockState), stateTimeoutMs);
        return { (int) state[msg::x], (int) state[msg::y], (int) state[msg::width], (int) state[msg::height] };
    }

    //==============================================================================
    void fillInPluginDescription (juce::PluginDescription& d) const override   { d = desc; }
    const juce::String getName() const override                              { return desc.name; }

    void prepareToPlay (double rate, int block) override
    {
        juce::ValueTree message (msg::prepare);
        message.setProperty (msg::rate, rate, nullptr);
        message.setProperty (msg::block, block, nullptr);
        prepared.store (remote->request (message, prepareTimeoutMs).isValid());
        outMidi.ensureSize (SharedBlock::midiBytes);
    }

    void releaseResources() override
    {
        prepared.store (false);
        remote->post (juce::ValueTree (msg::release));
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        outMidi.clear();

        for (int start = 0; start < buffer.getNumSamples(); start += SharedBlock::maxSamples)
        {
            const auto length = juce::jmin (SharedBlock::maxSamples, buffer.getNumSamples() - start);

            if (! processInHost (buffer, midi, start, length))
                bypass (buffer, midi, start, length);
        }

        // Copied, not swapped: outMidi keeps the room prepareToPlay made, so the audio thread doesn't allocate.
        midi.clear();
        midi.addEvents (outMidi, 0, -1, 0);
    }

    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        // The plug-in in the host keeps the layout it loaded with.
        return layout.getMainInputChannels() == numIns && layout.getMainOutputChannels() == numOuts;
    }

    double getTailLengthSeconds() const override   { return tailSeconds; }
    bool acceptsMidi() const override              { return midiIn; }
    bool producesMidi() const override             { return midiOut; }
    bool isMidiEffect() const override             { return midiEffect; }

    bool hasEditor() const override                { return ownEditor; }
    juce::AudioProcessorEditor* createEditor() override;

    /** The plug-in resized its own UI: so does the editor. */
    void ownEditorResized (juce::Point<int> size);

    /** A key typed in the plug-in's own UI that it didn't use: the editor passes it on. */
    void ownEditorKey (const juce::KeyPress&);

    int getNumPrograms() override                  { return juce::jmax (1, programNames.size()); }
    int getCurrentProgram() override               { return currentProgram; }
    const juce::String getProgramName (int index) override   { return programNames[index]; }
    void changeProgramName (int index, const juce::String& newName) override
    {
        if (! juce::isPositiveAndBelow (index, programNames.size()))
            return;

        programNames.set (index, newName);
        juce::ValueTree message (msg::programName);
        message.setProperty (msg::index, index, nullptr);
        message.setProperty (msg::name, newName, nullptr);
        remote->post (message);
    }

    void setCurrentProgram (int index) override
    {
        currentProgram = index;
        juce::ValueTree message (msg::program);
        message.setProperty (msg::index, index, nullptr);
        remote->post (message);
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        // A host that died keeps its last state: what Reload starts again from.
        if (auto reply = remote->request (juce::ValueTree (msg::getState), stateTimeoutMs); reply.isValid())
        {
            const std::scoped_lock lock (stateLock);
            lastState = binaryOf (reply[msg::data]);
        }

        const std::scoped_lock lock (stateLock);
        dest = lastState;
    }

    void setStateInformation (const void* data, int size) override
    {
        size = juce::jmax (0, size);

        {
            const std::scoped_lock lock (stateLock);
            lastState = juce::MemoryBlock (data, (size_t) size);
        }

        juce::ValueTree message (msg::setState);
        message.setProperty (msg::data, juce::MemoryBlock (data, (size_t) size), nullptr);

        // The reply carries every parameter as the new state left it.
        if (auto reply = remote->request (message, stateTimeoutMs); reply.isValid())
            for (const auto& p : reply)
                applyFromHost (p);
    }

private:
    /** One of the plug-in's parameters, mirrored. */
    class Parameter final : public juce::HostedAudioProcessorParameter
    {
    public:
        Parameter (Instance& o, int i, const juce::ValueTree& info)
            : owner (o), index (i),
              name (info[msg::name].toString()), label (info[msg::label].toString()), id (info[msg::parameterId].toString()),
              defaultValue ((float) info[msg::defaultValue]), steps ((int) info[msg::steps]),
              discrete ((bool) info[msg::discrete]), boolean ((bool) info[msg::boolean]),
              automatable ((bool) info[msg::automatable]),
              value ((float) info[msg::value]), text (info[msg::text].toString()), textValue ((float) info[msg::value])
        {
        }

        float getValue() const override   { return value.load(); }

        void setValue (float newValue) override
        {
            value.store (newValue);
            lastHostSet.store (juce::Time::getMillisecondCounter());
            auto& block = owner.remote->block();
            block.parameterValues[index].store (newValue, std::memory_order_relaxed);
            block.parameterStamps[index].fetch_add (1, std::memory_order_release);
            block.parameterSeq.fetch_add (1, std::memory_order_release);
        }

        float getDefaultValue() const override                 { return defaultValue; }
        juce::String getName (int maximumLength) const override { return name.substring (0, maximumLength); }
        juce::String getLabel() const override                 { return label; }
        int getNumSteps() const override                       { return steps; }
        bool isDiscrete() const override                       { return discrete; }
        bool isBoolean() const override                        { return boolean; }
        bool isAutomatable() const override                    { return automatable; }
        juce::String getParameterID() const override           { return id; }

        /** The plug-in's own text for the value it last reported; a number for any other. */
        juce::String getText (float v, int maximumLength) const override
        {
            const juce::SpinLock::ScopedLockType lock (textLock);
            return (juce::approximatelyEqual (v, textValue) ? text : juce::String (v, 2)).substring (0, maximumLength);
        }

        float getValueForText (const juce::String& t) const override   { return juce::jlimit (0.0f, 1.0f, t.getFloatValue()); }

        void setText (float v, const juce::String& t)
        {
            const juce::SpinLock::ScopedLockType lock (textLock);
            textValue = v;
            text = t;
        }

        /** Whether the host set this value lately: a report of an older value from the plug-in is stale then. */
        bool setByHostLately() const
        {
            return juce::Time::getMillisecondCounter() - lastHostSet.load() < (juce::uint32) hostSetGraceMs;
        }

    private:
        Instance& owner;
        const int index;
        const juce::String name, label, id;
        const float defaultValue;
        const int steps;
        const bool discrete, boolean, automatable;
        std::atomic<float> value;
        std::atomic<juce::uint32> lastHostSet { 0 };
        juce::SpinLock textLock;
        juce::String text;
        float textValue;
    };

    class Editor;

    juce::WeakReference<PluginSandbox> sandbox;
    const juce::PluginDescription desc;
    const juce::String pluginId;
    std::shared_ptr<Remote> remote;
    const int numIns, numOuts;
    const bool midiIn, midiOut, midiEffect, ownEditor;
    const double tailSeconds;
    std::vector<Parameter*> parameters;
    juce::StringArray programNames;
    int currentProgram = 0;
    std::atomic<bool> prepared { false };
    juce::MidiBuffer outMidi;
    juce::uint64 lastWaitBlock = 0;   ///< on the audio thread: the block it last waited for its host in (answerDeadline)
    std::mutex stateLock;
    juce::MemoryBlock lastState;
    juce::WeakReference<Instance> self;

    static BusesProperties busesFor (const juce::ValueTree& loaded)
    {
        BusesProperties buses;
        const int ins = loaded[msg::inputs], outs = loaded[msg::outputs];

        if (ins > 0)
            buses.addBus (true, "Input", juce::AudioChannelSet::canonicalChannelSet (ins), true);

        if (outs > 0)
            buses.addBus (false, "Output", juce::AudioChannelSet::canonicalChannelSet (outs), true);

        return buses;
    }

    /** One block through the host. False (the caller bypasses it) when the host is dead, busy or late. */
    bool processInHost (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int length)
    {
        auto& block = remote->block();
        const auto previous = block.requestSeq.load (std::memory_order_relaxed);

        // A host still on an earlier block (late, hung or dead) can't take this one.
        if (remote->dead.load() || ! prepared.load() || block.responseSeq.load (std::memory_order_acquire) != previous)
            return false;

        const auto channels = juce::jmin (buffer.getNumChannels(), SharedBlock::maxChannels);

        for (int c = 0; c < channels; ++c)
            std::memcpy (block.audio[c], buffer.getReadPointer (c, start), sizeof (float) * (size_t) length);

        block.numSamples = length;
        block.numChannels = channels;
        block.midiInBytes = writeMidi (midi, start, length, block.midiIn, SharedBlock::midiBytes);
        block.midiOutBytes = 0;

        const auto request = previous + 1;
        block.requestSeq.store (request, std::memory_order_release);
        remote->wakeHost();

        // Realtime, the host gets most of the block's time (shared along a chain);
        // offline (a render), as long as it needs.
        const auto rate = getSampleRate() > 0 ? getSampleRate() : 44100.0;   // nosemgrep: no-hardcoded-sample-rate -- only sizes the wait before the host reports a rate; no DSP uses it
        const auto sent = juce::Time::getHighResolutionTicks();
        const auto deadline = isNonRealtime() ? sent + juce::Time::secondsToHighResolutionTicks (offlineDeadlineMs / 1000.0)
                                              : answerDeadline (length, rate, lastWaitBlock);
        const auto spinUntil = sent + juce::Time::secondsToHighResolutionTicks (spinSeconds);
        const auto pollTicks = juce::Time::secondsToHighResolutionTicks (pollSeconds);

        // A short spin catches a quick answer at once; after it, short sleeps leave
        // the core to others (the host among them) instead of burning it.
        while (block.responseSeq.load (std::memory_order_acquire) != request)
        {
            const auto now = juce::Time::getHighResolutionTicks();

            if (remote->dead.load() || now > deadline)
                return false;

            // A sleep can overrun: near the deadline, only yield.
            if (now < spinUntil || deadline - now < pollTicks * 4)
                std::this_thread::yield();
            else
                std::this_thread::sleep_for (std::chrono::duration<double> (pollSeconds));
        }

        for (int c = 0; c < channels; ++c)
            std::memcpy (buffer.getWritePointer (c, start), block.audio[c], sizeof (float) * (size_t) length);

        readMidi (block.midiOut, juce::jlimit (0, SharedBlock::midiBytes, block.midiOutBytes), outMidi, start);
        return true;
    }

    /** A block the host didn't process: an effect's input passes through; an instrument is silent. */
    void bypass (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int start, int length)
    {
        if (numIns == 0)
            buffer.clear (start, length);

        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + length)
                outMidi.addEvent (meta.getMessage(), meta.samplePosition);
    }

    /** A parameter as the host reported it: its text always, its value when the plug-in itself changed it. */
    void applyFromHost (const juce::ValueTree& p)
    {
        const int index = p[msg::index];

        if (! juce::isPositiveAndBelow (index, (int) parameters.size()))
            return;

        auto* parameter = parameters[(size_t) index];
        const auto v = (float) p[msg::value];
        parameter->setText (v, p[msg::text].toString());

        if ((bool) p[msg::fromPlugin] && ! parameter->setByHostLately() && ! juce::approximatelyEqual (v, parameter->getValue()))
            parameter->setValueNotifyingHost (v);
    }

    /** On the pipe's thread: what the host tells unasked. */
    void received (const juce::ValueTree& m)
    {
        if (m.hasType (msg::parameters))
        {
            for (const auto& p : m)
                if (! (bool) p[msg::fromPlugin])
                    if (const int i = p[msg::index]; juce::isPositiveAndBelow (i, (int) parameters.size()))
                        parameters[(size_t) i]->setText ((float) p[msg::value], p[msg::text].toString());
        }

        // The rest touches the engine: on the message thread, while this stand-in lives.
        juce::MessageManager::callAsync ([weak = self, m]
        {
            auto* instance = weak.get();

            if (instance == nullptr)
                return;

            if (m.hasType (msg::parameters))
            {
                for (const auto& p : m)
                    if ((bool) p[msg::fromPlugin])
                        instance->applyFromHost (p);
            }
            else if (m.hasType (msg::gesture))
            {
                if (const int i = m[msg::index]; juce::isPositiveAndBelow (i, (int) instance->parameters.size()))
                {
                    if ((bool) m[msg::starting])
                        instance->parameters[(size_t) i]->beginChangeGesture();
                    else
                        instance->parameters[(size_t) i]->endChangeGesture();
                }
            }
            else if (m.hasType (msg::latency))
            {
                instance->setLatencySamples ((int) m[msg::samples]);
            }
            else if (m.hasType (msg::editorSize))
            {
                instance->ownEditorResized ({ (int) m[msg::width], (int) m[msg::height] });
            }
            else if (m.hasType (msg::key))
            {
                instance->ownEditorKey (keyIn (m));
            }
            else if (m.hasType (msg::clicked))
            {
                if (auto* s = instance->sandbox.get())
                    s->uiClicked (instance->pluginId);
            }
        });
    }

    /** On the pipe's thread (or the message thread): the host is gone. */
    void hostDied()
    {
        juce::MessageManager::callAsync ([sandboxRef = sandbox, id = pluginId]
        {
            if (auto* s = sandboxRef.get())
                s->crashed (id);
        });
    }

    JUCE_DECLARE_WEAK_REFERENCEABLE (Instance)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Instance)
};

//==============================================================================
/** In the plug-in window's vendor area: the place the plug-in's own UI shows.
    The UI itself runs in the sandbox host, in a panel the host lays over
    this editor's bounds on screen and keeps just above its window and below
    Resamper's popups; the editor tells the host where that is whenever it
    moves, resizes, shows or hides, or its window comes to the front or changes
    level. The editor has the UI's native size: the plug-in resizing its UI
    resizes it, and resizing it (the window's grip) resizes the UI. */
class PluginSandbox::Instance::Editor final : public juce::AudioProcessorEditor,
                                              private juce::ComponentMovementWatcher,
                                              private juce::Timer
{
public:
    Editor (Instance& owner, juce::Point<int> size, bool resizable)
        : juce::AudioProcessorEditor (owner), juce::ComponentMovementWatcher (this), instance (owner), pluginSize (size)
    {
        setResizable (resizable, false);
        setWantsKeyboardFocus (true);
        setSize (size.x, size.y);

        // A window's level changes (Resamper in front or not) have no callback.
        startTimer (levelPollMs);
    }

    ~Editor() override
    {
        stopTimer();
        instance.post (juce::ValueTree (msg::closeEditor));

        // Whoever deletes an editor tells its processor; the engine's window doesn't.
        processor.editorBeingDeleted (this);
    }

    /** The plug-in resized its own UI. */
    void pluginResized (juce::Point<int> size)
    {
        pluginSize = size;
        setSize (size.x, size.y);
    }

    /** A key typed in the plug-in's own UI that it didn't use goes to the
        window as if typed here, so Resamper's shortcuts work while the UI has
        the keys. Esc first takes the keys back to Resamper: here, so the
        window's Esc hands them to its chrome. */
    void keyFromOwnUi (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
            grabKeyboardFocus();

        for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
            if (c->keyPressed (key))
                return;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
    }

private:
    static constexpr int levelPollMs = 100;

    struct Dock
    {
        juce::Rectangle<int> area;
        bool visible = false;
        sandboxdock::WindowRef window;
        float scale = 1.0f;
        int raised = 0;

        bool operator== (const Dock&) const = default;
    };

    Instance& instance;
    juce::Point<int> pluginSize;
    Dock last;
    int raised = 0;

    void dock()
    {
        Dock now;
        now.visible = isShowing();
        now.window = sandboxdock::windowOf (*this);
        now.raised = raised;

        if (now.visible)
        {
            now.area = getScreenBounds();
            now.scale = getWidth() > 0 ? (float) now.area.getWidth() / (float) getWidth() : 1.0f;
        }

        if (now == last)
            return;

        // The host puts the panel just above this window. Raise popups first —
        // a drag docks immediately, without waiting for the 100 ms poll — so
        // that reorder can't cover a menu, dialog, tooltip or toast.
        sandboxdock::orderPopupsAboveSandboxedUi();

        last = now;
        juce::ValueTree message (msg::dock);
        message.setProperty (msg::x, now.area.getX(), nullptr);
        message.setProperty (msg::y, now.area.getY(), nullptr);
        message.setProperty (msg::width, now.area.getWidth(), nullptr);
        message.setProperty (msg::height, now.area.getHeight(), nullptr);
        message.setProperty (msg::visible, now.visible, nullptr);
        message.setProperty (msg::window, now.window.number, nullptr);
        message.setProperty (msg::level, now.window.level, nullptr);
        message.setProperty (msg::scale, now.scale, nullptr);
        instance.post (message);
    }

    // The watcher hears every parent, the window too: one brought to the front
    // goes over the UI, which the host puts back above it.
    void componentBroughtToFront (juce::Component&) override
    {
        ++raised;
        dock();
    }

    /** Resized here (the window's grip): the plug-in's UI follows. */
    void componentMovedOrResized (bool, bool wasResized) override
    {
        if (wasResized && (getWidth() != pluginSize.x || getHeight() != pluginSize.y))
        {
            pluginSize = { getWidth(), getHeight() };
            juce::ValueTree message (msg::editorSize);
            message.setProperty (msg::width, pluginSize.x, nullptr);
            message.setProperty (msg::height, pluginSize.y, nullptr);
            instance.post (message);
        }

        dock();
    }

    // The watcher calls the one above only when this editor moves within its window;
    // the window itself moving on the desktop (a title-bar drag) moves it too.
    void componentMovedOrResized (juce::Component& c, bool wasMoved, bool wasResized) override
    {
        juce::ComponentMovementWatcher::componentMovedOrResized (c, wasMoved, wasResized);
        dock();
    }

    void componentPeerChanged() override                       { dock(); }
    void componentVisibilityChanged() override                 { dock(); }
    using juce::ComponentMovementWatcher::componentVisibilityChanged;
    void timerCallback() override
    {
        sandboxdock::orderPopupsAboveSandboxedUi();
        dock();
    }
};

void PluginSandbox::Instance::ownEditorResized (juce::Point<int> size)
{
    if (auto* editor = dynamic_cast<Editor*> (getActiveEditor()))
        editor->pluginResized (size);
}

void PluginSandbox::Instance::ownEditorKey (const juce::KeyPress& key)
{
    if (auto* editor = dynamic_cast<Editor*> (getActiveEditor()))
        editor->keyFromOwnUi (key);
}

juce::AudioProcessorEditor* PluginSandbox::Instance::createEditor()
{
    if (! ownEditor)
        return nullptr;

    // The host makes the UI now, hidden; it shows once this editor is on screen.
    const auto opened = remote->request (juce::ValueTree (msg::openEditor), stateTimeoutMs);

    if (! opened.isValid() || opened.hasProperty (msg::error))
        return nullptr;

    return new Editor (*this, { juce::jmax (1, (int) opened[msg::width]), juce::jmax (1, (int) opened[msg::height]) },
                       (bool) opened[msg::resizable]);
}

//==============================================================================
namespace
{
    /** A sandbox host: this executable run again, serving one plug-in to its stand-in. */
    class Host final : public juce::ChildProcessWorker,
                       private juce::Timer,
                       private juce::ComponentListener,
                       private juce::KeyListener,
                       private juce::AudioProcessorListener,
                       private juce::AudioProcessorParameter::Listener
    {
    public:
        explicit Host (std::vector<std::unique_ptr<juce::AudioPluginFormat>> extraFormats)
        {
            juce::addDefaultFormatsToManager (formats);

            for (auto& format : extraFormats)
                formats.addFormat (std::move (format));
        }

        ~Host() override
        {
            stopTimer();
            stopAudio();
            closeEditor();

            if (plugin != nullptr)
            {
                for (auto* p : parameters)
                    p->removeListener (this);

                plugin->removeListener (this);
            }

            plugin.reset();
        }

        /** Whether the stand-in has gone: runHost returns. */
        bool isFinished() const noexcept   { return finished.load(); }

    private:
        /** Waits for a block from the stand-in, processes it, answers. */
        struct AudioThread final : juce::Thread
        {
            explicit AudioThread (Host& h) : juce::Thread ("Sandbox Audio"), host (h) {}

            void run() override
            {
                while (! threadShouldExit())
                    if (host.wake.wait() && ! threadShouldExit())
                        host.processRequest();
            }

            Host& host;
        };

        enum Dirty : juce::uint8 { byPlugin = 1, byHost = 2 };

        juce::AudioPluginFormatManager formats;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::MemoryMappedFile> mapped;
        SharedBlock* shared = nullptr;
        Semaphore wake;
        std::unique_ptr<AudioThread> audioThread;
        juce::CriticalSection processLock;
        bool prepared = false;
        juce::Array<juce::AudioProcessorParameter*> parameters;
        std::unique_ptr<std::atomic<juce::uint8>[]> dirty;
        std::vector<juce::uint32> seenStamps;
        juce::uint32 seenParameterSeq = 0;
        juce::MidiBuffer midi;
        int reportedLatency = 0;
        std::unique_ptr<juce::AudioProcessorEditor> editor;   ///< the plug-in's own UI, while the stand-in has an editor
        std::unique_ptr<sandboxdock::Panel> panel;          ///< where it shows, once docked
        float editorScale = 1.0f;
        bool resizingForStandIn = false;
        std::atomic<bool> finished { false };

        //==============================================================================
        void handleConnectionMade() override
        {
            juce::ValueTree hello (msg::hello);
            hello.setProperty (msg::pid, (int) ::getpid(), nullptr);
            sendMessageToCoordinator (encode (hello));
        }

        void handleConnectionLost() override
        {
            // The stand-in went, or the app died: nothing is left to serve. A plug-in
            // that won't let go is not waited for.
            std::thread ([] { std::this_thread::sleep_for (std::chrono::milliseconds (quitGraceMs)); std::_Exit (0); }).detach();
            finished.store (true);
        }

        void handleMessageFromCoordinator (const juce::MemoryBlock& data) override
        {
            // The plug-in is only touched on the message thread (the audio aside).
            juce::MessageManager::callAsync ([this, message = decode (data)] { handle (message); });   // nosemgrep: deferred-callback-guards-lifetime -- runHost keeps the Host alive until the dispatch loop stops
        }

        void reply (const juce::ValueTree& request, juce::ValueTree answer = juce::ValueTree (msg::reply))
        {
            answer.setProperty (msg::id, request[msg::id], nullptr);
            sendMessageToCoordinator (encode (answer));
        }

        void handle (const juce::ValueTree& m)
        {
            if (m.hasType (msg::load))
            {
                load (m);
                return;
            }

            if (plugin == nullptr)
            {
                reply (m);
                return;
            }

            // Values set since the last audio block come first: a state saved now
            // has them, and one restored now isn't overwritten by them later.
            applyPendingParameters();

            if (m.hasType (msg::prepare))
            {
                const juce::ScopedLock lock (processLock);
                plugin->setRateAndBufferSizeDetails ((double) m[msg::rate], (int) m[msg::block]);
                plugin->prepareToPlay ((double) m[msg::rate], (int) m[msg::block]);
                midi.ensureSize (SharedBlock::midiBytes);
                prepared = true;
                reply (m);
            }
            else if (m.hasType (msg::release))
            {
                const juce::ScopedLock lock (processLock);
                plugin->releaseResources();
                prepared = false;
            }
            else if (m.hasType (msg::getState))
            {
                juce::MemoryBlock state;
                plugin->getStateInformation (state);
                juce::ValueTree answer (msg::reply);
                answer.setProperty (msg::data, state, nullptr);
                reply (m, answer);
            }
            else if (m.hasType (msg::setState))
            {
                const auto state = binaryOf (m[msg::data]);
                plugin->setStateInformation (state.getData(), (int) state.getSize());

                juce::ValueTree answer (msg::reply);

                for (int i = 0; i < parameters.size(); ++i)
                    answer.appendChild (describeValue (i, true), nullptr);

                reply (m, answer);
            }
            else if (m.hasType (msg::program))
            {
                plugin->setCurrentProgram ((int) m[msg::index]);
                markAll (byPlugin);
            }
            else if (m.hasType (msg::programName))
            {
                plugin->changeProgramName ((int) m[msg::index], m[msg::name].toString());
            }
            else if (m.hasType (msg::openEditor))
            {
                openEditor (m);
            }
            else if (m.hasType (msg::dock))
            {
                dock (m);
            }
            else if (m.hasType (msg::editorSize))
            {
                resizeEditor ({ (int) m[msg::width], (int) m[msg::height] });
            }
            else if (m.hasType (msg::closeEditor))
            {
                closeEditor();
            }
            else if (m.hasType (msg::pressKey))
            {
                if (auto* peer = editor != nullptr ? editor->getPeer() : nullptr)
                    peer->handleKeyPress (keyIn (m));

                reply (m);
            }
            else if (m.hasType (msg::dockState))
            {
                const auto bounds = panel != nullptr ? panel->getScreenBounds() : juce::Rectangle<int>();
                juce::ValueTree answer (msg::reply);
                answer.setProperty (msg::x, bounds.getX(), nullptr);
                answer.setProperty (msg::y, bounds.getY(), nullptr);
                answer.setProperty (msg::width, bounds.getWidth(), nullptr);
                answer.setProperty (msg::height, bounds.getHeight(), nullptr);
                reply (m, answer);
            }
        }

        void load (const juce::ValueTree& m)
        {
            juce::PluginDescription desc;

            if (auto xml = juce::parseXML (m[msg::description].toString()); xml == nullptr || ! desc.loadFromXml (*xml))
                return fail (m, "The sandbox couldn't read the plug-in's description");

            mapped = std::make_unique<juce::MemoryMappedFile> (juce::File (m[msg::shared].toString()),
                                                               juce::MemoryMappedFile::readWrite);

            if (mapped->getData() == nullptr || mapped->getSize() < sizeof (SharedBlock) || ! wake.open (m[msg::semaphore].toString()))
                return fail (m, "The sandbox couldn't share memory with Resamper");

            shared = static_cast<SharedBlock*> (mapped->getData());

            formats.createPluginInstanceAsync (desc, (double) m[msg::rate], (int) m[msg::block],
                                               [this, m] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                if (instance == nullptr)
                    return fail (m, error.isNotEmpty() ? error : juce::String ("The plug-in didn't load"));

                loaded (m, std::move (instance));
            });
        }

        void fail (const juce::ValueTree& request, const juce::String& error)
        {
            juce::ValueTree answer (msg::reply);
            answer.setProperty (msg::error, error, nullptr);
            reply (request, answer);
        }

        void loaded (const juce::ValueTree& request, std::unique_ptr<juce::AudioPluginInstance> instance)
        {
            plugin = std::move (instance);
            plugin->enableAllBuses();
            plugin->addListener (this);
            parameters = plugin->getParameters();

            if (parameters.size() > SharedBlock::maxParameters)
                parameters.resize (SharedBlock::maxParameters);

            dirty = std::make_unique<std::atomic<juce::uint8>[]> ((size_t) juce::jmax (1, parameters.size()));
            seenStamps.assign ((size_t) parameters.size(), 0);

            for (int i = 0; i < parameters.size(); ++i)
            {
                dirty[(size_t) i].store (0);
                parameters[i]->addListener (this);
            }

            reportedLatency = plugin->getLatencySamples();

            juce::ValueTree answer (msg::reply);
            answer.setProperty (msg::inputs, plugin->getMainBusNumInputChannels(), nullptr);
            answer.setProperty (msg::outputs, plugin->getMainBusNumOutputChannels(), nullptr);
            answer.setProperty (msg::acceptsMidi, plugin->acceptsMidi(), nullptr);
            answer.setProperty (msg::producesMidi, plugin->producesMidi(), nullptr);
            answer.setProperty (msg::midiEffect, plugin->isMidiEffect(), nullptr);
            answer.setProperty (msg::tail, plugin->getTailLengthSeconds(), nullptr);
            answer.setProperty (msg::latency, reportedLatency, nullptr);
            answer.setProperty (msg::current, plugin->getCurrentProgram(), nullptr);
            answer.setProperty (msg::hasEditor, plugin->hasEditor(), nullptr);

            for (int i = 0; i < parameters.size(); ++i)
            {
                auto* p = parameters[i];
                auto info = describeValue (i, false);
                info.setProperty (msg::name, p->getName (1024), nullptr);
                info.setProperty (msg::label, p->getLabel(), nullptr);
                info.setProperty (msg::defaultValue, p->getDefaultValue(), nullptr);
                info.setProperty (msg::steps, p->getNumSteps(), nullptr);
                info.setProperty (msg::discrete, p->isDiscrete(), nullptr);
                info.setProperty (msg::boolean, p->isBoolean(), nullptr);
                info.setProperty (msg::automatable, p->isAutomatable(), nullptr);

                if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p))
                    info.setProperty (msg::parameterId, hosted->getParameterID(), nullptr);

                answer.appendChild (info, nullptr);
            }

            for (int i = 0; i < plugin->getNumPrograms(); ++i)
            {
                juce::ValueTree program (msg::programName);
                program.setProperty (msg::name, plugin->getProgramName (i), nullptr);
                answer.appendChild (program, nullptr);
            }

            // A real-time thread, as the app's audio thread is: a normal one, however high its
            // priority, can be put aside on a busy system and miss the block (#135).
            audioThread = std::make_unique<AudioThread> (*this);
            const auto rate = juce::jmax (8000.0, (double) request[msg::rate]);
            const auto blockSize = juce::jlimit (16, SharedBlock::maxSamples, (int) request[msg::block]);

            if (! audioThread->startRealtimeThread (juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime (blockSize, rate)))
                audioThread->startThread (juce::Thread::Priority::highest);

            startTimer (reportMs);
            reply (request, answer);
        }

        juce::ValueTree describeValue (int index, bool fromPlugin) const
        {
            auto* p = parameters[index];
            juce::ValueTree info (msg::parameter);
            info.setProperty (msg::index, index, nullptr);
            info.setProperty (msg::value, p->getValue(), nullptr);
            info.setProperty (msg::text, (p->getText (p->getValue(), 1024) + " " + p->getLabel()).trim(), nullptr);
            info.setProperty (msg::fromPlugin, fromPlugin, nullptr);
            return info;
        }

        void markAll (Dirty how)
        {
            for (int i = 0; i < parameters.size(); ++i)
                dirty[(size_t) i].fetch_or (how);
        }

        /** Makes the plug-in's own UI, hidden until the stand-in docks it; answers with its size. */
        void openEditor (const juce::ValueTree& request)
        {
            if (editor == nullptr)
            {
                editor.reset (plugin->createEditorAndMakeActive());

                if (editor == nullptr)
                    return fail (request, "The plug-in has no editor");

                editor->addComponentListener (this);
                editor->addKeyListener (this);
            }

            juce::ValueTree answer (msg::reply);
            answer.setProperty (msg::width, editor->getWidth(), nullptr);
            answer.setProperty (msg::height, editor->getHeight(), nullptr);
            answer.setProperty (msg::resizable, editor->isResizable(), nullptr);
            reply (request, answer);
        }

        /** Lays the UI over the stand-in editor's place on screen, or hides it. */
        void dock (const juce::ValueTree& m)
        {
            if (editor == nullptr)
                return;

            if (panel == nullptr)
                panel = std::make_unique<sandboxdock::Panel> (*editor, [this]
                {
                    sendMessageToCoordinator (encode (juce::ValueTree (msg::clicked)));
                });

            if (const auto scale = (float) m[msg::scale]; scale > 0 && ! juce::approximatelyEqual (scale, editorScale))
            {
                editorScale = scale;
                editor->setScaleFactor (scale);
            }

            panel->place ({ (int) m[msg::x], (int) m[msg::y], (int) m[msg::width], (int) m[msg::height] },
                          (bool) m[msg::visible], { (juce::int64) m[msg::window], (int) m[msg::level] });
        }

        /** The stand-in's editor was resized (the window's grip): the UI follows, within its own limits. */
        void resizeEditor (juce::Point<int> size)
        {
            if (editor == nullptr || size.x <= 0 || size.y <= 0)
                return;

            {
                const juce::ScopedValueSetter<bool> standIn (resizingForStandIn, true);
                editor->setSize (size.x, size.y);
            }

            // It kept a size of its own: the stand-in takes that.
            if (editor->getWidth() != size.x || editor->getHeight() != size.y)
                reportEditorSize();
        }

        void reportEditorSize()
        {
            juce::ValueTree message (msg::editorSize);
            message.setProperty (msg::width, editor->getWidth(), nullptr);
            message.setProperty (msg::height, editor->getHeight(), nullptr);
            sendMessageToCoordinator (encode (message));
        }

        void closeEditor()
        {
            panel.reset();

            if (editor != nullptr)
            {
                editor->removeComponentListener (this);
                editor->removeKeyListener (this);
                plugin->editorBeingDeleted (editor.get());
                editor.reset();
            }

            editorScale = 1.0f;
        }

        /** A key nothing in the plug-in's UI used, the editor itself last: Resamper's. */
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            if (editor == nullptr || editor->keyPressed (key))
                return true;

            sendMessageToCoordinator (encode (messageFor (msg::key, key)));
            return true;
        }

        /** The plug-in resized its own UI. */
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (wasResized && ! resizingForStandIn)
                reportEditorSize();
        }

        //==============================================================================
        /** On the audio thread: the block the stand-in published. */
        void processRequest()
        {
            auto& block = *shared;
            const auto request = block.requestSeq.load (std::memory_order_acquire);

            if (request == block.responseSeq.load (std::memory_order_relaxed))
                return;

            {
                const juce::ScopedLock lock (processLock);
                applyParameters();

                if (prepared)
                {
                    const auto length = juce::jlimit (0, SharedBlock::maxSamples, (int) block.numSamples);
                    const auto needed = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
                    const auto channels = juce::jlimit (0, SharedBlock::maxChannels, juce::jmax ((int) block.numChannels, needed));
                    float* pointers[SharedBlock::maxChannels];

                    for (int c = 0; c < channels; ++c)
                    {
                        pointers[c] = block.audio[c];

                        if (c >= block.numChannels)
                            juce::FloatVectorOperations::clear (pointers[c], length);
                    }

                    juce::AudioBuffer<float> buffer (pointers, channels, length);
                    midi.clear();
                    readMidi (block.midiIn, juce::jlimit (0, SharedBlock::midiBytes, (int) block.midiInBytes), midi, 0);
                    const auto started = juce::Time::getHighResolutionTicks();
                    plugin->processBlock (buffer, midi);
                    reportLoad (started, length);
                    block.midiOutBytes = writeMidi (midi, 0, length, block.midiOut, SharedBlock::midiBytes);
                }
            }

            block.responseSeq.store (request, std::memory_order_release);
        }

        /** Folds the block that took from started on into the load the stand-in reads. */
        void reportLoad (juce::int64 started, int length)
        {
            const auto rate = plugin->getSampleRate();

            if (length <= 0 || rate <= 0)
                return;

            const auto spent = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
            const auto share = (float) (spent * rate / length);
            auto& load = shared->hostLoad;
            load.store (load.load (std::memory_order_relaxed) * 0.9f + share * 0.1f, std::memory_order_relaxed);
        }

        /** The values the stand-in set since the last block. */
        void applyParameters()
        {
            auto& block = *shared;
            const auto seq = block.parameterSeq.load (std::memory_order_acquire);

            if (seq == seenParameterSeq)
                return;

            seenParameterSeq = seq;

            for (int i = 0; i < parameters.size(); ++i)
            {
                const auto stamp = block.parameterStamps[i].load (std::memory_order_acquire);

                if (stamp != seenStamps[(size_t) i])
                {
                    seenStamps[(size_t) i] = stamp;
                    parameters[i]->setValue (block.parameterValues[i].load (std::memory_order_relaxed));
                    dirty[(size_t) i].fetch_or (byHost);
                }
            }
        }

        /** Off the audio thread: the values the stand-in set, applied now rather than at the next block. */
        void applyPendingParameters()
        {
            const juce::ScopedLock lock (processLock);
            applyParameters();
        }

        void stopAudio()
        {
            if (audioThread != nullptr)
            {
                audioThread->signalThreadShouldExit();
                wake.post();
                audioThread->stopThread (quitGraceMs);
            }
        }

        //==============================================================================
        /** Reports what changed: values the plug-in changed itself, and every changed value's text. */
        void timerCallback() override
        {
            // Stopped, no audio blocks come: what the stand-in set still reaches the plug-in.
            applyPendingParameters();

            juce::ValueTree report (msg::parameters);

            for (int i = 0; i < parameters.size(); ++i)
                if (const auto how = dirty[(size_t) i].exchange (0); how != 0)
                    report.appendChild (describeValue (i, (how & byPlugin) != 0), nullptr);

            if (report.getNumChildren() > 0)
                sendMessageToCoordinator (encode (report));
        }

        void parameterValueChanged (int index, float) override
        {
            if (juce::isPositiveAndBelow (index, parameters.size()))
                dirty[(size_t) index].fetch_or (byPlugin);
        }

        void parameterGestureChanged (int index, bool starting) override
        {
            juce::ValueTree gesture (msg::gesture);
            gesture.setProperty (msg::index, index, nullptr);
            gesture.setProperty (msg::starting, starting, nullptr);
            sendMessageToCoordinator (encode (gesture));
        }

        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}

        void audioProcessorChanged (juce::AudioProcessor* processor, const ChangeDetails& details) override
        {
            if (! details.latencyChanged || processor == nullptr || processor->getLatencySamples() == reportedLatency)
                return;

            reportedLatency = processor->getLatencySamples();
            juce::ValueTree latency (msg::latency);
            latency.setProperty (msg::samples, reportedLatency, nullptr);
            sendMessageToCoordinator (encode (latency));
        }

        JUCE_DECLARE_NON_COPYABLE (Host)
    };
}
#endif

//==============================================================================
PluginSandbox::PluginSandbox()
{
   #if RESAMPER_SANDBOX
    // A loader mostly waits on its host: a project's plug-ins load side by side.
    loaders = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName ("Sandbox Loader")
                                                                            .withNumberOfThreads (maxLoaders));
   #endif
}

PluginSandbox::~PluginSandbox()
{
    masterReference.clear();

   #if RESAMPER_SANDBOX
    // A loader waiting on its host returns at once; the hosts quit with their loads.
    for (auto& [id, load] : loads)
        load->remote->cancel();

    loaders.reset();
   #endif
}

bool PluginSandbox::isHost (int argc, const char* const* argv)
{
    return argc >= 2 && juce::String (argv[1]).startsWith ("--" + juce::String (hostId) + ":");
}

int PluginSandbox::runHost (int argc, const char* const* argv, std::vector<std::unique_ptr<juce::AudioPluginFormat>> extraFormats)
{
   #if RESAMPER_SANDBOX
    if (! isHost (argc, argv))
        return 2;

   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif

    Host host (std::move (extraFormats));

    if (! host.initialiseFromCommandLine (argv[1], hostId))
        return 3;

    // Not runDispatchLoop: run outside an app bundle, [NSApp run] can return at once.
    auto* messages = juce::MessageManager::getInstance();

    while (! host.isFinished())
        messages->runDispatchLoopUntil (dispatchMs);

    return 0;
   #else
    juce::ignoreUnused (argc, argv, extraFormats);
    return 2;
   #endif
}

bool PluginSandbox::isAvailable()
{
    return RESAMPER_SANDBOX != 0;
}

juce::StringArray PluginSandbox::getDefaultFormatNames()
{
    juce::StringArray names;

   #if RESAMPER_SANDBOX
    // What a sandbox host knows: the default formats (runHost adds its extra ones).
    juce::AudioPluginFormatManager defaults;
    juce::addDefaultFormatsToManager (defaults);

    for (auto* format : defaults.getFormats())
        names.addIfNotAlreadyThere (format->getName());
   #endif

    return names;
}

bool PluginSandbox::loadInBackground (const juce::PluginDescription& desc, const juce::String& pluginId, double sampleRate,
                                      int blockSize, std::function<void()> onDone)
{
   #if RESAMPER_SANDBOX
    JUCE_ASSERT_MESSAGE_THREAD
    const auto identifier = desc.createIdentifierString();

    if (auto found = loads.find (pluginId); found != loads.end())
    {
        if (found->second->identifier == identifier)
        {
            // The newest plug-in object asking is the one to tell.
            if (! found->second->done)
                found->second->onDone = std::move (onDone);

            return found->second->done;
        }

        dropLoad (pluginId, found->second.get());
    }

    auto load = std::make_shared<Load>();
    load->identifier = identifier;
    load->remote = std::make_shared<Remote>();
    load->onDone = std::move (onDone);
    loads[pluginId] = load;

    juce::ValueTree request (msg::load);

    if (auto xml = desc.createXml())
        request.setProperty (msg::description, xml->toString(), nullptr);

    request.setProperty (msg::rate, sampleRate, nullptr);
    request.setProperty (msg::block, blockSize, nullptr);

    // Made here, on the message thread: the loader only copies it, and it is read back here.
    juce::WeakReference<PluginSandbox> self (this);

    // Weakly: a load dropped since is gone, and a new one may have taken its address.
    loaders->addJob ([remote = load->remote, request, name = desc.name, self, pluginId, which = std::weak_ptr<Load> (load)]() mutable
    {
        juce::String error;
        auto loaded = loadInHost (*remote, request, name, error);
        remote.reset();

        juce::MessageManager::callAsync ([self, pluginId, which, loaded, error]
        {
            if (auto* sandbox = self.get())
                if (const auto load = which.lock())
                    sandbox->loadFinished (pluginId, load.get(), loaded, error);
        });
    });

    return false;
   #else
    juce::ignoreUnused (desc, pluginId, sampleRate, blockSize, onDone);
    return true;
   #endif
}

void PluginSandbox::dropLoad (const juce::String& pluginId)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto found = loads.find (pluginId); found != loads.end())
        dropLoad (pluginId, found->second.get());
}

bool PluginSandbox::isLoading (const juce::String& pluginId) const
{
    const auto found = loads.find (pluginId);
    return found != loads.end() && ! found->second->done;
}

bool PluginSandbox::waitForLoads()
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto anyLoading = [this] { return std::any_of (loads.begin(), loads.end(), [] (const auto& l) { return ! l.second->done; }); };

    // A load ends by itself within loadTimeoutMs; a little more lets its answer arrive.
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) (loadTimeoutMs + 1000);

    while (anyLoading() && juce::Time::getMillisecondCounter() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    return ! anyLoading();
}

void PluginSandbox::loadFinished (const juce::String& pluginId, const Load* which, const juce::ValueTree& loaded,
                                  const juce::String& error)
{
   #if RESAMPER_SANDBOX
    const auto found = loads.find (pluginId);

    // Dropped (and maybe loading again) since.
    if (found == loads.end() || found->second.get() != which)
        return;

    auto& load = *found->second;
    load.done = true;
    load.loaded = loaded;
    load.error = error;

    if (auto onDone = std::move (load.onDone))
        onDone();

    // Nobody took it (the plug-in went, or runs in-process now): its host quits.
    dropLoad (pluginId, which);
   #else
    juce::ignoreUnused (pluginId, which, loaded, error);
   #endif
}

void PluginSandbox::dropLoad (const juce::String& pluginId, const Load* which)
{
   #if RESAMPER_SANDBOX
    if (auto found = loads.find (pluginId); found != loads.end() && found->second.get() == which)
    {
        found->second->remote->cancel();
        loads.erase (found);
    }
   #else
    juce::ignoreUnused (pluginId, which);
   #endif
}

std::unique_ptr<juce::AudioPluginInstance> PluginSandbox::createInstance (const juce::PluginDescription& desc,
                                                                           const juce::String& pluginId, juce::String& error)
{
   #if RESAMPER_SANDBOX
    const auto found = loads.find (pluginId);

    if (found == loads.end() || ! found->second->done || found->second->identifier != desc.createIdentifierString())
    {
        error = desc.name + " hasn't loaded in its sandbox";
        return {};
    }

    const auto load = found->second;
    loads.erase (found);

    if (load->error.isNotEmpty())
    {
        error = load->error;
        return {};
    }

    return std::make_unique<Instance> (*this, desc, pluginId, load->remote, load->loaded);
   #else
    juce::ignoreUnused (desc, pluginId);
    error = "Plug-ins can't run sandboxed on this platform";
    return {};
   #endif
}

void PluginSandbox::audioBlockFinished() noexcept
{
    blocksFinished.fetch_add (1, std::memory_order_relaxed);
}

bool PluginSandbox::isSandboxed (const juce::AudioProcessor* processor)
{
   #if RESAMPER_SANDBOX
    return dynamic_cast<const Instance*> (processor) != nullptr;
   #else
    juce::ignoreUnused (processor);
    return false;
   #endif
}

bool PluginSandbox::hasCrashed (const juce::AudioProcessor* processor)
{
   #if RESAMPER_SANDBOX
    auto* instance = dynamic_cast<const Instance*> (processor);
    return instance != nullptr && instance->hasCrashed();
   #else
    juce::ignoreUnused (processor);
    return false;
   #endif
}

double PluginSandbox::getHostCpuLoad (const juce::AudioProcessor* processor)
{
   #if RESAMPER_SANDBOX
    if (auto* instance = dynamic_cast<const Instance*> (processor); instance != nullptr && ! instance->hasCrashed())
        return juce::jlimit (0.0, 1.0, instance->getHostCpuLoad());
   #else
    juce::ignoreUnused (processor);
   #endif
    return 0.0;
}

void PluginSandbox::pressKeyInOwnEditor (juce::AudioProcessor* processor, const juce::KeyPress& key)
{
   #if RESAMPER_SANDBOX
    if (auto* instance = dynamic_cast<Instance*> (processor); instance != nullptr && ! instance->hasCrashed())
        instance->pressKeyInOwnEditor (key);
   #else
    juce::ignoreUnused (processor, key);
   #endif
}

juce::Rectangle<int> PluginSandbox::getOwnEditorScreenBounds (juce::AudioProcessor* processor)
{
   #if RESAMPER_SANDBOX
    if (auto* instance = dynamic_cast<Instance*> (processor); instance != nullptr && ! instance->hasCrashed())
        return instance->getOwnEditorScreenBounds();
   #else
    juce::ignoreUnused (processor);
   #endif
    return {};
}

void PluginSandbox::addListener (Listener* l)      { listeners.add (l); }
void PluginSandbox::removeListener (Listener* l)   { listeners.remove (l); }

void PluginSandbox::crashed (const juce::String& pluginId)
{
    listeners.call ([&] (Listener& l) { l.pluginCrashed (pluginId); });
}

void PluginSandbox::uiClicked (const juce::String& pluginId)
{
    listeners.call ([&] (Listener& l) { l.pluginUiClicked (pluginId); });
}

} // namespace resamper
