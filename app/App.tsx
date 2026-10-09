import type { LobbyTable, TableState } from '@pocket-club/server/src/protocol';
import { StatusBar } from 'expo-status-bar';
import { useEffect, useRef, useState } from 'react';
import { StyleSheet } from 'react-native';
import { SafeAreaProvider, SafeAreaView } from 'react-native-safe-area-context';
import { connect, GameSocket, request, tokenStore } from './src/net';
import { ConnectScreen } from './src/screens/ConnectScreen';
import { LobbyScreen } from './src/screens/LobbyScreen';
import { TableScreen } from './src/screens/TableScreen';
import { colors } from './src/theme';

type Screen = { name: 'connect' } | { name: 'lobby' } | { name: 'table'; tableId: string };

export default function App() {
  const socketRef = useRef<GameSocket | null>(null);
  const [screen, setScreen] = useState<Screen>({ name: 'connect' });
  const [me, setMe] = useState<{ name: string; bank: number } | null>(null);
  const [tables, setTables] = useState<LobbyTable[]>([]);
  const [tableState, setTableState] = useState<TableState | null>(null);
  // Read inside socket callbacks, which outlive renders.
  const screenRef = useRef(screen);
  screenRef.current = screen;

  useEffect(() => () => void socketRef.current?.disconnect(), []);

  const socket = () => socketRef.current!;

  const refreshBank = async () => {
    const { bank } = await request(socket(), 'wallet', {});
    setMe((m) => (m ? { ...m, bank } : m));
  };

  const handleConnect = (name: string, serverUrl: string) =>
    new Promise<void>((resolve, reject) => {
      socketRef.current?.disconnect();
      const s = connect(serverUrl);
      socketRef.current = s;
      let settled = false;

      s.on('connect', async () => {
        try {
          // Runs again after every reconnect, so the server knows who this socket is.
          const hello = await request(s, 'hello', { name, token: tokenStore.get() });
          tokenStore.set(hello.token);
          setMe({ name: hello.name, bank: hello.bank });
          setTables(hello.tables);
          const current = screenRef.current;
          if (current.name === 'table') await request(s, 'table:watch', { tableId: current.tableId });
          else if (current.name === 'connect') setScreen({ name: 'lobby' });
          if (!settled) {
            settled = true;
            resolve();
          }
        } catch (err) {
          if (!settled) {
            settled = true;
            reject(err);
          }
        }
      });
      s.on('connect_error', () => {
        if (!settled) {
          settled = true;
          s.disconnect();
          reject(new Error(`Can't reach ${serverUrl}. Is the server running?`));
        }
      });
      s.on('lobby', setTables);
      s.on('table:state', (state) => {
        const current = screenRef.current;
        if (current.name === 'table' && current.tableId === state.tableId) setTableState(state);
      });
    });

  const openTable = async (tableId: string) => {
    setTableState(null);
    setScreen({ name: 'table', tableId });
    await request(socket(), 'table:watch', { tableId });
  };

  const leaveTable = async () => {
    if (screen.name === 'table') await request(socket(), 'table:unwatch', { tableId: screen.tableId }).catch(() => {});
    setScreen({ name: 'lobby' });
    setTableState(null);
    await refreshBank().catch(() => {});
  };

  const tableId = screen.name === 'table' ? screen.tableId : '';

  return (
    <SafeAreaProvider>
      <SafeAreaView style={styles.app}>
        <StatusBar style="light" />
        {screen.name === 'connect' && <ConnectScreen onConnect={handleConnect} />}
        {screen.name === 'lobby' && me && (
          <LobbyScreen
            name={me.name}
            bank={me.bank}
            tables={tables}
            onOpenTable={(id) => void openTable(id)}
            onCreateTable={async (options) => {
              const { tableId: id } = await request(socket(), 'table:create', options);
              await openTable(id);
            }}
            onRefill={async () => {
              const { bank } = await request(socket(), 'wallet:refill', {});
              setMe((m) => (m ? { ...m, bank } : m));
            }}
          />
        )}
        {screen.name === 'table' && me && (
          <TableScreen
            state={tableState}
            bank={me.bank}
            onBack={() => void leaveTable()}
            onAction={async (action) => void (await request(socket(), 'table:action', { tableId, action }))}
            onSit={async (seat, buyIn) => {
              const { bank } = await request(socket(), 'table:sit', { tableId, seat, buyIn });
              setMe((m) => (m ? { ...m, bank } : m));
            }}
            onStand={async () => {
              const { bank } = await request(socket(), 'table:stand', { tableId });
              setMe((m) => (m ? { ...m, bank } : m));
            }}
            onRebuy={async (amount) => {
              const { bank } = await request(socket(), 'table:rebuy', { tableId, amount });
              setMe((m) => (m ? { ...m, bank } : m));
            }}
            onSitIn={async () => void (await request(socket(), 'table:sitIn', { tableId, sittingOut: false }))}
          />
        )}
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const styles = StyleSheet.create({
  app: { flex: 1, backgroundColor: colors.background },
});
