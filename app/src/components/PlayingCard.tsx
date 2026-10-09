import { StyleSheet, Text, View } from 'react-native';
import { colors } from '../theme';

const SUIT_SYMBOL: Record<string, string> = { s: '♠', h: '♥', d: '♦', c: '♣' };
const SIZES = {
  sm: { width: 26, height: 36, rank: 13, suit: 12 },
  md: { width: 40, height: 56, rank: 18, suit: 18 },
  lg: { width: 56, height: 78, rank: 24, suit: 26 },
};

interface Props {
  /** e.g. "As". Omit to draw the back of a card. */
  card?: string;
  size?: keyof typeof SIZES;
  /** Dim the card, e.g. for folded hands. */
  faded?: boolean;
  /** Highlight the card, e.g. when it is part of the winning hand. */
  highlight?: boolean;
}

export function PlayingCard({ card, size = 'md', faded, highlight }: Props) {
  const s = SIZES[size];
  const box = { width: s.width, height: s.height, opacity: faded ? 0.4 : 1 };
  if (!card) {
    return (
      <View style={[styles.card, styles.back, box]}>
        <View style={styles.backInner} />
      </View>
    );
  }
  const rank = card[0] === 'T' ? '10' : card[0];
  const suit = card[1];
  const color = suit === 'h' || suit === 'd' ? colors.cardRed : colors.cardBlack;
  return (
    <View style={[styles.card, styles.face, box, highlight && styles.highlight]} accessibilityLabel={`${rank} of ${suit}`}>
      <Text style={[styles.rank, { color, fontSize: s.rank }]}>{rank}</Text>
      <Text style={[styles.suit, { color, fontSize: s.suit }]}>{SUIT_SYMBOL[suit]}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  card: {
    borderRadius: 5,
    overflow: 'hidden',
    shadowColor: '#000',
    shadowOpacity: 0.35,
    shadowRadius: 3,
    shadowOffset: { width: 0, height: 2 },
    elevation: 3,
  },
  face: {
    backgroundColor: colors.cardFace,
    alignItems: 'center',
    justifyContent: 'center',
  },
  highlight: {
    borderWidth: 2,
    borderColor: colors.accent,
  },
  back: {
    backgroundColor: colors.cardBack,
    padding: 3,
  },
  backInner: {
    flex: 1,
    borderRadius: 3,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.35)',
  },
  rank: {
    fontWeight: '800',
    lineHeight: undefined,
  },
  suit: {
    marginTop: -2,
  },
});
