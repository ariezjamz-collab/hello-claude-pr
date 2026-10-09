/** A card is a two-character string: rank then suit, e.g. "As" (ace of spades), "Td" (ten of diamonds). */
export type Card = string;

export const RANKS = '23456789TJQKA';
export const SUITS = 'cdhs';

/** Returns a random number in [0, 1). Injectable so games can be replayed in tests. */
export type Rng = () => number;

export function rankValue(card: Card): number {
  // 2 → 2 ... A → 14
  return RANKS.indexOf(card[0]) + 2;
}

export function suitOf(card: Card): string {
  return card[1];
}

export function freshDeck(): Card[] {
  const deck: Card[] = [];
  for (const s of SUITS) for (const r of RANKS) deck.push(r + s);
  return deck;
}

/** Fisher–Yates shuffle into a new array. */
export function shuffle(cards: Card[], rng: Rng = Math.random): Card[] {
  const out = cards.slice();
  for (let i = out.length - 1; i > 0; i--) {
    const j = Math.floor(rng() * (i + 1));
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out;
}
