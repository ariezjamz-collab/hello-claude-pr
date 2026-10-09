import { Card, freshDeck, Rng, shuffle } from './cards';
import { evaluate } from './evaluator';

export type Street = 'preflop' | 'flop' | 'turn' | 'river';
export type Phase = 'waiting' | 'betting' | 'complete';

export interface TableConfig {
  smallBlind: number;
  bigBlind: number;
  maxSeats: number;
}

/**
 * `raise` is "raise to": the player's total bet for this street after the action.
 * When nobody has bet yet on the street, a raise is simply a bet.
 */
export type Action =
  | { type: 'fold' }
  | { type: 'check' }
  | { type: 'call' }
  | { type: 'raise'; amount: number };

export interface Seat {
  playerId: string;
  name: string;
  stack: number;
  holeCards: Card[];
  /** Chips put in on the current street. */
  bet: number;
  /** Chips put in over the whole hand (including `bet`). */
  committed: number;
  inHand: boolean;
  folded: boolean;
  allIn: boolean;
  hasActed: boolean;
  /** The full-raise counter value when this player last acted; used to stop re-raising after a short all-in. */
  actedOnRaise: number;
  sittingOut: boolean;
  leaving: boolean;
  lastAction: string | null;
}

export interface LegalActions {
  canFold: boolean;
  canCheck: boolean;
  /** Chips needed to call (0 when checking is possible). Capped at the player's stack. */
  callAmount: number;
  canRaise: boolean;
  minRaiseTo: number;
  maxRaiseTo: number;
}

export interface PotResult {
  amount: number;
  winners: string[];
  handName: string | null;
}

export interface HandResult {
  pots: PotResult[];
  /** Net chips each player received from the pot this hand (winners only). */
  winnings: Record<string, number>;
  /** Hole cards revealed at showdown, keyed by seat index. */
  shown: Record<number, { cards: Card[]; handName: string }>;
}

export interface TableOptions {
  /** Shuffles a fresh deck. Override to use a stronger RNG or to stack the deck in tests. */
  shuffle?: (deck: Card[]) => Card[];
  rng?: Rng;
}

export class Table {
  readonly config: TableConfig;
  readonly seats: (Seat | null)[];
  phase: Phase = 'waiting';
  street: Street = 'preflop';
  board: Card[] = [];
  button = -1;
  toAct = -1;
  currentBet = 0;
  /** Size of the last full bet or raise on this street; the next raise must be at least this much more. */
  minRaise = 0;
  handNumber = 0;
  result: HandResult | null = null;

  private deck: Card[] = [];
  private raiseCounter = 0;
  private readonly shuffleDeck: (deck: Card[]) => Card[];

  constructor(config: TableConfig, options: TableOptions = {}) {
    if (config.maxSeats < 2 || config.maxSeats > 10) throw new Error('maxSeats must be between 2 and 10');
    if (config.smallBlind <= 0 || config.bigBlind < config.smallBlind) throw new Error('invalid blinds');
    this.config = config;
    this.seats = Array(config.maxSeats).fill(null);
    this.shuffleDeck = options.shuffle ?? ((deck) => shuffle(deck, options.rng));
  }

  // ---- Seating -------------------------------------------------------------

  sit(seatIndex: number, playerId: string, name: string, stack: number): void {
    if (seatIndex < 0 || seatIndex >= this.seats.length) throw new Error('no such seat');
    if (this.seats[seatIndex]) throw new Error('seat is taken');
    if (this.seatOf(playerId) !== -1) throw new Error('already seated');
    if (!Number.isInteger(stack) || stack <= 0) throw new Error('stack must be a positive integer');
    this.seats[seatIndex] = {
      playerId,
      name,
      stack,
      holeCards: [],
      bet: 0,
      committed: 0,
      inHand: false,
      folded: false,
      allIn: false,
      hasActed: false,
      actedOnRaise: -1,
      sittingOut: false,
      leaving: false,
      lastAction: null,
    };
  }

  /**
   * Leaves the table. Returns the chips to cash out, or null if the player is in a hand:
   * then they fold now and are removed by `removeLeavers()` once the hand ends.
   */
  stand(playerId: string): number | null {
    const i = this.seatOf(playerId);
    if (i === -1) throw new Error('not seated');
    const seat = this.seats[i]!;
    if (this.phase === 'betting' && seat.inHand) {
      seat.leaving = true;
      if (!seat.folded) {
        if (this.toAct === i) {
          this.act(playerId, { type: 'fold' });
        } else {
          seat.folded = true;
          seat.lastAction = 'Fold';
          this.proceed(this.toAct - 1);
        }
      }
      return null;
    }
    this.seats[i] = null;
    return seat.stack;
  }

  /** Removes players who asked to leave during a hand. Call after the hand completes. */
  removeLeavers(): { playerId: string; stack: number }[] {
    if (this.phase === 'betting') return [];
    const out: { playerId: string; stack: number }[] = [];
    this.seats.forEach((seat, i) => {
      if (seat?.leaving) {
        out.push({ playerId: seat.playerId, stack: seat.stack });
        this.seats[i] = null;
      }
    });
    return out;
  }

  /** Adds chips between hands (rebuy). */
  addChips(playerId: string, amount: number): void {
    const seat = this.seats[this.seatOf(playerId)];
    if (!seat) throw new Error('not seated');
    if (this.phase === 'betting' && seat.inHand) throw new Error('cannot add chips during a hand');
    if (!Number.isInteger(amount) || amount <= 0) throw new Error('amount must be a positive integer');
    seat.stack += amount;
    seat.sittingOut = false;
  }

  setSittingOut(playerId: string, sittingOut: boolean): void {
    const seat = this.seats[this.seatOf(playerId)];
    if (!seat) throw new Error('not seated');
    seat.sittingOut = sittingOut;
  }

  seatOf(playerId: string): number {
    return this.seats.findIndex((s) => s?.playerId === playerId);
  }

  canStartHand(): boolean {
    return this.phase !== 'betting' && this.seats.filter((s) => s && this.isEligible(s)).length >= 2;
  }

  // ---- Hand flow -----------------------------------------------------------

  startHand(): void {
    if (!this.canStartHand()) throw new Error('need at least two players with chips');
    this.handNumber++;
    this.phase = 'betting';
    this.street = 'preflop';
    this.board = [];
    this.result = null;
    this.raiseCounter = 0;
    for (const seat of this.seats) {
      if (!seat) continue;
      Object.assign(seat, {
        holeCards: [],
        bet: 0,
        committed: 0,
        inHand: this.isEligible(seat),
        folded: false,
        allIn: false,
        hasActed: false,
        actedOnRaise: -1,
        lastAction: null,
      });
    }

    this.button = this.nextSeat(this.button, (s) => s.inHand);
    this.deck = this.shuffleDeck(freshDeck());
    for (let round = 0; round < 2; round++) {
      let i = this.button;
      do {
        i = this.nextSeat(i, (s) => s.inHand);
        this.seats[i]!.holeCards.push(this.deck.shift()!);
      } while (i !== this.button);
    }

    const headsUp = this.inHandSeats().length === 2;
    const sb = headsUp ? this.button : this.nextSeat(this.button, (s) => s.inHand);
    const bb = this.nextSeat(sb, (s) => s.inHand);
    this.post(sb, this.config.smallBlind, 'SB');
    this.post(bb, this.config.bigBlind, 'BB');
    // Players must call the full big blind even if the big blind is all-in for less.
    this.currentBet = this.config.bigBlind;
    this.minRaise = this.config.bigBlind;
    this.proceed(bb);
  }

  act(playerId: string, action: Action): void {
    if (this.phase !== 'betting') throw new Error('no hand in progress');
    const i = this.toAct;
    const seat = this.seats[i];
    if (!seat || seat.playerId !== playerId) throw new Error('not your turn');
    const legal = this.legalActions(i)!;

    switch (action.type) {
      case 'fold':
        seat.folded = true;
        seat.lastAction = 'Fold';
        break;
      case 'check':
        if (!legal.canCheck) throw new Error('cannot check, there is a bet to call');
        seat.lastAction = 'Check';
        break;
      case 'call':
        if (legal.callAmount === 0) {
          seat.lastAction = 'Check';
        } else {
          this.put(i, legal.callAmount);
          seat.lastAction = seat.allIn ? 'All-in' : 'Call';
        }
        break;
      case 'raise': {
        const to = action.amount;
        if (!legal.canRaise) throw new Error('raising is not allowed');
        if (!Number.isInteger(to) || to < legal.minRaiseTo || to > legal.maxRaiseTo) {
          throw new Error(`raise must be between ${legal.minRaiseTo} and ${legal.maxRaiseTo}`);
        }
        const wasBet = this.currentBet === 0;
        const increase = to - this.currentBet;
        this.put(i, to - seat.bet);
        if (increase >= this.minRaise) {
          // A full raise reopens the betting for everyone.
          this.minRaise = increase;
          this.raiseCounter++;
        }
        this.currentBet = Math.max(this.currentBet, to);
        seat.lastAction = seat.allIn ? 'All-in' : wasBet ? 'Bet' : 'Raise';
        break;
      }
      default:
        throw new Error('unknown action');
    }

    seat.hasActed = true;
    seat.actedOnRaise = this.raiseCounter;
    this.proceed(i);
  }

  legalActions(seatIndex: number): LegalActions | null {
    if (this.phase !== 'betting' || seatIndex !== this.toAct) return null;
    const seat = this.seats[seatIndex]!;
    const toCall = Math.max(0, this.currentBet - seat.bet);
    const maxRaiseTo = seat.bet + seat.stack;
    const othersCanRespond = this.seats.some((s, j) => j !== seatIndex && s && this.canAct(s));
    return {
      canFold: true,
      canCheck: toCall === 0,
      callAmount: Math.min(toCall, seat.stack),
      canRaise: seat.stack > toCall && othersCanRespond && seat.actedOnRaise !== this.raiseCounter,
      minRaiseTo: Math.min(this.currentBet + this.minRaise, maxRaiseTo),
      maxRaiseTo,
    };
  }

  /** Chips in the middle, not counting bets still in front of players on this street. */
  get pot(): number {
    return this.seats.reduce((sum, s) => sum + (s ? s.committed - s.bet : 0), 0);
  }

  /** Every chip committed to this hand. */
  get totalPot(): number {
    return this.seats.reduce((sum, s) => sum + (s?.committed ?? 0), 0);
  }

  // ---- Internals -----------------------------------------------------------

  private isEligible(seat: Seat): boolean {
    return seat.stack > 0 && !seat.sittingOut && !seat.leaving;
  }

  private canAct(seat: Seat): boolean {
    return seat.inHand && !seat.folded && !seat.allIn;
  }

  private inHandSeats(): Seat[] {
    return this.seats.filter((s): s is Seat => !!s?.inHand);
  }

  private contenders(): Seat[] {
    return this.inHandSeats().filter((s) => !s.folded);
  }

  /** Next seat index clockwise from `from` (exclusive) whose seat matches, wrapping around. */
  private nextSeat(from: number, match: (seat: Seat) => boolean): number {
    const n = this.seats.length;
    for (let step = 1; step <= n; step++) {
      const i = (((from + step) % n) + n) % n;
      const seat = this.seats[i];
      if (seat && match(seat)) return i;
    }
    return -1;
  }

  private put(seatIndex: number, amount: number): void {
    const seat = this.seats[seatIndex]!;
    const chips = Math.min(amount, seat.stack);
    seat.stack -= chips;
    seat.bet += chips;
    seat.committed += chips;
    if (seat.stack === 0) seat.allIn = true;
  }

  private post(seatIndex: number, amount: number, label: string): void {
    this.put(seatIndex, amount);
    this.seats[seatIndex]!.lastAction = label;
  }

  private needsToAct(seat: Seat): boolean {
    return this.canAct(seat) && (!seat.hasActed || seat.bet < this.currentBet);
  }

  private isRoundComplete(): boolean {
    const active = this.inHandSeats().filter((s) => this.canAct(s));
    if (active.length === 0) return true;
    if (active.length === 1) {
      // Everyone else is all-in or folded: the last player only has to match the biggest bet.
      const highest = Math.max(...this.contenders().map((s) => s.bet));
      if (active[0].bet >= highest) return true;
    }
    return active.every((s) => !this.needsToAct(s));
  }

  /** Moves the hand forward after something changed, starting the search for the next actor after `from`. */
  private proceed(from: number): void {
    if (this.contenders().length === 1) return this.finishUncontested();
    if (this.isRoundComplete()) return this.endStreet();
    this.toAct = this.nextSeat(from, (s) => this.needsToAct(s));
  }

  /** Gives back the part of the biggest bet that nobody matched. */
  private returnUncalledBet(): void {
    const inHand = this.inHandSeats();
    const sorted = [...inHand].sort((a, b) => b.bet - a.bet);
    if (sorted.length < 2 || sorted[0].bet === sorted[1].bet) return;
    const excess = sorted[0].bet - sorted[1].bet;
    sorted[0].bet -= excess;
    sorted[0].committed -= excess;
    sorted[0].stack += excess;
    if (sorted[0].stack > 0) sorted[0].allIn = false;
  }

  private endStreet(): void {
    this.returnUncalledBet();
    for (const seat of this.inHandSeats()) {
      seat.bet = 0;
      seat.hasActed = false;
    }
    this.currentBet = 0;
    this.minRaise = this.config.bigBlind;
    this.raiseCounter++;

    if (this.street === 'river') return this.showdown();
    this.dealNextStreet();

    // With at most one player able to bet, there is nothing left to decide: run out the board.
    if (this.inHandSeats().filter((s) => this.canAct(s)).length <= 1) return this.endStreet();
    for (const seat of this.inHandSeats()) if (!seat.folded && !seat.allIn) seat.lastAction = null;
    this.toAct = this.nextSeat(this.button, (s) => this.canAct(s));
  }

  private dealNextStreet(): void {
    if (this.street === 'preflop') {
      this.street = 'flop';
      this.board.push(...this.deck.splice(0, 3));
    } else if (this.street === 'flop') {
      this.street = 'turn';
      this.board.push(this.deck.shift()!);
    } else {
      this.street = 'river';
      this.board.push(this.deck.shift()!);
    }
  }

  private finishUncontested(): void {
    this.returnUncalledBet();
    const winner = this.contenders()[0];
    const amount = this.totalPot;
    winner.stack += amount;
    this.complete({ pots: [{ amount, winners: [winner.playerId], handName: null }], winnings: { [winner.playerId]: amount }, shown: {} });
  }

  private showdown(): void {
    const contenders = this.contenders();
    const hands = new Map(contenders.map((s) => [s, evaluate([...s.holeCards, ...this.board])]));
    const result: HandResult = { pots: [], winnings: {}, shown: {} };
    for (const [seat, hand] of hands) {
      result.shown[this.seats.indexOf(seat)] = { cards: seat.holeCards, handName: hand.name };
    }

    for (const pot of this.buildPots()) {
      const best = Math.max(...pot.eligible.map((s) => hands.get(s)!.score));
      // Seat order starting left of the button, so odd chips go to the first winner clockwise.
      const winners = pot.eligible
        .filter((s) => hands.get(s)!.score === best)
        .sort((a, b) => this.distanceFromButton(a) - this.distanceFromButton(b));
      const share = Math.floor(pot.amount / winners.length);
      let oddChips = pot.amount - share * winners.length;
      for (const w of winners) {
        const won = share + (oddChips-- > 0 ? 1 : 0);
        w.stack += won;
        result.winnings[w.playerId] = (result.winnings[w.playerId] ?? 0) + won;
      }
      result.pots.push({ amount: pot.amount, winners: winners.map((w) => w.playerId), handName: hands.get(winners[0])!.name });
    }
    this.complete(result);
  }

  /** Main pot and side pots, built from how much each player committed. */
  private buildPots(): { amount: number; eligible: Seat[] }[] {
    const inHand = this.inHandSeats();
    const contenders = this.contenders();
    const levels = [...new Set(contenders.map((s) => s.committed))].sort((a, b) => a - b);
    const pots: { amount: number; eligible: Seat[] }[] = [];
    let previous = 0;
    for (const level of levels) {
      const amount = inHand.reduce((sum, s) => sum + Math.min(s.committed, level) - Math.min(s.committed, previous), 0);
      const eligible = contenders.filter((s) => s.committed >= level);
      const last = pots[pots.length - 1];
      if (last && last.eligible.length === eligible.length) last.amount += amount;
      else if (amount > 0) pots.push({ amount, eligible });
      previous = level;
    }
    // Chips from folded players above the top contender level (should not happen once uncalled bets are returned).
    const leftover = this.totalPot - pots.reduce((sum, p) => sum + p.amount, 0);
    if (leftover > 0) pots[pots.length - 1].amount += leftover;
    return pots;
  }

  private distanceFromButton(seat: Seat): number {
    const n = this.seats.length;
    return (this.seats.indexOf(seat) - this.button - 1 + n) % n;
  }

  private complete(result: HandResult): void {
    this.result = result;
    this.phase = 'complete';
    this.toAct = -1;
    for (const seat of this.inHandSeats()) {
      seat.bet = 0;
      if (seat.stack === 0) seat.sittingOut = true;
    }
  }
}
