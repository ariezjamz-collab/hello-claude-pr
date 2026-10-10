import { Platform } from 'react-native';
import { GOOGLE_WEB_CLIENT_ID } from './config';

type GoogleModule = typeof import('@react-native-google-signin/google-signin');

let loaded: GoogleModule | null | undefined;

/**
 * Loads Google sign-in when it can work: an Android build that includes the native module
 * (not Expo Go) and has a web client ID configured. Otherwise the app offers guest play only.
 */
function load(): GoogleModule | null {
  if (loaded !== undefined) return loaded;
  loaded = null;
  if (Platform.OS !== 'android' || !GOOGLE_WEB_CLIENT_ID) return loaded;
  try {
    const mod: GoogleModule = require('@react-native-google-signin/google-signin');
    mod.GoogleSignin.configure({ webClientId: GOOGLE_WEB_CLIENT_ID });
    loaded = mod;
  } catch {
    // Expo Go doesn't contain the native module.
  }
  return loaded;
}

export const googleSignInAvailable = () => load() !== null;

/** Shows Google's account picker. Returns an ID token for the server to check, or null if the player cancelled. */
export async function signInWithGoogle(): Promise<string | null> {
  const mod = load();
  if (!mod) throw new Error('Google sign-in is not available in this version of the app');
  await mod.GoogleSignin.hasPlayServices({ showPlayServicesUpdateDialog: true });
  const response = await mod.GoogleSignin.signIn();
  if (!mod.isSuccessResponse(response)) return null;
  if (!response.data.idToken) throw new Error('Google sign-in did not complete, please try again');
  return response.data.idToken;
}

/** Forgets the Google account on this device so the next sign-in shows the account picker again. */
export async function signOutOfGoogle(): Promise<void> {
  try {
    await load()?.GoogleSignin.signOut();
  } catch {
    // Already signed out.
  }
}
