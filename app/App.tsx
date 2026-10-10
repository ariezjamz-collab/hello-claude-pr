import type { HelloPayload, LobbyTable, TableState } from '@pocket-club/server/src/protocol';
import { StatusBar } from 'expo-status-bar';
import { useEffect, useRef, useState } from 'react';
import { ActivityIndicator, StyleSheet, Text, View } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { SERVER_URL } from './src/config';
import { googleSignInAvailable, signInWithGoogle, signOutOfGoogle } from './src/googleAuth';
import { connect, defaultServerUrl, GameSocket, request } from './src/net';
import { LobbyScreen } from './src/screens/LobbyScreen';
import { SignInChoice, SignInScreen } from './src/screens/SignInScreen';
import { TableScreen } from './src/screens/TableScreen';
import { savedServerUrl, savedSession } from './src/storage';
import { colors } from './src/theme';

type Screen = { name: 'starting' } | { name: 'signin'; error?: string | null } | { name: 'lobby' } | { name: 'table'; tableId: string };

/** Errors that mean the saved login is no longer valid (as opposed to a network hiccup). */
const isSignInError = (message: string) => /sign-in has expired|sign in again/.test(message);

export default function App() {
  const socketRef = useRef<GameSocket | null>(null);
  /** What to send with `hello` on the next (re)connect: a new sign-in first, then the session. */
  const authRef = useRef<HelloPayload>({});
  const [screen, setScreen] = useState<Screen>({ name: 'starting' });
  const [me, setMe] = useState<{ name: string; bank: number; isGuest: boolean } | null>(null);
  const [tables, setTables] = useState<LobbyTable[]>([]);
  const [tableState, setTableState] = useState<TableState | null>(null);
  const [connected, setConnected] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [serverUrl, setServerUrl] = useState(SERVER_URL ?? defaultServerUrl());
  // Read inside socket callbacks, which outlive renders.
  const screenRef = useRef(screen);
  screenRef.current = screen;

  const socket = () => socketRef.current!;
  const setBank = (bank: number) => setMe((m) => (m ? { ...m, bank } : m));

  /** Connects and signs in. Resolves once signed in; afterwards it re-signs in after every reconnect. */
  const signIn = (url: string, auth: HelloPayload) =>
    new Promise<void>((resolve, reject) => {
      socketRef.current?.disconnect();
      const s = connect(url);
      socketRef.current = s;
      authRef.current = auth;
      let settled = false;
      const settle = (err?: Error) => {
        if (settled) return;
        settled = true;
        if (err) reject(err);
        else resolve();
      };

      s.on('connect', async () => {
        setConnected(true);
        try {
          const hello = await request(s, 'hello', authRef.current);
          authRef.current = { session: hello.session };
          await savedSession.set(hello.session);
          if (!SERVER_URL) await savedServerUrl.set(url);
          setMe({ name: hello.name, bank: hello.bank, isGuest: hello.isGuest });
          setTables(hello.tables);
          const current = screenRef.current;
          if (current.name === 'table') await request(s, 'table:watch', { tableId: current.tableId });
          else if (current.name !== 'lobby') setScreen({ name: 'lobby' });
          settle();
        } catch (err) {
          const message = (err as Error).message;
          if (isSignInError(message) || !settled) {
            s.disconnect();
            if (isSignInError(message)) await savedSession.clear();
            if (settled) setScreen({ name: 'signin', error: message });
            settle(err as Error);
          } else {
            // A hiccup while re-signing in: reconnect and try again.
            s.disconnect().connect();
          }
        }
      });
      s.on('disconnect', () => setConnected(false));
      s.on('connect_error', () => {
        setConnected(false);
        if (!settled) {
          s.disconnect();
          settle(new Error(SERVER_URL ? "Can't reach the game server. Check your internet and try again." : `Can't reach ${url}. Is the server running?`));
        }
      });
      s.on('lobby', setTables);
      s.on('notice', setNotice);
      s.on('table:state', (state) => {
        const current = screenRef.current;
        if (current.name === 'table' && current.tableId === state.tableId) setTableState(state);
      });
    });

  // On launch, resume the saved login if there is one.
  useEffect(() => {
    void (async () => {
      const [session, url] = await Promise.all([savedSession.get(), savedServerUrl.get()]);
      const target = SERVER_URL ?? url ?? defaultServerUrl();
      setServerUrl(target);
      if (!session) return setScreen({ name: 'signin' });
      try {
        await signIn(target, { session });
      } catch (err) {
        setScreen({ name: 'signin', error: (err as Error).message });
      }
    })();
    return () => void socketRef.current?.disconnect();
  }, []);

  useEffect(() => {
    if (!notice) return;
    const t = setTimeout(() => setNotice(null), 10_000);
    return () => clearTimeout(t);
  }, [notice]);

  const handleSignIn = async (choice: SignInChoice, url: string) => {
    setServerUrl(url);
    if (choice.kind === 'google') {
      const idToken = await signInWithGoogle();
      if (idToken) await signIn(url, { googleIdToken: idToken });
    } else {
      await signIn(url, { guestName: choice.name });
    }
  };

  const linkGoogle = async () => {
    const idToken = await signInWithGoogle();
    if (!idToken) return;
    const hello = await request(socket(), 'hello', { ...authRef.current, googleIdToken: idToken });
    authRef.current = { session: hello.session };
    await savedSession.set(hello.session);
    setMe({ name: hello.name, bank: hello.bank, isGuest: hello.isGuest });
  };

  const signOut = async () => {
    await request(socket(), 'logout', {}).catch(() => {});
    socketRef.current?.disconnect();
    socketRef.current = null;
    await Promise.all([savedSession.clear(), signOutOfGoogle()]);
    setMe(null);
    setScreen({ name: 'signin' });
  };

  const openTable = async (tableId: string) => {
    setTableState(null);
    setScreen({ name: 'table', tableId });
    await request(socket(), 'table:watch', { tableId });
  };

  const leaveTable = async () => {
    if (screen.name === 'table') await request(socket(), 'table:unwatch', { tableId: screen.tableId }).catch(() => {});
    setScreen({ name: 'lobby' });
    setTableState(null);
    await request(socket(), 'wallet', {})
      .then(({ bank }) => setBank(bank))
      .catch(() => {});
  };

  const tableId = screen.name === 'table' ? screen.tableId : '';

  return (
    <SafeAreaProvider>
      <SafeAreaView style={styles.app}>
        <StatusBar style="light" />
        {me && !connected && (
          <View style={[styles.banner, { backgroundColor: colors.raise }]}>
            <ActivityIndicator size="small" color={colors.background} />
            <Text style={styles.bannerText}>Reconnecting… your seat is kept for a minute</Text>
          </View>
        )}
        {notice && (
          <View style={[styles.banner, { backgroundColor: colors.call }]}>
            <Text style={[styles.bannerText, { color: '#fff' }]}>{notice}</Text>
          </View>
        )}

        {screen.name === 'starting' && (
          <View style={styles.center}>
            <ActivityIndicator color={colors.accent} size="large" />
          </View>
        )}
        {screen.name === 'signin' && <SignInScreen initialServerUrl={serverUrl} initialError={screen.error} onSignIn={handleSignIn} />}
        {screen.name === 'lobby' && me && (
          <LobbyScreen
            name={me.name}
            bank={me.bank}
            isGuest={me.isGuest}
            canLinkGoogle={googleSignInAvailable()}
            tables={tables}
            onOpenTable={(id) => void openTable(id)}
            onCreateTable={async (options) => {
              const { tableId: id } = await request(socket(), 'table:create', options);
              await openTable(id);
            }}
            onRefill={async () => setBank((await request(socket(), 'wallet:refill', {})).bank)}
            onLinkGoogle={linkGoogle}
            onSignOut={() => void signOut()}
          />
        )}
        {screen.name === 'table' && me && (
          <TableScreen
            state={tableState}
            bank={me.bank}
            onBack={() => void leaveTable()}
            onAction={async (action) => void (await request(socket(), 'table:action', { tableId, action }))}
            onSit={async (seat, buyIn) => setBank((await request(socket(), 'table:sit', { tableId, seat, buyIn })).bank)}
            onStand={async () => setBank((await request(socket(), 'table:stand', { tableId })).bank)}
            onRebuy={async (amount) => setBank((await request(socket(), 'table:rebuy', { tableId, amount })).bank)}
            onSitIn={async () => void (await request(socket(), 'table:sitIn', { tableId, sittingOut: false }))}
          />
        )}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({
  app: { flex: 1, backgroundColor: colors.background },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  banner: { flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8, paddingVertical: 8, paddingHorizontal: 12 },
  bannerText: { color: colors.background, fontWeight: '700', textAlign: 'center', flexShrink: 1 },
});
