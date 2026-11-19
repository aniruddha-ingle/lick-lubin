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
endif()
