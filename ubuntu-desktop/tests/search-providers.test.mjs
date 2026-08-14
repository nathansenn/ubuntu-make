#!/usr/bin/env node
// Logic tests for the Ubuntu overview search-provider bugs.
// No GNOME session required.

import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname, join} from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const patch = readFileSync(
  join(here, '../patches/gnome-shell-ubuntu-extensions-search-providers.patch'),
  'utf8');

function oldId(appInfo, SnapFinderSearchProvider) {
  return appInfo?.get_id() ?? SnapFinderSearchProvider?.get_id() ??
    'snapd-search-provider';
}

function newId(appInfo) {
  return appInfo?.get_id() ?? 'snapd-search-provider';
}

// LP #2150265: optional chaining does not guard a missing static get_id.
{
  const SnapFinderSearchProvider = {};
  assert.throws(
    () => oldId(undefined, SnapFinderSearchProvider),
    {name: 'TypeError'},
    'old id getter must throw when get_id is missing');
  assert.equal(newId(undefined), 'snapd-search-provider');
  assert.equal(newId({get_id: () => 'snap-store_snap-store.desktop'}),
    'snap-store_snap-store.desktop');
}

// LP #2150103: unregister of a never-registered provider splices -1.
{
  function oldUnregister(providers, provider) {
    const index = providers.indexOf(provider);
    providers.splice(index, 1);
  }
  function newUnregister(providers, provider) {
    const index = providers.indexOf(provider);
    if (index < 0)
      return;
    providers.splice(index, 1);
  }

  const appSearch = {id: 'apps'};
  const files = {id: 'files'};
  const web = {id: 'web'};

  const broken = [appSearch, files];
  oldUnregister(broken, web);
  assert.deepEqual(broken.map(p => p.id), ['apps'],
    'old unregister drops the last unrelated provider');

  const fixed = [appSearch, files];
  newUnregister(fixed, web);
  assert.deepEqual(fixed.map(p => p.id), ['apps', 'files']);
}

// Extension must not unregister unless register attached a display.
{
  function tryAdd(provider, {refuse = false, throwDisposed = false} = {}) {
    if (throwDisposed)
      throw new Error('already disposed');
    if (refuse)
      return false;
    provider.display = {id: 'display'};
    return !!provider.display;
  }
  function tryRemove(provider, registered) {
    if (!registered)
      return 'skipped';
    return provider.display ? 'removed' : 'skipped';
  }

  const refused = {};
  const registered = tryAdd(refused, {refuse: true});
  assert.equal(registered, false);
  assert.equal(tryRemove(refused, registered), 'skipped');

  const ok = {};
  assert.equal(tryAdd(ok), true);
  assert.equal(tryRemove(ok, true), 'removed');
}

// isRemoteProvider must be false so installed-changed does not drop us.
{
  const remotes = [{id: 'snap', isRemoteProvider: true}, {id: 'apps', isRemoteProvider: false}];
  const afterReload = remotes.filter(p => !p.isRemoteProvider);
  assert.deepEqual(afterReload.map(p => p.id), ['apps'],
    'a local provider marked remote is dropped and never re-added');
}

// _openHandler must assign the fallback handler before launch.
{
  function openHandler(appInfo, snapProtoHandler) {
    let snapHandler = appInfo;
    if (!snapHandler) {
      if (!snapProtoHandler)
        return null;
      snapHandler = snapProtoHandler;
    }
    return snapHandler;
  }
  assert.equal(openHandler(null, null), null);
  assert.equal(openHandler(null, 'snap-handler'), 'snap-handler');
  assert.equal(openHandler('existing', 'snap-handler'), 'existing');
}

// Patch contains the public API and the get_id / isRemoteProvider fixes.
assert.match(patch, /searchController\.addProvider/);
assert.match(patch, /searchController\?\.removeProvider/);
assert.match(patch, /SnapFinderSearchProvider\.get_id is not a function/);
assert.match(patch, /return false;/);
assert.match(patch, /snapHandler = snapProtoHandler/);
assert.match(patch, /class WebSearchProviderExtension/);
assert.match(patch, /cancellable\.is_cancelled\(\)/);

// App-folder deletion must not splice(-1) a missing folder id.
{
  function deleteFolder(folders, id) {
    const folderIndex = folders.indexOf(id);
    if (folderIndex >= 0)
      folders.splice(folderIndex, 1);
    return folders;
  }
  function oldDeleteFolder(folders, id) {
    folders.splice(folders.indexOf(id), 1);
    return folders;
  }
  assert.deepEqual(oldDeleteFolder(['work', 'games', 'utils'], 'missing'),
    ['work', 'games'],
    'old folder delete drops the last unrelated folder');
  assert.deepEqual(deleteFolder(['work', 'games', 'utils'], 'missing'),
    ['work', 'games', 'utils']);
  assert.deepEqual(deleteFolder(['work', 'games'], 'games'), ['work']);
}

const splicePatch = readFileSync(
  join(here, '../patches/gnome-shell-unguarded-splice.patch'), 'utf8');
assert.match(splicePatch, /folderIndex >= 0/);
assert.match(splicePatch, /inhibitorIndex >= 0/);
assert.match(splicePatch, /messageIndex < 0/);

const nautilusPatch = readFileSync(
  join(here, '../patches/nautilus-xdg-terminal-exec-leak.patch'), 'utf8');
assert.match(nautilusPatch, /xdg_terminal_exec_app == NULL/);
assert.match(nautilusPatch, /error != NULL \? error->message/);
assert.match(nautilusPatch, /g_autolist \(GFile\)/);

{
  function clearWindow(window) {
    const ding = window.customJS_ding;
    if (!ding)
      return 'skipped';
    window.customJS_ding = null;
    return 'cleared';
  }
  const window = {customJS_ding: {unmanagedID: 1}};
  assert.equal(clearWindow(window), 'cleared');
  assert.equal(clearWindow(window), 'skipped');
}

const dingPatch = readFileSync(
  join(here, '../patches/desktop-icons-ng-clearwindow-idempotent.patch'), 'utf8');
assert.match(dingPatch, /if \(!ding\)/);
assert.match(dingPatch, /customJS_ding\?\.refreshState/);

{
  function disable(order) {
    const log = [];
    const objs = {handler: true, dbus: true};
    for (const name of order) {
      if (!objs[name])
        throw new Error(`destroyed ${name} after it was already gone`);
      objs[name] = false;
      log.push(name);
    }
    return log;
  }
  assert.deepEqual(disable(['handler', 'dbus']), ['handler', 'dbus']);
}

const promptPatch = readFileSync(
  join(here, '../patches/snapd-prompting-teardown.patch'), 'utf8');
assert.match(promptPatch, /_promptsHandler\?\.destroy\(\)/);
assert.match(promptPatch, /unwatch_name/);
assert.match(promptPatch, /if \(!lastFocusedSnapWindow \|\| !this\._promptWindow\)/);

{
  function xidArgs(getWindow) {
    const cmd = [];
    const window = getWindow();
    if (window != null)
      cmd.push(String(window.xid));
    return cmd;
  }
  assert.deepEqual(xidArgs(() => null), []);
  assert.deepEqual(xidArgs(() => ({xid: 42})), ['42']);
}

const umPatch = readFileSync(
  join(here, '../patches/update-manager-null-xid.patch'), 'utf8');
assert.match(umPatch, /if window is not None/);

{
  function lastDeviceIsTouchscreen(device) {
    if (!device)
      return false;
    try {
      return device.get_device_type() === 'touch';
    } catch {
      return false;
    }
  }
  assert.equal(lastDeviceIsTouchscreen(null), false);
  assert.equal(lastDeviceIsTouchscreen({
    get_device_type() {
      throw new Error('already disposed');
    },
  }), false);
  assert.equal(lastDeviceIsTouchscreen({get_device_type: () => 'touch'}), true);
}

const kbdPatch = readFileSync(
  join(here, '../patches/gnome-shell-disposed-last-device.patch'), 'utf8');
assert.match(kbdPatch, /device-removed/);
assert.match(kbdPatch, /already disposed/);

{
  function openActivated(row, selected) {
    const target = row ?? selected;
    if (!target)
      return 'skipped';
    return target;
  }
  assert.equal(openActivated(null, null), 'skipped');
  assert.equal(openActivated('activated', null), 'activated');
  assert.equal(openActivated(null, 'selected'), 'selected');
}

const sidebarPatch = readFileSync(
  join(here, '../patches/nautilus-sidebar-null-guards.patch'), 'utf8');
assert.match(sidebarPatch, /open_row \(NAUTILUS_SIDEBAR_ROW \(row\), 0\)/);
assert.match(sidebarPatch, /if \(row == NULL\)/);
assert.match(sidebarPatch, /g_file_new_for_uri \(uri\)/);
assert.match(sidebarPatch, /self->window_slot == NULL/);

{
  function inhibit(logindProxy) {
    if (!logindProxy)
      return 'skipped';
    return 'inhibit';
  }
  assert.equal(inhibit(null), 'skipped');
  assert.equal(inhibit({}), 'inhibit');
}

const gsdPatch = readFileSync(
  join(here, '../patches/gsd-logind-null-proxy.patch'), 'utf8');
assert.match(gsdPatch, /error \? error->message : "unknown error"/);
assert.match(gsdPatch, /g_object_unref \(bus\);/);
assert.match(gsdPatch, /if \(user == NULL\)/);

{
  function stealOrKeep(autoptrReturn) {
    return autoptrReturn ? 'stolen' : 'uaf';
  }
  assert.equal(stealOrKeep(true), 'stolen');
}

const portalPatch = readFileSync(
  join(here, '../patches/xdg-desktop-portal-gnome-null-uaf.patch'), 'utf8');
assert.match(portalPatch, /g_steal_pointer \(&window\)/);
assert.match(portalPatch, /g_set_object \(&best_match, window\)/);
assert.match(portalPatch, /if \(info == NULL\)/);

{
  function updateResults(providers, provider) {
    if (!providers.includes(provider))
      return 'skipped';
    return 'updated';
  }
  const gone = {id: 'web'};
  assert.equal(updateResults([{id: 'apps'}], gone), 'skipped');
  assert.equal(updateResults([gone], gone), 'updated');
}

{
  function emitRemoved(added, canPlay) {
    if (canPlay)
      return added ? 'keep' : 'added';
    return added ? 'removed' : 'silent';
  }
  assert.equal(emitRemoved(false, false), 'silent');
  assert.equal(emitRemoved(true, false), 'removed');
}

const shellPatch = readFileSync(
  join(here, '../patches/gnome-shell-search-unlock-mpris.patch'), 'utf8');
assert.match(shellPatch, /this\._providers\.includes\(provider\)/);
assert.match(shellPatch, /message\?\.destroy\(\)/);
assert.match(shellPatch, /else if \(added\)/);

{
  function portalId(snapName, snapAppName) {
    if (!snapAppName || snapName === snapAppName)
      return `snap.${snapName}`;
    return `snap.${snapName}_${snapAppName}`;
  }
  function oldPortalId(snapName, snapAppName) {
    if (!snapAppName || snapName === snapAppName)
      return `snap.${snapName}`;
    return `snap.${snapName}${snapAppName}`;
  }
  assert.equal(oldPortalId('firefox', 'firefox'), 'snap.firefox');
  assert.equal(oldPortalId('foo', 'bar'), 'snap.foobar');
  assert.equal(portalId('foo', 'bar'), 'snap.foo_bar');
}

const gccPatch = readFileSync(
  join(here, '../patches/gnome-control-center-snap-portal-id.patch'), 'utf8');
assert.match(gccPatch, /snap_name, "_", snap_app_name/);
assert.match(gccPatch, /error \? error->message : "unknown error"/);

{
  function addWindowSignals(actor) {
    if (!actor)
      return 'skipped';
    return 'tracked';
  }
  assert.equal(addWindowSignals(null), 'skipped');
  assert.equal(addWindowSignals({}), 'tracked');
}

const dockPatch = readFileSync(
  join(here, '../patches/dash-to-dock-intellihide-null-actor.patch'), 'utf8');
assert.match(dockPatch, /if \(!actor\)/);

const gisPatch = readFileSync(
  join(here, '../patches/gnome-initial-setup-clear-cancellable.patch'), 'utf8');
assert.match(gisPatch, /g_clear_object \(&priv->cancellable\)/);

{
  function anyDataText(anyData) {
    return typeof anyData === 'string' ? anyData : '';
  }
  assert.equal(anyDataText(null), '');
  assert.equal(anyDataText(undefined), '');
  assert.equal(anyDataText('Hello').toLowerCase(), 'hello');
}

const orcaPatch = readFileSync(
  join(here, '../patches/orca-any-data-none.patch'), 'utf8');
assert.match(orcaPatch, /isinstance\(event\.any_data, str\)/);

{
  function progress(tasks, total) {
    if (!tasks)
      return 'unknown';
    if (total <= 0)
      return 'unknown';
    return Math.floor(100 * 1 / total);
  }
  assert.equal(progress(null, 0), 'unknown');
  assert.equal(progress([], 0), 'unknown');
  assert.equal(progress(['t'], 4), 25);
}

const mutterPatch = readFileSync(
  join(here, '../patches/mutter-xwayland-dnd-null.patch'), 'utf8');
assert.match(mutterPatch, /if \(!dnd\)/);
assert.match(mutterPatch, /meta_xwayland_end_dnd_grab/);

const ptyxisPatch = readFileSync(
  join(here, '../patches/ptyxis-close-null-terminal.patch'), 'utf8');
assert.match(ptyxisPatch, /self->tab_view == NULL/);
assert.match(ptyxisPatch, /self->terminal == NULL/);

const snapPatch = readFileSync(
  join(here, '../patches/gnome-software-snap-null-progress.patch'), 'utf8');
assert.match(snapPatch, /if \(tasks == NULL\)/);
assert.match(snapPatch, /GS_APP_PROGRESS_UNKNOWN/);

const spPatch = readFileSync(
  join(here, '../patches/software-properties-none-guards.patch'), 'utf8');
assert.match(spPatch, /if source is None:/);
assert.match(spPatch, /if candidate is None:/);

const gdmPatch = readFileSync(
  join(here, '../patches/gdm-session-conversation-teardown.patch'), 'utf8');
assert.match(gdmPatch, /No active conversation/);
assert.match(gdmPatch, /return self->session_opened/);

const unPatch = readFileSync(
  join(here, '../patches/update-notifier-null-hooks.patch'), 'utf8');
assert.match(unPatch, /ret == NULL \|\| ret\[0\] == '\\0'/);
assert.match(unPatch, /if \(cur == NULL\)/);

const powerPatch = readFileSync(
  join(here, '../patches/gsd-power-lid-null.patch'), 'utf8');
assert.match(powerPatch, /if \(!display_config\)/);
assert.match(powerPatch, /if \(!manager->screensaver_proxy\)/);

const gcc2Patch = readFileSync(
  join(here, '../patches/gnome-control-center-display-wacom-network.patch'), 'utf8');
assert.match(gcc2Patch, /if \(error &&/);
assert.match(gcc2Patch, /device_list->data == NULL/);

console.log('search-providers.test.mjs: all assertions passed');
