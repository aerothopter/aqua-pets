# Over-the-air updates

A keeper opens settings, taps UPDATES, then CHECK FOR UPDATES, and the tank updates itself. The cable is for the first install. The Wi-Fi radio is on while checking or
installing and off at every other moment: a tank that is not being asked for an update never touches the network.

The pages and the flow are `common/update.c`, shared with the sim. The radio, the store and the download are the net port (`firmware/main/net_port_esp.c`,
`sim/net_port_sim.c`), and the boot branch is `firmware/main/update_mode.c`. The partition table is in [memory_budget.md](memory_budget.md).

## Update mode: at boot, before the tank

The Wi-Fi driver can't run beside the live tank: the running tank leaves about 23 KB of internal RAM, and a station with lwIP and a TLS session wants 50-80 KB. So the settings tap
doesn't start Wi-Fi. It saves the tank, sets a magic word in RTC memory (magic plus checksum, cleared by a true power-off, so a request can never get stuck) and restarts.

At the top of `app_main`, before the LLM, audio, IMU or the tank are touched, the firmware checks the word. If set, it clears it and runs update mode with the display, touch and Wi-Fi
only, so the driver has the whole internal heap:

- **Cancel, up to date, or any failure:** Wi-Fi stops and de-initializes, and the normal boot follows with no second restart.
- **Success:** the boot slot is switched and the board restarts. "The radio is off when it comes back up" is a property of the structure, not a cleanup step.

| internal RAM, measured | KB |
|---|---|
| free at the top of `app_main` | 240 |
| in update mode, before the radio | 192 |
| with the radio up | 119 |

Wi-Fi IRAM optimizations are off (they buy throughput with internal RAM), buffer counts are small, and lwIP, Wi-Fi allocations and mbedTLS may use PSRAM. The Wi-Fi stack's statics take
~36 KB of internal RAM, so the tank task starts from a one-shot timer after `app_main`'s stack is freed.

## The keeper's flow

1. **The UPDATES page** (from settings) shows the version, the saved network or NO NETWORK, CHECK FOR UPDATES, FORGET NETWORK and CLOSE. No radio.
2. **Battery gate:** below about 20% and not charging, the tank says to plug in first.
3. **Save, restart, update mode:** a plain page, with no creatures drawn.
4. **No network saved: the wizard.** LOOKING FOR NETWORKS → the list (name, signal bars, OPEN) → the password keyboard → CONNECTING.
5. **Connected:** fetch the manifest and compare the release number with the running one. Nothing new: YOUR TANK IS UP TO DATE, radio off, normal boot.
6. **Something new:** VERSION X IS READY with its one-line note, and YES / NOT NOW.
7. **YES:** stream the image into the other slot with a progress bar and CANCEL, verify, switch the boot slot, restart.
8. **First boot of the new build:** an UPDATED card once, and the app marks itself valid for rollback.

Every failure leaves the old tank untouched. A short message names it (wrong password, no signal, server unreachable, interrupted download, bad signature, another board's image) with TRY AGAIN /
OTHER NETWORK / BACK TO TANK, and the radio goes off.

**The keyboard:** 7 x 3 big keys, letters on two pages with a case toggle, digits and every printable symbol on three more, because a WPA passphrase is 8 to 63 of any printable character. The function
row keeps the keys a long passphrase needs most (MORE, DEL, SPACE) away from the two that leave the page (BACK, JOIN). BACK and a refused try keep the text, and a refused password offers TRY AGAIN first. The
typed password shows in the clear (it is the keeper's own desk). **Never log the typed field:** on the saved-network path it is the passphrase.

**Credentials:** one network, in its own NVS namespace and never in the save. Plaintext in NVS, like every hobby ESP device. FORGET NETWORK erases the namespace.

**The touch map.** A V2 panel (CST816) reports its position stretched ~14% about the top left (reported = 1.142 x - 33, 1.138 y - 22), so the touch port maps every tap through the inverse, in the tank and in update
mode. A press in update mode's nine-crosshair calibration (director `ota calib`, `tools/touch_calib.py`, logs in `docs/touch/`) is how a panel's map is fitted.

## Provisioning at install (Improv-serial)

ESP Web Tools speaks Improv Wi-Fi over the serial port after a flash, so the installer page can give a new tank its network with no typing on the glass (`update_mode.c` `provision_mode_run`). A boot caused by a reset over USB
on a tank with no network saved doesn't start the tank: the glass stays dark, the radio is free, the page's scan gets the real list, and the network it sends is connected to (2 tries, inside the page's 45 s) before it is stored. A wrong
passphrase is an error on the page, with nothing saved.

It ends when a network is stored, when the page never asks (8 s), when the page goes quiet (25 s), or at a touch. A RUNNING tank asked for the networks (the page's "Connect to Wi-Fi") saves and restarts into the same mode.

## Safety

- **Signed images without secure boot** (ESP-IDF's "signed app verification without secure boot"): the build appends a signature block and `esp_ota_end` verifies it. OTA accepts only images signed with the key the running image carries (`tools/ota_key.sh`).
  USB flashing still accepts anything, so forks and community ports keep working; they just can't push into a stock tank. Secure boot proper burns eFuses and is wrong for a kit.
- **HTTPS with the IDF certificate bundle,** no pinning: pinned certificates rot on a device that sits on a desk for a year. The client follows GitHub's redirects.
- **Rollback.** A fresh image boots as "pending" and marks itself valid once the save has loaded and the tank has drawn its first frames. A crash before that boots the previous slot.
- **Rollback and the save.** The loader reads its own prefix of a LONGER save too, and a new build marks itself valid BEFORE its first save write.
- **Size and hash before the write** (from the manifest), the signature after.
- **One manifest per board** ([BOARDS.md](BOARDS.md)): `latest-<PT_BOARD>.json`, with a `board` field. The tank refuses another board's manifest and another board's image (its marker, read back at the first 4 KB).

## Releases

- **A workflow on `push: tags: v*`** (`.github/workflows/release.yml`) builds the firmware with the signing key (the repository secret `OTA_SIGNING_KEY`; fork PRs never see it, and PR builds stay unsigned) and attaches the signed image and the
  manifest (`tools/make_ota_manifest.py`) to the GitHub Release. Releases aren't wiped by Pages deploys and have stable URLs.
- **The tag is the gate.** The tank offers only a HIGHER release number than it runs (`PT_RELEASE_NUM`), the key is used by the tag workflow alone, and a pre-release is invisible to "latest".
- **The test channel** is a private folder reached with the director's `ota url <manifest>`, so an OTA is tested end to end before anyone else can see it. Use build-named files and a build-named manifest (`manifest-<build>.json`): the site's proxy
  caches the plain name.

Set `OTA_SIGNING_KEY` on a repo BEFORE its first tagged build, or the cable image ships unsigned and can never update. Losing the key means a cable release for everyone.

### The manifest

Small JSON, one per release and board, fetched before anything is downloaded (`update_manifest_t`).

| field | meaning |
|---|---|
| release, release_num | "0.4.0" and its number; the tank compares the number |
| board | the board the image is for, from the image's own marker |
| build | the git build id |
| min_from | the oldest release this one can update from |
| app: url, size, sha256 | the signed image |
| model: version, size, url | the model this app needs; url only if it is offered over the air |
| note | one plain line for the YES / NOT NOW page |
| needs_cable | true when the model changed and the running build can't fetch it |

## The model is not updated over the air

An update whose manifest names another model says NEEDS THE CABLE. **The model path isn't built;** the hooks for it are in place.

A 7.5 MB model can't be double-buffered in 16 MB (two app slots plus two model slots don't fit), so a model update would be an in-place write, made safe in three ways:

1. **Stage the whole model in PSRAM first** (update mode has it to itself), verify there, and only then erase and write: about 30 s of danger rather than a whole download.
2. **A validity trailer, written last.** The model partition's last sector (0xA8F000; the body stays at offset 0) holds magic, version, length and sha256. *This part is built:* the cable installer writes it, `firmware/main/model_trailer.c` checks it before the mmap,
   and a tank without one hashes the body once and writes it if the body is the shipped model (`PT_MODEL_*` in `version.h`). A cable update writes the app and skips the 7.5 MB model.
3. **A missing model is plain, never masked:** the advisor visibly off, and the next check re-offers it. No silent fallback: the model owns the decisions.

The order in one update would be the model first and then the app, with the boot slot switched only when both verified, and the charger required.

## Testing

- **Director:** `wifi set ssid=... pass=...`, `wifi forget`, `ota check`, `ota url <manifest>`, `ota tap <x> <y>`, `ota page`. The console works in update mode, so the flow can be driven over serial without hands.
- **Sim:** `--selftest-update` walks the whole wizard over pretend networks (`sim/net_port_sim.c` says how to steer them; `AQUA_PETS_FAKE_UPDATE`), `--snapshot` writes every page, and the E key enters update mode.
- **On the device:** the end-to-end run is against the test channel. Every flash still goes through `tools/flash.sh` ([DEVICE.md](DEVICE.md)).

## Not built

Hidden SSIDs, WPA2-Enterprise, captive portals, more than one saved network, scheduled or automatic checks (the radio is on only when asked), Improv over BLE, an access-point portal, NVS encryption.
