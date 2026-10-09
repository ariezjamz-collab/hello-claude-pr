import type { Card } from './cards';
import type { HandResult, LegalActions, Phase, Street, Table, TableConfig } from './table';

export * from './cards';
export * from './evaluator';
export * from './table';

export interface SeatView {
  index: number;
  playerId: string;
  name: string;
  stack: number;
  bet: number;
  inHand: boolean;
  folded: boolean;
  allIn: boolean;
  sittingOut: boolean;
  lastAction: string | null;
  /** Visible hole cards, or null when this viewer may not see them. */
  cards: Card[] | null;
  /** Whether the seat holds cards (so the client can draw card backs). */
  hasCards: boolean;
}

export interface TableView {
  config: TableConfig;
  phase: Phase;
  street: Street;
  board: Card[];
  pot: number;
  button: number;
  toAct: number;
  currentBet: number;
  handNumber: number;
  seats: (SeatView | null)[];
  /** The viewer's seat, or -1 when watching. */
  mySeat: number;
  /** What the viewer may do now, or null when it is not their turn. */
  legal: LegalActions | null;
  result: HandResult | null;
}

/** What one player is allowed to see of the table: everyone else's hole cards stay hidden until showdown. */
export function viewFor(table: Table, viewerId: string | null): TableView {
  const mySeat = viewerId ? table.seatOf(viewerId) : -1;
  const shown = table.result?.shown ?? {};
  return {
    config: table.config,
    phase: table.phase,
    street: table.street,
    board: [...table.board],
    pot: table.phase === 'betting' ? table.pot : 0,
    button: table.button,
    toAct: table.toAct,
    currentBet: table.currentBet,
    handNumber: table.handNumber,
    seats: table.seats.map((s, index) => {
      if (!s) return null;
      const holding = s.inHand && !s.folded && s.holeCards.length > 0;
      const visible = index === mySeat || shown[index] !== undefined;
      return {
        index,
        playerId: s.playerId,
        name: s.name,
        stack: s.stack,
        bet: s.bet,
        inHand: s.inHand,
        folded: s.folded,
        allIn: s.allIn,
        sittingOut: s.sittingOut,
        lastAction: s.lastAction,
        cards: visible && s.holeCards.length > 0 ? [...s.holeCards] : null,
        hasCards: holding,
      };
    }),
    mySeat,
    legal: mySeat === -1 ? null : table.legalActions(mySeat),
    result: table.result,
  };
}
