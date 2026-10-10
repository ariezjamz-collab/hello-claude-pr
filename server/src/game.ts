import { randomInt } from 'node:crypto';
import { createServer, Server as HttpServer } from 'node:http';
import { AddressInfo } from 'node:net';
import { Action, shuffle, Table, viewFor } from '@pocket-club/engine';
import { Server, Socket } from 'socket.io';
import type { Config } from './config.js';
import type { GoogleVerifier } from './google.js';
import type { ClientToServer, HelloReply, LobbyTable, ServerToClient, TableState } from './protocol.js';
import type { Store, TableRecord } from './store.js';

interface Room {
  record: TableRecord;
  table: Table;
  turnDeadline: number | null;
  turnTimer: NodeJS.Timeout | null;
  nextHandTimer: NodeJS.Timeout | null;
  /** Database writes for this table run one after another, in the order the game made them. */
  writes: Promise<unknown>;
  savedHand: number;
  emptySince: number | null;
}

interface SocketData {
  playerId?: string;
  name?: string;
  session?: string;
}
type ClientSocket = Socket<ClientToServer, ServerToClient, object, SocketData>;

export interface GameServer {
  httpServer: HttpServer;
  io: Server<ClientToServer, ServerToClient, object, SocketData>;
  /** Starts listening; returns the port (useful with port 0 in tests). */
  listen(port: number): Promise<number>;
  /** Stops new hands, lets running hands finish (up to `maxWaitMs`), returns all seated chips to banks and closes. */
  shutdown(maxWaitMs?: number): Promise<void>;
}

interface Options {
  config: Config;
  store: Store;
  verifyGoogle: GoogleVerifier;
  log?: (...args: unknown[]) => void;
}

const DEFAULT_TABLES = [
  { name: 'Welcome Table', smallBlind: 5, bigBlind: 10, maxSeats: 6 },
  { name: 'High Rollers', smallBlind: 50, bigBlind: 100, maxSeats: 9 },
];
const MAX_TABLES = 100;
const AWAY_TURN_SECONDS = 2;
const EMPTY_TABLE_LIFETIME_MS = 30 * 60_000;

// Shuffle with a cryptographic RNG so the deal can't be predicted.
const secureShuffle = (deck: string[]) => shuffle(deck, () => randomInt(0, 2 ** 32) / 2 ** 32);

export async function createGameServer({ config, store, verifyGoogle, log = console.log }: Options): Promise<GameServer> {
  // During a deploy the old server may still be finishing its hands; wait for it to let go.
  await store.acquireExclusiveLock(() => log('another game server is using the database, waiting for it to stop…'), 5 * 60_000);
  await store.migrate();
  const refunded = await store.refundAllSeats();
  if (refunded > 0) log(`returned ${refunded} seated stacks to player banks after restart`);

  const rooms = new Map<string, Room>();
  const addRoom = (record: TableRecord) => {
    const room: Room = {
      record,
      table: new Table(
        { smallBlind: record.smallBlind, bigBlind: record.bigBlind, maxSeats: record.maxSeats },
        { shuffle: secureShuffle },
      ),
      turnDeadline: null,
      turnTimer: null,
      nextHandTimer: null,
      writes: Promise.resolve(),
      savedHand: 0,
      emptySince: Date.now(),
    };
    rooms.set(record.id, room);
    return room;
  };
  let records = await store.listTables();
  const missing = DEFAULT_TABLES.filter((d) => !records.some((t) => t.createdBy === null && t.name === d.name));
  for (const t of missing) await store.createTable({ ...t, createdBy: null });
  if (missing.length > 0) records = await store.listTables();
  records.forEach(addRoom);

  let shuttingDown = false;

  const httpServer = createServer((req, res) => {
    if (req.url === '/healthz') {
      res.writeHead(shuttingDown ? 503 : 200, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ ok: !shuttingDown, tables: rooms.size, connections: io.engine.clientsCount }));
      return;
    }
    res.writeHead(200, { 'content-type': 'text/plain' });
    res.end('Pocket Club server is running\n');
  });
  const io = new Server<ClientToServer, ServerToClient, object, SocketData>(httpServer, {
    cors: { origin: '*' },
    // Notice dead mobile connections quickly, but tolerate slow ones.
    pingInterval: 10_000,
    pingTimeout: 20_000,
    maxHttpBufferSize: 16_000,
  });

  /** Queues a database write for a table, keeping writes in game order. */
  function persist<T>(room: Room, write: () => Promise<T>): Promise<T> {
    const result = room.writes.then(write);
    room.writes = result.catch((err) => log('database write failed', err));
    return result;
  }

  // ---- Broadcasting ----------------------------------------------------------

  const lobby = (): LobbyTable[] =>
    [...rooms.values()].map(({ record, table }) => ({
      id: record.id,
      name: record.name,
      smallBlind: record.smallBlind,
      bigBlind: record.bigBlind,
      maxSeats: record.maxSeats,
      players: table.seats.filter(Boolean).length,
    }));

  let lastLobby = '';
  const broadcastLobby = (force = false) => {
    const tables = lobby();
    const signature = JSON.stringify(tables);
    if (force || signature !== lastLobby) io.emit('lobby', tables);
    lastLobby = signature;
  };

  const stateFor = (room: Room, playerId: string | null): TableState => ({
    ...viewFor(room.table, playerId),
    tableId: room.record.id,
    name: room.record.name,
    turnDeadline: room.turnDeadline,
  });

  async function broadcast(room: Room) {
    // Each socket gets its own view, so hole cards only go to their owner.
    for (const socket of await io.in(`table:${room.record.id}`).fetchSockets()) {
      socket.emit('table:state', stateFor(room, socket.data.playerId ?? null));
    }
    broadcastLobby();
  }

  // ---- Game loop -------------------------------------------------------------

  /** Call after anything changes at a table: settles finished hands, starts new ones and runs the turn clock. */
  function update(room: Room) {
    const { table } = room;
    if (room.turnTimer) clearTimeout(room.turnTimer);
    room.turnTimer = null;
    room.turnDeadline = null;

    if (table.phase === 'betting') {
      const seat = table.seats[table.toAct]!;
      // Players already marked away get a short clock so they don't hold up the table every street.
      const seconds = seat.sittingOut ? AWAY_TURN_SECONDS : config.turnSeconds;
      room.turnDeadline = Date.now() + seconds * 1000;
      room.turnTimer = setTimeout(() => {
        // Out of time: check if free, otherwise fold, and mark the player away until they come back.
        try {
          const legal = table.legalActions(table.toAct);
          table.act(seat.playerId, legal?.canCheck ? { type: 'check' } : { type: 'fold' });
          table.setSittingOut(seat.playerId, true);
        } catch (err) {
          log('turn timeout failed', err);
        }
        update(room);
      }, seconds * 1000);
    } else {
      for (const { playerId, stack } of table.removeLeavers()) {
        void persist(room, () => store.cashOut(playerId, room.record.id, stack));
      }
      if (table.phase === 'complete' && room.savedHand !== table.handNumber) {
        room.savedHand = table.handNumber;
        const stacks = table.seats.flatMap((s) => (s ? [{ playerId: s.playerId, stack: s.stack }] : []));
        void persist(room, () => store.saveStacks(room.record.id, stacks));
      }
      if (!room.nextHandTimer && !shuttingDown && table.canStartHand()) {
        const delay = table.phase === 'complete' ? config.nextHandDelayMs : 1_000;
        room.nextHandTimer = setTimeout(() => {
          room.nextHandTimer = null;
          if (!shuttingDown && table.canStartHand()) table.startHand();
          update(room);
        }, delay);
      }
    }
    const seated = table.seats.some(Boolean);
    room.emptySince = seated ? null : (room.emptySince ?? Date.now());
    void broadcast(room);
  }

  // Remove player-made tables that have sat empty for a while.
  const cleanup = setInterval(() => {
    for (const room of rooms.values()) {
      const watchers = io.sockets.adapter.rooms.get(`table:${room.record.id}`)?.size ?? 0;
      if (room.record.createdBy && room.emptySince && Date.now() - room.emptySince > EMPTY_TABLE_LIFETIME_MS && watchers === 0) {
        rooms.delete(room.record.id);
        void store.deleteTable(room.record.id).catch((err) => log('could not delete table', err));
      }
    }
    broadcastLobby();
  }, 5 * 60_000);
  cleanup.unref();

  // ---- Requests --------------------------------------------------------------

  const positiveInt = (value: unknown, field: string) => {
    if (typeof value !== 'number' || !Number.isInteger(value) || value <= 0) throw new Error(`${field} must be a positive whole number`);
    return value;
  };

  /** Keeps letters (any script), digits, spaces and . _ - ; 2–20 characters. */
  const cleanName = (value: unknown, fallback?: string) => {
    const name = (typeof value === 'string' ? value : '')
      .replace(/[^\p{L}\p{M}\p{N} ._-]/gu, '')
      .replace(/\s+/g, ' ')
      .trim()
      .slice(0, 20);
    if (name.length >= 2) return name;
    if (fallback) return fallback;
    throw new Error('names need 2 to 20 letters or numbers');
  };

  const getRoom = (tableId: unknown) => {
    const room = rooms.get(String(tableId));
    if (!room) throw new Error('table not found');
    return room;
  };

  const notShuttingDown = () => {
    if (shuttingDown) throw new Error('the server is restarting, try again in a minute');
  };

  io.on('connection', (socket: ClientSocket) => {
    const me = () => {
      const { playerId, name } = socket.data;
      if (!playerId || !name) throw new Error('please sign in again');
      return { id: playerId, name };
    };

    // A token bucket per connection: bursts of 20 requests, 10 per second sustained.
    let tokens = 20;
    let lastRefill = Date.now();
    const allowRequest = () => {
      const now = Date.now();
      tokens = Math.min(20, tokens + ((now - lastRefill) / 1000) * 10);
      lastRefill = now;
      if (tokens < 1) return false;
      tokens--;
      return true;
    };

    /** Registers a handler that replies through the acknowledgement callback, turning errors into `{ ok: false }`. */
    function handle<E extends keyof ClientToServer>(
      event: E,
      fn: (payload: Parameters<ClientToServer[E]>[0]) => Promise<object | void> | object | void,
    ) {
      socket.on(event, (async (payload: Parameters<ClientToServer[E]>[0], ack?: (reply: object) => void) => {
        const reply = typeof ack === 'function' ? ack : () => {};
        if (!allowRequest()) return reply({ ok: false, error: 'slow down a little' });
        try {
          const result = await fn(payload && typeof payload === 'object' ? payload : ({} as never));
          reply({ ok: true, ...(result ?? {}) });
        } catch (err) {
          reply({ ok: false, error: err instanceof Error ? err.message : 'something went wrong' });
        }
      }) as never);
    }

    handle('hello', async ({ session, guestName, googleIdToken }): Promise<HelloReply> => {
      let player = typeof session === 'string' && session ? await store.playerBySession(session) : null;
      let newSession: string | null = null;

      if (typeof googleIdToken === 'string' && googleIdToken) {
        const google = await verifyGoogle(googleIdToken);
        const existing = await store.playerByGoogleSub(google.sub);
        if (existing) {
          player = existing;
        } else if (player && !player.googleSub) {
          // A guest signing in with Google keeps their account and chips.
          await store.linkGoogle(player.id, google.sub);
          player.googleSub = google.sub;
        } else {
          player = await store.createPlayer(cleanName(google.name, 'Player'), google.sub, config.startingBank);
        }
        newSession = await store.createSession(player.id);
      } else if (!player) {
        if (!guestName) throw new Error('your sign-in has expired, please sign in again');
        player = await store.createPlayer(cleanName(guestName), null, config.startingBank);
        newSession = await store.createSession(player.id);
      }

      socket.data.playerId = player.id;
      socket.data.name = player.name;
      socket.data.session = newSession ?? session;
      return {
        playerId: player.id,
        session: newSession ?? session!,
        name: player.name,
        bank: player.bank,
        isGuest: !player.googleSub,
        tables: lobby(),
      };
    });

    handle('logout', async () => {
      if (socket.data.session) await store.deleteSession(socket.data.session);
      socket.data = {};
    });

    handle('wallet', async () => {
      const player = await store.playerById(me().id);
      return { bank: player?.bank ?? 0 };
    });

    handle('wallet:refill', async () => ({ bank: await store.refill(me().id, config.startingBank) }));

    handle('lobby:list', () => ({ tables: lobby() }));

    handle('table:create', async ({ name, smallBlind, bigBlind, maxSeats }) => {
      const player = me();
      notShuttingDown();
      const sb = positiveInt(smallBlind, 'small blind');
      const bb = positiveInt(bigBlind, 'big blind');
      const seats = positiveInt(maxSeats, 'seats');
      if (bb < sb) throw new Error('big blind must be at least the small blind');
      if (bb > 1_000) throw new Error('the biggest big blind is 1,000');
      if (seats < 2 || seats > 9) throw new Error('tables have 2 to 9 seats');
      const mine = [...rooms.values()].filter((r) => r.record.createdBy === player.id).length;
      if (mine >= config.maxTablesPerPlayer) throw new Error(`you can have ${config.maxTablesPerPlayer} tables at a time`);
      if (rooms.size >= MAX_TABLES) throw new Error('too many tables right now, join an existing one');
      const record = await store.createTable({ name: cleanName(name), smallBlind: sb, bigBlind: bb, maxSeats: seats, createdBy: player.id });
      addRoom(record);
      broadcastLobby(true);
      return { tableId: record.id };
    });

    handle('table:watch', ({ tableId }) => {
      const room = getRoom(tableId);
      for (const joined of socket.rooms) if (joined.startsWith('table:')) void socket.leave(joined);
      void socket.join(`table:${room.record.id}`);
      socket.emit('table:state', stateFor(room, socket.data.playerId ?? null));
    });

    handle('table:unwatch', ({ tableId }) => {
      void socket.leave(`table:${String(tableId)}`);
    });

    handle('table:sit', async ({ tableId, seat, buyIn }) => {
      const player = me();
      notShuttingDown();
      const room = getRoom(tableId);
      const amount = positiveInt(buyIn, 'buy-in');
      const { bigBlind } = room.table.config;
      if (amount < bigBlind * 20 || amount > bigBlind * 200) throw new Error(`buy-in must be between ${bigBlind * 20} and ${bigBlind * 200}`);
      if (typeof seat !== 'number' || !Number.isInteger(seat) || seat < 0 || seat >= room.table.seats.length) throw new Error('choose a seat');
      if (room.table.seatOf(player.id) !== -1) throw new Error('you are already seated here');
      if (room.table.seats[seat]) throw new Error('seat is taken');

      // Take the chips from the bank first, then seat the player; undo if the seat went in the meantime.
      const bank = await persist(room, () => store.buyIn(player.id, room.record.id, amount));
      if (bank === null) throw new Error('not enough chips in your bank');
      try {
        room.table.sit(seat, player.id, player.name, amount);
      } catch (err) {
        await persist(room, () => store.cashOut(player.id, room.record.id, amount));
        throw err;
      }
      update(room);
      return { bank };
    });

    handle('table:stand', async ({ tableId }) => {
      const player = me();
      const room = getRoom(tableId);
      const cashOut = room.table.stand(player.id);
      update(room);
      // In a hand, the player folds now and is cashed out when the hand ends.
      if (cashOut === null) return { bank: (await store.playerById(player.id))?.bank ?? 0 };
      return { bank: await persist(room, () => store.cashOut(player.id, room.record.id, cashOut)) };
    });

    handle('table:rebuy', async ({ tableId, amount }) => {
      const player = me();
      notShuttingDown();
      const room = getRoom(tableId);
      const chips = positiveInt(amount, 'amount');
      const seat = room.table.seats[room.table.seatOf(player.id)];
      if (!seat) throw new Error('you are not seated');
      if (room.table.phase === 'betting' && seat.inHand) throw new Error('wait for this hand to finish');
      if (seat.stack + chips > room.table.config.bigBlind * 200) throw new Error(`you can have at most ${room.table.config.bigBlind * 200} on the table`);
      const bank = await persist(room, () => store.buyIn(player.id, room.record.id, chips));
      if (bank === null) throw new Error('not enough chips in your bank');
      room.table.addChips(player.id, chips);
      update(room);
      return { bank };
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
        if (reconnected || shuttingDown) return;
        for (const room of rooms.values()) {
          if (room.table.seatOf(playerId) === -1) continue;
          const cashOut = room.table.stand(playerId);
          if (cashOut !== null) void persist(room, () => store.cashOut(playerId, room.record.id, cashOut));
          update(room);
        }
      }, config.disconnectGraceSeconds * 1000).unref();
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

  return {
    httpServer,
    io,
    listen: (port) =>
      new Promise((resolve) => httpServer.listen(port, () => resolve((httpServer.address() as AddressInfo).port))),

    async shutdown(maxWaitMs = 120_000) {
      if (shuttingDown) return;
      shuttingDown = true;
      clearInterval(cleanup);
      io.emit('notice', 'The server is restarting. Your chips are safe; you will be reconnected shortly.');
      const deadline = Date.now() + maxWaitMs;
      while ([...rooms.values()].some((r) => r.table.phase === 'betting') && Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 250));
      }
      for (const room of rooms.values()) {
        if (room.turnTimer) clearTimeout(room.turnTimer);
        if (room.nextHandTimer) clearTimeout(room.nextHandTimer);
        const midHand = room.table.phase === 'betting';
        for (const seat of room.table.seats) {
          if (!seat) continue;
          // A hand cut off by the deadline is void: everyone gets back what they put in.
          const stack = seat.stack + (midHand && seat.inHand ? seat.committed : 0);
          void persist(room, () => store.cashOut(seat.playerId, room.record.id, stack));
        }
      }
      await Promise.all([...rooms.values()].map((r) => r.writes));
      await new Promise<void>((resolve) => io.close(() => resolve()));
      await store.close();
    },
  };
}
