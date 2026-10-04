#pragma once

/** First include of an engine-private header. resamper_engine and the tests
    define RESAMPER_ENGINE_INTERNAL; UI, Commands and the app do not, so
    including one of these headers there stops here, before the implementation. */
#ifndef RESAMPER_ENGINE_INTERNAL
 #error This header is engine-internal. Use the facade that area already has (ApplicationModel, Mixer, PluginRack, PluginHosting, NativeDevices) instead of this implementation.
#endif
