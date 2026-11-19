#include "../Source/CommandDispatcher.h"
#include "../Source/CommandServer.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct TemporaryDirectory
{
    TemporaryDirectory()
    {
        char name[] = "/tmp/lick-lubin-control-XXXXXX";
        const auto* path = ::mkdtemp(name);
        require(path != nullptr, "could not create temporary test directory");
        folder = juce::File(path);
    }

    ~TemporaryDirectory() { folder.deleteRecursively(); }

    juce::File folder;
};

struct ClientSocket
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

    ~ClientSocket() { if (fd >= 0) ::close(fd); }

    void connect(const juce::File& socketPath) const
    {
        require(fd >= 0, "could not create client socket");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const auto path = socketPath.getFullPathName().toStdString();
        require(path.size() < sizeof(address.sun_path), "test socket path is too long");
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        require(::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
                "could not connect to server");
        int on = 1;
        require(::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == 0,
                "could not configure client socket");
    }

    void send(std::string_view chunk) const
    {
        while (!chunk.empty())
        {
            const auto n = ::send(fd, chunk.data(), chunk.size(), 0);
            if (n < 0 && errno == EINTR)
                continue;
            require(n > 0, "could not send request chunk");
            chunk.remove_prefix(static_cast<std::size_t>(n));
        }
    }

    juce::var readReply(juce::MessageManager& messages) const
    {
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(2);
        std::string frame;
        while (steady_clock::now() < deadline)
        {
            require(messages.runDispatchLoopUntil(10), "JUCE message loop stopped");
            char bytes[1024];
            const auto n = ::recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                continue;
            if (n == 0)
            {
                require(!frame.empty() && frame.back() == '\n', "server did not reply with a complete frame");
                const auto reply = juce::JSON::parse(juce::String::fromUTF8(frame.data(), static_cast<int>(frame.size())));
                require(reply.isObject(), "server reply must be a JSON object");
                return reply;
            }
            require(n > 0, "could not read server reply");
            frame.append(bytes, static_cast<std::size_t>(n));
        }
        throw std::runtime_error("server reply timed out");
    }
};

juce::var request(juce::MessageManager& messages, const juce::File& socket, std::string_view frame)
{
    ClientSocket client;
    client.connect(socket);
    client.send(frame);
    return client.readReply(messages);
}

mode_t modeOf(const juce::File& file)
{
    struct stat info{};
    require(::lstat(file.getFullPathName().toRawUTF8(), &info) == 0, "expected socket or directory to exist");
    require(info.st_uid == ::getuid(), "socket and directory must be owned by the current user");
    return info.st_mode;
}

void exercisesSocketProtocolAndOwnership(juce::MessageManager& messages)
{
    const TemporaryDirectory temporary;
    const auto directory = temporary.folder.getChildFile("private-control");
    const auto socket = directory.getChildFile("control.sock");
    const auto lock = directory.getChildFile("control.lock");
    CommandDispatcher dispatcher;
    int applied = 0;
    const auto handle = [&](const juce::var& value)
    {
        return dispatcher.dispatch(value, [&](const juce::var&)
        {
            ++applied;
            return juce::String();
        });
    };

    {
        CommandServer server(handle, directory);
        require(server.start(), "first listener must start");
        const auto directoryMode = modeOf(directory);
        require(S_ISDIR(directoryMode) && (directoryMode & 0777) == 0700, "control directory must be mode 0700");
        const auto socketMode = modeOf(socket);
        require(S_ISSOCK(socketMode) && (socketMode & 0777) == 0600, "control socket must be mode 0600");
        const auto lockMode = modeOf(lock);
        require(S_ISREG(lockMode) && (lockMode & 0777) == 0600, "listener lock must be mode 0600");

        {
            CommandServer competitor(handle, directory);
            require(!competitor.start(), "second listener must not take over the live socket");
        }

        ClientSocket partial;
        partial.connect(socket);
        partial.send("{\"op\":\"play\",\"command_id\":\"split-}");
        const auto status = request(messages, socket, "{\"op\":\"status\"}\n");
        require(bool(status["accepted"]) && int(status["revision"]) == 0 && applied == 0,
                "partial input must wait for the rest while another client gets a reply");

        partial.send("\\\"\",\"expected_revision\":0}");
        const auto beforeNewline = request(messages, socket, "{\"op\":\"status\"}\n");
        require(bool(beforeNewline["accepted"]) && int(beforeNewline["revision"]) == 0 && applied == 0,
                "a complete value must still wait for the newline delimiter");
        partial.send("\n");
        const auto accepted = partial.readReply(messages);
        require(bool(accepted["accepted"]) && int(accepted["revision"]) == 1 && applied == 1,
                "completed request must apply exactly once");

        for (const std::string_view bad : { "{\"op\": broken}\n", "{\"op\":\"status\"}junk{}\n" })
        {
            const auto rejected = request(messages, socket, bad);
            require(!bool(rejected["accepted"]) && int(rejected["revision"]) == 1
                    && rejected["reason"].toString().isNotEmpty() && applied == 1,
                    "malformed request must return a rejection without changing state");
        }
    }

    struct stat info{};
    require(::lstat(socket.getFullPathName().toRawUTF8(), &info) != 0 && errno == ENOENT,
            "destroying the listener must remove its socket");
    CommandServer replacement(handle, directory);
    require(replacement.start(), "a replacement must be able to take the released listener lock");
    const auto status = request(messages, socket, "{\"op\":\"status\"}\n");
    require(bool(status["accepted"]), "replacement listener must respond on the same path");
}
}

int main()
{
    try
    {
        const juce::ScopedJuceInitialiser_GUI initialiseJuce;
        exercisesSocketProtocolAndOwnership(*juce::MessageManager::getInstance());
    }
    catch (const std::exception& error)
    {
        std::cerr << "Command server scenario failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Command server integration scenario passed.\n";
    return 0;
}
