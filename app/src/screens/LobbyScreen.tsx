import { useState } from 'react';
import { FlatList, Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import type { LobbyTable } from '@pocket-club/server/src/protocol';
import { colors, formatChips } from '../theme';

const STAKES = [
  [1, 2],
  [5, 10],
  [25, 50],
  [100, 200],
];
const SEAT_OPTIONS = [2, 6, 9];

interface Props {
  name: string;
  bank: number;
  tables: LobbyTable[];
  onOpenTable: (tableId: string) => void;
  onCreateTable: (options: { name: string; smallBlind: number; bigBlind: number; maxSeats: number }) => Promise<void>;
  onRefill: () => void;
}

export function LobbyScreen({ name, bank, tables, onOpenTable, onCreateTable, onRefill }: Props) {
  const [creating, setCreating] = useState(false);
  const [tableName, setTableName] = useState('');
  const [stakes, setStakes] = useState(1);
  const [seats, setSeats] = useState(6);
  const [error, setError] = useState<string | null>(null);

  const create = async () => {
    setError(null);
    try {
      const [smallBlind, bigBlind] = STAKES[stakes];
      await onCreateTable({ name: tableName.trim() || `${name}'s table`, smallBlind, bigBlind, maxSeats: seats });
      setCreating(false);
      setTableName('');
    } catch (err) {
      setError((err as Error).message);
    }
  };

  return (
    <View style={styles.screen}>
      <View style={styles.header}>
        <View>
          <Text style={styles.hello}>Hi, {name}</Text>
          <Text style={styles.bank}>
            <Text style={{ color: colors.accent }}>●</Text> {formatChips(bank)} chips
          </Text>
        </View>
        {bank < 10_000 && (
          <Pressable accessibilityRole="button" style={styles.smallButton} onPress={onRefill}>
            <Text style={styles.smallButtonText}>Free chips</Text>
          </Pressable>
        )}
      </View>

      <View style={styles.sectionRow}>
        <Text style={styles.section}>Tables</Text>
        <Pressable accessibilityRole="button" onPress={() => setCreating((c) => !c)}>
          <Text style={styles.link}>{creating ? 'Cancel' : '+ New table'}</Text>
        </Pressable>
      </View>

      {creating && (
        <View style={styles.createBox}>
          <TextInput
            style={styles.input}
            value={tableName}
            onChangeText={setTableName}
            placeholder={`${name}'s table`}
            placeholderTextColor={colors.textMuted}
            maxLength={20}
          />
          <Text style={styles.label}>Blinds</Text>
          <View style={styles.chips}>
            {STAKES.map(([sb, bb], i) => (
              <Choice key={i} label={`${sb}/${bb}`} selected={stakes === i} onPress={() => setStakes(i)} />
            ))}
          </View>
          <Text style={styles.label}>Seats</Text>
          <View style={styles.chips}>
            {SEAT_OPTIONS.map((n) => (
              <Choice key={n} label={n === 2 ? 'Heads-up' : `${n}-max`} selected={seats === n} onPress={() => setSeats(n)} />
            ))}
          </View>
          {error && <Text style={styles.error}>{error}</Text>}
          <Pressable accessibilityRole="button" style={styles.createButton} onPress={create}>
            <Text style={styles.createButtonText}>Create table</Text>
          </Pressable>
        </View>
      )}

      <FlatList
        data={tables}
        keyExtractor={(t) => t.id}
        contentContainerStyle={{ gap: 10, paddingBottom: 24 }}
        ListEmptyComponent={<Text style={styles.empty}>No tables yet. Create one!</Text>}
        renderItem={({ item }) => (
          <Pressable accessibilityRole="button" style={({ pressed }) => [styles.table, pressed && { opacity: 0.8 }]} onPress={() => onOpenTable(item.id)}>
            <View style={{ flex: 1 }}>
              <Text style={styles.tableName}>{item.name}</Text>
              <Text style={styles.tableInfo}>
                NL Hold'em · Blinds {item.smallBlind}/{item.bigBlind}
              </Text>
            </View>
            <View style={styles.seatsBadge}>
              <Text style={styles.seatsText}>
                {item.players}/{item.maxSeats}
              </Text>
            </View>
          </Pressable>
        )}
      />
    </View>
  );
}

function Choice({ label, selected, onPress }: { label: string; selected: boolean; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" style={[styles.choice, selected && styles.choiceSelected]} onPress={onPress}>
      <Text style={[styles.choiceText, selected && { color: colors.background }]}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, padding: 16, maxWidth: 560, width: '100%', alignSelf: 'center' },
  header: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', marginBottom: 24 },
  hello: { color: colors.text, fontSize: 24, fontWeight: '800' },
  bank: { color: colors.textMuted, fontSize: 16, marginTop: 4 },
  smallButton: { borderColor: colors.accent, borderWidth: 1, borderRadius: 20, paddingHorizontal: 14, paddingVertical: 8 },
  smallButtonText: { color: colors.accent, fontWeight: '700' },
  sectionRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10 },
  section: { color: colors.text, fontSize: 18, fontWeight: '700' },
  link: { color: colors.accent, fontSize: 15, fontWeight: '700' },
  createBox: { backgroundColor: colors.surface, borderRadius: 14, padding: 14, marginBottom: 14, borderColor: colors.border, borderWidth: 1 },
  input: {
    backgroundColor: colors.background,
    borderRadius: 10,
    color: colors.text,
    fontSize: 16,
    paddingHorizontal: 12,
    paddingVertical: 10,
  },
  label: { color: colors.textMuted, fontSize: 13, fontWeight: '600', marginTop: 12, marginBottom: 6 },
  chips: { flexDirection: 'row', flexWrap: 'wrap', gap: 8 },
  choice: { borderColor: colors.border, borderWidth: 1, borderRadius: 18, paddingHorizontal: 14, paddingVertical: 7 },
  choiceSelected: { backgroundColor: colors.accent, borderColor: colors.accent },
  choiceText: { color: colors.text, fontWeight: '600' },
  error: { color: colors.danger, marginTop: 10 },
  createButton: { backgroundColor: colors.accent, borderRadius: 10, paddingVertical: 12, alignItems: 'center', marginTop: 16 },
  createButtonText: { color: colors.background, fontWeight: '800', fontSize: 16 },
  table: {
    backgroundColor: colors.surface,
    borderRadius: 14,
    padding: 16,
    flexDirection: 'row',
    alignItems: 'center',
    borderColor: colors.border,
    borderWidth: 1,
  },
  tableName: { color: colors.text, fontSize: 17, fontWeight: '700' },
  tableInfo: { color: colors.textMuted, marginTop: 4 },
  seatsBadge: { backgroundColor: colors.surfaceRaised, borderRadius: 12, paddingHorizontal: 10, paddingVertical: 5 },
  seatsText: { color: colors.text, fontWeight: '700' },
  empty: { color: colors.textMuted, textAlign: 'center', marginTop: 32 },
});
