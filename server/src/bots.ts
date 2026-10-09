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
  const name = `${NAMES[index % NAMES.length]} (bot)`;
  const hello = await request<HelloReply>(socket, 'hello', { name });
  const table = hello.tables.find((t: LobbyTable) => t.name === tableName);
  if (!table) throw new Error(`no table named "${tableName}"`);

  let seated = false;
  let thinking = false;
  let latest: TableState | null = null;
  socket.on('table:state', async (state: TableState) => {
    latest = state;
    try {
      if (!seated) {
        const free = state.seats.flatMap((s, i) => (s === null ? [i] : []));
        if (free.length === 0) return console.log(`${name}: table is full`);
        // Spread bots over the free seats; if another bot wins the race, the next state update retries.
        const seat = free[index % free.length];
        seated = true;
        try {
          await request(socket, 'table:sit', { tableId: table.id, seat, buyIn: state.config.bigBlind * 100 });
          console.log(`${name} sat in seat ${seat}`);
        } catch {
          seated = false;
          setTimeout(() => request(socket, 'table:watch', { tableId: table.id }), 200 * (index + 1));
        }
        return;
      }
      const me = state.seats[state.mySeat];
      if (me && me.stack === 0 && state.phase !== 'betting') {
        await request(socket, 'table:rebuy', { tableId: table.id, amount: state.config.bigBlind * 100 });
      } else if (me?.sittingOut && me.stack > 0) {
        await request(socket, 'table:sitIn', { tableId: table.id, sittingOut: false });
      }
      if (!state.legal || thinking) return;
      thinking = true;
      await new Promise((r) => setTimeout(r, 600 + Math.random() * 1200));
      thinking = false;
      // The table may have moved on while the bot was "thinking".
      const now: TableState = latest;
      if (now.legal && now.handNumber === state.handNumber) {
        await request(socket, 'table:action', { tableId: table.id, action: decide(now) });
      }
    } catch (err) {
      thinking = false;
      console.log(`${name}: ${(err as Error).message}`);
    }
  });
  await request(socket, 'table:watch', { tableId: table.id });
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
