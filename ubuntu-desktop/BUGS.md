# Ubuntu 26.04.1 desktop — non-performance bugs

Researched against the Resolute 26.04.1 source trees fetched into `src/`.
Performance / I/O / SIMD issues are out of scope (see `ubuntu24/`).

## Fixed here (`patches/snapd-prompting-teardown.patch`)

Snap prompting `disable()` destroyed the D-Bus server before `PromptsHandler`,
so lock-screen teardown raced in-flight `prompt-request` signals.
`PromptsHandler.destroy()` left `WindowsGroup` actors and a `watch_name`
alive. `_adjustPromptPosition()` called `get_frame_rect()` when every snap
window was already gone.

## Fixed here (`patches/desktop-icons-ng-clearwindow-idempotent.patch`)

Desktop Icons NG `emulateX11WindowType._clearWindow()` is not idempotent.
`disable()` (lock screen) walks `_windowList` while an `unmanaged` handler
can clear the same MetaWindow. The second call reads `window.customJS_ding`
after it was set to null and throws, taking down the desktop icons surface.

## Fixed here (`patches/gnome-shell-unguarded-splice.patch`)

Same class as LP #2161808, still present in gnome-shell 50.1-0ubuntu1.2
outside `search.js`: `indexOf` + `splice` with no `index < 0` guard.

| Site | What `splice(-1)` drops |
|---|---|
| `js/ui/appDisplay.js` `_removeItem` / `_moveItem` | last app icon in the grid |
| `js/ui/appDisplay.js` folder delete | **wrong folder** from `org.gnome.desktop.app-folders` |
| `js/ui/endSessionDialog.js` | last logout inhibitor |
| `js/ui/layout.js` | last pressure barrier (hot corner) |
| `js/ui/dateMenu.js` | last notification source |
| `js/ui/workspace.js` / `workspaceAnimation.js` / `workspaceThumbnail.js` | last overview window/thumbnail |
| `js/ui/messageList.js` `_moveMessage` | last banner if the message was destroyed mid-animation |
| `js/ui/iconGrid.js` `_removeItemData` | last child on the page |

## Fixed here (`patches/nautilus-xdg-terminal-exec-leak.patch`)

Ubuntu's `xdg-terminal-exec` path in `nautilus_files_view_update_actions_state()`
(runs on every selection change) called `g_app_info_create_from_commandline()`
unconditionally, leaking the previous static `GAppInfo`. Failure used
`error->message` without a NULL check (same class as LP #2000063). Open in
Terminal leaked the `GFile` because `g_autoptr(GList)` does not free list data.

## Fixed here (`patches/gnome-shell-ubuntu-extensions-search-providers.patch`)

### LP #2150103 / #2161808 / #2156486 — overview search dies after lock

**Packages:** `gnome-shell-ubuntu-extensions` (trigger), `gnome-shell` (shell half)

After lock/unlock, Super+type returns no apps. Journal:

```
Object Gjs_ui_search_ListSearchResults, has been already disposed
```

or, on older gnome-shell, search is silently empty with no JS error.

**Shell half (already in 50.1-0ubuntu1.2):**
`_unregisterProvider` did `indexOf` then `splice(-1, 1)` when the provider was
never in `_providers`. That removes the last, unrelated provider. After a few
lock cycles the overview has nothing left.

**Extension half (still broken in 50.26.04.7ubuntu, fixed by this patch):**

1. Web Search Provider `enable()` is `async`. After `await getDefaultBrowser`,
   `disable()` may already have run (lock screen). The old code still
   registered.
2. `_registerProvider` is private and can **refuse** (parental controls /
   Chrome) without throwing. `disable()` still called `_unregisterProvider`.
3. Both extensions walked
   `Main.overview._overview.controls._searchController._searchResults`
   with no dispose guard. After overview recreation that object is dead and
   the exception escapes the extension system.
4. Web Search Provider class was copy-pasted as `SnapdSearchProviderExtension`.
5. `error.matches(Gio.IOErrorEnum.CANCELLED)` is the wrong GError signature
   (needs domain + code), so a cancelled enable was logged as a real failure.

**Fix:** use public `Main.overview.searchController.addProvider` /
`removeProvider`; track `_registered` from `!!provider.display`; only
unregister if register attached a display; abort enable after cancel; catch
disposed SearchController.

### LP #2150265 — `SnapFinderSearchProvider.get_id is not a function`

```javascript
return this.appInfo?.get_id() ?? SnapFinderSearchProvider?.get_id() ??
    'snapd-search-provider';
```

`SnapFinderSearchProvider.get_id` does not exist. `?.` only guards the class
object, so this throws `TypeError` during `_doSearch` and that provider's
results (and, in the `finally` of `_doProviderSearch`, the update of
`this._results[provider.id]`) blow up.

`this.appInfo` is often unset because the constructor used
`snapProtoHandler?.should_show()`. The `snap:` URI fallback is a plain
`Gio.AppInfo` with no `should_show`, so optional-call returns `undefined` and
`appInfo` stays unset — which is exactly when the broken fallback runs.

**Fix:** `this.appInfo?.get_id() ?? 'snapd-search-provider'`, and set
`appInfo` for any handler that is not an explicitly hidden DesktopAppInfo.

### Snap finder advertised as a remote provider

`isRemoteProvider` was `true`. `SearchResultsView._reloadRemoteProviders`
unregisters every remote provider on `AppSystem::installed-changed` and only
re-adds D-Bus remotes from `RemoteSearch.loadRemoteSearchProviders`. The snap
finder is in-process, so the first apt/snap install or remove dropped it for
the rest of the session.

**Fix:** `isRemoteProvider = false` (same as the web searcher).

### Snap finder `_openHandler` launched a null handler

If `this.appInfo` was unset but a `snap:` handler existed, the code logged
nothing, left `snapHandler` null, and called `launch_uris_async` on it.
Also `launch_uris_async` was never promisified in this file.

**Fix:** assign `snapHandler = snapProtoHandler`; promisify
`Gio.AppInfo.prototype.launch_uris_async`.

### Snap finder `init()` required `this.display`

`init()` always ran after `_registerProvider`, even when parental controls
refused and `_ensureProviderDisplay` never ran. `this.display.connect` then
threw.

**Fix:** only `init()` after a successful register; no-op if `display` is
missing.

## Already fixed in the fetched 26.04.1 trees (do not re-patch)

- `gnome-shell` 50.1-0ubuntu1.2: unregister `index < 0` guard (LP #2161808).
- `gdm3` 50.1-0ubuntu0.1: session-type / login-screen fixes from the 50.x
  series (LP #2125720 class).
- `gnome-control-center` 50.3: nullable libsecret error in remote-desktop.
- `mutter` 50.1-0ubuntu2.2: Wayland popup `wl_resource_get_client` crash
  (LP #2127757, fixed upstream in 49.2 and present here).

## Looked at, not changed

- Dash-to-dock lock-screen watchdog and a11y focus: already updated in
  50.26.04.7ubuntu (LP #2146516, #2147922, #2148339).
- Desktop Icons NG stacking / unmanaged window: already in this version
  (LP #2147581).
- AppIndicator lock-screen name-own race: documented FIXME, watchdog already
  present; no new evidence it is still broken.
- Nautilus / mutter / gsd: no additional unfixed crashers identified in this
  pass that are clearly ours to patch without a reproducer.

## Seed coverage

Fetched under `src/` (apt source of Resolute / resolute-updates). Session
stack plus the desktop / desktop-minimal seed:

- gnome-shell 50.1-0ubuntu1.2, mutter 50.1-0ubuntu2.2, gdm3 50.1-0ubuntu0.1
- nautilus 1:50.2.2-0ubuntu0.1, gnome-control-center 1:50.3-0ubuntu0.1
- gnome-settings-daemon 50.0-1ubuntu1, gnome-session 50.1-0ubuntu0.1
- gnome-shell-ubuntu-extensions 50.26.04.7ubuntu
- ubuntu-settings 26.04.6, yaru-theme 26.04.5.1ubuntu, ubuntu-meta 1.570.2
- gnome-initial-setup 50.0-0ubuntu7.1, gnome-keyring 50.0-1
- gnome-remote-desktop 50.0-0ubuntu2, gnome-software 50.0-1
- xdg-desktop-portal 1.21.1+ds-1ubuntu3, xdg-desktop-portal-gnome 50.0
- pipewire 1.6.2-1ubuntu1.1, wireplumber 0.5.13-1ubuntu1
- loupe 50.0, papers 50.2, ptyxis 50.1, gnome-text-editor 50.1
- gnome-calculator/calendar/characters/clocks/snapshot 50.x
- orca 50.2, at-spi2-core 2.60.4, ibus 1.5.34~rc2
- update-manager 26.04.5, software-properties 0.120.1,
  ubuntu-release-upgrader 26.04.22

`ubuntu-wallpapers` orig (~900 MiB) is downloaded but not unpacked.
`gnome-bluetooth` on this Noble host resolved to the 3.34 GTK3 source;
Resolute uses `gnome-bluetooth3` — fetch that name next time.
