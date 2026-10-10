/**
 * Fills a table with simple bots so you can play against someone.
 *
 *   npm run bots -w @pocket-club/server -- [count] [tableName]
 *
 * Bots connect like any other client, so this also exercises the server end to end.
 * Set SERVER_URL to point somewhere other than http://localhost:3000.
 */
import { io, Socket } from 'socket.io-client';
import type { ClientToServer, HelloReply, LobbyTable, Reply, ServerToClient, TableState } from './protocol.js';

const SERVER_URL = process.env.SERVER_URL ?? 'http://localhost:3000';
const count = Number(process.argv[2] ?? 3);
const tableName = process.argv[3] ?? 'Welcome Table';
const NAMES = ['Ace', 'Bluff', 'Chip', 'Dealer Dan', 'River Rat', 'Nuts', 'Fish', 'Shark', 'Lucky'];

type BotSocket = Socket<ServerToClient, ClientToServer>;

function request<T>(socket: BotSocket, event: keyof ClientToServer, payload: object): Promise<T> {
  return new Promise((resolve, reject) => {
    (socket.emit as (e: string, p: object, ack: (r: Reply<T>) => void) => void)(event, payload, (reply) =>
      reply.ok ? resolve(reply as T) : reject(new Error(reply.error)),
    );
  });
}

async function runBot(index: number): Promise<void> {
  const socket: BotSocket = io(SERVER_URL, { transports: ['websocket'] });
  const name = `${NAMES[index % NAMES.length]} Bot`;
  let session: string | undefined;
  let tableId: string | undefined;

  // Sign in (again) on every connection, so bots come back after a server restart.
  socket.on('connect', async () => {
    try {
      const hello = await request<HelloReply>(socket, 'hello', session ? { session } : { guestName: name });
      session = hello.session;
      tableId = hello.tables.find((t: LobbyTable) => t.name === tableName)?.id;
      if (!tableId) return console.log(`${name}: no table named "${tableName}"`);
      await request(socket, 'table:watch', { tableId });
    } catch (err) {
      console.log(`${name}: ${(err as Error).message}`);
    }
  });

  let sitting = false;
  let thinking = false;
  let latest: TableState | null = null;
  socket.on('table:state', async (state: TableState) => {
    latest = state;
    if (!tableId) return;
    try {
      if (state.mySeat === -1) {
        if (sitting) return;
        const free = state.seats.flatMap((s, i) => (s === null ? [i] : []));
        if (free.length === 0) return console.log(`${name}: table is full`);
        // Spread bots over the free seats; if another bot wins the race, the next state update retries.
        const seat = free[index % free.length];
        sitting = true;
        try {
          await request(socket, 'table:sit', { tableId, seat, buyIn: state.config.bigBlind * 100 });
          console.log(`${name} sat in seat ${seat}`);
        } catch {
          setTimeout(() => request(socket, 'table:watch', { tableId: tableId! }).catch(() => {}), 200 * (index + 1));
        } finally {
          sitting = false;
        }
        return;
      }
      const me = state.seats[state.mySeat];
      if (me && me.stack === 0 && state.phase !== 'betting') {
        await request(socket, 'table:rebuy', { tableId, amount: state.config.bigBlind * 100 });
      } else if (me?.sittingOut && me.stack > 0) {
        await request(socket, 'table:sitIn', { tableId, sittingOut: false });
      }
      if (!state.legal || thinking) return;
      thinking = true;
      await new Promise((r) => setTimeout(r, 600 + Math.random() * 1200));
      thinking = false;
      // The table may have moved on while the bot was "thinking".
      const now: TableState = latest;
      if (now.legal && now.handNumber === state.handNumber) {
        await request(socket, 'table:action', { tableId, action: decide(now) });
      }
    } catch (err) {
      thinking = false;
      console.log(`${name}: ${(err as Error).message}`);
    }
  });
}

/** A loose-passive bot with the occasional raise. Good enough to practise against. */
function decide(state: TableState) {
  const legal = state.legal!;
  const me = state.seats[state.mySeat]!;
  const roll = Math.random();
  if (legal.canRaise && roll < 0.15) {
    const target = Math.max(legal.minRaiseTo, Math.round(state.pot * 0.6) + state.currentBet);
    return { type: 'raise' as const, amount: Math.min(target, legal.maxRaiseTo) };
  }
  if (legal.canCheck) return { type: 'check' as const };
  const priceTooHigh = legal.callAmount > me.stack * 0.3;
  if (priceTooHigh ? roll < 0.7 : roll < 0.2) return { type: 'fold' as const };
  return { type: 'call' as const };
}

for (let i = 0; i < count; i++) {
  runBot(i).catch((err) => console.error(`bot ${i} failed: ${err.message}`));
}
