# Browser installer

Plug the board in, open a page, click Install: the same "flash it from the
browser" flow ESPHome and Home Assistant use ([ESP Web
Tools](https://esphome.github.io/esp-web-tools/), Apache-2.0, vendored under
`vendor/`). Chrome or Edge on a desktop; it uses Web Serial, which Safari
and Firefox don't have. Chrome on Android works too: it has WebUSB
but no Web Serial, so the page installs Google's web-serial-polyfill (vendored under
`vendor/web-serial-polyfill/`, Apache-2.0) as `navigator.serial` only when Web Serial
is missing, and ESP Web Tools runs unchanged over the board's USB CDC interface.
`?polyfill=1` forces it on a desktop for testing. iOS has neither API.

## Build the upload folder

```
idf.py -B ~/.cache/aqua-pets/fw-build build     # in firmware/ (or any -B dir)
tools/make_installer.py                            # -> installer/dist/
python3 -m http.server -d installer/dist 8765      # local check at http://localhost:8765
```

The page uses a cream and lagoon-green layout with previews of the themes
(only the Original is built for now: docs/THEMES.md). It requires an explicit board choice
before enabling Install, and validates the selected manifest. ESP Web Tools and
all artwork are served locally, with no external runtime dependencies.

`installer/dist/` is the whole thing: `index.html`, `manifest.json`,
`manifest-erase.json`, `firmware/*.bin` (bootloader, partition table, app,
otadata, model, model trailer), `vendor/`. The offsets in the manifests come
from the build's `flasher_args.json` and from `firmware/partitions.csv`, and the
page carries the release and build id. It is gitignored: rebuild it for every
release.

## Updating never erases (the two manifests, and one patched line)

The tank's save lives in NVS at `0x9000`. The six parts sit at `0x0`, `0x8000`,
`0x10000`, `0x290000`, `0xA8F000` and `0xA90000`, so a plain write of them leaves
it alone and the tank carries on after an update (`tools/flash.sh` does the same
writes). The catch is in the dialog: ESP Web Tools 10.4.0 has no manifest option
for "install, don't erase" on a device without Improv.
`new_install_prompt_erase: true` asks (checkbox off by default), and without
it the dialog erases the chip first, unconditionally. So:

- `manifest.json` (the **Install update** button) carries `"never_erase": true` and
  `make_installer.py` patches the copied `install-dialog-*.js` at assembly:
  the two click handlers that read `_startInstall(!0)` (erase) become
  `_startInstall(!this._manifest.never_erase)`. The build fails loudly if
  the handler text is not found exactly twice, so a vendor upgrade can't
  ship an erasing page by accident. `vendor/` itself stays pristine.
- `manifest-erase.json` (under **Install fresh / erase options**)
  is the same parts with the erase question, for a board that is stuck or
  a keeper who wants a clean flash. The on-device reset (hold BOOT, tap the
  glass) is the normal way to start over and needs no page.

The firmware side of the promise: `common/progression.c` writes save layout 3
and loads any layout 3 save. Within it a field only ever appends, and a NEWER
build's longer save loads its head, so rolling back keeps the tank. **The one
exception:** a tank saved by an earlier layout (PTK2) is not loaded, and the
tank starts fresh; the first release on layout 3 must say so in its changelog.
What guards it:

- **SAVE LAYOUT LOCK** (progression.c): a compile-time assert on every
  save_t / fish_save_t / decor and helper record offset, the magic, the NVS
  names, and the NVS size budget (4000 B, math in the comment). A field
  inserted mid-struct, a rename of `tank`/`save`, or a save too big for the
  partition fails the build, on the device and in the sim.
- **`sim/fishsim --selftest-saves`**: a tank with every field set comes back
  field by field and re-saves the same bytes; every layout 3 save in
  `sim/testdata/saves/` loads; every PTK2 save there starts fresh cleanly;
  a newer build's longer save loads (the rollback). Add a save from each
  release there (its README says how).
- **`check_nvs_untouched`** (make_installer.py): the installer build fails
  if the partition table it ships moves or resizes nvs, or if any part it
  writes overlaps 0x9000..0xF000.
- **Boot never erases on a guess** (main.c `nvs_start`): only an NVS that
  can't mount at all ("no free pages") is erased, and a raw copy goes to the
  front of the unused `storage` partition first (`esptool read_flash
  0xD20000 0x6000`); any other init error runs the tank without saving and
  leaves the flash alone. A save that exists but won't read turns saving
  off until a reset, so it is never overwritten by a fresh tank.

## It updates itself (GitHub Pages)

`.github/workflows/installer.yml` builds the firmware with ESP-IDF v5.4.1 on every
push to `main` that touches `firmware/`, `common/`, `model/out/`, `installer/`,
the assembler or the workflow. In `aerothopter/aqua-pets` (the workflow's guards) it
then runs `tools/make_installer.py` and deploys the folder to
**https://aquapets.com/install/**; in any other repo the job stops at the compile
check. The build signs with the `OTA_SIGNING_KEY` secret. Without that secret the
build fails in `aerothopter/aqua-pets`; any other repo builds with a throwaway key,
and an image signed with it can never take an update over the air.

Pages serves HTTPS with `Access-Control-Allow-Origin: *`, so a page on another
site can point its button at that manifest:

```
tools/make_installer.py --manifest-url https://aquapets.com/install/manifest.json --out /tmp/site
```

That `index.html` and its `vendor/esp-web-tools-<tag>/` folder go up together,
because the never-erase patch is in the vendor's dialog bundle: an old `vendor/`
next to a new manifest erases every install without asking. Hosts serve `.js`
with a year's max-age, so the folder name carries the patched dialog's hash.

The page fetches the manifest on load and shows the version and build date of
what it will actually flash, so pushing is the whole release step: no upload, no
cache purge. If a release does not appear, the Actions run is red: `gh run list
--repo aerothopter/aqua-pets`.

## Android, and a local server's certificate

Chrome on Android installs through WebUSB (the vendored web-serial-polyfill) and
shows a USB chooser on Install, but Chrome refuses every permission prompt on a
page it opened through a certificate warning. A self-signed server must therefore
be TRUSTED by the phone first: it presents a CA certificate whose Subject
Alternative Names are the machine's addresses, and serves it at
`/aqua-pets-installer.crt`. The server is not in this repo. The page's Android
section links that certificate with the install steps (Settings → Security →
Install a certificate → CA certificate) and has a **Test USB access** button that
runs the chooser alone and says in words what happened. A public HTTPS host with
a real certificate needs none of this.

## Host it

Web Serial needs a secure context, so the page must be on **HTTPS** (or
`localhost`). Any static host works; the manifest and the `.bin` files must be
fetchable from the page's origin (or send CORS headers).

**WordPress behind Cloudflare:** upload `installer/dist/` as a folder next to
WordPress, e.g. `public_html/aqua-pets/`. Being a plain folder it is outside
WordPress, so themes, caching and security plugins don't touch it. Things to
check once:

- Open the page and choose a board: the button must say *Install update*, not the
  unsupported text. If it never appears, Cloudflare's Rocket Loader is
  rewriting the module script: exclude `/aqua-pets/*` from it (a
  Configuration Rule), or turn it off.
- In the browser's network tab `manifest.json` and `firmware/*.bin` must be
  200. A 403 on `.bin` means the host blocks the type: the `.htaccess` in the
  folder adds it for Apache/LiteSpeed; on nginx add
  `types { application/octet-stream bin; }`.
- Cloudflare caches `.bin` and `.js` by default. After uploading a new build,
  purge `/aqua-pets/*` (the manifest carries the version, so a stale
  cache shows an old version string on the page).

**GitHub Pages** is what the workflow above uses. The button can also live on a
WordPress page (Custom HTML block with the `<script type="module">` tag and the
`<esp-web-install-button manifest="https://...manifest.json">` element)
while the binaries stay on Pages.

## What the user sees

Click → the browser's port picker (`USB JTAG/serial debug unit`) → *Install
Aqua Pets* → "Do you want to install Aqua Pets <version>?" → a progress
bar over the parts → *Installation complete*, and the board resets into
the tank: fresh on a blank board, the same tank on one that had it. The erase
option adds the erase question (tick *Erase device*). The page's steps and its
**Cannot connect?** section cover the usual snags: a sleeping tank hides its USB
port (press PWR), holding BOOT while plugging in forces download mode, a USB-C
cable must carry data, other tabs or serial monitors can hold the port, and Linux
may need serial-port permissions.

## Testing a build

Write the artifact set in `dist/firmware/` to a board at the manifest's offsets
with `esptool.py write_flash`: it is the operation ESP Web Tools performs, from the
same files. Check the page, manifest and vendor bundle from a local server. The
browser's port picker is a native dialog, so the click-through itself is a human
test.
