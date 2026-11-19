#include "../Source/CommandDispatcher.h"
#include <iostream>

int main()
{
    CommandDispatcher dispatcher;
    int applied = 0;
    const auto send = [&](const char* json)
    {
        return dispatcher.dispatch(juce::JSON::parse(json), [&](const juce::var&) {
            ++applied;
            return juce::String{};
        });
    };
    auto first = send(R"({"op":"set_cell","bar":1,"enabled":true,"track_id":"clean_kick","command_id":"one","expected_revision":0})");
    auto replay = send(R"({"command_id":"one","op":"set_cell","expected_revision":0,"track_id":"clean_kick","bar":1,"enabled":true})");
    if (!bool(first["accepted"]) || !bool(replay["accepted"]) || int(replay["revision"]) != 1 || applied != 1) return 1;
    auto collision = send(R"({"op":"stop","expected_revision":0,"command_id":"one"})");
    auto stale = send(R"({"op":"play","expected_revision":0,"command_id":"two"})");
    auto invalid = send(R"({"op":"set_echo","enabled":"true","expected_revision":1,"command_id":"three","track_id":"bunker_rimshot"})");
    if (bool(collision["accepted"]) || bool(stale["accepted"]) || bool(invalid["accepted"]) || applied != 1) return 2;
    dispatcher.localEdit();
    auto status = send(R"({"op":"status"})");
    auto stop = send(R"({"op":"stop","expected_revision":0,"command_id":"four"})");
    if (int(status["revision"]) != 2 || !bool(stop["accepted"]) || int(stop["revision"]) != 3 || applied != 2) return 3;
    std::cout << "Command dispatcher scenarios passed.\n";
}
