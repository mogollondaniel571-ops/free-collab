// Servidor de Free Collab v0.2
//
// - Lobby: todos los conectados se ven entre si y se pueden invitar.
// - Salas: hasta 6 personas. La sala sigue viva aunque el anfitrion se salga,
//   porque el servidor guarda el nivel base y los cambios (de forma compacta).
// - Es un programa muy liviano: solo reenvia lineas de texto.
//
// Uso:   node server.js          (puerto 7777 por defecto)
//        PORT=8000 node server.js
//
// Protocolo (una linea de texto por mensaje):
//   cliente -> servidor
//     HELLO <nombre>
//     OPEN <sala> <nombreNivel> <nivelBase>
//     INVITE <usuario> <sala>
//     JOIN <sala>
//     OP SET <gid> <objeto>   |  OP DEL <gid>  |  OP CUR <x> <y>
//     LEAVE
//   servidor -> cliente
//     HELLOOK | USERS a b c | USERJOIN n | USERLEAVE n | INFO texto | ERR texto
//     INVITE <de> <sala> <nombreNivel> | ROOMOK <sala> | MEMBERS <sala> a b c
//     STATE <sala> <nombreNivel> <nivelBase> | REPLAY SET ... | REPLAY DEL ... | READY <sala>
//     OP <de> SET ... | OP <de> DEL ... | OP <de> CUR ...

const net = require("net");

const PORT = parseInt(process.env.PORT || "7777", 10);
const MAX_PER_ROOM = 6;
const MAX_LINE = 64 * 1024 * 1024; // 64 MB (niveles grandes)

const users = new Map(); // nombre -> cliente
const rooms = new Map(); // id sala -> sala

function clean(text) {
  return String(text || "").replace(/[^A-Za-z0-9_\-]/g, "").slice(0, 24);
}

function send(client, line) {
  if (client && !client.sock.destroyed) client.sock.write(line + "\n");
}

function broadcastUsers(line, except) {
  for (const c of users.values()) if (c !== except) send(c, line);
}

function broadcastRoom(room, line, except) {
  for (const c of room.members) if (c !== except) send(c, line);
}

function memberNames(room) {
  return [...room.members].map((c) => c.name).join(" ");
}

// Divide "a b c resto con espacios" en n tokens + el resto
function splitN(text, n) {
  const out = [];
  let rest = text;
  for (let i = 0; i < n; i++) {
    const k = rest.indexOf(" ");
    if (k < 0) {
      out.push(rest);
      rest = "";
    } else {
      out.push(rest.slice(0, k));
      rest = rest.slice(k + 1);
    }
  }
  out.push(rest);
  return out;
}

function leaveRoom(client) {
  const room = client.room;
  if (!room) return;
  room.members.delete(client);
  client.room = null;
  if (room.members.size === 0) {
    rooms.delete(room.id);
    console.log(`[${room.id}] sala cerrada (vacia)`);
    return;
  }
  broadcastRoom(room, `INFO ${client.name} salio de la sala`);
  broadcastRoom(room, `MEMBERS ${room.id} ${memberNames(room)}`);
}

function joinRoom(client, room) {
  if (client.room === room) return true;
  if (room.members.size >= MAX_PER_ROOM) {
    send(client, `ERR La sala esta llena (maximo ${MAX_PER_ROOM})`);
    return false;
  }
  leaveRoom(client);
  room.members.add(client);
  client.room = room;
  return true;
}

// Guardado compacto de cambios: solo el ultimo SET de cada objeto
function recordOp(room, payload) {
  const [kind, gid] = splitN(payload, 1);
  if (kind === "SET") {
    const [g] = splitN(gid, 1);
    room.entries.delete(g);
    room.entries.set(g, { seq: ++room.seq, payload });
    room.tombs.delete(g);
  } else if (kind === "DEL") {
    const g = gid.trim();
    room.entries.delete(g);
    if (/^b\d+$/.test(g)) room.tombs.add(g); // objeto del nivel base (b0, b1, ...)
  }
}

function handle(client, line) {
  const [cmd, rest] = splitN(line, 1);

  if (!client.name) {
    if (cmd !== "HELLO") return client.sock.destroy();
    const name = clean(rest) || "jugador";
    if (users.has(name)) {
      send(client, "ERR Ese nombre ya esta en uso, cambialo en los ajustes del mod");
      return client.sock.end();
    }
    client.name = name;
    users.set(name, client);
    send(client, "HELLOOK");
    send(client, `USERS ${[...users.keys()].join(" ")}`);
    broadcastUsers(`USERJOIN ${name}`, client);
    console.log(`+ ${name} (${users.size} conectados)`);
    return;
  }

  switch (cmd) {
    case "OPEN": {
      const [id, levelName, base] = splitN(rest, 2);
      const roomId = clean(id);
      if (!roomId) return;
      let room = rooms.get(roomId);
      if (!room) {
        room = {
          id: roomId,
          levelName,
          base,
          members: new Set(),
          entries: new Map(),
          tombs: new Set(),
          seq: 0,
        };
        rooms.set(roomId, room);
        console.log(`[${roomId}] sala creada por ${client.name}`);
      }
      if (joinRoom(client, room)) {
        send(client, `ROOMOK ${roomId}`);
        broadcastRoom(room, `MEMBERS ${room.id} ${memberNames(room)}`);
      }
      break;
    }
    case "INVITE": {
      const [target, roomId] = splitN(rest, 1);
      const room = rooms.get(clean(roomId));
      const other = users.get(target);
      if (!room || client.room !== room) return send(client, "ERR Primero abre una sala");
      if (!other) return send(client, `ERR ${target} no esta conectado`);
      send(other, `INVITE ${client.name} ${room.id} ${room.levelName}`);
      send(client, `INFO Invitacion enviada a ${target}`);
      break;
    }
    case "JOIN": {
      const room = rooms.get(clean(rest));
      if (!room) return send(client, "ERR Esa sala ya no existe");
      if (!joinRoom(client, room)) return;
      send(client, `STATE ${room.id} ${room.levelName} ${room.base}`);
      for (const g of room.tombs) send(client, `REPLAY DEL ${g}`);
      const sorted = [...room.entries.values()].sort((a, b) => a.seq - b.seq);
      for (const e of sorted) send(client, `REPLAY ${e.payload}`);
      send(client, `READY ${room.id}`);
      broadcastRoom(room, `INFO ${client.name} entro a la sala`, client);
      broadcastRoom(room, `MEMBERS ${room.id} ${memberNames(room)}`);
      console.log(`[${room.id}] ${client.name} entro (${room.members.size}/${MAX_PER_ROOM})`);
      break;
    }
    case "OP": {
      const room = client.room;
      if (!room) return;
      if (!rest.startsWith("CUR ")) recordOp(room, rest);
      broadcastRoom(room, `OP ${client.name} ${rest}`, client);
      break;
    }
    case "LEAVE":
      leaveRoom(client);
      break;
  }
}

function disconnect(client) {
  leaveRoom(client);
  if (client.name && users.get(client.name) === client) {
    users.delete(client.name);
    broadcastUsers(`USERLEAVE ${client.name}`);
    console.log(`- ${client.name} (${users.size} conectados)`);
  }
}

const server = net.createServer((sock) => {
  sock.setEncoding("utf8");
  sock.setNoDelay(true);
  const client = { sock, name: null, room: null };
  let buffer = "";

  sock.on("data", (data) => {
    buffer += data;
    let i;
    while ((i = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, i).replace(/\r$/, "");
      buffer = buffer.slice(i + 1);
      if (line) {
        try {
          handle(client, line);
        } catch (e) {
          console.error("Error procesando mensaje:", e.message);
        }
      }
    }
    if (buffer.length > MAX_LINE) sock.destroy();
  });

  sock.on("close", () => disconnect(client));
  sock.on("error", () => {});
});

server.listen(PORT, () => {
  console.log(`Servidor Free Collab v0.2 escuchando en el puerto ${PORT}`);
  console.log(`Maximo ${MAX_PER_ROOM} personas por sala`);
});
