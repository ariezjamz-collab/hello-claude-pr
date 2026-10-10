import { afterEach, describe, expect, it } from 'vitest';
import { io as connect, Socket } from 'socket.io-client';
import { loadConfig } from '../src/config';
import { createGameServer, GameServer } from '../src/game';
import type { GoogleVerifier } from '../src/google';
import type { HelloReply, Reply, TableState } from '../src/protocol';
import { MemoryStore, Store } from '../src/store';

const config = { ...loadConfig({}), nextHandDelayMs: 50, disconnectGraceSeconds: 0.2 };

// Accepts tokens shaped like "google:<sub>:<name>" instead of calling Google.
const fakeGoogle: GoogleVerifier = async (token) => {
  const [prefix, sub, name] = token.split(':');
  if (prefix !== 'google') throw new Error('Google sign-in failed, please try again');
  return { sub, name };
};

let servers: GameServer[] = [];
let sockets: Socket[] = [];

afterEach(async () => {
  sockets.forEach((s) => s.disconnect());
  await Promise.all(servers.map((s) => s.shutdown(0).catch(() => {})));
  servers = [];
  sockets = [];
});

async function start(store: Store = new MemoryStore()) {
  const server = await createGameServer({ config, store, verifyGoogle: fakeGoogle, log: () => {} });
  servers.push(server);
  const port = await server.listen(0);
  return { server, store, url: `http://localhost:${port}` };
}

function client(url: string) {
  const socket = connect(url, { transports: ['websocket'], forceNew: true });
  sockets.push(socket);
  const ask = <T = object>(event: string, payload: object = {}) =>
    new Promise<Reply<T>>((resolve) => socket.emit(event, payload, resolve));
  const ok = async <T = object>(event: string, payload: object = {}) => {
    const reply = await ask<T>(event, payload);
    if (!reply.ok) throw new Error(`${event} failed: ${reply.error}`);
    return reply as T;
  };
  const nextState = (match: (s: TableState) => boolean) =>
    new Promise<TableState>((resolve) => {
      const listener = (s: TableState) => {
        if (match(s)) {
          socket.off('table:state', listener);
          resolve(s);
        }
      };
      socket.on('table:state', listener);
    });
  return { socket, ask, ok, nextState };
}

const welcome = (hello: HelloReply) => hello.tables.find((t) => t.name === 'Welcome Table')!.id;

describe('sign-in', () => {
  it('creates a guest with starting chips and resumes them with the session token', async () => {
    const { url } = await start();
    const a = client(url);
    const first = await a.ok<HelloReply>('hello', { guestName: 'Asha' });
    expect(first).toMatchObject({ name: 'Asha', bank: 10_000, isGuest: true });
    expect(first.session).toBeTruthy();

    const b = client(url);
    const again = await b.ok<HelloReply>('hello', { session: first.session });
    expect(again.playerId).toBe(first.playerId);
    expect(again.session).toBe(first.session);
  });

  it('asks to sign in again when the session is unknown', async () => {
    const { url } = await start();
    const reply = await client(url).ask('hello', { session: 'nope' });
    expect(reply).toEqual({ ok: false, error: 'your sign-in has expired, please sign in again' });
  });

  it('rejects names that are too short or only symbols', async () => {
    const { url } = await start();
    const c = client(url);
    expect((await c.ask('hello', { guestName: 'x' })).ok).toBe(false);
    expect((await c.ask('hello', { guestName: '<<>>' })).ok).toBe(false);
    expect(await c.ok<HelloReply>('hello', { guestName: '  Bhaskar  Das ' })).toMatchObject({ name: 'Bhaskar Das' });
  });

  it('signs in with Google and finds the same account next time', async () => {
    const { url } = await start();
    const first = await client(url).ok<HelloReply>('hello', { googleIdToken: 'google:123:Rina' });
    expect(first).toMatchObject({ name: 'Rina', isGuest: false });
    const again = await client(url).ok<HelloReply>('hello', { googleIdToken: 'google:123:Rina' });
    expect(again.playerId).toBe(first.playerId);
    expect((await client(url).ask('hello', { googleIdToken: 'forged' })).ok).toBe(false);
  });

  it('links a guest to Google, keeping their chips', async () => {
    const { url } = await start();
    const c = client(url);
    const guest = await c.ok<HelloReply>('hello', { guestName: 'Tenzin' });
    await c.ok('table:sit', { tableId: welcome(guest), seat: 0, buyIn: 1000 });
    const linked = await c.ok<HelloReply>('hello', { session: guest.session, googleIdToken: 'google:555:Tenzin' });
    expect(linked).toMatchObject({ playerId: guest.playerId, isGuest: false, bank: 9_000 });
  });

  it('logout ends the session', async () => {
    const { url } = await start();
    const c = client(url);
    const hello = await c.ok<HelloReply>('hello', { guestName: 'Mon' });
    await c.ok('logout');
    expect((await client(url).ask('hello', { session: hello.session })).ok).toBe(false);
  });
});

describe('chips', () => {
  it('moves chips from the bank to the table and back', async () => {
    const { url, store } = await start();
    const c = client(url);
    const hello = await c.ok<HelloReply>('hello', { guestName: 'Asha' });
    const tableId = welcome(hello);
    expect(await c.ok('table:sit', { tableId, seat: 2, buyIn: 1500 })).toMatchObject({ bank: 8_500 });
    expect((store as MemoryStore).storedStack(hello.playerId, tableId)).toBe(1500);
    expect(await c.ok('table:stand', { tableId })).toMatchObject({ bank: 10_000 });
    expect((store as MemoryStore).storedStack(hello.playerId, tableId)).toBeUndefined();
  });

  it('refuses buy-ins outside the limits or above the bank', async () => {
    const { url } = await start();
    const c = client(url);
    const hello = await c.ok<HelloReply>('hello', { guestName: 'Asha' });
    const highRollers = hello.tables.find((t) => t.name === 'High Rollers')!.id;
    expect(await c.ask('table:sit', { tableId: welcome(hello), seat: 0, buyIn: 50 })).toMatchObject({ ok: false });
    // High Rollers: 100 big blind, so 20,000 is a legal buy-in, but the bank only has 10,000.
    expect(await c.ask('table:sit', { tableId: highRollers, seat: 0, buyIn: 20_000 })).toEqual({
      ok: false,
      error: 'not enough chips in your bank',
    });
  });

  it('returns seated chips to banks after a crash', async () => {
    const store = new MemoryStore();
    const first = await start(store);
    const c = client(first.url);
    const hello = await c.ok<HelloReply>('hello', { guestName: 'Asha' });
    await c.ok('table:sit', { tableId: welcome(hello), seat: 0, buyIn: 2000 });
    c.socket.disconnect();
    // Simulate a crash: the old server vanishes without cleaning up, a new one starts on the same data.
    first.server.io.close();
    servers = [];

    const second = await start(store);
    const back = await client(second.url).ok<HelloReply>('hello', { session: hello.session });
    expect(back.bank).toBe(10_000);
  });

  it('a graceful shutdown in the middle of a hand voids it and refunds everyone', async () => {
    const store = new MemoryStore();
    const { url, server } = await start(store);
    const a = client(url);
    const b = client(url);
    const ha = await a.ok<HelloReply>('hello', { guestName: 'Asha' });
    const hb = await b.ok<HelloReply>('hello', { guestName: 'Bijoy' });
    const tableId = welcome(ha);
    const handStarted = a.nextState((s) => s.phase === 'betting');
    await a.ok('table:watch', { tableId });
    await a.ok('table:sit', { tableId, seat: 0, buyIn: 1000 });
    await b.ok('table:sit', { tableId, seat: 1, buyIn: 1000 });
    await handStarted;

    await server.shutdown(0);
    servers = [];
    expect((await store.playerById(ha.playerId))!.bank).toBe(10_000);
    expect((await store.playerById(hb.playerId))!.bank).toBe(10_000);
  });

  it('saves stacks after each hand', async () => {
    const store = new MemoryStore();
    const { url } = await start(store);
    const a = client(url);
    const b = client(url);
    const ha = await a.ok<HelloReply>('hello', { guestName: 'Asha' });
    const hb = await b.ok<HelloReply>('hello', { guestName: 'Bijoy' });
    const tableId = welcome(ha);
    await a.ok('table:watch', { tableId });
    const myTurn = a.nextState((s) => s.phase === 'betting' && s.legal !== null);
    await a.ok('table:sit', { tableId, seat: 0, buyIn: 1000 });
    await b.ok('table:sit', { tableId, seat: 1, buyIn: 1000 });
    // Heads-up, seat 0 is the button and acts first: fold the small blind.
    await myTurn;
    const done = a.nextState((s) => s.phase === 'complete');
    await a.ok('table:action', { tableId, action: { type: 'fold' } });
    await done;
    await new Promise((r) => setTimeout(r, 20));
    expect(store.storedStack(ha.playerId, tableId)).toBe(995);
    expect(store.storedStack(hb.playerId, tableId)).toBe(1005);
  });
});

describe('turn clock', () => {
  it('acts quickly for a player who is away instead of waiting the full turn', async () => {
    const { url } = await start();
    const a = client(url);
    const b = client(url);
    const ha = await a.ok<HelloReply>('hello', { guestName: 'Asha' });
    await b.ok('hello', { guestName: 'Bijoy' });
    const tableId = welcome(ha);
    await a.ok('table:watch', { tableId });
    const myTurn = a.nextState((s) => s.phase === 'betting' && s.legal !== null);
    await a.ok('table:sit', { tableId, seat: 0, buyIn: 1000 });
    await b.ok('table:sit', { tableId, seat: 1, buyIn: 1000 });
    await myTurn;

    const started = Date.now();
    const done = a.nextState((s) => s.phase === 'complete');
    await a.ok('table:sitIn', { tableId, sittingOut: true });
    const result = await done;
    expect(Date.now() - started).toBeLessThan(4_000);
    expect(result.seats[0]?.lastAction).toBe('Fold');
  });
});

describe('protection', () => {
  it('limits how fast one connection can send requests', async () => {
    const { url } = await start();
    const c = client(url);
    await c.ok('hello', { guestName: 'Spammer' });
    const replies = await Promise.all(Array.from({ length: 40 }, () => c.ask('lobby:list')));
    expect(replies.filter((r) => !r.ok && r.error === 'slow down a little').length).toBeGreaterThan(10);
  });

  it('limits how many tables one player can create', async () => {
    const { url } = await start();
    const c = client(url);
    await c.ok('hello', { guestName: 'Maker' });
    const make = (n: number) => c.ask('table:create', { name: `Table ${n}`, smallBlind: 1, bigBlind: 2, maxSeats: 6 });
    expect((await make(1)).ok).toBe(true);
    expect((await make(2)).ok).toBe(true);
    expect(await make(3)).toEqual({ ok: false, error: 'you can have 2 tables at a time' });
  });

  it('frees the seat of a player who stays disconnected', async () => {
    const { url, store } = await start();
    const c = client(url);
    const hello = await c.ok<HelloReply>('hello', { guestName: 'Leaver' });
    await c.ok('table:sit', { tableId: welcome(hello), seat: 3, buyIn: 1000 });
    c.socket.disconnect();
    await new Promise((r) => setTimeout(r, 500));
    expect((await store.playerById(hello.playerId))!.bank).toBe(10_000);
  });

  it('reports health', async () => {
    const { url } = await start();
    const res = await fetch(`${url}/healthz`);
    expect(res.status).toBe(200);
    expect(await res.json()).toMatchObject({ ok: true, tables: 2 });
  });
});
