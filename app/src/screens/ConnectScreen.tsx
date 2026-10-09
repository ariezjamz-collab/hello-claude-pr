import { useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import { defaultServerUrl } from '../net';
import { colors } from '../theme';

interface Props {
  onConnect: (name: string, serverUrl: string) => Promise<void>;
}

export function ConnectScreen({ onConnect }: Props) {
  const [name, setName] = useState('');
  const [serverUrl, setServerUrl] = useState(defaultServerUrl);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async () => {
    if (!name.trim()) return setError('Pick a name first');
    setBusy(true);
    setError(null);
    try {
      await onConnect(name.trim(), serverUrl.trim());
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <View style={styles.screen}>
      <Text style={styles.logo}>♠ ♥ ♣ ♦</Text>
      <Text style={styles.title}>Pocket Club</Text>
      <Text style={styles.subtitle}>Hold'em with your friends. Play chips only.</Text>

      <Text style={styles.label}>Your name</Text>
      <TextInput
        style={styles.input}
        value={name}
        onChangeText={setName}
        placeholder="e.g. River Queen"
        placeholderTextColor={colors.textMuted}
        maxLength={20}
        autoFocus
        onSubmitEditing={submit}
      />

      <Text style={styles.label}>Server</Text>
      <TextInput
        style={styles.input}
        value={serverUrl}
        onChangeText={setServerUrl}
        autoCapitalize="none"
        autoCorrect={false}
        keyboardType="url"
      />

      {error && <Text style={styles.error}>{error}</Text>}

      <Pressable accessibilityRole="button" style={({ pressed }) => [styles.button, pressed && { opacity: 0.8 }]} onPress={submit} disabled={busy}>
        {busy ? <ActivityIndicator color={colors.background} /> : <Text style={styles.buttonText}>Play</Text>}
      </Pressable>
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, justifyContent: 'center', padding: 24, maxWidth: 420, width: '100%', alignSelf: 'center' },
  logo: { color: colors.accent, fontSize: 28, textAlign: 'center', letterSpacing: 6 },
  title: { color: colors.text, fontSize: 40, fontWeight: '800', textAlign: 'center', marginTop: 8 },
  subtitle: { color: colors.textMuted, fontSize: 15, textAlign: 'center', marginBottom: 32 },
  label: { color: colors.textMuted, fontSize: 13, fontWeight: '600', marginBottom: 6, marginTop: 12 },
  input: {
    backgroundColor: colors.surface,
    borderColor: colors.border,
    borderWidth: 1,
    borderRadius: 10,
    color: colors.text,
    fontSize: 17,
    paddingHorizontal: 14,
    paddingVertical: 12,
  },
  error: { color: colors.danger, marginTop: 14, textAlign: 'center' },
  button: { backgroundColor: colors.accent, borderRadius: 12, paddingVertical: 15, alignItems: 'center', marginTop: 28 },
  buttonText: { color: colors.background, fontSize: 18, fontWeight: '800' },
});
