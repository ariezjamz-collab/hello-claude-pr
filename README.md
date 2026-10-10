# Pocket Club

A club-style No-Limit Texas Hold'em app for phones, inspired by apps like PPPoker. You make up a name, pick a table, buy in with free play chips and play real-time hands against friends (or bots).

**Play chips only.** Chips have no cash value and can't be bought or cashed out. Running real-money poker requires a gambling license wherever you operate, so don't add payments without legal advice.

## What's here

| Folder | What it is |
| --- | --- |
| `packages/engine` | The poker rules in TypeScript: dealing, blinds, betting rounds, minimum raises, all-ins, side pots, showdowns and hand ranking. No UI or network code, fully tested. |
| `server` | The real-time game server (Node + Socket.IO + PostgreSQL). It runs every table, enforces the rules, shuffles with a cryptographic RNG, sends each player only the cards they're allowed to see, and stores players and chips. |
| `app` | The Android app (Expo / React Native): sign-in (Google or guest), lobby and the poker table. |
| `deploy` | Docker setup for production: database, game server and automatic HTTPS. See **[DEPLOY.md](DEPLOY.md)**. |

The server is the only source of truth. The app sends requests such as "I raise to 60", and the server checks them against the engine before anything happens. A modified app can't peek at other players' cards or make illegal moves.

## Running it

You need Node.js 20+ and, to play on your phone, the **Expo Go** app ([Android](https://play.google.com/store/apps/details?id=host.exp.exponent) / [iOS](https://apps.apple.com/app/expo-go/id982107779)). In Expo Go you play as a guest; Google sign-in needs a real build (see DEPLOY.md).

```bash
npm install            # once, from the repo root

npm run server         # terminal 1: game server on port 3000
npm run app            # terminal 2: Expo dev server, shows a QR code
```

Scan the QR code with Expo Go (Android) or the Camera app (iOS). Your phone must be on the same Wi-Fi as your computer. The app guesses the server address from where it was loaded (`http://<your computer's IP>:3000`); you can change it on the sign-in screen.

Without a database the server keeps everything in memory, which is fine for trying things out. To keep players and chips between restarts, run PostgreSQL and set `DATABASE_URL` (see `server/.env.example`).

To try it in a browser instead, press `w` in the Expo terminal.

### Playing alone

Add some bots to a table:

```bash
npm run bots -w @pocket-club/server -- 3                 # 3 bots at "Welcome Table"
npm run bots -w @pocket-club/server -- 5 "High Rollers"
```

### Checks

```bash
npm test               # engine and server tests (2,000 hands of random self-play, sign-in, chips, crash recovery)
npm run typecheck      # engine, server and app

# Also run the storage tests against a real PostgreSQL database (it gets wiped):
TEST_DATABASE_URL=postgres://user:pass@localhost/pocket_test npm test
```

## How a game works

- Sign in with **Google** (keeps your account if you change phones) or play as a **guest**. A guest can link Google later and keep their chips.
- New players get **10,000 play chips**. When your bank runs low, the lobby offers free chips.
- Buy-ins are 20 to 200 big blinds. When you stand up, your stack goes back to your bank.
- A hand starts automatically once two players with chips are seated.
- You have **30 seconds** to act. If time runs out you check when you can, otherwise fold, and you're marked away until you tap **I'm back**. While you're away the table doesn't wait for you.
- If you lose your connection the app shows "Reconnecting…" and you keep your seat for 60 seconds; after that your chips return to your bank.
- Chips are always either in your bank or on a table, and every move between the two is saved. If the server restarts or crashes, everyone's chips return to their banks (a hand cut off by a crash is void).

## Server settings

See the settings reference in [DEPLOY.md](DEPLOY.md#settings-reference).

## Current limitations

This is the first milestone: one table type, played well. Not built yet:

- **iPhone app.** The app is Android-first; iOS needs an Apple developer account and the Google sign-in plugin set up.
- **Clubs and unions.** No private clubs, invite codes, club owners handing out chips or roles yet.
- **Other games.** No Omaha, short deck or tournaments (sit & go / MTT).
- **Hand history, chat, emojis, sound or animations.**

## Roadmap ideas

1. Clubs: create a club, invite by code, member list, owner/manager roles, club-only tables. (Keep chips non-transferable between players: see the legal note in DEPLOY.md.)
2. Phone number sign-in with OTP (needs TRAI DLT registration for SMS in India).
3. Hand history and replays.
4. Pot-Limit Omaha (mostly an engine change: four hole cards, using exactly two, plus pot-limit sizing).
5. Sit & Go tournaments with rising blinds.
6. Moderation tools: report a player, ban accounts that trade chips for money.
