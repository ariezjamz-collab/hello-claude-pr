/**
 * The messages exchanged between the app and the server over Socket.IO.
 * Every client request takes an acknowledgement callback that receives `{ ok: true, ...data }`
 * or `{ ok: false, error }`.
 */
import type { Action, TableView } from '@pocket-club/engine';

export interface LobbyTable {
  id: string;
  name: string;
  smallBlind: number;
  bigBlind: number;
  maxSeats: number;
  players: number;
}

export interface TableState extends TableView {
  tableId: string;
  name: string;
  /** When the current player's turn times out (ms since epoch), or null. */
  turnDeadline: number | null;
}

export type Reply<T = object> = ({ ok: true } & T) | { ok: false; error: string };
type Ack<T = object> = (reply: Reply<T>) => void;

export interface HelloReply {
  playerId: string;
  /** Keep this to reconnect as the same player. */
  token: string;
  name: string;
  bank: number;
  tables: LobbyTable[];
}

export interface ClientToServer {
  hello(payload: { name?: string; token?: string }, ack: Ack<HelloReply>): void;
  wallet(payload: object, ack: Ack<{ bank: number }>): void;
  'wallet:refill'(payload: object, ack: Ack<{ bank: number }>): void;
  'lobby:list'(payload: object, ack: Ack<{ tables: LobbyTable[] }>): void;
  'table:create'(payload: { name: string; smallBlind: number; bigBlind: number; maxSeats: number }, ack: Ack<{ tableId: string }>): void;
  'table:watch'(payload: { tableId: string }, ack: Ack): void;
  'table:unwatch'(payload: { tableId: string }, ack: Ack): void;
  'table:sit'(payload: { tableId: string; seat: number; buyIn: number }, ack: Ack<{ bank: number }>): void;
  'table:stand'(payload: { tableId: string }, ack: Ack<{ bank: number }>): void;
  'table:rebuy'(payload: { tableId: string; amount: number }, ack: Ack<{ bank: number }>): void;
  'table:sitIn'(payload: { tableId: string; sittingOut: boolean }, ack: Ack): void;
  'table:action'(payload: { tableId: string; action: Action }, ack: Ack): void;
}

export interface ServerToClient {
  lobby(tables: LobbyTable[]): void;
  'table:state'(state: TableState): void;
}
