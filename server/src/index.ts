import { randomInt, randomUUID } from 'node:crypto';
import { createServer } from 'node:http';
import { Action, shuffle, Table, viewFor } from '@pocket-club/engine';
import { Server, Socket } from 'socket.io';
import type { ClientToServer, LobbyTable, ServerToClient, TableState } from './protocol.js';

const PORT = Number(process.env.PORT ?? 3000);
const STARTING_BANK = 10_000;
const TURN_SECONDS = Number(process.env.TURN_SECONDS ?? 20);
const NEXT_HAND_DELAY_MS = 4_000;
const DISCONNECT_GRACE_MS = Number(process.env.DISCONNECT_GRACE_SECONDS ?? 60) * 1000;

interface Player {
  /** Public id, visible to other players. */
  id: string;
  /** Secret used to reconnect as this player. Never sent to anyone else. */
  token: string;
  name: string;
  /** Play chips not currently on a table. */
  bank: number;
}

interface Room {
  id: string;
  name: string;
  table: Table;
  turnDeadline: number | null;
  turnTimer: NodeJS.Timeout | null;
  nextHandTimer: NodeJS.Timeout | null;
}

type ClientSocket = Socket<ClientToServer, ServerToClient, object, { playerId?: string }>;

const players = new Map<string, Player>();
const playersByToken = new Map<string, Player>();
const rooms = new Map<string, Room>();

// Shuffle with a cryptographic RNG so the deal can't be predicted.
const secureShuffle = (deck: string[]) => shuffle(deck, () => randomInt(0, 2 ** 32) / 2 ** 32);

function createRoom(name: string, smallBlind: number, bigBlind: number, maxSeats: number): Room {
  const room: Room = {
    id: randomUUID().slice(0, 8),
    name,
    table: new Table({ smallBlind, bigBlind, maxSeats }, { shuffle: secureShuffle }),
    turnDeadline: null,
    turnTimer: null,
    nextHandTimer: null,
  };
  rooms.set(room.id, room);
  return room;
}

createRoom('Welcome Table', 5, 10, 6);
createRoom('High Rollers', 50, 100, 9);

const httpServer = createServer((_req, res) => {
  res.writeHead(200, { 'content-type': 'text/plain' });
  res.end('Pocket Club server is running\n');
});
const io = new Server<ClientToServer, ServerToClient>(httpServer, { cors: { origin: '*' } });

// ---- Broadcasting ------------------------------------------------------------

function lobby(): LobbyTable[] {
  return [...rooms.values()].map((r) => ({
    id: r.id,
    name: r.name,
    smallBlind: r.table.config.smallBlind,
    bigBlind: r.table.config.bigBlind,
    maxSeats: r.table.config.maxSeats,
    players: r.table.seats.filter(Boolean).length,
  }));
}

function stateFor(room: Room, playerId: string | null): TableState {
  return { ...viewFor(room.table, playerId), tableId: room.id, name: room.name, turnDeadline: room.turnDeadline };
}

async function broadcast(room: Room): Promise<void> {
  // Each socket gets its own view, so hole cards only go to their owner.
  for (const socket of await io.in(`table:${room.id}`).fetchSockets()) {
    socket.emit('table:state', stateFor(room, socket.data.playerId ?? null));
  }
  io.emit('lobby', lobby());
}

// ---- Game loop ---------------------------------------------------------------

/** Call after anything changes at a table: settles finished hands, starts new ones and runs the turn clock. */
function update(room: Room): void {
  const { table } = room;
  if (room.turnTimer) clearTimeout(room.turnTimer);
  room.turnTimer = null;
  room.turnDeadline = null;

  if (table.phase === 'betting') {
    const seat = table.seats[table.toAct]!;
    room.turnDeadline = Date.now() + TURN_SECONDS * 1000;
    room.turnTimer = setTimeout(() => {
      // Out of time: check if free, otherwise fold, and sit the player out until they come back.
      const legal = table.legalActions(table.toAct);
      table.act(seat.playerId, legal?.canCheck ? { type: 'check' } : { type: 'fold' });
      table.setSittingOut(seat.playerId, true);
      update(room);
    }, TURN_SECONDS * 1000);
  } else {
    for (const { playerId, stack } of table.removeLeavers()) {
      const player = players.get(playerId);
      if (player) player.bank += stack;
    }
    if (!room.nextHandTimer && table.canStartHand()) {
      const delay = table.phase === 'complete' ? NEXT_HAND_DELAY_MS : 1_000;
      room.nextHandTimer = setTimeout(() => {
        room.nextHandTimer = null;
        if (table.canStartHand()) table.startHand();
        update(room);
      }, delay);
    }
  }
  void broadcast(room);
}

// ---- Requests ----------------------------------------------------------------

function positiveInt(value: unknown, field: string): number {
  if (typeof value !== 'number' || !Number.isInteger(value) || value <= 0) throw new Error(`${field} must be a positive whole number`);
  return value;
}

function cleanName(value: unknown): string {
  const name = typeof value === 'string' ? value.trim().slice(0, 20) : '';
  if (!name) throw new Error('name is required');
  return name;
}

function getRoom(tableId: unknown): Room {
  const room = rooms.get(String(tableId));
  if (!room) throw new Error('table not found');
  return room;
}

io.on('connection', (socket: ClientSocket) => {
  const me = (): Player => {
    const player = socket.data.playerId && players.get(socket.data.playerId);
    if (!player) throw new Error('say hello first');
    return player;
  };

  /** Registers a handler that replies through the acknowledgement callback, turning thrown errors into `{ ok: false }`. */
  function handle<E extends keyof ClientToServer>(event: E, fn: (payload: Parameters<ClientToServer[E]>[0]) => object | void) {
    socket.on(event, ((payload: Parameters<ClientToServer[E]>[0], ack?: (reply: object) => void) => {
      try {
        const result = fn(payload ?? ({} as never));
        ack?.({ ok: true, ...(result ?? {}) });
      } catch (err) {
        ack?.({ ok: false, error: err instanceof Error ? err.message : 'something went wrong' });
      }
    }) as never);
  }

  handle('hello', ({ name, token }) => {
    let player = token ? playersByToken.get(token) : undefined;
    if (!player) {
      player = { id: randomUUID(), token: randomUUID(), name: cleanName(name), bank: STARTING_BANK };
      players.set(player.id, player);
      playersByToken.set(player.token, player);
    } else if (name) {
      player.name = cleanName(name);
    }
    socket.data.playerId = player.id;
    return { playerId: player.id, token: player.token, name: player.name, bank: player.bank, tables: lobby() };
  });

  handle('wallet', () => ({ bank: me().bank }));

  handle('wallet:refill', () => {
    const player = me();
    player.bank = Math.max(player.bank, STARTING_BANK);
    return { bank: player.bank };
  });

  handle('lobby:list', () => ({ tables: lobby() }));

  handle('table:create', ({ name, smallBlind, bigBlind, maxSeats }) => {
    me();
    const sb = positiveInt(smallBlind, 'small blind');
    const bb = positiveInt(bigBlind, 'big blind');
    const seats = positiveInt(maxSeats, 'seats');
    if (bb < sb) throw new Error('big blind must be at least the small blind');
    if (seats < 2 || seats > 9) throw new Error('tables have 2 to 9 seats');
    const room = createRoom(cleanName(name), sb, bb, seats);
    io.emit('lobby', lobby());
    return { tableId: room.id };
  });

  handle('table:watch', ({ tableId }) => {
    const room = getRoom(tableId);
    for (const joined of socket.rooms) if (joined.startsWith('table:')) void socket.leave(joined);
    void socket.join(`table:${room.id}`);
    socket.emit('table:state', stateFor(room, socket.data.playerId ?? null));
  });

  handle('table:unwatch', ({ tableId }) => {
    void socket.leave(`table:${String(tableId)}`);
  });

  handle('table:sit', ({ tableId, seat, buyIn }) => {
    const player = me();
    const room = getRoom(tableId);
    const amount = positiveInt(buyIn, 'buy-in');
    const { bigBlind } = room.table.config;
    if (amount < bigBlind * 20 || amount > bigBlind * 200) throw new Error(`buy-in must be between ${bigBlind * 20} and ${bigBlind * 200}`);
    if (amount > player.bank) throw new Error('not enough chips in your bank');
    if (typeof seat !== 'number') throw new Error('choose a seat');
    room.table.sit(seat, player.id, player.name, amount);
    player.bank -= amount;
    update(room);
    return { bank: player.bank };
  });

  handle('table:stand', ({ tableId }) => {
    const player = me();
    const room = getRoom(tableId);
    const cashOut = room.table.stand(player.id);
    if (cashOut !== null) player.bank += cashOut;
    update(room);
    return { bank: player.bank };
  });

  handle('table:rebuy', ({ tableId, amount }) => {
    const player = me();
    const room = getRoom(tableId);
    const chips = positiveInt(amount, 'amount');
    if (chips > player.bank) throw new Error('not enough chips in your bank');
    room.table.addChips(player.id, chips);
    player.bank -= chips;
    update(room);
    return { bank: player.bank };
  });

  handle('table:sitIn', ({ tableId, sittingOut }) => {
    const room = getRoom(tableId);
    room.table.setSittingOut(me().id, !!sittingOut);
    update(room);
  });

  handle('table:action', ({ tableId, action }) => {
    const room = getRoom(tableId);
    room.table.act(me().id, parseAction(action));
    update(room);
  });

  socket.on('disconnect', () => {
    const playerId = socket.data.playerId;
    if (!playerId) return;
    // Give dropped connections time to come back before giving up their seats.
    setTimeout(async () => {
      const reconnected = (await io.fetchSockets()).some((s) => s.data.playerId === playerId);
      const player = players.get(playerId);
      if (reconnected || !player) return;
      for (const room of rooms.values()) {
        if (room.table.seatOf(playerId) === -1) continue;
        const cashOut = room.table.stand(playerId);
        if (cashOut !== null) player.bank += cashOut;
        update(room);
      }
    }, DISCONNECT_GRACE_MS);
  });
});

function parseAction(action: unknown): Action {
  const a = (action ?? {}) as { type?: unknown; amount?: unknown };
  switch (a.type) {
    case 'fold':
    case 'check':
    case 'call':
      return { type: a.type };
    case 'raise':
      return { type: 'raise', amount: positiveInt(a.amount, 'amount') };
    default:
      throw new Error('unknown action');
  }
}

httpServer.listen(PORT, () => {
  console.log(`Pocket Club server listening on http://localhost:${PORT}`);
});
