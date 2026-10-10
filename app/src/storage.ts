import * as SecureStore from 'expo-secure-store';
import { Platform } from 'react-native';

/**
 * Small values that survive app restarts. On phones they live in the encrypted keystore;
 * on the web (development only) in localStorage. Failures are treated as "nothing saved".
 */
function item(key: string) {
  return {
    async get(): Promise<string | null> {
      try {
        if (Platform.OS === 'web') return localStorage.getItem(key);
        return await SecureStore.getItemAsync(key);
      } catch {
        return null;
      }
    },
    async set(value: string): Promise<void> {
      try {
        if (Platform.OS === 'web') localStorage.setItem(key, value);
        else await SecureStore.setItemAsync(key, value);
      } catch {
        // Not fatal: the player just signs in again next time.
      }
    },
    async clear(): Promise<void> {
      try {
        if (Platform.OS === 'web') localStorage.removeItem(key);
        else await SecureStore.deleteItemAsync(key);
      } catch {
        // Nothing to clear.
      }
    },
  };
}

/** The login session token from the server. */
export const savedSession = item('pocket-club-session');
/** The server address typed in during development. */
export const savedServerUrl = item('pocket-club-server');
