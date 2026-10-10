# Going live

A step-by-step guide to putting Pocket Club in front of players in North East India: a game server in India with HTTPS and a database, Google sign-in, and an Android app on Google Play.

Budget roughly **₹1,500–2,500 a month** to start, plus **$25 (about ₹2,100) once** for Google Play.

> **Legal first.** Online money games, poker included, are banned across India under the Promotion and Regulation of Online Gaming Act, 2025 and its Rules (in force from 1 May 2026). Pocket Club is built as a free **social game**: chips are free, can't be bought, sold, transferred between players or cashed out. Keep it that way, and have an Indian gaming/IT lawyer review the app, your terms of service and your privacy policy (DPDP Act) before launch.

## What you need

| Item | Where | Cost |
| --- | --- | --- |
| A domain, e.g. `pocketclub.in` | Any registrar | ~₹600–900 a year |
| A server in India | DigitalOcean **Bangalore (BLR1)**, or AWS/Google Cloud **Mumbai** | ~₹1,000–2,000 a month (2 GB RAM) |
| Google Cloud project | console.cloud.google.com | Free |
| Expo account | expo.dev | Free plan to start |
| Google Play developer account | play.google.com/console | $25 once |
| Privacy policy and terms pages | Any public web page | — |

Before choosing the region, have a few friends in Guwahati, Shillong or Imphal on Jio and Airtel compare response times to each region (`ping` to a test server, or any online "cloud ping" test). Pick the fastest.

## 1. Server

1. **Create the server.** Ubuntu 24.04, 2 GB RAM, Bangalore region. Add your SSH key.
2. **Point your domain at it.** At your registrar, add an `A` record: `play` → the server's IP address. (This guide uses `play.pocketclub.in` as the example.)
3. **Install Docker and open the firewall** (on the server):
   ```bash
   curl -fsSL https://get.docker.com | sh
   ufw allow OpenSSH && ufw allow 80 && ufw allow 443 && ufw --force enable
   ```
4. **Get the code and configure it:**
   ```bash
   git clone https://github.com/<you>/<repo>.git pocket-club
   cd pocket-club/deploy
   cp .env.example .env
   nano .env        # set DOMAIN, POSTGRES_PASSWORD (openssl rand -hex 24), GOOGLE_CLIENT_IDS (step 2)
   ```
5. **Start everything:**
   ```bash
   docker compose up -d --build
   ```
   Caddy fetches an HTTPS certificate automatically the first time (DNS from step 2 must already point at the server).
6. **Check it:** open `https://play.pocketclub.in/healthz` in a browser. You should see `{"ok":true,...}`.

### Updating the server

```bash
cd pocket-club && git pull
cd deploy && docker compose up -d --build server
```

Players are not cut off mid-hand. The old server stops starting new hands, finishes the ones in progress (up to about two minutes), returns every seated stack to its owner's bank, and only then stops. Apps show "Reconnecting…" and come back on their own when the new server is up.

If the server ever crashes, the next start returns everyone's chips as of the last finished hand; the interrupted hand is void.

### Backups

Take a daily database copy (add to `crontab -e` on the server):

```cron
0 3 * * * cd /root/pocket-club/deploy && docker compose exec -T db pg_dump -U pocket pocket | gzip > /root/backups/pocket-$(date +\%F).sql.gz
```

Create `/root/backups` first, and copy backups off the server now and then. DigitalOcean's weekly droplet backups (+20%) are a good second layer.

## 2. Google sign-in

In [Google Cloud Console](https://console.cloud.google.com), create a project, then under **APIs & Services**:

1. **OAuth consent screen:** app name, support email, your privacy policy URL. Publish it ("In production").
2. **Credentials → Create OAuth client ID → Web application.** Name it "Pocket Club server". Copy its **client ID**. It goes in two places:
   - `GOOGLE_CLIENT_IDS` in `deploy/.env` on the server (then `docker compose up -d server`)
   - `EXPO_PUBLIC_GOOGLE_WEB_CLIENT_ID` in `app/eas.json`
3. **Credentials → Create OAuth client ID → Android**, once per signing key, each with package name `in.pocketclub.app` and that key's **SHA-1**:
   - **EAS build key** (for APKs you share directly): run `npx eas-cli@latest credentials -p android` in `app/` and copy the SHA-1.
   - **Play App Signing key** (for the Play Store version): after your first upload, Play Console → your app → **Test and release → App integrity** → copy the SHA-1.

If Google sign-in shows `DEVELOPER_ERROR`, a SHA-1 or the package name doesn't match. Guest play works without any of this.

## 3. Android app

All commands run in the `app/` folder.

1. **Set the app's identity.** The package name `in.pocketclub.app` in `app.json` is permanent once published; change it now if you want a different one (and use the same name in step 2.3). Replace the placeholder icons in `app/assets/`.
2. **Point the app at your server.** In `app/eas.json`, replace both placeholders in the `preview` and `production` profiles:
   ```json
   "EXPO_PUBLIC_SERVER_URL": "https://play.pocketclub.in",
   "EXPO_PUBLIC_GOOGLE_WEB_CLIENT_ID": "1234567890-abc.apps.googleusercontent.com"
   ```
3. **Sign in to Expo and link the project:**
   ```bash
   npx eas-cli@latest login
   npx eas-cli@latest init
   ```
4. **Test build (APK) for friends:**
   ```bash
   npx eas-cli@latest build -p android --profile preview
   ```
   EAS builds in the cloud (no Android Studio needed) and gives you a download link and QR code. Install it on a few phones, ideally including a cheap one, and play a few evenings on Jio and Airtel.
5. **Play Store build:**
   ```bash
   npx eas-cli@latest build -p android --profile production
   npx eas-cli@latest submit -p android --profile production
   ```
   The first upload to Play Console has to be done by hand (download the `.aab` from the EAS build page). After that, `eas submit` uploads to the internal testing track.

### Play Console checklist

- **App category:** Games → Card (or Casino). **Ads:** declare whether the app shows any.
- **Content rating questionnaire:** answer yes to **simulated gambling**. Expect an adult rating.
- **Target audience:** 18 and over only.
- **Data safety form:** the app stores a display name, a Google account ID (if used) and game data. No location, contacts or payments.
- **Privacy policy URL:** required.
- **Testing before production:** new *personal* developer accounts must run a **closed test with at least 12 testers for 14 days** before they can publish to everyone. Check Play Console for the current rule, and line up your testers early.

## 4. Before inviting players

- [ ] `https://<your domain>/healthz` answers `ok`.
- [ ] Sign in with Google and as a guest on a real phone.
- [ ] Play with friends on mobile data; switch the phone to airplane mode for 20 seconds mid-hand and back. You should see "Reconnecting…" and keep your seat.
- [ ] Run `docker compose up -d --build server` during a game: everyone should come back with their chips.
- [ ] Backups are running (`ls /root/backups`).
- [ ] Lawyer has reviewed the app, terms and privacy policy.

## Settings reference

Server (`deploy/.env`):

| Variable | Default | Meaning |
| --- | --- | --- |
| `DOMAIN` | — | Public address; Caddy gets its HTTPS certificate |
| `POSTGRES_PASSWORD` | — | Database password (letters and digits) |
| `GOOGLE_CLIENT_IDS` | empty | Web OAuth client ID(s); empty = guests only |
| `TURN_SECONDS` | `30` | Time to act before auto check/fold |
| `DISCONNECT_GRACE_SECONDS` | `60` | How long a dropped player keeps their seat |

App (`app/eas.json` → `env`, or `app/.env` for development):

| Variable | Meaning |
| --- | --- |
| `EXPO_PUBLIC_SERVER_URL` | The game server. Unset in development (the sign-in screen asks) |
| `EXPO_PUBLIC_GOOGLE_WEB_CLIENT_ID` | Web OAuth client ID; enables "Continue with Google" |
