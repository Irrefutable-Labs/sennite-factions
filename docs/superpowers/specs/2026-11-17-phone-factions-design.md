# Factions on the phone: design

Owner, 2026-11-17: "make it work on the phone", after asking whether pooling resources, building
together and voting members out were done. All of that is built (Server and base editor) but was
keyboard-only, and the phone build had no publisher's server.

## Approaches

- **The DSM Wallet app on the phone** (chosen by the owner, 2026-11-17: "DSM Wallet app on the phone").
  The game opens its connect code as a `dsm:connect/v1:` link (the editor's O key already does this on
  Android); the wallet on the same phone takes it.
- A terminal dev-wallet on the Mac for each player (recommended, not chosen): no wallet build needed,
  but the wallets are not in the phones.
- The same on local storage nodes: offline, more setup.

DSM version, owner, 2026-11-17: "Main, on the beta set (Recommended)". The beta storage set was rebuilt
on 11-16 for 0.1.1-beta.4, so the app host and the wallet are built from DSM main, and the Server's
packed DSM client has to come up to main (the DSM integration session's work).

The wallet on the test phone may be replaced (owner: "Yes, it's a test phone"; "This phone ... does not
use any hardware"). Debug build for now (owner: "I can do it for debug for now, I guess. We'll be redoing
it later"), with the shipping native library (`dsm-android-anchor`, default flavor: no on-device installs).

## The game

- In play, a BASE button (key B) beside the building row, shown only where there is a publisher's
  server and so an editor.
- In the planning view, a grid of buttons down the right side, each pressing one of the editor's keys;
  labels follow the page (PLACE is BUY or WEAR, INVENTORY is INVITE among the people, DISMANTLE is
  WITHDRAW, PEOPLE/SHOP/INVENTORY are BACK on their own page). A button whose key would do nothing now
  is dimmed, and still presses its key.
- A tap on the map moves the cursor to the cell tapped, by pressing W A S D.
- Checks: the base editor and people checks on DSM, run with SENNITE_TOUCH=1, play by touch.

## Left to the builder

The phone reaches the publisher's server on the Mac by its Wi-Fi address, given in the game's
command-line file on the phone, not built into the APK; the publisher's account's relay is bound on the
Wi-Fi so the wallet on the phone reaches it.
