import pg from 'pg';
import { hashToken, newSessionToken, newTableId, PlayerRecord, Store, TableRecord } from './store.js';

// Chip amounts are BIGINT in Postgres; they fit comfortably in a JS number.
pg.types.setTypeParser(pg.types.builtins.INT8, (v) => Number(v));

const SCHEMA = `
create table if not exists players (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  bank bigint not null check (bank >= 0),
  google_sub text unique,
  created_at timestamptz not null default now()
);
create table if not exists sessions (
  token_hash text primary key,
  player_id uuid not null references players(id) on delete cascade,
  created_at timestamptz not null default now()
);
create index if not exists sessions_player on sessions(player_id);
create table if not exists poker_tables (
  id text primary key,
  name text not null,
  small_blind integer not null,
  big_blind integer not null,
  max_seats integer not null,
  created_by uuid references players(id) on delete set null,
  created_at timestamptz not null default now()
);
create table if not exists seats (
  player_id uuid not null references players(id) on delete cascade,
  table_id text not null,
  stack bigint not null check (stack >= 0),
  primary key (player_id, table_id)
);
`;

type Row = Record<string, unknown>;
const toPlayer = (r: Row): PlayerRecord => ({
  id: r.id as string,
  name: r.name as string,
  bank: r.bank as number,
  googleSub: (r.google_sub as string | null) ?? null,
});
const toTable = (r: Row): TableRecord => ({
  id: r.id as string,
  name: r.name as string,
  smallBlind: r.small_blind as number,
  bigBlind: r.big_blind as number,
  maxSeats: r.max_seats as number,
  createdBy: (r.created_by as string | null) ?? null,
});

/** Arbitrary constant naming the "one game server per database" advisory lock. */
const SERVER_LOCK_KEY = 727_001;

export class PgStore implements Store {
  private pool: pg.Pool;
  private lockClient: pg.Client | null = null;

  constructor(private connectionString: string) {
    this.pool = new pg.Pool({ connectionString, max: 10 });
  }

  async acquireExclusiveLock(onWaiting: () => void, timeoutMs: number) {
    // The lock belongs to this connection: it is released when the connection closes, even if the process crashes.
    const client = new pg.Client({ connectionString: this.connectionString });
    await client.connect();
    const deadline = Date.now() + timeoutMs;
    let warned = false;
    for (;;) {
      const { rows } = await client.query('select pg_try_advisory_lock($1) as locked', [SERVER_LOCK_KEY]);
      if (rows[0].locked) break;
      if (Date.now() > deadline) {
        await client.end();
        throw new Error('another game server is still using this database');
      }
      if (!warned) onWaiting();
      warned = true;
      await new Promise((r) => setTimeout(r, 1000));
    }
    this.lockClient = client;
  }

  async migrate() {
    await this.pool.query(SCHEMA);
  }

  async close() {
    await this.pool.end();
    await this.lockClient?.end();
    this.lockClient = null;
  }

  private async transaction<T>(fn: (client: pg.PoolClient) => Promise<T>): Promise<T> {
    const client = await this.pool.connect();
    try {
      await client.query('begin');
      const result = await fn(client);
      await client.query('commit');
      return result;
    } catch (err) {
      await client.query('rollback');
      throw err;
    } finally {
      client.release();
    }
  }

  async createPlayer(name: string, googleSub: string | null, startingBank: number) {
    const { rows } = await this.pool.query('insert into players (name, google_sub, bank) values ($1, $2, $3) returning *', [
      name,
      googleSub,
      startingBank,
    ]);
    return toPlayer(rows[0]);
  }

  async playerById(id: string) {
    const { rows } = await this.pool.query('select * from players where id = $1', [id]);
    return rows[0] ? toPlayer(rows[0]) : null;
  }

  async playerByGoogleSub(sub: string) {
    const { rows } = await this.pool.query('select * from players where google_sub = $1', [sub]);
    return rows[0] ? toPlayer(rows[0]) : null;
  }

  async linkGoogle(playerId: string, sub: string) {
    try {
      await this.pool.query('update players set google_sub = $2 where id = $1', [playerId, sub]);
    } catch (err) {
      if ((err as { code?: string }).code === '23505') throw new Error('that Google account is already linked');
      throw err;
    }
  }

  async createSession(playerId: string) {
    const token = newSessionToken();
    await this.pool.query('insert into sessions (token_hash, player_id) values ($1, $2)', [hashToken(token), playerId]);
    return token;
  }

  async playerBySession(token: string) {
    const { rows } = await this.pool.query(
      'select p.* from sessions s join players p on p.id = s.player_id where s.token_hash = $1',
      [hashToken(token)],
    );
    return rows[0] ? toPlayer(rows[0]) : null;
  }

  async deleteSession(token: string) {
    await this.pool.query('delete from sessions where token_hash = $1', [hashToken(token)]);
  }

  async buyIn(playerId: string, tableId: string, amount: number) {
    return this.transaction(async (c) => {
      const { rows } = await c.query('update players set bank = bank - $2 where id = $1 and bank >= $2 returning bank', [playerId, amount]);
      if (!rows[0]) return null;
      await c.query(
        `insert into seats (player_id, table_id, stack) values ($1, $2, $3)
         on conflict (player_id, table_id) do update set stack = seats.stack + excluded.stack`,
        [playerId, tableId, amount],
      );
      return rows[0].bank as number;
    });
  }

  async saveStacks(tableId: string, stacks: { playerId: string; stack: number }[]) {
    if (stacks.length === 0) return;
    await this.pool.query(
      `update seats set stack = v.stack
       from unnest($2::uuid[], $3::bigint[]) as v(player_id, stack)
       where seats.table_id = $1 and seats.player_id = v.player_id`,
      [tableId, stacks.map((s) => s.playerId), stacks.map((s) => s.stack)],
    );
  }

  async cashOut(playerId: string, tableId: string, stack: number) {
    return this.transaction(async (c) => {
      await c.query('delete from seats where player_id = $1 and table_id = $2', [playerId, tableId]);
      const { rows } = await c.query('update players set bank = bank + $2 where id = $1 returning bank', [playerId, stack]);
      return rows[0].bank as number;
    });
  }

  async refundAllSeats() {
    return this.transaction(async (c) => {
      await c.query(
        `update players p set bank = p.bank + s.total
         from (select player_id, sum(stack) as total from seats group by player_id) s
         where p.id = s.player_id`,
      );
      const { rowCount } = await c.query('delete from seats');
      return rowCount ?? 0;
    });
  }

  async refill(playerId: string, minimum: number) {
    const { rows } = await this.pool.query('update players set bank = greatest(bank, $2) where id = $1 returning bank', [playerId, minimum]);
    return rows[0].bank as number;
  }

  async listTables() {
    const { rows } = await this.pool.query('select * from poker_tables order by created_at');
    return rows.map(toTable);
  }

  async createTable(table: Omit<TableRecord, 'id'>) {
    const { rows } = await this.pool.query(
      `insert into poker_tables (id, name, small_blind, big_blind, max_seats, created_by)
       values ($1, $2, $3, $4, $5, $6) returning *`,
      [newTableId(), table.name, table.smallBlind, table.bigBlind, table.maxSeats, table.createdBy],
    );
    return toTable(rows[0]);
  }

  async deleteTable(id: string) {
    await this.pool.query('delete from poker_tables where id = $1', [id]);
  }
}
