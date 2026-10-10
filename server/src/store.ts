import { createHash, randomBytes, randomUUID } from 'node:crypto';

export interface PlayerRecord {
  id: string;
  name: string;
  bank: number;
  /** Google account id, or null for guests. */
  googleSub: string | null;
}

export interface TableRecord {
  id: string;
  name: string;
  smallBlind: number;
  bigBlind: number;
  maxSeats: number;
  /** Player who created the table, or null for the built-in tables. */
  createdBy: string | null;
}

/**
 * Everything the server keeps between restarts.
 *
 * Chips are always in exactly one place: a player's bank, or a seat at a table. Moving chips
 * between the two is atomic, so a crash can't create or destroy chips. Seat stacks are saved
 * after every hand; if the server dies mid-hand, `refundAllSeats` returns everyone's stack as of
 * the last finished hand (the interrupted hand is void).
 */
export interface Store {
  /**
   * Waits until this is the only game server using the data, then holds that claim until `close()`.
   * Two servers on one database would each refund and cash out the same seats, creating chips.
   */
  acquireExclusiveLock(onWaiting: () => void, timeoutMs: number): Promise<void>;
  migrate(): Promise<void>;
  close(): Promise<void>;

  createPlayer(name: string, googleSub: string | null, startingBank: number): Promise<PlayerRecord>;
  playerById(id: string): Promise<PlayerRecord | null>;
  playerByGoogleSub(sub: string): Promise<PlayerRecord | null>;
  linkGoogle(playerId: string, sub: string): Promise<void>;

  /** Creates a login session and returns its secret token (only a hash is stored). */
  createSession(playerId: string): Promise<string>;
  playerBySession(token: string): Promise<PlayerRecord | null>;
  deleteSession(token: string): Promise<void>;

  /** Moves chips from the bank onto a seat. Returns the new bank, or null if the bank is too small. */
  buyIn(playerId: string, tableId: string, amount: number): Promise<number | null>;
  /** Records seat stacks after a hand. */
  saveStacks(tableId: string, stacks: { playerId: string; stack: number }[]): Promise<void>;
  /** Removes the seat and moves `stack` chips back to the bank. Returns the new bank. */
  cashOut(playerId: string, tableId: string, stack: number): Promise<number>;
  /** After a crash or restart: returns every seated stack to its owner's bank. Returns how many seats were refunded. */
  refundAllSeats(): Promise<number>;
  /** Tops the bank up to `minimum` if it is below it. Returns the new bank. */
  refill(playerId: string, minimum: number): Promise<number>;

  listTables(): Promise<TableRecord[]>;
  createTable(table: Omit<TableRecord, 'id'>): Promise<TableRecord>;
  deleteTable(id: string): Promise<void>;
}

export const newSessionToken = () => randomBytes(32).toString('base64url');
export const hashToken = (token: string) => createHash('sha256').update(token).digest('hex');
export const newTableId = () => randomUUID().slice(0, 8);

/** Keeps everything in memory. For local development and tests; data is lost on restart. */
export class MemoryStore implements Store {
  private players = new Map<string, PlayerRecord>();
  private sessions = new Map<string, string>();
  private seats = new Map<string, number>(); // `${playerId}/${tableId}` → stack
  private tables = new Map<string, TableRecord>();

  async acquireExclusiveLock() {}
  async migrate() {}
  async close() {}

  async createPlayer(name: string, googleSub: string | null, startingBank: number) {
    const player = { id: randomUUID(), name, bank: startingBank, googleSub };
    this.players.set(player.id, player);
    return { ...player };
  }

  async playerById(id: string) {
    const p = this.players.get(id);
    return p ? { ...p } : null;
  }

  async playerByGoogleSub(sub: string) {
    const p = [...this.players.values()].find((x) => x.googleSub === sub);
    return p ? { ...p } : null;
  }

  async linkGoogle(playerId: string, sub: string) {
    if (await this.playerByGoogleSub(sub)) throw new Error('that Google account is already linked');
    this.mustGet(playerId).googleSub = sub;
  }

  async createSession(playerId: string) {
    const token = newSessionToken();
    this.sessions.set(hashToken(token), playerId);
    return token;
  }

  async playerBySession(token: string) {
    const id = this.sessions.get(hashToken(token));
    return id ? this.playerById(id) : null;
  }

  async deleteSession(token: string) {
    this.sessions.delete(hashToken(token));
  }

  async buyIn(playerId: string, tableId: string, amount: number) {
    const player = this.mustGet(playerId);
    if (player.bank < amount) return null;
    player.bank -= amount;
    const key = `${playerId}/${tableId}`;
    this.seats.set(key, (this.seats.get(key) ?? 0) + amount);
    return player.bank;
  }

  async saveStacks(tableId: string, stacks: { playerId: string; stack: number }[]) {
    for (const { playerId, stack } of stacks) {
      const key = `${playerId}/${tableId}`;
      if (this.seats.has(key)) this.seats.set(key, stack);
    }
  }

  async cashOut(playerId: string, tableId: string, stack: number) {
    const player = this.mustGet(playerId);
    this.seats.delete(`${playerId}/${tableId}`);
    player.bank += stack;
    return player.bank;
  }

  async refundAllSeats() {
    const count = this.seats.size;
    for (const [key, stack] of this.seats) this.mustGet(key.split('/')[0]).bank += stack;
    this.seats.clear();
    return count;
  }

  async refill(playerId: string, minimum: number) {
    const player = this.mustGet(playerId);
    player.bank = Math.max(player.bank, minimum);
    return player.bank;
  }

  async listTables() {
    return [...this.tables.values()].map((t) => ({ ...t }));
  }

  async createTable(table: Omit<TableRecord, 'id'>) {
    const record = { ...table, id: newTableId() };
    this.tables.set(record.id, record);
    return { ...record };
  }

  async deleteTable(id: string) {
    this.tables.delete(id);
  }

  /** Test helper: the stack stored for a seat, or undefined. */
  storedStack(playerId: string, tableId: string) {
    return this.seats.get(`${playerId}/${tableId}`);
  }

  private mustGet(id: string) {
    const p = this.players.get(id);
    if (!p) throw new Error('unknown player');
    return p;
  }
}
