import { Card, rankValue, suitOf } from './cards';

export enum HandCategory {
  HighCard = 0,
  Pair,
  TwoPair,
  Trips,
  Straight,
  Flush,
  FullHouse,
  Quads,
  StraightFlush,
}

export const CATEGORY_NAMES: Record<HandCategory, string> = {
  [HandCategory.HighCard]: 'High Card',
  [HandCategory.Pair]: 'Pair',
  [HandCategory.TwoPair]: 'Two Pair',
  [HandCategory.Trips]: 'Three of a Kind',
  [HandCategory.Straight]: 'Straight',
  [HandCategory.Flush]: 'Flush',
  [HandCategory.FullHouse]: 'Full House',
  [HandCategory.Quads]: 'Four of a Kind',
  [HandCategory.StraightFlush]: 'Straight Flush',
};

export interface HandValue {
  category: HandCategory;
  /** Comparable score: higher is better, equal means a tie. */
  score: number;
  /** The five cards that make the hand. */
  cards: Card[];
  name: string;
}

/** Packs a category plus up to five tiebreak ranks (2–14) into one comparable number. */
function pack(category: HandCategory, kickers: number[]): number {
  let score = category;
  for (let i = 0; i < 5; i++) score = score * 15 + (kickers[i] ?? 0);
  return score;
}

/** Highest card of a straight in these ranks (descending, unique), or 0 if none. Handles the A-2-3-4-5 wheel. */
function straightHigh(uniqueDesc: number[]): number {
  const ranks = uniqueDesc[0] === 14 ? [...uniqueDesc, 1] : uniqueDesc;
  let run = 1;
  for (let i = 1; i < ranks.length; i++) {
    run = ranks[i] === ranks[i - 1] - 1 ? run + 1 : 1;
    if (run === 5) return ranks[i] + 4;
  }
  return 0;
}

export function evaluateFive(cards: Card[]): HandValue {
  const values = cards.map(rankValue).sort((a, b) => b - a);
  const isFlush = cards.every((c) => suitOf(c) === suitOf(cards[0]));
  const unique = [...new Set(values)];
  const high = unique.length === 5 ? straightHigh(unique) : 0;

  // Group ranks by count, biggest group first, then by rank.
  const counts = new Map<number, number>();
  for (const v of values) counts.set(v, (counts.get(v) ?? 0) + 1);
  const groups = [...counts.entries()].sort((a, b) => b[1] - a[1] || b[0] - a[0]);
  const byGroup = groups.map(([rank]) => rank);

  let category: HandCategory;
  let kickers: number[];
  if (high && isFlush) [category, kickers] = [HandCategory.StraightFlush, [high]];
  else if (groups[0][1] === 4) [category, kickers] = [HandCategory.Quads, byGroup];
  else if (groups[0][1] === 3 && groups[1][1] === 2) [category, kickers] = [HandCategory.FullHouse, byGroup];
  else if (isFlush) [category, kickers] = [HandCategory.Flush, values];
  else if (high) [category, kickers] = [HandCategory.Straight, [high]];
  else if (groups[0][1] === 3) [category, kickers] = [HandCategory.Trips, byGroup];
  else if (groups[0][1] === 2 && groups[1][1] === 2) [category, kickers] = [HandCategory.TwoPair, byGroup];
  else if (groups[0][1] === 2) [category, kickers] = [HandCategory.Pair, byGroup];
  else [category, kickers] = [HandCategory.HighCard, values];

  return { category, score: pack(category, kickers), cards, name: CATEGORY_NAMES[category] };
}

/** Best five-card hand from five to seven cards. */
export function evaluate(cards: Card[]): HandValue {
  if (cards.length < 5 || cards.length > 7) throw new Error(`evaluate needs 5-7 cards, got ${cards.length}`);
  let best: HandValue | null = null;
  for (const hand of fiveCardCombos(cards)) {
    const value = evaluateFive(hand);
    if (!best || value.score > best.score) best = value;
  }
  return best!;
}

function* fiveCardCombos(cards: Card[], start = 0, picked: Card[] = []): Generator<Card[]> {
  if (picked.length === 5) {
    yield picked;
    return;
  }
  for (let i = start; i <= cards.length - (5 - picked.length); i++) {
    yield* fiveCardCombos(cards, i + 1, [...picked, cards[i]]);
  }
}
