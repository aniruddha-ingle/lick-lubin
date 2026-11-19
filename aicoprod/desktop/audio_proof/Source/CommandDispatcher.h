#pragma once
#include <juce_core/juce_core.h>
#include <deque>
#include <functional>

class CommandDispatcher {
public:
    void localEdit() { ++revision; }
    juce::var dispatch(const juce::var& r, std::function<juce::String(const juce::var&)> apply) {
        if (!r.isObject()) return reply(false, "Expected a JSON object.");
        const auto op = r["op"].toString();
        if (op == "status") return reply(true, {});
        const auto id = r["command_id"].toString();
        if (!r["command_id"].isString() || id.trim().isEmpty() || id.length() > 128)
            return reply(false, "Invalid command ID.");
        // The wire grammar is flat. Ignore JSON object field order on retry.
        juce::StringArray names;
        for (const auto& field : r.getDynamicObject()->getProperties()) names.add(field.name.toString());
        names.sort(false);
        auto sorted = std::make_unique<juce::DynamicObject>();
        for (const auto& name : names) sorted->setProperty(name, r[juce::Identifier(name)]);
        const auto body = juce::JSON::toString(juce::var(sorted.release()), true);
        for (const auto& entry : recent)
            if (entry.id == id) return entry.body == body ? entry.result : reply(false, "Command ID reused.");
        auto reason = validate(r, op);
        if (reason.isEmpty()) reason = apply(r);
        if (reason.isEmpty()) localEdit();
        auto result = reply(reason.isEmpty(), reason);
        recent.push_back({id, body, result});
        if (recent.size() > 256) recent.pop_front();
        return result;
    }
private:
    juce::String validate(const juce::var& r, const juce::String& op) const {
        juce::StringArray allowed {"op", "command_id", "expected_revision"};
        if (op == "set_sidechain") allowed.addArray({"source_id", "target_id", "enabled"});
        else if (op == "set_reverb") allowed.addArray({"track_id", "mode"});
        else if (op == "set_cell") allowed.addArray({"track_id", "bar", "enabled"});
        else if (op == "set_echo" || op == "set_cut_itself") allowed.addArray({"track_id", "enabled"});
        for (const auto& field : r.getDynamicObject()->getProperties())
            if (!allowed.contains(field.name.toString())) return "Unexpected command field.";
        const auto expected = r["expected_revision"];
        if ((!expected.isInt64() && !expected.isInt()) || static_cast<juce::int64>(expected) < 0)
            return "Invalid expected revision.";
        if (op != "stop" && static_cast<juce::int64>(expected) != revision) return "Stale revision.";
        if (op == "play" || op == "stop") return {};
        if (op == "set_sidechain") {
            if (!r["source_id"].isString() || !r["target_id"].isString() || r["source_id"] == r["target_id"]
                || !r["enabled"].isBool()) return "Invalid sidechain route.";
            return {};
        }
        if (!r["track_id"].isString()) return "Missing track ID.";
        if (op == "set_reverb") {
            const auto mode = r["mode"].toString();
            if (mode == "off" || mode == "room" || mode == "hall") return {};
            return "Unknown reverb mode.";
        }
        if (!r["enabled"].isBool()) return "Expected a boolean enabled field.";
        if (op == "set_cut_itself" || op == "set_echo") return {};
        if (op == "set_cell" && r["bar"].isInt() && int(r["bar"]) >= 1 && int(r["bar"]) <= 16) return {};
        return "Unknown command or invalid bar.";
    }
    juce::var reply(bool ok, const juce::String& reason) const {
        auto* object = new juce::DynamicObject();
        object->setProperty("accepted", ok);
        object->setProperty("revision", revision);
        if (reason.isNotEmpty()) object->setProperty("reason", reason);
        return juce::var(object);
    }
    struct Entry { juce::String id, body; juce::var result; };
    std::deque<Entry> recent;
    juce::int64 revision = 0;
};
