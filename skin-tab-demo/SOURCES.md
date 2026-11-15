# Web preview assets

The browser demo's `data.js` is a reduced snapshot of the English
[ByMykel/CSGO-API](https://github.com/ByMykel/CSGO-API) data (MIT licence),
retrieved 2026-11-15. It contains names, identifiers, rarity colours, team
metadata and image URLs only, including music kit entries. The dataset also contains Counter-Strike 2 items
and is not the live CS:GO item schema used by the DLL.

The referenced inventory images are delivered remotely by Steam; they are
Valve/Counter-Strike assets, not provided under the API's MIT licence. An
internet connection is required to show them. No Steam images are bundled in
this repository. The user-provided `assets/agent-preview.png` is only a fallback
if an agent image fails to load. Josefin Sans files are extracted from the
existing Fatality GUI resources in this repository.

The animated agent and weapon preview is a 2D web mock, not a Source engine
model renderer. The web page has its own temporary selection state and never
changes the game or the DLL's settings.
