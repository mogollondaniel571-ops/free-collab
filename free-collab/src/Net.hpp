#pragma once

// Capa de red de Free Collab. No usa nada de Geode a proposito:
// asi se puede incluir winsock en Net.cpp sin chocar con los headers del juego.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct NetConn;

class Net {
public:
    static Net& get();

    // Empieza una conexion nueva (cierra la anterior si existia). No bloquea.
    void connectTo(std::string host, int port);
    void disconnect();

    bool connected() const;
    bool connecting() const;

    // Encola una linea para enviarla (no bloquea).
    void send(std::string const& line);

    // Devuelve los mensajes recibidos. Los eventos propios de la red
    // empiezan con '@':  @UP  @DOWN  @FAIL <motivo>
    std::vector<std::string> poll();

    // uso interno (hilos de red)
    void push(NetConn* from, std::string line);

private:
    Net() = default;

    std::mutex m_mutex;
    std::shared_ptr<NetConn> m_conn;
    std::vector<std::string> m_in;
};
