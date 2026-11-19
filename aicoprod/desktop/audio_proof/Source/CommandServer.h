#pragma once
#include <juce_events/juce_events.h>
#include <array>
#include <functional>
#include <string_view>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <ctime>

// Bounded nonblocking work on the message thread keeps command application serial
// with mouse edits. Each connection carries one newline-delimited JSON request.
class CommandServer final : private juce::Timer {
public:
    using Handler = std::function<juce::var(const juce::var&)>;
    explicit CommandServer(Handler fn)
        : CommandServer(std::move(fn), juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                          .getChildFile(".lick-lubin")) {}
    CommandServer(Handler fn, const juce::File& directory)
        : handle(std::move(fn)), dir(directory.getFullPathName().toStdString()) {}
    ~CommandServer() override {
        stopTimer();
        for (auto& client : clients) closeClient(client);
        if (listener >= 0) ::close(listener);
        if (ownsPath) ::unlink(path.c_str());
        if (lockFd >= 0) ::close(lockFd);
    }
    bool start() {
        if (::mkdir(dir.c_str(), 0700) && errno != EEXIST) return false;
        struct stat st{};
        if (lstat(dir.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()
            || (st.st_mode & 0077)) return false;
        lockFd = ::open((dir + "/control.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB)) return false;
        path = dir + "/control.sock";
        sockaddr_un addr{};
        if (path.size() >= sizeof(addr.sun_path)) return false;
        if (lstat(path.c_str(), &st) == 0 && (!S_ISSOCK(st.st_mode) || ::unlink(path.c_str()))) return false;
        listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listener < 0 || !configure(listener)) return false;
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.c_str(), path.size()+1);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) return false;
        ownsPath = true;
        if (::chmod(path.c_str(), 0600) || ::listen(listener, 8)) return false;
        startTimer(10);
        return true;
    }
private:
    struct Client { int fd = -1; std::string input, output; double deadline = 0; };
    static bool configure(int fd) {
        int on = 1;
        return fcntl(fd, F_SETFL, O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0
            && setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == 0;
    }
    static void closeClient(Client& c) { if (c.fd >= 0) ::close(c.fd); c = {}; }
    static bool fatal() { return errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR; }
    static juce::var parseRequest(std::string_view line) {
        const auto first = line.find_first_not_of(" \t\r");
        const auto last = line.find_last_not_of(" \t\r");
        if (first == std::string_view::npos || line[first] != '{' || line[last] != '}'
            || line.find('\0') != std::string_view::npos
            || !juce::CharPointer_UTF8::isValidString(line.data(), static_cast<int>(line.size()))) return {};

        // JUCE parses a value prefix. Require the first complete object to fill the frame.
        int depth = 0;
        bool quoted = false, escaped = false;
        for (auto pos = first; pos <= last; ++pos) {
            const auto c = line[pos];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') quoted = true;
            else if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && --depth == 0 && pos != last) return {};
        }
        return juce::JSON::parse(juce::String::fromUTF8(line.data(), static_cast<int>(line.size())));
    }
    void timerCallback() override {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (auto& c : clients) {
            if (c.fd < 0) {
                c.fd = ::accept(listener, nullptr, nullptr);
                if (c.fd < 0) continue;
                if (!configure(c.fd)) { closeClient(c); continue; }
                c.deadline = now + 3000;
            }
            if (now > c.deadline) { closeClient(c); continue; }
            if (c.output.empty()) {
                char buf[4096];
                auto n = ::recv(c.fd, buf, sizeof(buf), 0);
                if (n <= 0) { if (n == 0 || fatal()) closeClient(c); continue; }
                c.input.append(buf, static_cast<size_t>(n));
                if (c.input.size() > 8192) { closeClient(c); continue; }
                auto end = c.input.find('\n');
                if (end == std::string::npos) continue;
                auto req = parseRequest(std::string_view(c.input).substr(0, end));
                c.output = juce::JSON::toString(handle(req), true).toStdString() + "\n";
            }
            auto n = ::send(c.fd, c.output.data(), c.output.size(), 0);
            if (n < 0) { if (fatal()) closeClient(c); continue; }
            c.output.erase(0, static_cast<size_t>(n));
            if (c.output.empty()) closeClient(c);
        }
    }
    Handler handle;
    std::array<Client, 8> clients;
    const std::string dir;
    std::string path;
    int lockFd = -1, listener = -1;
    bool ownsPath = false;
};
