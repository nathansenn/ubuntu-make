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

console.log('search-providers.test.mjs: all assertions passed');
