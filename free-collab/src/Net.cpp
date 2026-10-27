// Implementacion de red con Winsock (solo Windows).
// IMPORTANTE: este archivo no incluye headers de Geode.

#include <winsock2.h>
#include <ws2tcpip.h>

#include "Net.hpp"

#include <condition_variable>
#include <deque>
#include <thread>

struct NetConn {
    std::atomic<bool> stop{false};
    std::atomic<bool> up{false};
    std::mutex m;                 // protege out y sock
    std::condition_variable cv;
    std::deque<std::string> out;
    SOCKET sock = INVALID_SOCKET;
};

Net& Net::get() {
    static Net instance;
    return instance;
}

static void ensureWinsock() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}

static void closeSock(NetConn& c) {
    std::lock_guard lock(c.m);
    if (c.sock != INVALID_SOCKET) {
        shutdown(c.sock, SD_BOTH);
        closesocket(c.sock);
        c.sock = INVALID_SOCKET;
    }
}

static void writerLoop(std::shared_ptr<NetConn> c) {
    while (true) {
        std::string line;
        SOCKET s;
        {
            std::unique_lock lock(c->m);
            c->cv.wait(lock, [&] { return c->stop || !c->out.empty(); });
            if (c->stop) return;
            line = std::move(c->out.front());
            c->out.pop_front();
            s = c->sock;
        }
        if (s == INVALID_SOCKET) return;
        line.push_back('\n');
        size_t sent = 0;
        while (sent < line.size()) {
            int n = ::send(s, line.data() + sent, static_cast<int>(line.size() - sent), 0);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }
}

static void runConn(std::shared_ptr<NetConn> c, std::string host, int port) {
    ensureWinsock();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
        Net::get().push(c.get(), "@FAIL No se encontro el servidor " + host);
        return;
    }

    SOCKET s = INVALID_SOCKET;
    for (auto* p = result; p && !c->stop; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (s == INVALID_SOCKET || c->stop) {
        if (s != INVALID_SOCKET) closesocket(s);
        Net::get().push(c.get(), "@FAIL No se pudo conectar al servidor");
        return;
    }

    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char const*>(&yes), sizeof(yes));

    {
        std::lock_guard lock(c->m);
        c->sock = s;
    }
    c->up = true;
    std::thread(writerLoop, c).detach();
    Net::get().push(c.get(), "@UP");

    std::string buffer;
    std::vector<char> chunk(64 * 1024);
    while (!c->stop) {
        int n = recv(s, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (n <= 0) break;
        buffer.append(chunk.data(), static_cast<size_t>(n));
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) Net::get().push(c.get(), std::move(line));
        }
    }

    c->up = false;
    bool wasStopped = c->stop;
    c->stop = true;
    c->cv.notify_all();
    closeSock(*c);
    if (!wasStopped) Net::get().push(c.get(), "@DOWN");
}

void Net::connectTo(std::string host, int port) {
    disconnect();
    auto conn = std::make_shared<NetConn>();
    {
        std::lock_guard lock(m_mutex);
        m_conn = conn;
        m_in.clear();
    }
    std::thread(runConn, conn, std::move(host), port).detach();
}

void Net::disconnect() {
    std::shared_ptr<NetConn> old;
    {
        std::lock_guard lock(m_mutex);
        old = std::move(m_conn);
        m_conn.reset();
        m_in.clear();
    }
    if (old) {
        old->stop = true;
        old->cv.notify_all();
        closeSock(*old);
    }
}

bool Net::connected() const {
    auto self = const_cast<Net*>(this);
    std::lock_guard lock(self->m_mutex);
    return m_conn && m_conn->up && !m_conn->stop;
}

bool Net::connecting() const {
    auto self = const_cast<Net*>(this);
    std::lock_guard lock(self->m_mutex);
    return m_conn && !m_conn->up && !m_conn->stop;
}

void Net::send(std::string const& line) {
    std::shared_ptr<NetConn> c;
    {
        std::lock_guard lock(m_mutex);
        c = m_conn;
    }
    if (!c || c->stop) return;
    {
        std::lock_guard lock(c->m);
        c->out.push_back(line);
    }
    c->cv.notify_one();
}

std::vector<std::string> Net::poll() {
    std::vector<std::string> out;
    std::lock_guard lock(m_mutex);
    out.swap(m_in);
    return out;
}

void Net::push(NetConn* from, std::string line) {
    std::lock_guard lock(m_mutex);
    // ignora mensajes de una conexion vieja
    if (!m_conn || m_conn.get() != from) return;
    m_in.push_back(std::move(line));
}
