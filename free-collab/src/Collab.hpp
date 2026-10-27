#pragma once

#include <Geode/Geode.hpp>

#include <deque>
#include <string>
#include <vector>

// Un cambio hecho por otra persona, listo para aplicarse en el editor.
struct RemoteOp {
    std::string from;
    std::string payload; // "SET gid objeto" | "DEL gid" | "CUR x y"
};

// Estado global de Free Collab: conexion, lista de conectados, invitaciones y sala.
class Collab {
public:
    static Collab& get();

    // Se llama una vez al abrir el juego y luego cada 0.05 s.
    void start();
    void pump(float dt);

    // --- conexion ---
    bool online() const { return m_online; }
    std::string const& myName() const { return m_name; }
    std::vector<std::string> const& users() const { return m_users; }
    void reconnect();

    // Sube cada vez que cambia algo que la interfaz deberia redibujar.
    unsigned version() const { return m_version; }

    // --- sala ---
    bool inRoom() const { return m_inRoom; }
    std::string const& room() const { return m_room; }
    std::vector<std::string> const& members() const { return m_members; }
    std::string const& lastRoom() const { return m_lastRoom; }

    bool hostLevel(GJGameLevel* level); // abre la sala de este nivel (anfitrion)
    void invite(std::string const& user);
    void joinRoom(std::string const& room);
    void leaveRoom();

    // --- editor ---
    // Devuelve true si el nivel que se esta abriendo es el de la sala.
    bool attachEditor(GJGameLevel* level, std::vector<std::string>& replay);
    void sendOp(std::string const& payload);
    std::vector<RemoteOp> takeOps();

private:
    Collab() = default;

    struct Pending {
        std::string room, levelName, base;
        std::vector<std::string> replay;
    };

    void handleLine(std::string const& line);
    void notify(std::string const& text, geode::NotificationIcon icon);
    void askInvite(std::string const& from, std::string const& room, std::string const& levelName);
    void openJoinedLevel();
    void readSettings();

    bool m_started = false;
    bool m_online = false;
    bool m_autoConnect = true;
    bool m_failNotified = false;
    std::string m_host = "127.0.0.1";
    int m_port = 7777;
    std::string m_name = "jugador";
    float m_retryTimer = 0.f;

    std::vector<std::string> m_users;
    unsigned m_version = 0;

    bool m_inRoom = false;
    std::string m_room;
    std::string m_lastRoom;
    std::vector<std::string> m_members;
    geode::Ref<GJGameLevel> m_hostLevel;
    geode::Ref<GJGameLevel> m_joinLevel;

    bool m_hasPending = false;
    Pending m_pending;
    std::vector<std::string> m_replayForEditor;

    bool m_editorLive = false;
    float m_editorAbsent = 0.f;
    bool m_resyncing = false;
    std::string m_resumeRoom;

    std::deque<RemoteOp> m_ops;
};
