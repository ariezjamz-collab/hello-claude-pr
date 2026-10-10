/**
 * The same checks against the in-memory store and, when TEST_DATABASE_URL is set, against PostgreSQL:
 *
 *   TEST_DATABASE_URL=postgres://user:pass@localhost/pocket_test npm test -w @pocket-club/server
 */
import pg from 'pg';
import { afterAll, beforeEach, describe, expect, it } from 'vitest';
import { PgStore } from '../src/pgStore';
import { MemoryStore, Store } from '../src/store';

const databaseUrl = process.env.TEST_DATABASE_URL;

async function resetDatabase() {
  const client = new pg.Client({ connectionString: databaseUrl });
  await client.connect();
  await client.query('drop table if exists seats, sessions, poker_tables, players cascade');
  await client.end();
}

const kinds: [string, () => Promise<Store>][] = [['memory', async () => new MemoryStore()]];
if (databaseUrl) {
  kinds.push([
    'postgres',
    async () => {
      await resetDatabase();
      return new PgStore(databaseUrl);
    },
  ]);
}

describe.skipIf(!databaseUrl)('postgres server lock', () => {
  it('lets only one game server use the database at a time', async () => {
    const first = new PgStore(databaseUrl!);
    const second = new PgStore(databaseUrl!);
    await first.acquireExclusiveLock(() => {}, 1000);
    let waited = false;
    await expect(second.acquireExclusiveLock(() => (waited = true), 1500)).rejects.toThrow('another game server');
    expect(waited).toBe(true);
    // Once the first server is gone, the next one gets in.
    await first.close();
    await second.acquireExclusiveLock(() => {}, 1000);
    await second.close();
  });
});

describe.each(kinds)('%s store', (_kind, make) => {
  let store: Store;
  const opened: Store[] = [];

  beforeEach(async () => {
    store = await make();
    opened.push(store);
    await store.migrate();
  });
  afterAll(async () => {
    for (const s of opened) await s.close().catch(() => {});
  });

  it('creates players and finds them by id, Google account and session', async () => {
    const guest = await store.createPlayer('Asha', null, 10_000);
    const google = await store.createPlayer('Rina', 'g-1', 10_000);
    expect(await store.playerById(guest.id)).toEqual(guest);
    expect(await store.playerByGoogleSub('g-1')).toEqual(google);
    const token = await store.createSession(guest.id);
    expect((await store.playerBySession(token))?.id).toBe(guest.id);
    await store.deleteSession(token);
    expect(await store.playerBySession(token)).toBeNull();
  });

  it('will not link one Google account to two players', async () => {
    await store.createPlayer('Rina', 'g-1', 10_000);
    const guest = await store.createPlayer('Asha', null, 10_000);
    await expect(store.linkGoogle(guest.id, 'g-1')).rejects.toThrow('already linked');
    await store.linkGoogle(guest.id, 'g-2');
    expect((await store.playerById(guest.id))?.googleSub).toBe('g-2');
  });

  it('moves chips between bank and seat without losing any', async () => {
    const p = await store.createPlayer('Asha', null, 10_000);
    const t = await store.createTable({ name: 'T', smallBlind: 5, bigBlind: 10, maxSeats: 6, createdBy: null });
    expect(await store.buyIn(p.id, t.id, 3_000)).toBe(7_000);
    expect(await store.buyIn(p.id, t.id, 8_000)).toBeNull(); // not enough
    expect(await store.buyIn(p.id, t.id, 500)).toBe(6_500); // rebuy adds to the seat
    await store.saveStacks(t.id, [{ playerId: p.id, stack: 4_200 }]);
    expect(await store.cashOut(p.id, t.id, 4_200)).toBe(10_700);
    // Cashing out removed the seat, so a later refund finds nothing to return.
    expect(await store.refundAllSeats()).toBe(0);
    expect((await store.playerById(p.id))?.bank).toBe(10_700);
  });

  it('refunds saved stacks after a crash', async () => {
    const a = await store.createPlayer('Asha', null, 10_000);
    const b = await store.createPlayer('Bijoy', null, 10_000);
    const t1 = await store.createTable({ name: 'One', smallBlind: 5, bigBlind: 10, maxSeats: 6, createdBy: null });
    const t2 = await store.createTable({ name: 'Two', smallBlind: 5, bigBlind: 10, maxSeats: 6, createdBy: a.id });
    await store.buyIn(a.id, t1.id, 1_000);
    await store.buyIn(a.id, t2.id, 2_000);
    await store.buyIn(b.id, t1.id, 1_000);
    await store.saveStacks(t1.id, [
      { playerId: a.id, stack: 1_300 },
      { playerId: b.id, stack: 700 },
    ]);
    expect(await store.refundAllSeats()).toBe(3);
    expect((await store.playerById(a.id))?.bank).toBe(7_000 + 1_300 + 2_000);
    expect((await store.playerById(b.id))?.bank).toBe(9_000 + 700);
  });

  it('refills only when the bank is low', async () => {
    const p = await store.createPlayer('Asha', null, 500);
    expect(await store.refill(p.id, 10_000)).toBe(10_000);
    expect(await store.refill(p.id, 5_000)).toBe(10_000);
  });

  it('stores and deletes tables', async () => {
    const t = await store.createTable({ name: 'Friday game', smallBlind: 1, bigBlind: 2, maxSeats: 9, createdBy: null });
    expect(await store.listTables()).toEqual([t]);
    await store.deleteTable(t.id);
    expect(await store.listTables()).toEqual([]);
  });

  it('never lets a bank go negative, even with simultaneous buy-ins', async () => {
    const p = await store.createPlayer('Asha', null, 1_000);
    const t = await store.createTable({ name: 'T', smallBlind: 5, bigBlind: 10, maxSeats: 6, createdBy: null });
    const results = await Promise.all(Array.from({ length: 5 }, () => store.buyIn(p.id, t.id, 400)));
    expect(results.filter((r) => r !== null)).toHaveLength(2);
    expect((await store.playerById(p.id))?.bank).toBe(200);
  });
});
