# Pocket Club

A club-style No-Limit Texas Hold'em app for phones, inspired by apps like PPPoker. You make up a name, pick a table, buy in with free play chips and play real-time hands against friends (or bots).

**Play chips only.** Chips have no cash value and can't be bought or cashed out. Running real-money poker requires a gambling license wherever you operate, so don't add payments without legal advice.

## What's here

| Folder | What it is |
| --- | --- |
| `packages/engine` | The poker rules in TypeScript: dealing, blinds, betting rounds, minimum raises, all-ins, side pots, showdowns and hand ranking. No UI or network code, fully tested. |
| `server` | The real-time game server (Node + Socket.IO). It runs every table, enforces the rules, shuffles with a cryptographic RNG and sends each player only the cards they're allowed to see. |
| `app` | The mobile app (Expo / React Native): connect screen, lobby, and the poker table. |

The server is the only source of truth. The app sends requests such as "I raise to 60", and the server checks them against the engine before anything happens. A modified app can't peek at other players' cards or make illegal moves.

## Running it

You need Node.js 20+ and, to play on your phone, the **Expo Go** app ([Android](https://play.google.com/store/apps/details?id=host.exp.exponent) / [iOS](https://apps.apple.com/app/expo-go/id982107779)).

```bash
npm install            # once, from the repo root

npm run server         # terminal 1: game server on port 3000
npm run app            # terminal 2: Expo dev server, shows a QR code
```

Scan the QR code with Expo Go (Android) or the Camera app (iOS). Your phone must be on the same Wi-Fi as your computer. The app guesses the server address from where it was loaded (`http://<your computer's IP>:3000`); you can change it on the first screen.

To try it in a browser instead, press `w` in the Expo terminal.

### Playing alone

Add some bots to a table:

```bash
npm run bots -w @pocket-club/server -- 3                 # 3 bots at "Welcome Table"
npm run bots -w @pocket-club/server -- 5 "High Rollers"
```

### Checks

```bash
npm test               # engine tests, including 2,000 hands of random self-play
npm run typecheck      # engine, server and app
```

## How a game works

- New players get **10,000 play chips**. When your bank runs low, the lobby offers free chips.
- Buy-ins are 20 to 200 big blinds. When you stand up, your stack goes back to your bank.
- A hand starts automatically once two players with chips are seated.
- You have **20 seconds** to act. If time runs out you check when you can, otherwise fold, and you're marked away until you tap **I'm back**.
- If you lose your connection you have 60 seconds to come back before your seat is given up and your chips return to your bank.

## Server settings

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `3000` | Port to listen on |
| `TURN_SECONDS` | `20` | Time to act before auto check/fold |
| `DISCONNECT_GRACE_SECONDS` | `60` | How long a dropped player keeps their seat |

## Current limitations

This is the first milestone: one table type, played well. Not built yet:

- **Saved data.** Players, banks and tables are kept in memory and reset when the server restarts. Accounts last only as long as the app session (or browser storage on web).
- **Real accounts.** Anyone can pick any name; the reconnect token is the only identity.
- **Clubs and unions.** No private clubs, invite codes, club owners handing out chips or roles yet.
- **Other games.** No Omaha, short deck or tournaments (sit & go / MTT).
- **Hand history, chat, emojis, sound or animations.**

## Roadmap ideas

1. Database (e.g. PostgreSQL) and sign-in, so players and chips survive restarts.
2. Clubs: create a club, invite by code, member list, owner/manager roles, club-only tables, owners granting play chips.
3. Hand history and replays.
4. Pot-Limit Omaha (mostly an engine change: four hole cards, using exactly two, plus pot-limit sizing).
5. Sit & Go tournaments with rising blinds.
6. App store builds with EAS Build.
