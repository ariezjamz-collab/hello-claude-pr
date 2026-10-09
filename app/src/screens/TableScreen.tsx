import { evaluate, type Action, type SeatView } from '@pocket-club/engine';
import type { TableState } from '@pocket-club/server/src/protocol';
import { useEffect, useMemo, useState } from 'react';
import { LayoutRectangle, Modal, Pressable, StyleSheet, Text, View } from 'react-native';
import { PlayingCard } from '../components/PlayingCard';
import { colors, formatChips } from '../theme';

interface Props {
  state: TableState | null;
  bank: number;
  onBack: () => void;
  onAction: (action: Action) => Promise<void>;
  onSit: (seat: number, buyIn: number) => Promise<void>;
  onStand: () => Promise<void>;
  onRebuy: (amount: number) => Promise<void>;
  onSitIn: () => Promise<void>;
}

export function TableScreen({ state, bank, onBack, onAction, onSit, onStand, onRebuy, onSitIn }: Props) {
  const [felt, setFelt] = useState<LayoutRectangle | null>(null);
  const [buyInSeat, setBuyInSeat] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Clear error messages after a moment.
  useEffect(() => {
    if (!error) return;
    const t = setTimeout(() => setError(null), 3000);
    return () => clearTimeout(t);
  }, [error]);

  const run = (fn: () => Promise<void>) => fn().catch((err: Error) => setError(err.message));

  if (!state) {
    return (
      <View style={styles.center}>
        <Text style={styles.muted}>Joining table…</Text>
      </View>
    );
  }

  const { config, seats, mySeat } = state;
  const me = mySeat >= 0 ? seats[mySeat] : null;
  const n = seats.length;

  /** Seats are drawn around an oval with the viewer at the bottom centre. */
  const position = (index: number) => {
    if (!felt) return { left: 0, top: 0 };
    const offset = (index - (mySeat >= 0 ? mySeat : 0) + n) % n;
    const angle = Math.PI / 2 + (2 * Math.PI * offset) / n;
    return {
      left: felt.width / 2 + (felt.width / 2 - 8) * Math.cos(angle),
      top: felt.height / 2 + (felt.height / 2 - 8) * Math.sin(angle),
    };
  };

  return (
    <View style={styles.screen}>
      <View style={styles.topBar}>
        <Pressable accessibilityRole="button" onPress={onBack} hitSlop={12}>
          <Text style={styles.link}>‹ Lobby</Text>
        </Pressable>
        <View style={{ alignItems: 'center' }}>
          <Text style={styles.tableName}>{state.name}</Text>
          <Text style={styles.muted}>
            NL Hold'em {config.smallBlind}/{config.bigBlind}
            {state.handNumber > 0 ? ` · Hand #${state.handNumber}` : ''}
          </Text>
        </View>
        {me ? (
          <Pressable accessibilityRole="button" onPress={() => run(onStand)} hitSlop={12}>
            <Text style={styles.link}>Stand up</Text>
          </Pressable>
        ) : (
          <Text style={styles.muted}>{formatChips(bank)}</Text>
        )}
      </View>

      <View style={styles.tableArea}>
        <View style={styles.rail} onLayout={(e) => setFelt(e.nativeEvent.layout)}>
          <View style={styles.felt}>
            <Center state={state} />
          </View>
        </View>
        {felt &&
          seats.map((seat, i) => {
            const { left, top } = position(i);
            return (
              <View key={i} style={[styles.seatAnchor, { left: felt.x + left, top: felt.y + top }]}>
                {seat ? (
                  <Seat seat={seat} state={state} isMe={i === mySeat} />
                ) : !me ? (
                  <Pressable accessibilityRole="button" style={styles.emptySeat} onPress={() => setBuyInSeat(i)}>
                    <Text style={styles.emptySeatText}>Sit</Text>
                  </Pressable>
                ) : (
                  <View style={[styles.emptySeat, { opacity: 0.35 }]} />
                )}
              </View>
            );
          })}
      </View>

      {error && <Text style={styles.error}>{error}</Text>}

      <View style={styles.bottom}>
        {me ? (
          <MyArea state={state} me={me} onAction={(a) => run(() => onAction(a))} onRebuy={() => setBuyInSeat(mySeat)} onSitIn={() => run(onSitIn)} />
        ) : (
          <Text style={[styles.muted, { textAlign: 'center' }]}>You are watching. Tap an empty seat to join.</Text>
        )}
      </View>

      <BuyIn
        visible={buyInSeat !== null}
        rebuy={buyInSeat !== null && buyInSeat === mySeat}
        bigBlind={config.bigBlind}
        bank={bank}
        onCancel={() => setBuyInSeat(null)}
        onConfirm={(amount) => {
          const seat = buyInSeat!;
          setBuyInSeat(null);
          run(() => (seat === mySeat ? onRebuy(amount) : onSit(seat, amount)));
        }}
      />
    </View>
  );
}

// ---- Middle of the table ------------------------------------------------------

function Center({ state }: { state: TableState }) {
  const names = Object.fromEntries(state.seats.filter((s): s is SeatView => !!s).map((s) => [s.playerId, s.name]));
  const totalPot = state.pot + state.seats.reduce((sum, s) => sum + (s?.bet ?? 0), 0);
  return (
    <View style={styles.centerContent}>
      <View style={styles.board}>
        {[0, 1, 2, 3, 4].map((i) =>
          state.board[i] ? <PlayingCard key={i} card={state.board[i]} size="md" /> : <View key={i} style={styles.boardSlot} />,
        )}
      </View>
      {state.phase === 'complete' && state.result ? (
        <View style={styles.resultBox}>
          {state.result.pots.map((pot, i) => (
            <Text key={i} style={styles.resultText}>
              {pot.winners.map((w) => names[w] ?? 'Someone').join(' & ')} {pot.winners.length > 1 ? 'split' : 'wins'}{' '}
              {formatChips(pot.amount)}
              {pot.handName ? ` with ${pot.handName}` : ''}
            </Text>
          ))}
        </View>
      ) : totalPot > 0 ? (
        <Text style={styles.pot}>Pot {formatChips(totalPot)}</Text>
      ) : (
        <Text style={styles.muted}>{state.phase === 'waiting' ? 'Waiting for players…' : ''}</Text>
      )}
    </View>
  );
}

// ---- A seat around the table --------------------------------------------------

function Seat({ seat, state, isMe }: { seat: SeatView; state: TableState; isMe: boolean }) {
  const acting = state.phase === 'betting' && state.toAct === seat.index;
  const shown = state.result?.shown[seat.index];
  const won = state.result?.winnings[seat.playerId];
  const secondsLeft = useCountdown(acting ? state.turnDeadline : null);

  return (
    <View style={styles.seat}>
      {!isMe && (seat.cards || seat.hasCards) && (
        <View style={styles.seatCards}>
          {(seat.cards ?? [undefined, undefined]).map((c, i) => (
            <PlayingCard key={i} card={c} size="sm" />
          ))}
        </View>
      )}
      <View style={[styles.avatar, acting && styles.avatarActing, seat.folded && { opacity: 0.45 }, won ? styles.avatarWon : null]}>
        <Text style={styles.avatarText}>{seat.name.slice(0, 2).toUpperCase()}</Text>
        {state.button === seat.index && state.handNumber > 0 && (
          <View style={styles.dealer}>
            <Text style={styles.dealerText}>D</Text>
          </View>
        )}
      </View>
      <View style={styles.nameplate}>
        <Text style={styles.seatName} numberOfLines={1}>
          {seat.name}
        </Text>
        <Text style={styles.seatStack}>{seat.sittingOut ? 'Away' : formatChips(seat.stack)}</Text>
      </View>
      {acting && secondsLeft !== null ? (
        <Text style={[styles.badge, { color: secondsLeft <= 5 ? colors.danger : colors.accent }]}>{secondsLeft}s</Text>
      ) : won ? (
        <Text style={[styles.badge, { color: colors.success }]}>+{formatChips(won)}</Text>
      ) : shown ? (
        <Text style={styles.badge}>{shown.handName}</Text>
      ) : seat.lastAction ? (
        <Text style={styles.badge}>{seat.lastAction}</Text>
      ) : null}
      {seat.bet > 0 && <Text style={styles.bet}>● {formatChips(seat.bet)}</Text>}
    </View>
  );
}

function useCountdown(deadline: number | null): number | null {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (!deadline) return;
    const t = setInterval(() => setNow(Date.now()), 250);
    return () => clearInterval(t);
  }, [deadline]);
  return deadline ? Math.max(0, Math.ceil((deadline - now) / 1000)) : null;
}

// ---- The player's own cards and action buttons --------------------------------

interface MyAreaProps {
  state: TableState;
  me: SeatView;
  onAction: (action: Action) => void;
  onRebuy: () => void;
  onSitIn: () => void;
}

function MyArea({ state, me, onAction, onRebuy, onSitIn }: MyAreaProps) {
  const legal = state.legal;
  const handName = useMemo(() => {
    if (!me.cards || me.folded) return null;
    if (state.board.length >= 3) return evaluate([...me.cards, ...state.board]).name;
    return me.cards[0][0] === me.cards[1][0] ? 'Pocket pair' : null;
  }, [me.cards, me.folded, state.board]);

  return (
    <View>
      <View style={styles.myCardsRow}>
        <View style={styles.myCards}>
          {me.cards && me.inHand ? (
            me.cards.map((c) => <PlayingCard key={c} card={c} size="lg" faded={me.folded} />)
          ) : (
            <Text style={styles.muted}>{state.phase === 'betting' ? 'Waiting for the next hand' : ''}</Text>
          )}
        </View>
        <View style={{ alignItems: 'flex-end' }}>
          <Text style={styles.myStack}>{formatChips(me.stack)}</Text>
          {handName && <Text style={styles.handName}>{handName}</Text>}
        </View>
      </View>

      {me.sittingOut ? (
        <View style={styles.actions}>
          {me.stack === 0 ? (
            <ActionButton label="Rebuy" color={colors.accent} onPress={onRebuy} />
          ) : (
            <ActionButton label="I'm back" color={colors.success} onPress={onSitIn} />
          )}
        </View>
      ) : legal ? (
        <Actions state={state} me={me} onAction={onAction} />
      ) : (
        <View style={styles.actionsPlaceholder} />
      )}
    </View>
  );
}

function Actions({ state, me, onAction }: { state: TableState; me: SeatView; onAction: (action: Action) => void }) {
  const legal = state.legal!;
  const bigBlind = state.config.bigBlind;
  const [raiseTo, setRaiseTo] = useState(legal.minRaiseTo);
  const [raising, setRaising] = useState(false);

  // Reset the raise size whenever the situation changes.
  useEffect(() => {
    setRaiseTo(legal.minRaiseTo);
    setRaising(false);
  }, [legal.minRaiseTo, legal.maxRaiseTo, state.street]);

  const clamp = (v: number) => Math.max(legal.minRaiseTo, Math.min(legal.maxRaiseTo, Math.round(v)));
  const totalPot = state.pot + state.seats.reduce((sum, s) => sum + (s?.bet ?? 0), 0);
  // A pot-sized raise: call first, then raise by the size of the pot after calling.
  const potRaise = (fraction: number) => clamp(state.currentBet + (totalPot + legal.callAmount) * fraction);
  const verb = state.currentBet === 0 ? 'Bet' : 'Raise to';

  if (raising) {
    return (
      <View>
        <View style={styles.presets}>
          <Preset label="Min" onPress={() => setRaiseTo(legal.minRaiseTo)} />
          <Preset label="½ Pot" onPress={() => setRaiseTo(potRaise(0.5))} />
          <Preset label="Pot" onPress={() => setRaiseTo(potRaise(1))} />
          <Preset label="All-in" onPress={() => setRaiseTo(legal.maxRaiseTo)} />
        </View>
        <View style={styles.stepper}>
          <Preset label={`− ${bigBlind}`} onPress={() => setRaiseTo((v) => clamp(v - bigBlind))} />
          <Text style={styles.raiseAmount}>{formatChips(raiseTo)}</Text>
          <Preset label={`+ ${bigBlind}`} onPress={() => setRaiseTo((v) => clamp(v + bigBlind))} />
        </View>
        <View style={styles.actions}>
          <ActionButton label="Back" color={colors.surfaceRaised} onPress={() => setRaising(false)} />
          <ActionButton
            label={raiseTo === legal.maxRaiseTo ? `All-in ${formatChips(raiseTo)}` : `${verb} ${formatChips(raiseTo)}`}
            color={colors.raise}
            onPress={() => onAction({ type: 'raise', amount: raiseTo })}
          />
        </View>
      </View>
    );
  }

  return (
    <View style={styles.actions}>
      <ActionButton label="Fold" color={colors.danger} onPress={() => onAction({ type: 'fold' })} />
      {legal.canCheck ? (
        <ActionButton label="Check" color={colors.call} onPress={() => onAction({ type: 'check' })} />
      ) : (
        <ActionButton
          label={legal.callAmount >= me.stack ? `All-in ${formatChips(legal.callAmount)}` : `Call ${formatChips(legal.callAmount)}`}
          color={colors.call}
          onPress={() => onAction({ type: 'call' })}
        />
      )}
      {legal.canRaise && <ActionButton label={state.currentBet === 0 ? 'Bet' : 'Raise'} color={colors.raise} onPress={() => setRaising(true)} />}
    </View>
  );
}

function ActionButton({ label, color, onPress }: { label: string; color: string; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" style={({ pressed }) => [styles.actionButton, { backgroundColor: color }, pressed && { opacity: 0.8 }]} onPress={onPress}>
      <Text style={styles.actionText}>{label}</Text>
    </Pressable>
  );
}

function Preset({ label, onPress }: { label: string; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" style={({ pressed }) => [styles.preset, pressed && { opacity: 0.7 }]} onPress={onPress}>
      <Text style={styles.presetText}>{label}</Text>
    </Pressable>
  );
}

// ---- Buy-in dialog --------------------------------------------------------------

interface BuyInProps {
  visible: boolean;
  rebuy: boolean;
  bigBlind: number;
  bank: number;
  onCancel: () => void;
  onConfirm: (amount: number) => void;
}

function BuyIn({ visible, rebuy, bigBlind, bank, onCancel, onConfirm }: BuyInProps) {
  const min = bigBlind * 20;
  const max = Math.min(bigBlind * 200, bank);
  const [amount, setAmount] = useState(bigBlind * 100);
  useEffect(() => setAmount(Math.max(min, Math.min(bigBlind * 100, max))), [visible, min, max, bigBlind]);
  const step = bigBlind * 10;
  const affordable = max >= min;

  return (
    <Modal visible={visible} transparent animationType="fade" onRequestClose={onCancel}>
      <View style={styles.modalBackdrop}>
        <View style={styles.modal}>
          <Text style={styles.modalTitle}>{rebuy ? 'Rebuy' : 'Buy in'}</Text>
          <Text style={styles.muted}>
            {formatChips(min)} – {formatChips(bigBlind * 200)} · you have {formatChips(bank)}
          </Text>
          {affordable ? (
            <>
              <View style={styles.stepper}>
                <Preset label="−" onPress={() => setAmount((a) => Math.max(min, a - step))} />
                <Text style={styles.raiseAmount}>{formatChips(amount)}</Text>
                <Preset label="+" onPress={() => setAmount((a) => Math.min(max, a + step))} />
              </View>
              <View style={styles.actions}>
                <ActionButton label="Cancel" color={colors.surfaceRaised} onPress={onCancel} />
                <ActionButton label={rebuy ? 'Add chips' : 'Sit down'} color={colors.success} onPress={() => onConfirm(amount)} />
              </View>
            </>
          ) : (
            <>
              <Text style={[styles.error, { marginVertical: 16 }]}>Not enough chips. Grab free chips in the lobby.</Text>
              <ActionButton label="OK" color={colors.surfaceRaised} onPress={onCancel} />
            </>
          )}
        </View>
      </View>
    </Modal>
  );
}

// ---- Styles -------------------------------------------------------------------------

const SEAT_WIDTH = 96;

const styles = StyleSheet.create({
  screen: { flex: 1, maxWidth: 560, width: '100%', alignSelf: 'center' },
  center: { flex: 1, alignItems: 'center', justifyContent: 'center' },
  muted: { color: colors.textMuted, fontSize: 13 },
  link: { color: colors.accent, fontSize: 15, fontWeight: '700' },
  topBar: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', paddingHorizontal: 16, paddingVertical: 10 },
  tableName: { color: colors.text, fontSize: 16, fontWeight: '700' },
  tableArea: { flex: 1, paddingHorizontal: 56, paddingTop: 56, paddingBottom: 64 },
  rail: { flex: 1, borderRadius: 400, backgroundColor: colors.feltEdge, padding: 10 },
  felt: { flex: 1, borderRadius: 400, backgroundColor: colors.felt, alignItems: 'center', justifyContent: 'center', borderWidth: 2, borderColor: '#13704E' },
  centerContent: { alignItems: 'center', gap: 10, paddingHorizontal: 8 },
  board: { flexDirection: 'row', gap: 4 },
  boardSlot: { width: 40, height: 56, borderRadius: 5, borderWidth: 1, borderColor: 'rgba(255,255,255,0.15)' },
  pot: { color: colors.text, fontWeight: '800', fontSize: 15, backgroundColor: 'rgba(0,0,0,0.35)', paddingHorizontal: 12, paddingVertical: 4, borderRadius: 12 },
  resultBox: { backgroundColor: 'rgba(0,0,0,0.45)', borderRadius: 10, paddingHorizontal: 12, paddingVertical: 6 },
  resultText: { color: colors.accent, fontWeight: '700', textAlign: 'center' },
  seatAnchor: { position: 'absolute', width: SEAT_WIDTH, marginLeft: -SEAT_WIDTH / 2, marginTop: -40, alignItems: 'center' },
  seat: { alignItems: 'center', width: SEAT_WIDTH },
  seatCards: { flexDirection: 'row', gap: 2, marginBottom: -10, zIndex: 1 },
  avatar: {
    width: 46,
    height: 46,
    borderRadius: 23,
    backgroundColor: colors.surfaceRaised,
    borderWidth: 2,
    borderColor: colors.border,
    alignItems: 'center',
    justifyContent: 'center',
  },
  avatarActing: { borderColor: colors.accent, borderWidth: 3 },
  avatarWon: { borderColor: colors.success, borderWidth: 3 },
  avatarText: { color: colors.text, fontWeight: '800', fontSize: 15 },
  dealer: {
    position: 'absolute',
    right: -6,
    top: -4,
    width: 20,
    height: 20,
    borderRadius: 10,
    backgroundColor: '#fff',
    alignItems: 'center',
    justifyContent: 'center',
  },
  dealerText: { color: '#000', fontWeight: '900', fontSize: 11 },
  nameplate: { backgroundColor: 'rgba(0,0,0,0.65)', borderRadius: 8, paddingHorizontal: 6, paddingVertical: 2, marginTop: -6, alignItems: 'center', maxWidth: SEAT_WIDTH },
  seatName: { color: colors.text, fontSize: 11, fontWeight: '600' },
  seatStack: { color: colors.accent, fontSize: 12, fontWeight: '800' },
  badge: { color: colors.text, fontSize: 11, fontWeight: '700', marginTop: 2 },
  bet: { color: colors.text, fontSize: 12, fontWeight: '700', marginTop: 2 },
  emptySeat: {
    width: 46,
    height: 46,
    borderRadius: 23,
    borderWidth: 2,
    borderStyle: 'dashed',
    borderColor: colors.textMuted,
    alignItems: 'center',
    justifyContent: 'center',
  },
  emptySeatText: { color: colors.textMuted, fontWeight: '700' },
  error: { color: colors.danger, textAlign: 'center', marginBottom: 6 },
  bottom: { padding: 16, paddingTop: 4, backgroundColor: colors.surface, borderTopLeftRadius: 18, borderTopRightRadius: 18 },
  myCardsRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 10, minHeight: 82 },
  myCards: { flexDirection: 'row', gap: 6 },
  myStack: { color: colors.accent, fontSize: 22, fontWeight: '800' },
  handName: { color: colors.text, fontSize: 14, marginTop: 2 },
  actions: { flexDirection: 'row', gap: 8 },
  actionsPlaceholder: { height: 50 },
  actionButton: { flex: 1, borderRadius: 12, paddingVertical: 14, alignItems: 'center' },
  actionText: { color: '#fff', fontWeight: '800', fontSize: 16 },
  presets: { flexDirection: 'row', gap: 6, marginBottom: 8 },
  preset: { flex: 1, backgroundColor: colors.surfaceRaised, borderRadius: 10, paddingVertical: 9, alignItems: 'center' },
  presetText: { color: colors.text, fontWeight: '700' },
  stepper: { flexDirection: 'row', alignItems: 'center', gap: 10, marginVertical: 12 },
  raiseAmount: { color: colors.text, fontSize: 24, fontWeight: '800', minWidth: 110, textAlign: 'center' },
  modalBackdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.6)', justifyContent: 'center', padding: 24 },
  modal: { backgroundColor: colors.surface, borderRadius: 16, padding: 20, maxWidth: 400, width: '100%', alignSelf: 'center' },
  modalTitle: { color: colors.text, fontSize: 22, fontWeight: '800', marginBottom: 4 },
});
