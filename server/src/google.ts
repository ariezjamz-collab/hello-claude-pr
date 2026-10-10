import { OAuth2Client } from 'google-auth-library';

export interface GoogleIdentity {
  sub: string;
  name: string;
}

export type GoogleVerifier = (idToken: string) => Promise<GoogleIdentity>;

/** Checks a Google sign-in ID token (signature, expiry and audience) and returns who it belongs to. */
export function googleVerifier(clientIds: string[]): GoogleVerifier {
  const client = new OAuth2Client();
  return async (idToken) => {
    if (clientIds.length === 0) throw new Error('Google sign-in is not set up on this server');
    let payload;
    try {
      const ticket = await client.verifyIdToken({ idToken, audience: clientIds });
      payload = ticket.getPayload();
    } catch {
      throw new Error('Google sign-in failed, please try again');
    }
    if (!payload?.sub) throw new Error('Google sign-in failed, please try again');
    return { sub: payload.sub, name: payload.given_name || payload.name || 'Player' };
  };
}
