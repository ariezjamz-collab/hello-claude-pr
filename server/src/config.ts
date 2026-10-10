/** Server settings, read from environment variables. See `.env.example` and DEPLOY.md. */
export interface Config {
  port: number;
  /** PostgreSQL connection string. Without it the server keeps everything in memory (development only). */
  databaseUrl: string | null;
  /** OAuth client IDs whose Google sign-in tokens we accept. Empty disables Google sign-in. */
  googleClientIds: string[];
  startingBank: number;
  turnSeconds: number;
  disconnectGraceSeconds: number;
  nextHandDelayMs: number;
  /** How many tables one player may have created at the same time. */
  maxTablesPerPlayer: number;
}

const num = (value: string | undefined, fallback: number) => {
  const n = Number(value);
  return value !== undefined && value !== '' && Number.isFinite(n) ? n : fallback;
};

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  return {
    port: num(env.PORT, 3000),
    databaseUrl: env.DATABASE_URL || null,
    googleClientIds: (env.GOOGLE_CLIENT_IDS ?? '')
      .split(',')
      .map((s) => s.trim())
      .filter(Boolean),
    startingBank: num(env.STARTING_BANK, 10_000),
    // Generous defaults for patchy mobile networks.
    turnSeconds: num(env.TURN_SECONDS, 30),
    disconnectGraceSeconds: num(env.DISCONNECT_GRACE_SECONDS, 60),
    nextHandDelayMs: num(env.NEXT_HAND_DELAY_MS, 4_000),
    maxTablesPerPlayer: num(env.MAX_TABLES_PER_PLAYER, 2),
  };
}
