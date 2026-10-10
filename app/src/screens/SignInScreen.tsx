import { useState } from 'react';
import { ActivityIndicator, Pressable, StyleSheet, Text, TextInput, View } from 'react-native';
import { SERVER_URL } from '../config';
import { googleSignInAvailable } from '../googleAuth';
import { colors } from '../theme';

export type SignInChoice = { kind: 'guest'; name: string } | { kind: 'google' };

interface Props {
  /** Pre-filled server address (development builds only). */
  initialServerUrl: string;
  /** A message to show, e.g. why the player was signed out. */
  initialError?: string | null;
  onSignIn: (choice: SignInChoice, serverUrl: string) => Promise<void>;
}

export function SignInScreen({ initialServerUrl, initialError = null, onSignIn }: Props) {
  const [name, setName] = useState('');
  const [serverUrl, setServerUrl] = useState(initialServerUrl);
  const [busy, setBusy] = useState<'guest' | 'google' | null>(null);
  const [error, setError] = useState<string | null>(initialError);
  const google = googleSignInAvailable();

  const submit = async (choice: SignInChoice) => {
    if (choice.kind === 'guest' && choice.name.trim().length < 2) return setError('Pick a name with at least 2 letters');
    setBusy(choice.kind);
    setError(null);
    try {
      await onSignIn(choice, (SERVER_URL ?? serverUrl).trim());
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(null);
    }
  };

  return (
    <View style={styles.screen}>
      <Text style={styles.logo}>♠ ♥ ♣ ♦</Text>
      <Text style={styles.title}>Pocket Club</Text>
      <Text style={styles.subtitle}>Hold'em with your friends. Free play chips only.</Text>

      {google && (
        <>
          <Pressable
            accessibilityRole="button"
            style={({ pressed }) => [styles.googleButton, pressed && { opacity: 0.85 }]}
            onPress={() => submit({ kind: 'google' })}
            disabled={busy !== null}
          >
            {busy === 'google' ? (
              <ActivityIndicator color={colors.cardBlack} />
            ) : (
              <Text style={styles.googleText}>
                <Text style={styles.googleG}>G</Text> Continue with Google
              </Text>
            )}
          </Pressable>
          <Text style={styles.hint}>Keeps your chips safe if you change phones.</Text>
          <View style={styles.divider}>
            <View style={styles.line} />
            <Text style={styles.or}>or play as a guest</Text>
            <View style={styles.line} />
          </View>
        </>
      )}

      <Text style={styles.label}>Your name</Text>
      <TextInput
        style={styles.input}
        value={name}
        onChangeText={setName}
        placeholder="e.g. River Queen"
        placeholderTextColor={colors.textMuted}
        maxLength={20}
        onSubmitEditing={() => submit({ kind: 'guest', name })}
      />

      {!SERVER_URL && (
        <>
          <Text style={styles.label}>Server (development)</Text>
          <TextInput
            style={styles.input}
            value={serverUrl}
            onChangeText={setServerUrl}
            autoCapitalize="none"
            autoCorrect={false}
            keyboardType="url"
          />
        </>
      )}

      {error && <Text style={styles.error}>{error}</Text>}

      <Pressable
        accessibilityRole="button"
        style={({ pressed }) => [styles.button, pressed && { opacity: 0.8 }]}
        onPress={() => submit({ kind: 'guest', name })}
        disabled={busy !== null}
      >
        {busy === 'guest' ? <ActivityIndicator color={colors.background} /> : <Text style={styles.buttonText}>Play as guest</Text>}
      </Pressable>

      <Text style={styles.footer}>18+ only. Chips have no cash value and cannot be bought, sold or cashed out.</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, justifyContent: 'center', padding: 24, maxWidth: 420, width: '100%', alignSelf: 'center' },
  logo: { color: colors.accent, fontSize: 28, textAlign: 'center', letterSpacing: 6 },
  title: { color: colors.text, fontSize: 40, fontWeight: '800', textAlign: 'center', marginTop: 8 },
  subtitle: { color: colors.textMuted, fontSize: 15, textAlign: 'center', marginBottom: 28 },
  googleButton: { backgroundColor: '#fff', borderRadius: 12, paddingVertical: 15, alignItems: 'center' },
  googleText: { color: colors.cardBlack, fontSize: 17, fontWeight: '700' },
  googleG: { color: '#4285F4', fontWeight: '900' },
  hint: { color: colors.textMuted, fontSize: 13, textAlign: 'center', marginTop: 8 },
  divider: { flexDirection: 'row', alignItems: 'center', gap: 10, marginVertical: 18 },
  line: { flex: 1, height: 1, backgroundColor: colors.border },
  or: { color: colors.textMuted, fontSize: 13 },
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
  button: { backgroundColor: colors.accent, borderRadius: 12, paddingVertical: 15, alignItems: 'center', marginTop: 24 },
  buttonText: { color: colors.background, fontSize: 18, fontWeight: '800' },
  footer: { color: colors.textMuted, fontSize: 12, textAlign: 'center', marginTop: 24 },
});
