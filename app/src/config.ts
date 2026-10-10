/**
 * Build-time settings. Expo bakes EXPO_PUBLIC_* variables into the app when it is built,
 * so set them in eas.json (store builds) or in app/.env (development). See .env.example.
 */

/** The game server, e.g. "https://play.example.in". When unset (development), the sign-in screen asks for it. */
export const SERVER_URL: string | null = process.env.EXPO_PUBLIC_SERVER_URL || null;

/** The "Web application" OAuth client ID from Google Cloud; needed for Google sign-in on Android. */
export const GOOGLE_WEB_CLIENT_ID: string | null = process.env.EXPO_PUBLIC_GOOGLE_WEB_CLIENT_ID || null;
