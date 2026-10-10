import Constants from 'expo-constants';
import { Platform } from 'react-native';
import { io, Socket } from 'socket.io-client';
import type { ClientToServer, ServerToClient } from '@pocket-club/server/src/protocol';

export type GameSocket = Socket<ServerToClient, ClientToServer>;
type Event = keyof ClientToServer;
type Payload<E extends Event> = Parameters<ClientToServer[E]>[0];
type ReplyOf<E extends Event> = Parameters<Parameters<ClientToServer[E]>[1]>[0];
/** The data part of a successful reply. */
export type Success<E extends Event> = Extract<ReplyOf<E>, { ok: true }>;

export function connect(url: string): GameSocket {
  // Keep retrying quickly: on hill-area mobile networks short drop-outs are normal.
  return io(url, { transports: ['websocket'], reconnectionDelay: 1000, reconnectionDelayMax: 5000, timeout: 10_000 });
}

/** Sends a request and resolves with the reply, or rejects with the server's error message. */
export function request<E extends Event>(socket: GameSocket, event: E, payload: Payload<E>): Promise<Success<E>> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('the server did not answer')), 8000);
    const emit = socket.emit as unknown as (e: string, p: unknown, ack: (r: ReplyOf<E>) => void) => void;
    emit.call(socket, event, payload, (reply) => {
      clearTimeout(timer);
      if (reply.ok) resolve(reply as Success<E>);
      else reject(new Error(reply.error));
    });
  });
}

/**
 * Best guess at where the game server runs during development: the same machine that serves the app.
 * In Expo Go, `hostUri` is the dev machine's LAN address (e.g. "192.168.1.20:8081").
 */
export function defaultServerUrl(): string {
  if (Platform.OS === 'web' && typeof window !== 'undefined') return `http://${window.location.hostname}:3000`;
  const host = Constants.expoConfig?.hostUri?.split(':')[0];
  return `http://${host ?? 'localhost'}:3000`;
}
