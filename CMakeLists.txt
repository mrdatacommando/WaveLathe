cmake_minimum_required(VERSION 3.22)

# Keep in step with wavelathe::version in Source/Version.h. Two copies because
# the standalone reads the header and a DAW reads the plugin module - and the
# one a DAW shows is the one nobody remembers to bump.
#
# Bumping this line is necessary but is not on its own enough: see the resource
# rule under juce_add_plugin below, which is what actually carries the number
# into the binary. For a month this line said 0.37.0 and the plugin said 0.1.0.
project(WaveLathe VERSION 0.49.0)

# Release builds keep their symbols. Without a .pdb a crash dump can name the
# module and an offset and nothing else - "WaveLathe.exe+0x15e61a" - and placing
# that one meant disassembling the faulting bytes out of the dump and searching
# every object file in the tree for them. The dump is worth having; so is being
# able to read it.
#
# /Zi only adds the .pdb, it changes no code. /DEBUG is what makes the linker
# write one, but it also switches OFF the two optimisations a Release link does
# by default, so both are asked for again by name and the binary stays what it
# was. FULL rather than FASTLINK: a FASTLINK pdb is a set of pointers back at
# the object files on the machine that built it and is worthless once they are
# gone, which defeats the point of keeping it beside the build that shipped.
#
# Set before JUCE is pulled in, deliberately. JUCE's own code is compiled into
# this binary and inlined all through it, and it is where the one real crash was.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG:FULL>
                     $<$<CONFIG:Release>:/OPT:REF>
                     $<$<CONFIG:Release>:/OPT:ICF>)

    # The C++ runtime goes inside the binary rather than beside it. By default
    # MSVC links it as a DLL, which means WaveLathe needs MSVCP140.dll,
    # VCRUNTIME140.dll and VCRUNTIME140_1.dll present on the machine running it.
    # Those ship with Microsoft's Visual C++ redistributable, not with Windows,
    # so whether WaveLathe starts on somebody else's PC comes down to whether
    # something else happened to install that first.
    #
    # Most machines do have it, which is the trap: it works everywhere you test
    # and fails on the one friend who has a clean install. The standalone at
    # least says which DLL is missing. The plugin does not - a host that cannot
    # load a module drops it from the scan, so WaveLathe simply is not in the
    # list, with nothing anywhere saying why.
    #
    # Safe here because nothing built with one runtime hands the other a runtime
    # object: the plugin talks to the host through VST3's virtual interfaces,
    # which carry plain types. JUCE and the VST3 SDK compile into these binaries,
    # so this must be set before they are pulled in, same as /Zi above.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(WaveLathe
    COMPANY_NAME "MrDataCommando"
    # Who made it and what it is. A host reads these off the plugin and shows
    # them in its browser and its plugin manager; left unset they are blank or
    # repeat the name, which is what a plugin nobody finished looks like.
    COMPANY_COPYRIGHT "Copyright (c) 2026 Mark Van de Velde"
    DESCRIPTION "Wavetable synthesiser with sound matching"
    # No email address. Bug reports and questions go to the repository's
    # issues, which is also where the source and the releases are - and under
    # the AGPL the source is part of what is offered with every copy, so the
    # one link a host shows points there.
    COMPANY_WEBSITE "https://github.com/mrdatacommando/WaveLathe"
    #
    # Reverse-DNS, lowercase, set explicitly rather than left to JUCE to derive
    # from the company and product names. It identifies the plugin on macOS, so
    # it wants pinning now while nothing has shipped under a different one.
    BUNDLE_ID "com.mrdatacommando.wavelathe"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE MDCo
    PLUGIN_CODE Wlt1
    # Instrument comes free from IS_SYNTH above - JUCE picks it rather than Fx
    # without being told. Synth is the refinement, and it is worth saying: a
    # host that groups instruments by subcategory files this one with the
    # synthesisers instead of under a general heading.
    VST3_CATEGORIES "Instrument" "Synth"
    # VST3 is one word here.
    # Deliberately NOT copied to C:\Program Files\Common Files\VST3 after a
    # build - point the host at build/WaveLathe_artefacts/Release/VST3/ instead,
    # so a rebuild is picked up in place rather than needing an install step
    # between every change.
    FORMATS Standalone VST3
    PRODUCT_NAME "WaveLathe"
)

# The version a DAW reads, actually reaching the binary.
#
# JUCE builds the Windows version resource with a custom command that declares
# no inputs at all, so the first build writes WaveLathe_resources.rc and no
# later build ever rebuilds it. The number baked into it is whatever the version
# was the day this build tree was first configured - which is why the plugin
# reported 0.1.0 while project() above said 0.37.0 and the panel agreed with
# project(). Nothing was stale except the one file nobody ever looks at.
#
# Info.txt sits beside it, is rewritten on every configure, and already carries
# the right number. Naming it as a dependency is the edge JUCE left out.
if(WIN32)
    get_target_property(wavelathe_generated_dir WaveLathe JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(wavelathe_info_file WaveLathe JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${wavelathe_generated_dir}/WaveLathe_resources.rc" APPEND
                       DEPENDS "${wavelathe_info_file}")
endif()

target_sources(WaveLathe
    PRIVATE
        Source/PluginProcessor.cpp
        Source/DrumSampleLoader.cpp
        Source/PluginEditor.cpp
        Source/WavetableOscillator.cpp
        Source/SynthVoice.cpp
        Source/FxpPreset.cpp
        Source/SampleMatcher.cpp
        Source/OfflineRenderer.cpp
        Source/PresetOptimizer.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/Arpeggiator.cpp
        Source/TempoSync.cpp
        Source/AppSettings.cpp
        Source/ParameterRegistry.cpp
        Source/MidiMap.cpp
        Source/MidiMonitor.cpp
        Source/HostParameters.cpp
        Source/MidiMapView.cpp
        Source/OutputTap.cpp
        Source/OutputScope.cpp
        Source/SpectrumAnalyser.cpp
        Source/StepSequencer.cpp
        Source/NoteLatch.cpp
        Source/UndoHistory.cpp
        Source/MonoVoice.cpp
        Source/SequencerPanel.cpp
        Source/DrumPanel.cpp
        Source/MasteringPanel.cpp
        Source/BusEffectsPanel.cpp
        Source/Scales.cpp
        Source/HarmonyProcessor.cpp
        Source/FactoryPresets.cpp
        Source/PresetBrowser.cpp
        Source/SampleBrowser.cpp
        Source/UiComponents.cpp
        Source/WaveformDisplay.cpp
)

target_compile_definitions(WaveLathe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(WaveLathe
    PRIVATE
        # Defined further down this file. CMake resolves a target by name at
        # generate time rather than where it is written, so the order reads
        # oddly and builds correctly.
        MasteringChain
        DrumKit
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- The mastering chain, as a library rather than as more source files -----
#
# Every other target in this file compiles the sources it needs straight into
# itself, which is the right arrangement for code only WaveLathe will ever use.
# This is the exception. The mastering chain is meant to be linked by a second
# product - an effect plugin sitting on a mix bus - and by whatever is built
# after that, so the boundary between it and the synth has to be real rather
# than a promise in a comment.
#
# A library target is what makes it real. Nothing in here can reach back into
# SynthParameters or any other synth header without this failing to compile,
# and MasteringChainTest below links THIS rather than listing the sources again,
# so the library is proven to stand up on its own every time the suite runs.
add_library(MasteringChain STATIC
    Source/Mastering/MasteringChain.cpp
)
target_include_directories(MasteringChain PUBLIC Source)
target_compile_definitions(MasteringChain PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MasteringChain
    PUBLIC
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
    PRIVATE
        juce::juce_recommended_config_flags
)

# ---- The drum kit, a library for the same reason ----------------------------
#
# Sixteen synthesised voices, the choke between two of them, and the queue that
# lets a hit land between block boundaries. Nothing about a sequencer, a
# pattern, a preset or a user interface: it is handed hits and a block, and it
# adds sound to the block.
#
# A library for the reason MasteringChain is one. The boundary is what keeps
# the DSP testable without a processor - and DrumVoiceTest below links THIS
# rather than listing the sources again, so the day the kit reaches back into
# SynthParameters or StepSequencer, the suite stops building.
#
# Note what it does NOT link: juce_dsp. The one filter the voices need is
# written out in DrumVoice.cpp, which costs a dozen lines and buys a library
# that anything with juce_audio_basics can pick up.
add_library(DrumKit STATIC
    Source/Drums/DrumVoice.cpp
    Source/Drums/DrumFx.cpp
    Source/Drums/DrumKit.cpp
)
target_include_directories(DrumKit PUBLIC Source)
target_compile_definitions(DrumKit PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(DrumKit
    PUBLIC
        juce::juce_core
        juce::juce_audio_basics
    PRIVATE
        juce::juce_recommended_config_flags
)

# ---- The mastering chain as an effect plugin of its own ---------------------
#
# The half the synth's own Master page cannot do. A stage inside an instrument
# only ever hears that instrument; this one sits on a mix bus and catches
# whatever is routed into it, including plugins nobody here wrote.
#
# Its own PLUGIN_CODE, because a host identifies a plugin by that four-letter
# code and two products sharing one is how a DAW ends up loading the wrong one.
# Its own BUNDLE_ID for the same reason on macOS.
juce_add_plugin(WaveLatheMaster
    COMPANY_NAME "MrDataCommando"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Mark Van de Velde"
    DESCRIPTION "Mastering chain: saturation, compressor and limiter"
    COMPANY_WEBSITE "https://github.com/mrdatacommando/WaveLathe"
    BUNDLE_ID "com.mrdatacommando.wavelathemaster"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE MDCo
    PLUGIN_CODE WlM1
    # Dynamics as well as Fx, so a host that groups by subcategory files it with
    # the compressors rather than under a general heading - the same courtesy
    # the synth asks for with Instrument|Synth.
    VST3_CATEGORIES "Fx" "Dynamics"
    FORMATS Standalone VST3
    PRODUCT_NAME "WaveLathe Master"
)

# Four files, and two of them are shared with the synth rather than copied.
# MasteringPanel reaches its values through mastering::Controls, and
# UiComponents depends on nothing but JUCE's GUI module - which is what makes
# the same page usable here without dragging a synthesiser in behind it.
target_sources(WaveLatheMaster
    PRIVATE
        Source/MasterPlugin/MasterPluginProcessor.cpp
        Source/MasterPlugin/MasterPluginEditor.cpp
        Source/MasteringPanel.cpp
        Source/UiComponents.cpp
)

target_include_directories(WaveLatheMaster PRIVATE Source)

target_compile_definitions(WaveLatheMaster
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# The same edge JUCE leaves out, for the same reason, on the second product.
# Its resource .rc declares no inputs, so it is written once when the build tree
# is first configured and never again - and this one was first configured at
# 0.38.8. Without this it would report that number for ever while everything
# else moved on, which is precisely the bug documented above WaveLathe's copy.
if(WIN32)
    get_target_property(master_generated_dir WaveLatheMaster JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(master_info_file WaveLatheMaster JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${master_generated_dir}/WaveLatheMaster_resources.rc" APPEND
                       DEPENDS "${master_info_file}")
endif()

target_link_libraries(WaveLatheMaster
    PRIVATE
        MasteringChain
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(FxpRoundTripTest PRODUCT_NAME "FxpRoundTripTest")
target_sources(FxpRoundTripTest
    PRIVATE
        Tests/FxpRoundTripTest.cpp
        Source/FxpPreset.cpp
)
target_include_directories(FxpRoundTripTest PRIVATE Source)
target_compile_definitions(FxpRoundTripTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(FxpRoundTripTest PRIVATE juce::juce_core juce::juce_recommended_config_flags)

juce_add_console_app(DrumPatternTest PRODUCT_NAME "DrumPatternTest")
target_sources(DrumPatternTest
    PRIVATE
        Tests/DrumPatternTest.cpp
        Source/StepSequencer.cpp
        Source/FxpPreset.cpp
        Source/ParameterRegistry.cpp
        Source/Scales.cpp
        Source/TempoSync.cpp
)
target_include_directories(DrumPatternTest PRIVATE Source)
target_compile_definitions(DrumPatternTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
# The drum kit is linked here as well as the sequencer, and this is the only
# target that links both. That is what makes it the place the two sixteens are
# held equal - see the static_assert at the top of the test.
target_link_libraries(DrumPatternTest
    PRIVATE
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
)

juce_add_console_app(ArpPatternTest PRODUCT_NAME "ArpPatternTest")
target_sources(ArpPatternTest
    PRIVATE
        Tests/ArpPatternTest.cpp
        Source/Arpeggiator.cpp
        Source/TempoSync.cpp
)
target_include_directories(ArpPatternTest PRIVATE Source)
target_compile_definitions(ArpPatternTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ArpPatternTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

juce_add_console_app(HarmonyTest PRODUCT_NAME "HarmonyTest")
target_sources(HarmonyTest
    PRIVATE
        Tests/HarmonyTest.cpp
        Source/Scales.cpp
        Source/HarmonyProcessor.cpp
        Source/FactoryPresets.cpp
        Source/FxpPreset.cpp
)
target_include_directories(HarmonyTest PRIVATE Source)
target_compile_definitions(HarmonyTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(HarmonyTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

juce_add_console_app(SequencerTest PRODUCT_NAME "SequencerTest")
target_sources(SequencerTest
    PRIVATE
        Tests/SequencerTest.cpp
        Source/StepSequencer.cpp
        Source/NoteLatch.cpp
        Source/UndoHistory.cpp
        Source/MonoVoice.cpp
        Source/ParameterRegistry.cpp
        Source/Scales.cpp
        Source/TempoSync.cpp
)
target_include_directories(SequencerTest PRIVATE Source)
target_compile_definitions(SequencerTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(SequencerTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)


# This suite used to need /STACK:16777216. A StepSequencer carried a lane per
# parameter and a tick per lane INLINE - a quarter of a megabyte at a cap of 64 -
# and main declares about thirty of them as locals. MSVC reserves a function's
# WHOLE frame on entry, summing sibling scopes rather than reusing them, so the
# default 1MB stack overflowed before the first line of output was printed.
#
# Raising the cap to 128 would have doubled that to roughly 10MB against a 16MB
# stack, and 256 would have needed another raise. The lanes went to the heap
# instead - allocated once, in the constructor, never on the audio thread - so a
# StepSequencer is a few dozen bytes again and the stack option is gone rather
# than doubled. See the note on the lanes member in StepSequencer.h.

juce_add_console_app(MidiMapTest PRODUCT_NAME "MidiMapTest")
target_sources(MidiMapTest
    PRIVATE
        Tests/MidiMapTest.cpp
        Source/MidiMap.cpp
        Source/MidiMonitor.cpp
        Source/ParameterRegistry.cpp
)
target_include_directories(MidiMapTest PRIVATE Source)
target_compile_definitions(MidiMapTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MidiMapTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

juce_add_console_app(OutputTapTest PRODUCT_NAME "OutputTapTest")
target_sources(OutputTapTest
    PRIVATE
        Tests/OutputTapTest.cpp
        Source/OutputTap.cpp
)
target_include_directories(OutputTapTest PRIVATE Source)
target_compile_definitions(OutputTapTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(OutputTapTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

juce_add_console_app(SpectrumTest PRODUCT_NAME "SpectrumTest")
target_sources(SpectrumTest
    PRIVATE
        Tests/SpectrumTest.cpp
        Source/SpectrumAnalyser.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
)
target_include_directories(SpectrumTest PRIVATE Source)
target_compile_definitions(SpectrumTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(SpectrumTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(BankAuditTest PRODUCT_NAME "BankAuditTest")
target_sources(BankAuditTest
    PRIVATE
        Tests/BankAuditTest.cpp
        Source/FactoryPresets.cpp
        Source/FxpPreset.cpp
        Source/OfflineRenderer.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/SynthVoice.cpp
        Source/WavetableOscillator.cpp
        Source/ParameterRegistry.cpp
)
target_include_directories(BankAuditTest PRIVATE Source)
target_compile_definitions(BankAuditTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(BankAuditTest
    PRIVATE
        juce::juce_core
        juce::juce_graphics
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(TempoLinkTest PRODUCT_NAME "TempoLinkTest")
target_sources(TempoLinkTest
    PRIVATE
        Tests/TempoLinkTest.cpp
        Source/Arpeggiator.cpp
        Source/StepSequencer.cpp
        Source/ParameterRegistry.cpp
        Source/Scales.cpp
        Source/TempoSync.cpp
)
target_include_directories(TempoLinkTest PRIVATE Source)
target_compile_definitions(TempoLinkTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(TempoLinkTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

juce_add_console_app(ExpressionTest PRODUCT_NAME "ExpressionTest")
target_sources(ExpressionTest
    PRIVATE
        Tests/ExpressionTest.cpp
        Source/SynthVoice.cpp
        Source/WavetableOscillator.cpp
        Source/TempoSync.cpp
)
target_include_directories(ExpressionTest PRIVATE Source)
target_compile_definitions(ExpressionTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ExpressionTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(GlideTest PRODUCT_NAME "GlideTest")
target_sources(GlideTest
    PRIVATE
        Tests/GlideTest.cpp
        Source/SynthVoice.cpp
        Source/MonoVoice.cpp
        Source/WavetableOscillator.cpp
        Source/TempoSync.cpp
)
target_include_directories(GlideTest PRIVATE Source)
target_compile_definitions(GlideTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(GlideTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(MasterFxTest PRODUCT_NAME "MasterFxTest")
target_sources(MasterFxTest
    PRIVATE
        Tests/MasterFxTest.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/TempoSync.cpp
)
target_include_directories(MasterFxTest PRIVATE Source)
target_compile_definitions(MasterFxTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MasterFxTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(VerifyPreset PRODUCT_NAME "VerifyPreset")
target_sources(VerifyPreset PRIVATE Analysis/VerifyPreset.cpp Source/FxpPreset.cpp)
target_include_directories(VerifyPreset PRIVATE Source)
target_compile_definitions(VerifyPreset PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(VerifyPreset PRIVATE juce::juce_core juce::juce_recommended_config_flags)

juce_add_console_app(MatchTest PRODUCT_NAME "MatchTest")
target_sources(MatchTest
    PRIVATE
        Tests/MatchTest.cpp
        Source/SampleMatcher.cpp
        Source/FxpPreset.cpp
        Source/OfflineRenderer.cpp
        Source/PresetOptimizer.cpp
        Source/FactoryPresets.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/TempoSync.cpp
        Source/SynthVoice.cpp
        Source/WavetableOscillator.cpp
)
target_include_directories(MatchTest PRIVATE Source)
target_compile_definitions(MatchTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MatchTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

# The slow half of the match suite: sweeps and multi-seed comparisons that
# answer questions rather than guard behaviour. Same sources, separate binary,
# so the guards above stay quick enough to run after every change.
juce_add_console_app(MatchProbe PRODUCT_NAME "MatchProbe")
target_sources(MatchProbe
    PRIVATE
        Tests/MatchProbe.cpp
        Source/SampleMatcher.cpp
        Source/FxpPreset.cpp
        Source/OfflineRenderer.cpp
        Source/PresetOptimizer.cpp
        Source/FactoryPresets.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/TempoSync.cpp
        Source/SynthVoice.cpp
        Source/WavetableOscillator.cpp
)
target_include_directories(MatchProbe PRIVATE Source)
target_compile_definitions(MatchProbe PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MatchProbe
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(WavAnalyzer PRODUCT_NAME "WavAnalyzer")
target_sources(WavAnalyzer
    PRIVATE
        Analysis/WavAnalyzer.cpp
        Source/FxpPreset.cpp
        Source/SampleMatcher.cpp
        Source/OfflineRenderer.cpp
        Source/PresetOptimizer.cpp
        Source/FactoryPresets.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/TempoSync.cpp
        Source/SynthVoice.cpp
        Source/WavetableOscillator.cpp
)
target_include_directories(WavAnalyzer PRIVATE Source)
target_compile_definitions(WavAnalyzer PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(WavAnalyzer
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

# A tool, not a suite - named so run-tests.ps1 leaves it alone, the way it
# leaves WavAnalyzer, MatchProbe and VerifyPreset alone. DrumVoiceTest measures
# what the voices do; this is for listening to them.
juce_add_console_app(DrumRender PRODUCT_NAME "DrumRender")
target_sources(DrumRender
    PRIVATE
        Analysis/DrumRender.cpp
)
target_include_directories(DrumRender PRIVATE Source)
target_compile_definitions(DrumRender PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(DrumRender
    PRIVATE
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
)

juce_add_console_app(HostParameterTest PRODUCT_NAME "HostParameterTest")
target_sources(HostParameterTest
    PRIVATE
        Tests/HostParameterTest.cpp
        Source/HostParameters.cpp
        Source/ParameterRegistry.cpp
)
target_include_directories(HostParameterTest PRIVATE Source)
target_compile_definitions(HostParameterTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(HostParameterTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
)

juce_add_console_app(MasteringChainTest PRODUCT_NAME "MasteringChainTest")
target_sources(MasteringChainTest
    PRIVATE
        Tests/MasteringChainTest.cpp
)
# No Source/*.cpp listed, deliberately. This links the MasteringChain library
# and nothing else, so if the chain ever grows a dependency on the synth's own
# code this target stops linking - which is the cheapest possible alarm on the
# one property the library exists to have.
target_include_directories(MasteringChainTest PRIVATE Source)
target_compile_definitions(MasteringChainTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MasteringChainTest
    PRIVATE
        MasteringChain
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(DrumVoiceTest PRODUCT_NAME "DrumVoiceTest")
target_sources(DrumVoiceTest
    PRIVATE
        Tests/DrumVoiceTest.cpp
)
# No Source/*.cpp listed, for the same reason MasteringChainTest lists none: it
# links the DrumKit library and nothing else, so if the kit ever grows a
# dependency on the synth this target stops linking. Nor does it link juce_dsp,
# which is the other half of the claim.
target_include_directories(DrumVoiceTest PRIVATE Source)
target_compile_definitions(DrumVoiceTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(DrumVoiceTest
    PRIVATE
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
)

# The loader is decisions - what counts as silence, how much run-up to keep,
# what to do with a stereo file, when a recording is too long - and every one
# of them is testable without a synth, an audio device or a host. It links
# juce_audio_formats because it is the half that knows what a WAV is; the
# DrumKit library above deliberately does not.
# Points the loader at a real file and prints what it made of it. "Could not
# load that" is the one thing a user can report and the one thing that does
# not say what went wrong; this is how that gets answered without guessing.
#
# Named without "Test" so run-tests.ps1 leaves it alone - it takes an
# argument and has nothing to assert.
juce_add_console_app(SampleProbe PRODUCT_NAME "SampleProbe")
target_sources(SampleProbe
    PRIVATE
        Analysis/SampleProbe.cpp
        Source/DrumSampleLoader.cpp
)
target_include_directories(SampleProbe PRIVATE Source)
target_compile_definitions(SampleProbe PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(SampleProbe
    PRIVATE
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
)

juce_add_console_app(DrumSampleTest PRODUCT_NAME "DrumSampleTest")
target_sources(DrumSampleTest
    PRIVATE
        Tests/DrumSampleTest.cpp
        Source/DrumSampleLoader.cpp
)
target_include_directories(DrumSampleTest PRIVATE Source)
target_compile_definitions(DrumSampleTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(DrumSampleTest
    PRIVATE
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
)

juce_add_console_app(MasterPluginTest PRODUCT_NAME "MasterPluginTest")
target_sources(MasterPluginTest
    PRIVATE
        Tests/MasterPluginTest.cpp
        Source/MasterPlugin/MasterPluginProcessor.cpp
        Source/MasterPlugin/MasterPluginEditor.cpp
        Source/MasteringPanel.cpp
        Source/UiComponents.cpp
)
# The editor and the panel come along because the processor's createEditor
# names them. Nothing here opens a window - a console app that linked only the
# processor would fail at the link rather than at the point anybody cared.
target_include_directories(MasterPluginTest PRIVATE Source)
target_compile_definitions(MasterPluginTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MasterPluginTest
    PRIVATE
        MasteringChain
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
)

juce_add_console_app(ParameterMaskTest PRODUCT_NAME "ParameterMaskTest")
target_sources(ParameterMaskTest
    PRIVATE
        Tests/ParameterMaskTest.cpp
        Source/ParameterRegistry.cpp
)
target_include_directories(ParameterMaskTest PRIVATE Source)
target_compile_definitions(ParameterMaskTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ParameterMaskTest
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
)

# The main processor, under test for the first time.
#
# This is the largest target in the file and it is worth knowing why it has to
# be. Source/PluginProcessor.cpp was named by exactly one target - the plugin -
# so the suite could run seventeen suites green without ever compiling it.
# processBlock, the host-write drain and the controller hold countdown had no
# coverage at all, which was discovered while widening the host-write mask: the
# failure it would have hidden is a host writing parameter 64 and the hold
# landing on parameter 0, silently.
#
# Everything below is here because the processor's own sources need it. The
# editor and the panels come along because createEditor names them - nothing
# here opens a window, but a console app that linked only the processor would
# fail at the link rather than at the point anybody cared.
juce_add_console_app(ProcessorTest PRODUCT_NAME "ProcessorTest")
target_sources(ProcessorTest
    PRIVATE
        Tests/ProcessorTest.cpp
        Source/PluginProcessor.cpp
        Source/DrumSampleLoader.cpp
        Source/PluginEditor.cpp
        Source/WavetableOscillator.cpp
        Source/SynthVoice.cpp
        Source/FxpPreset.cpp
        Source/SampleMatcher.cpp
        Source/OfflineRenderer.cpp
        Source/PresetOptimizer.cpp
        Source/MasterEffects.cpp
        Source/TempoSync.cpp
        Source/Arpeggiator.cpp
        Source/AppSettings.cpp
        Source/ParameterRegistry.cpp
        Source/MidiMap.cpp
        Source/MidiMonitor.cpp
        Source/HostParameters.cpp
        Source/MidiMapView.cpp
        Source/OutputTap.cpp
        Source/OutputScope.cpp
        Source/SpectrumAnalyser.cpp
        Source/StepSequencer.cpp
        Source/NoteLatch.cpp
        Source/UndoHistory.cpp
        Source/MonoVoice.cpp
        Source/SequencerPanel.cpp
        Source/DrumPanel.cpp
        Source/MasteringPanel.cpp
        Source/BusEffectsPanel.cpp
        Source/Scales.cpp
        Source/HarmonyProcessor.cpp
        Source/FactoryPresets.cpp
        Source/PresetBrowser.cpp
        Source/SampleBrowser.cpp
        Source/UiComponents.cpp
        Source/WaveformDisplay.cpp
)
target_include_directories(ProcessorTest PRIVATE Source)
target_compile_definitions(ProcessorTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ProcessorTest
    PRIVATE
        MasteringChain
        DrumKit
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
)
