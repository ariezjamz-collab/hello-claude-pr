import { loadConfig } from './config.js';
import { createGameServer } from './game.js';
import { googleVerifier } from './google.js';
import { PgStore } from './pgStore.js';
import { MemoryStore } from './store.js';

const config = loadConfig();

if (!config.databaseUrl) {
  console.warn('DATABASE_URL is not set: keeping players and chips in memory. They will be lost when the server stops.');
}
if (config.googleClientIds.length === 0) {
  console.warn('GOOGLE_CLIENT_IDS is not set: Google sign-in is disabled, guests only.');
}

const store = config.databaseUrl ? new PgStore(config.databaseUrl) : new MemoryStore();
const server = await createGameServer({ config, store, verifyGoogle: googleVerifier(config.googleClientIds) });
const port = await server.listen(config.port);
console.log(`Pocket Club server listening on port ${port}`);

// Deploys send SIGTERM: finish running hands, put chips back in banks, then exit.
let stopping = false;
for (const signal of ['SIGTERM', 'SIGINT'] as const) {
  process.on(signal, async () => {
    if (stopping) return process.exit(1);
    stopping = true;
    console.log(`${signal} received: finishing running hands before stopping`);
    try {
      await server.shutdown();
      process.exit(0);
    } catch (err) {
      console.error('shutdown failed', err);
      process.exit(1);
    }
  });
}
