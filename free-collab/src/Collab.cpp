#include "Collab.hpp"
#include "Net.hpp"

#include <algorithm>
#include <random>

using namespace geode::prelude;

namespace {
    // Los espacios no pueden viajar dentro de un campo del protocolo.
    std::string enc(std::string s) {
        for (auto& c : s) if (c == ' ') c = '\x01';
        return s;
    }
    std::string dec(std::string s) {
        for (auto& c : s) if (c == '\x01') c = ' ';
        return s;
    }

    std::string clean(std::string const& s) {
        std::string out;
        for (char c : s) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                out.push_back(c);
            }
        }
        if (out.size() > 24) out.resize(24);
        return out;
    }

    std::pair<std::string, std::string> splitFirst(std::string const& s) {
        auto p = s.find(' ');
        if (p == std::string::npos) return {s, ""};
        return {s.substr(0, p), s.substr(p + 1)};
    }

    std::vector<std::string> words(std::string const& s) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && s[i] == ' ') i++;
            size_t j = i;
            while (j < s.size() && s[j] != ' ') j++;
            if (j > i) out.push_back(s.substr(i, j - i));
            i = j;
        }
        return out;
    }
}

Collab& Collab::get() {
    static Collab instance;
    return instance;
}

void Collab::readSettings() {
    auto mod = Mod::get();
    m_host = mod->getSettingValue<std::string>("server-host");
    m_port = static_cast<int>(mod->getSettingValue<int64_t>("server-port"));
    m_name = clean(mod->getSettingValue<std::string>("username"));
    if (m_name.empty()) m_name = "jugador";
    if (m_name == "jugador") {
        // el servidor no deja nombres repetidos: si no pusiste el tuyo, te damos uno unico
        static std::string suffix = std::to_string(100 + std::random_device{}() % 900);
        m_name += suffix;
    }
    m_autoConnect = mod->getSettingValue<bool>("auto-connect");
}

void Collab::start() {
    if (m_started) return;
    m_started = true;
    readSettings();
    if (m_autoConnect) Net::get().connectTo(m_host, m_port);
}

void Collab::reconnect() {
    readSettings();
    m_failNotified = false;
    m_retryTimer = 0.f;
    Net::get().connectTo(m_host, m_port);
    m_version++;
}

void Collab::notify(std::string const& text, NotificationIcon icon) {
    Notification::create(text, icon)->show();
}

void Collab::pump(float dt) {
    for (auto const& line : Net::get().poll()) {
        this->handleLine(line);
    }

    // reconexion automatica cada 10 segundos
    if (m_autoConnect && !Net::get().connected() && !Net::get().connecting()) {
        m_retryTimer += dt;
        if (m_retryTimer >= 10.f) {
            m_retryTimer = 0.f;
            this->readSettings();
            Net::get().connectTo(m_host, m_port);
        }
    } else {
        m_retryTimer = 0.f;
    }

    // si ya no hay editor en pantalla (con un margen por las transiciones), salimos de la sala
    if (m_editorLive) {
        auto scene = CCDirector::get()->getRunningScene();
        bool present = scene && scene->getChildByType<LevelEditorLayer>(0) != nullptr;
        if (present) {
            m_editorAbsent = 0.f;
        } else {
            m_editorAbsent += dt;
            if (m_editorAbsent > 1.5f) {
                m_editorLive = false;
                m_editorAbsent = 0.f;
                if (m_inRoom) this->leaveRoom();
            }
        }
    }
}

void Collab::handleLine(std::string const& line) {
    auto [cmd, rest] = splitFirst(line);

    if (cmd == "@UP") {
        m_failNotified = false;
        Net::get().send("HELLO " + m_name);
    }
    else if (cmd == "@DOWN") {
        bool was = m_online;
        m_online = false;
        m_users.clear();
        m_version++;
        if (m_inRoom && m_editorLive) {
            m_resumeRoom = m_room; // al reconectar volvemos a la sala
        } else if (m_inRoom) {
            m_inRoom = false;
            m_room.clear();
            m_members.clear();
            m_hostLevel = nullptr;
            m_joinLevel = nullptr;
        }
        if (was) this->notify("Free Collab: se perdio la conexion", NotificationIcon::Warning);
    }
    else if (cmd == "@FAIL") {
        m_online = false;
        m_version++;
        if (!m_failNotified) {
            m_failNotified = true;
            this->notify("Free Collab: " + rest, NotificationIcon::Warning);
        }
    }
    else if (cmd == "HELLOOK") {
        m_online = true;
        m_version++;
        this->notify("Free Collab: conectado como " + m_name, NotificationIcon::Success);
        if (!m_resumeRoom.empty()) {
            m_resyncing = true;
            Net::get().send("JOIN " + m_resumeRoom);
            m_resumeRoom.clear();
        }
    }
    else if (cmd == "USERS") {
        m_users.clear();
        for (auto const& w : words(rest)) if (w != m_name) m_users.push_back(w);
        m_version++;
    }
    else if (cmd == "USERJOIN") {
        if (rest != m_name && std::find(m_users.begin(), m_users.end(), rest) == m_users.end()) {
            m_users.push_back(rest);
            m_version++;
        }
    }
    else if (cmd == "USERLEAVE") {
        m_users.erase(std::remove(m_users.begin(), m_users.end(), rest), m_users.end());
        m_version++;
    }
    else if (cmd == "INFO") {
        this->notify(rest, NotificationIcon::Info);
    }
    else if (cmd == "ERR") {
        if (m_resyncing) {
            m_resyncing = false;
            m_inRoom = false;
            m_room.clear();
            m_members.clear();
        }
        m_hasPending = false;
        m_lastRoom.clear();
        m_version++;
        this->notify(rest, NotificationIcon::Error);
    }
    else if (cmd == "INVITE") {
        auto [from, r1] = splitFirst(rest);
        auto [room, levelName] = splitFirst(r1);
        this->askInvite(from, room, levelName);
    }
    else if (cmd == "ROOMOK") {
        m_inRoom = true;
        m_room = rest;
        m_lastRoom = rest;
        m_version++;
    }
    else if (cmd == "MEMBERS") {
        auto [room, names] = splitFirst(rest);
        m_members = words(names);
        m_version++;
    }
    else if (cmd == "STATE") {
        if (!m_resyncing) {
            auto [room, r1] = splitFirst(rest);
            auto [levelName, base] = splitFirst(r1);
            m_hasPending = true;
            m_pending = Pending{room, levelName, base, {}};
        }
    }
    else if (cmd == "REPLAY") {
        if (m_resyncing) m_ops.push_back({"servidor", rest});
        else if (m_hasPending) m_pending.replay.push_back(rest);
    }
    else if (cmd == "READY") {
        if (m_resyncing) {
            m_resyncing = false;
            m_inRoom = true;
            m_room = rest;
            m_version++;
        } else if (m_hasPending) {
            m_hasPending = false;
            this->openJoinedLevel();
        }
    }
    else if (cmd == "OP") {
        if (m_inRoom) {
            auto [from, payload] = splitFirst(rest);
            m_ops.push_back({from, payload});
            if (m_ops.size() > 100000) m_ops.pop_front();
        }
    }
}

void Collab::askInvite(std::string const& from, std::string const& room, std::string const& levelName) {
    std::string lvl = dec(levelName);
    createQuickPopup(
        "Invitacion",
        from + " te invita a editar en equipo el nivel:\n<cy>" + lvl + "</c>",
        "Ahora no", "Entrar",
        [this, room](FLAlertLayer*, bool accepted) {
            if (accepted) this->joinRoom(room);
        }
    );
}

void Collab::openJoinedLevel() {
    // Copia local del nivel de la sala. Se guarda en "Mis niveles" con [collab] al final.
    auto level = GJGameLevel::create();
    level->m_levelName = dec(m_pending.levelName) + " [collab]";
    level->m_levelString = m_pending.base;
    level->m_levelType = GJLevelType::Editor;
    level->m_isEditable = true;

    if (auto mgr = LocalLevelManager::get()) {
        if (mgr->m_localLevels) mgr->m_localLevels->insertObject(level, 0);
    }

    m_joinLevel = level;
    m_hostLevel = nullptr;
    m_inRoom = true;
    m_room = m_pending.room;
    m_lastRoom = m_room;
    m_replayForEditor = std::move(m_pending.replay);
    m_version++;

    CCDirector::get()->replaceScene(LevelEditorLayer::scene(level, false));
}

bool Collab::hostLevel(GJGameLevel* level) {
    if (!level) return false;
    if (!m_online) {
        this->notify("No estas conectado al servidor", NotificationIcon::Warning);
        return false;
    }
    if (m_inRoom && m_hostLevel.data() == level) return true;
    if (m_inRoom) this->leaveRoom();

    std::string levelName = level->m_levelName;
    std::string base = level->m_levelString;

    std::random_device rd;
    std::string suffix = std::to_string(1000 + rd() % 9000);
    m_room = clean(m_name) + "-" + clean(levelName) + "-" + suffix;
    m_lastRoom = m_room;
    m_hostLevel = level;
    m_joinLevel = nullptr;
    m_inRoom = true;
    m_members = {m_name};
    m_version++;

    Net::get().send("OPEN " + m_room + " " + enc(levelName) + " " + base);
    return true;
}

void Collab::invite(std::string const& user) {
    if (!m_inRoom || !m_online) return;
    Net::get().send("INVITE " + user + " " + m_room);
}

void Collab::joinRoom(std::string const& room) {
    if (!m_online) {
        this->notify("No estas conectado al servidor", NotificationIcon::Warning);
        return;
    }
    if (m_inRoom) this->leaveRoom();
    m_hasPending = false;
    Net::get().send("JOIN " + room);
}

void Collab::leaveRoom() {
    if (m_inRoom) {
        Net::get().send("LEAVE");
        m_lastRoom = m_room;
    }
    m_inRoom = false;
    m_room.clear();
    m_members.clear();
    m_hostLevel = nullptr;
    m_joinLevel = nullptr;
    m_hasPending = false;
    m_resyncing = false;
    m_resumeRoom.clear();
    m_replayForEditor.clear();
    m_ops.clear();
    m_version++;
}

bool Collab::attachEditor(GJGameLevel* level, std::vector<std::string>& replay) {
    if (!m_inRoom) return false;
    if (level != m_hostLevel.data() && level != m_joinLevel.data()) return false;
    replay = std::move(m_replayForEditor);
    m_replayForEditor.clear();
    m_editorLive = true;
    m_editorAbsent = 0.f;
    return true;
}

void Collab::sendOp(std::string const& payload) {
    if (!m_inRoom || !m_online) return;
    Net::get().send("OP " + payload);
}

std::vector<RemoteOp> Collab::takeOps() {
    std::vector<RemoteOp> out(m_ops.begin(), m_ops.end());
    m_ops.clear();
    return out;
}
