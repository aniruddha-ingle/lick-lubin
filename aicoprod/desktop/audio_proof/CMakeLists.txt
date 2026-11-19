cmake_minimum_required(VERSION 3.22)

project(LickLubinAudioProof VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(OrchidManifestData
    NAMESPACE BinaryData
    SOURCES manifest.json
)

juce_add_gui_app(LickLubinAudioProof
    PRODUCT_NAME "Lick-Lubin Audio Proof"
    COMPANY_NAME "Lick-Lubin"
    BUNDLE_ID "dev.licklubin.audio-proof"
)

target_sources(LickLubinAudioProof
    PRIVATE
        Source/Main.cpp
        Source/SampleBank.cpp
        Source/TrackEffects.cpp
)

target_compile_features(LickLubinAudioProof PRIVATE cxx_std_17)

target_compile_definitions(LickLubinAudioProof
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(LickLubinAudioProof
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
        OrchidManifestData
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if (BUILD_TESTING)
    add_executable(ArrangementModelTests Tests/ArrangementModelTests.cpp)
    target_compile_features(ArrangementModelTests PRIVATE cxx_std_17)
    add_test(NAME ArrangementModelBehavior COMMAND ArrangementModelTests)

    add_executable(ArrangementTransportTests Tests/ArrangementTransportTests.cpp)
    target_compile_features(ArrangementTransportTests PRIVATE cxx_std_17)
    add_test(NAME ArrangementTransportBehavior COMMAND ArrangementTransportTests)

    add_executable(SampleBankTests Tests/SampleBankTests.cpp Source/SampleBank.cpp)
    target_compile_features(SampleBankTests PRIVATE cxx_std_17)
    target_link_libraries(SampleBankTests PRIVATE juce::juce_audio_formats OrchidManifestData)
    add_test(NAME SampleBankBehavior COMMAND SampleBankTests)

    add_executable(TrackEffectsTests Tests/TrackEffectsTests.cpp Source/TrackEffects.cpp)
    target_compile_features(TrackEffectsTests PRIVATE cxx_std_17)
    target_link_libraries(TrackEffectsTests PRIVATE juce::juce_audio_basics)
    add_test(NAME TrackEffectsBehavior COMMAND TrackEffectsTests)

    add_executable(CommandDispatcherTests Tests/CommandDispatcherTests.cpp)
    target_compile_features(CommandDispatcherTests PRIVATE cxx_std_17)
    target_link_libraries(CommandDispatcherTests PRIVATE juce::juce_core)
    add_test(NAME CommandDispatcherBehavior COMMAND CommandDispatcherTests)
endif()
