# Ubuntu 26.04.1 desktop — non-performance bugs

Researched against the Resolute 26.04.1 source trees fetched into `src/`.
Performance / I/O / SIMD issues are out of scope (see `ubuntu24/`).

## Fixed here (`patches/gnome-shell-disposed-last-device.patch`)

`KeyboardManager` kept the last `MetaInputDeviceNative` after the seat
destroyed it (logout / GDM greeter). `_lastDeviceIsTouchscreen()` then
threw "already disposed" (journal from LP #2125720) and left the OSK /
password entry unresponsive. Clear on `device-removed` and catch disposed
access.

## Fixed here (`patches/update-manager-null-xid.patch`)

`UpdateManager.show_settings()` on X11 called `self.get_window().get_xid()`
with no realize check. After `hide()` (install backend pane) that is
`AttributeError: 'NoneType' object has no attribute 'get_xid'`.

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

## Fixed here (`patches/nautilus-sidebar-null-guards.patch`)

Places sidebar activation opened `gtk_list_box_get_selected_row()` instead
of the activated row, so keyboard/touch activation with no selection called
`g_object_get()` on NULL. Alt+Down unmount had the same gap. Bookmark
reorder used `nautilus_file_get_location()` when the target was gone
(unmounted), then `g_file_get_uri(NULL)`. Opening a place before a
`window_slot` was set dereferenced the slot. Wallpaper portal failure
logged `error->message` when finish returned FALSE with a NULL error.

## Fixed here (`patches/gsd-logind-null-proxy.patch`)

Media-keys logged a failed logind proxy and then called
`g_dbus_proxy_call_with_unix_fd_list()` on it (guaranteed SEGV when
systemd is missing). Lock-screen finish used `error->message` when
`error` was NULL. xsettings called `act_user_is_loaded()` on a NULL
`ActUser`.

## Fixed here (`patches/xdg-desktop-portal-gnome-null-uaf.patch`)

Screencast restore returned a `g_autoptr(ShellWindow)` and stored another
as `best_match` without taking a ref — use-after-free when restoring a
window session. App chooser rows and the account dialog called
`g_app_info_get_*` when `g_desktop_app_info_new()` returned NULL (snap
desktop IDs).

## Fixed here (`patches/gnome-shell-search-unlock-mpris.patch`)

`_doProviderSearch` ran `_updateResults` in `finally` after the provider
was unregistered (disposed display). unlockDialog `_removePlayer` called
`message.destroy()` when MPRIS emitted `player-removed` for a player that
never got a lock-screen UI. mpris `notify::can-play` emitted
`player-removed` on the first false transition.

## Fixed here (`patches/gnome-control-center-snap-portal-id.patch`)

`cc_util_app_get_portal_id()` built `snap.<name><app>` instead of
`snap.<name>_<app>`. The applications panel already splits on `_`.
Desktop sharing still used `error->message` on `secret_service_get_*`
failure (collection-for-alias was already guarded).

## Fixed here (`patches/dash-to-dock-intellihide-null-actor.patch`)

`window-created` can fire before `get_compositor_private()` returns an
actor. Intellihide connected signals on NULL and crashed the dock.

## Fixed here (`patches/gnome-initial-setup-clear-cancellable.patch`)

Summary page used `g_clear_pointer(&cancellable, g_free)` on a
`GCancellable` (should be `g_clear_object`).

## Fixed here (`patches/orca-any-data-none.patch`)

AT-SPI text events can have `any_data=None`. Typing echo, live regions,
chat, terminal utilities, and event-reason helpers called `.lower()` /
`.strip()` / `"in"` on it and took Orca down.

## Fixed here (`patches/mutter-xwayland-dnd-null.patch`)

X11 display close freed the XWayland DnD manager while grab handlers
could still run. Enter/release/dest callbacks also used a NULL dnd
object or `dnd_data_source`.

## Fixed here (`patches/ptyxis-close-null-terminal.patch`)

Async close-tab dialog resolved the ancestor window after teardown.
Agent poll, save-size, grab-focus, and notify destroy used a NULL
terminal widget.

## Fixed here (`patches/gnome-software-snap-null-progress.patch`)

Snap `progress_cb` divided by a zero total and walked a NULL task
array. App/media refine and markdown description did the same with
NULL GPtrArrays. Flatpak size refine passed a NULL runtime.

## Fixed here (`patches/software-properties-none-guards.patch`)

D-Bus ToggleSourceUse / EnableChildSource crashed on unknown lines.
Driver apply used `candidate.ver_str` when apt had no candidate. Qt
mirror dialog assumed `currentItem()`.

## Fixed here (`patches/gdm-session-conversation-teardown.patch`)

Logout cancelled conversations without completing BeginVerification
or worker queries (greeter hang). SessionExited cleared a newer
conversation pointer. `get_session_id` ignored `session_opened`.

## Fixed here (`patches/update-notifier-null-hooks.patch`)

`g_spawn_sync` stderr can be NULL (live-CD path). Hook RFC822
continuation before any header, missing Description, and a failed
`/proc/uptime` read crashed the daemon.

## Fixed here (`patches/gsd-power-lid-null.patch`)

Lid close queried DisplayConfig with a NULL proxy and logged a NULL
GError. Inhibit/suspend finish and ScreenSaver lock had the same
gap.

## Fixed here (`patches/gnome-control-center-display-wacom-network.patch`)

Display panel init logged `error->message` when finish left error
NULL. Wacom mock-stylus idle and output combo used empty/stale
models. QR/Wi-Fi rows assumed NM wireless/connection settings exist.

## Looked at, not changed

- Dash-to-dock lock-screen watchdog and a11y focus: already updated in
  50.26.04.7ubuntu (LP #2146516, #2147922, #2148339).
- Desktop Icons NG stacking / unmanaged window: already in this version
  (LP #2147581).
- AppIndicator lock-screen name-own race: documented FIXME, watchdog already
  present; no new evidence it is still broken.
- Remaining mutter Wayland leftover-resource / NULL actor paths
  (touch cancel, xwayland regions) are still open.

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
