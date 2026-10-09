import { describe, expect, it } from 'vitest';
import { evaluate, HandCategory } from '../src';

const hand = (s: string) => evaluate(s.split(' '));

describe('evaluate', () => {
  it('recognises every category', () => {
    expect(hand('As Ks Qs Js Ts').category).toBe(HandCategory.StraightFlush);
    expect(hand('9c 9d 9h 9s 2c').category).toBe(HandCategory.Quads);
    expect(hand('9c 9d 9h 2s 2c').category).toBe(HandCategory.FullHouse);
    expect(hand('2h 7h 9h Jh Kh').category).toBe(HandCategory.Flush);
    expect(hand('5c 6d 7h 8s 9c').category).toBe(HandCategory.Straight);
    expect(hand('5c 5d 5h 8s 9c').category).toBe(HandCategory.Trips);
    expect(hand('5c 5d 8h 8s 9c').category).toBe(HandCategory.TwoPair);
    expect(hand('5c 5d 7h 8s 9c').category).toBe(HandCategory.Pair);
    expect(hand('2c 5d 7h 8s 9c').category).toBe(HandCategory.HighCard);
  });

  it('ranks categories in order', () => {
    const order = [
      '2c 5d 7h 8s 9c',
      '5c 5d 7h 8s 9c',
      '5c 5d 8h 8s 9c',
      '5c 5d 5h 8s 9c',
      'Ac 2d 3h 4s 5c',
      '2h 7h 9h Jh Kh',
      '9c 9d 9h 2s 2c',
      '9c 9d 9h 9s 2c',
      'Ah 2h 3h 4h 5h',
    ].map((h) => hand(h).score);
    expect([...order].sort((a, b) => a - b)).toEqual(order);
  });

  it('treats A-2-3-4-5 as the lowest straight', () => {
    expect(hand('Ac 2d 3h 4s 5c').category).toBe(HandCategory.Straight);
    expect(hand('Ac 2d 3h 4s 5c').score).toBeLessThan(hand('2c 3d 4h 5s 6c').score);
    expect(hand('Ac Kd Qh Js Tc').score).toBeGreaterThan(hand('9c Kd Qh Js Tc').score);
  });

  it('does not wrap straights around the ace', () => {
    expect(hand('Qc Kd Ah 2s 3c').category).toBe(HandCategory.HighCard);
  });

  it('uses kickers to break ties', () => {
    expect(hand('Ac Ad Kh 8s 2c').score).toBeGreaterThan(hand('Ac Ad Qh Js Tc').score);
    expect(hand('Kc Kd 2h 2s Ac').score).toBeGreaterThan(hand('Qc Qd Jh Js Ac').score);
    expect(hand('Kc Kd 5h 5s 3c').score).toBeGreaterThan(hand('Kc Kd 5h 5s 2c').score);
    expect(hand('3c 3d 3h As Ac').score).toBeLessThan(hand('4c 4d 4h 2s 2c').score);
  });

  it('scores equal hands equally regardless of suit', () => {
    expect(hand('Ac Kd 9h 7s 3c').score).toBe(hand('Ah Ks 9d 7c 3d').score);
  });

  it('picks the best five of seven cards', () => {
    const best = hand('2c 3c 4c 5c 9d 9h 6c');
    expect(best.category).toBe(HandCategory.StraightFlush);
    expect(hand('As Ad Ah Ks Kd Kh 2c').category).toBe(HandCategory.FullHouse);
    expect(hand('As Ad Ah Ks Kd Kh 2c').score).toBe(hand('As Ad Ah Ks Kd').score);
  });
});
