import { describe, expect, it } from 'vitest';
import { Card, freshDeck, Table, viewFor } from '../src';

/**
 * A deck that deals `holes` (in deal order, starting left of the button) and then `board`.
 * The rest of the deck follows in its usual order.
 */
function stacked(holes: Card[][], board: Card[] = []) {
  const top = [...holes.map((h) => h[0]), ...holes.map((h) => h[1]), ...board];
  return () => [...top, ...freshDeck().filter((c) => !top.includes(c))];
}

function table(stacks: number[], options: { holes?: Card[][]; board?: Card[] } = {}) {
  const t = new Table(
    { smallBlind: 5, bigBlind: 10, maxSeats: 6 },
    options.holes ? { shuffle: stacked(options.holes, options.board) } : {},
  );
  stacks.forEach((stack, i) => t.sit(i, `p${i}`, `Player ${i}`, stack));
  return t;
}

const actor = (t: Table) => t.seats[t.toAct]!.playerId;
const stack = (t: Table, seat: number) => t.seats[seat]!.stack;

describe('blinds and turn order', () => {
  it('heads-up: the button posts the small blind and acts first preflop, last after', () => {
    const t = table([1000, 1000]);
    t.startHand();
    expect(t.button).toBe(0);
    expect(t.seats[0]!.bet).toBe(5);
    expect(t.seats[1]!.bet).toBe(10);
    expect(t.toAct).toBe(0);
    t.act('p0', { type: 'call' });
    t.act('p1', { type: 'check' });
    expect(t.street).toBe('flop');
    expect(t.toAct).toBe(1);
  });

  it('three-handed: small blind left of the button, under the gun acts first', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    expect(t.button).toBe(0);
    expect(t.seats[1]!.bet).toBe(5);
    expect(t.seats[2]!.bet).toBe(10);
    expect(t.toAct).toBe(0);
  });

  it('gives the big blind the option when everyone limps', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    t.act('p0', { type: 'call' });
    t.act('p1', { type: 'call' });
    expect(t.street).toBe('preflop');
    expect(t.toAct).toBe(2);
    expect(t.legalActions(2)).toMatchObject({ canCheck: true, canRaise: true, minRaiseTo: 20 });
    t.act('p2', { type: 'check' });
    expect(t.street).toBe('flop');
    expect(t.board).toHaveLength(3);
  });

  it('moves the button each hand', () => {
    const t = table([1000, 1000, 1000]);
    const buttons: number[] = [];
    for (let i = 0; i < 4; i++) {
      t.startHand();
      buttons.push(t.button);
      while (t.phase === 'betting') t.act(actor(t), { type: 'fold' });
    }
    expect(buttons).toEqual([0, 1, 2, 0]);
  });

  it('rejects acting out of turn', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    expect(() => t.act('p1', { type: 'call' })).toThrow('not your turn');
  });
});

describe('betting', () => {
  it('awards the pot when everyone else folds and returns the uncalled bet', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    t.act('p0', { type: 'raise', amount: 30 });
    t.act('p1', { type: 'fold' });
    t.act('p2', { type: 'fold' });
    expect(t.phase).toBe('complete');
    // The uncalled 20 goes back first, so p0 collects a 25 pot (their 10 plus both blinds).
    expect(t.result!.winnings).toEqual({ p0: 25 });
    expect([stack(t, 0), stack(t, 1), stack(t, 2)]).toEqual([1015, 995, 990]);
  });

  it('enforces the minimum raise', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    expect(() => t.act('p0', { type: 'raise', amount: 15 })).toThrow('between 20 and 1000');
    t.act('p0', { type: 'raise', amount: 40 }); // raises by 30
    expect(t.legalActions(1)!.minRaiseTo).toBe(70);
    expect(() => t.act('p1', { type: 'raise', amount: 60 })).toThrow();
  });

  it('cannot check facing a bet', () => {
    const t = table([1000, 1000]);
    t.startHand();
    expect(() => t.act('p0', { type: 'check' })).toThrow('cannot check');
  });

  it('a short all-in raise does not reopen betting for players who already acted', () => {
    // p2 (big blind) has only 35 behind after posting.
    const t = table([1000, 1000, 45]);
    t.startHand();
    t.act('p0', { type: 'raise', amount: 30 });
    t.act('p1', { type: 'call' });
    t.act('p2', { type: 'raise', amount: 45 }); // all-in, only 15 more: not a full raise
    expect(t.toAct).toBe(0);
    expect(t.legalActions(0)).toMatchObject({ canRaise: false, callAmount: 15 });
    t.act('p0', { type: 'call' });
    t.act('p1', { type: 'call' });
    expect(t.street).toBe('flop');
  });

  it('runs out the board when everyone is all-in', () => {
    const t = table([100, 100]);
    t.startHand();
    t.act('p0', { type: 'raise', amount: 100 });
    t.act('p1', { type: 'call' });
    expect(t.phase).toBe('complete');
    expect(t.board).toHaveLength(5);
    expect(stack(t, 0) + stack(t, 1)).toBe(200);
  });
});

describe('showdown', () => {
  it('pays the best hand', () => {
    // Deal order heads-up starts at seat 1 (left of the button).
    const t = table([1000, 1000], {
      holes: [['Kd', 'Kc'], ['As', 'Ah']],
      board: ['2c', '7d', '9h', 'Js', '3c'],
    });
    t.startHand();
    t.act('p0', { type: 'call' });
    t.act('p1', { type: 'check' });
    for (let street = 0; street < 3; street++) {
      t.act(actor(t), { type: 'check' });
      t.act(actor(t), { type: 'check' });
    }
    expect(t.phase).toBe('complete');
    expect(t.result!.winnings).toEqual({ p0: 20 });
    expect(t.result!.shown[0].handName).toBe('Pair');
    expect(stack(t, 0)).toBe(1010);
  });

  it('splits a tied pot and gives the odd chip to the first player left of the button', () => {
    const t = table([1000, 1000, 1000], {
      holes: [['2c', '3d'], ['2d', '3c'], ['4h', '5h']],
      board: ['As', 'Ks', 'Qs', 'Js', 'Ts'], // royal flush on board: everyone ties
    });
    t.startHand();
    t.act('p0', { type: 'raise', amount: 25 });
    t.act('p1', { type: 'call' });
    t.act('p2', { type: 'call' });
    // Flop: p1 and p2 check, p0 bets 10, p1 folds, p2 calls.
    t.act('p1', { type: 'check' });
    t.act('p2', { type: 'check' });
    t.act('p0', { type: 'raise', amount: 10 });
    t.act('p1', { type: 'fold' });
    t.act('p2', { type: 'call' });
    while (t.phase === 'betting') t.act(actor(t), { type: 'check' });
    // Pot 95 split between p0 and p2; p2 sits first left of the button (seat 0) and gets the odd chip.
    expect(t.result!.winnings).toEqual({ p0: 47, p2: 48 });
  });

  it('builds side pots for all-in players', () => {
    // p0 is short. Deal order from seat 1: p1, p2, p0.
    const t = table([100, 300, 300], {
      holes: [['Kd', 'Kc'], ['Qd', 'Qc'], ['As', 'Ah']],
      board: ['2c', '7d', '9h', 'Js', '3c'],
    });
    t.startHand();
    t.act('p0', { type: 'raise', amount: 100 }); // all-in
    t.act('p1', { type: 'raise', amount: 300 }); // all-in
    t.act('p2', { type: 'call' }); // all-in
    expect(t.phase).toBe('complete');
    // Main pot 300 to p0 (aces); side pot 400 to p1 (kings).
    expect(t.result!.pots).toEqual([
      { amount: 300, winners: ['p0'], handName: 'Pair' },
      { amount: 400, winners: ['p1'], handName: 'Pair' },
    ]);
    expect([stack(t, 0), stack(t, 1), stack(t, 2)]).toEqual([300, 400, 0]);
    expect(t.seats[2]!.sittingOut).toBe(true);
  });
});

describe('seating', () => {
  it('a player leaving mid-hand folds and is removed when the hand ends', () => {
    const t = table([1000, 1000, 1000]);
    t.startHand();
    expect(t.stand('p1')).toBeNull();
    expect(t.seats[1]!.folded).toBe(true);
    t.act('p0', { type: 'fold' });
    expect(t.phase).toBe('complete');
    expect(t.removeLeavers()).toEqual([{ playerId: 'p1', stack: 995 }]);
    expect(t.seats[1]).toBeNull();
  });

  it('new players wait for the next hand', () => {
    const t = table([1000, 1000]);
    t.startHand();
    t.sit(3, 'late', 'Late', 500);
    expect(t.seats[3]!.inHand).toBe(false);
    expect(() => t.act('late', { type: 'fold' })).toThrow();
  });
});

describe('viewFor', () => {
  it('hides other players hole cards', () => {
    const t = table([1000, 1000]);
    t.startHand();
    const view = viewFor(t, 'p0');
    expect(view.seats[0]!.cards).toHaveLength(2);
    expect(view.seats[1]!.cards).toBeNull();
    expect(view.seats[1]!.hasCards).toBe(true);
    expect(view.legal).not.toBeNull();
    expect(viewFor(t, null).seats.filter(Boolean).every((s) => s!.cards === null)).toBe(true);
  });
});

describe('random self-play', () => {
  it('never creates or loses chips over thousands of random actions', () => {
    let seed = 42;
    const rng = () => ((seed = (seed * 1103515245 + 12345) % 2 ** 31) / 2 ** 31);
    const t = new Table({ smallBlind: 5, bigBlind: 10, maxSeats: 6 }, { rng });
    let total = 6 * 500;
    let handsPlayed = 0;
    for (let i = 0; i < 6; i++) t.sit(i, `p${i}`, `P${i}`, 500);

    for (let hand = 0; hand < 2000; hand++) {
      t.startHand();
      let guard = 0;
      while (t.phase === 'betting') {
        if (guard++ > 200) throw new Error('hand did not finish');
        const legal = t.legalActions(t.toAct)!;
        const roll = rng();
        const id = actor(t);
        if (legal.canRaise && roll < 0.25) {
          const span = legal.maxRaiseTo - legal.minRaiseTo;
          t.act(id, { type: 'raise', amount: legal.minRaiseTo + Math.floor(rng() * (span + 1)) });
        } else if (roll < 0.4 && !legal.canCheck) t.act(id, { type: 'fold' });
        else t.act(id, { type: 'call' });
      }
      const chips = t.seats.reduce((sum, s) => sum + (s?.stack ?? 0), 0);
      expect(chips).toBe(total);
      // Busted players leave and new ones take empty seats, so the game keeps going.
      for (const s of t.seats) if (s && s.stack === 0) total -= t.stand(s.playerId)!;
      for (let i = 0; i < 6; i++) {
        if (!t.seats[i] && (rng() < 0.3 || !t.canStartHand())) {
          t.sit(i, `n${hand}-${i}`, 'New', 500);
          total += 500;
        }
      }
      handsPlayed = hand + 1;
    }
    expect(handsPlayed).toBe(2000);
  });
});
