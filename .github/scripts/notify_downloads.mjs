import { pathToFileURL } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';

// Keep the ledger separate from the app's source and from expiring Actions caches.
const stateBranch = 'download-counter-state';
const stateFile = 'downloads.json';
const avatar = 'https://raw.githubusercontent.com/fenixJK/Aurex/main/assets/aurex.png';

export function githubClient(repository, token, request = fetch) {
  if (!/^[\w.-]+\/[\w.-]+$/.test(repository ?? '') || !token)
    throw new Error('GitHub repository and token are required.');
  return async (path, { method = 'GET', body, allowMissing = false } = {}) => {
    let response;
    try {
      response = await request(`https://api.github.com/repos/${repository}/${path}`, {
        method, redirect: 'error', signal: AbortSignal.timeout(20000),
        headers: {
          Authorization: `Bearer ${token}`, Accept: 'application/vnd.github+json',
          'X-GitHub-Api-Version': '2022-11-28', 'Content-Type': 'application/json',
          'User-Agent': 'Aurex-Downloads',
        },
        body: body === undefined ? undefined : JSON.stringify(body),
      });
    } catch {
      throw new Error('GitHub request interrupted; the saved counter was not reset.');
    }
    if (allowMissing && response.status === 404) return null;
    if (!response.ok) throw new Error(`GitHub request failed (HTTP ${response.status}).`);
    return response.json();
  };
}

export function discordClient(webhook, request = fetch, pause = sleep) {
  let url;
  try { url = new URL(webhook); } catch { throw new Error('DOWNLOAD_WEBHOOK_URL is missing or invalid.'); }
  if (url.protocol !== 'https:' || !['discord.com', 'discordapp.com'].includes(url.hostname)
      || url.username || url.password || url.port
      || !/^\/api(?:\/v\d+)?\/webhooks\/\d+\/[A-Za-z0-9_-]+$/.test(url.pathname))
    throw new Error('DOWNLOAD_WEBHOOK_URL must be a Discord webhook.');
  url.searchParams.set('wait', 'true');
  return async body => {
    for (let attempt = 0; attempt < 3; ++attempt) {
      let response;
      try {
        response = await request(url, {
          method: 'POST', redirect: 'error', signal: AbortSignal.timeout(20000),
          headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
        });
      } catch {
        throw new Error('Discord delivery was interrupted; check the channel before manually retrying.');
      }
      if (response.status === 429 && attempt < 2) {
        const delay = Number((await response.json()).retry_after);
        if (!Number.isFinite(delay) || delay < 0 || delay > 45)
          throw new Error('Discord rate limit is too long; remaining downloads stay queued.');
        await pause(Math.ceil(delay * 1000));
        continue;
      }
      if (!response.ok) throw new Error(`Discord delivery failed (HTTP ${response.status}).`);
      const message = await response.json();
      if (!message.id) throw new Error('Discord did not confirm saving the message.');
      return message.id;
    }
  };
}

async function listAll(github, path) {
  const items = [];
  for (let page = 1; ; ++page) {
    const batch = await github(`${path}?per_page=100&page=${page}`);
    items.push(...batch);
    if (batch.length < 100) return items;
  }
}

export async function releaseAssets(github) {
  const assets = [];
  for (const release of await listAll(github, 'releases')) {
    if (release.draft) continue;
    for (const asset of await listAll(github, `releases/${release.id}/assets`)) {
      if (asset.state !== 'uploaded') continue;
      if (!Number.isSafeInteger(asset.id) || !Number.isSafeInteger(asset.download_count) || asset.download_count < 0)
        throw new Error('GitHub returned an invalid asset count; no counts were changed.');
      assets.push({ ...asset, tag: release.tag_name, releaseUrl: release.html_url });
    }
  }
  return assets.sort((a, b) => a.id - b.id);
}

export function readState(content) {
  const state = JSON.parse(Buffer.from(content, 'base64').toString('utf8'));
  if (state.version !== 1 || !state.assets || typeof state.assets !== 'object' || Array.isArray(state.assets)
      || !Number.isSafeInteger(state.total) || state.total < 0
      || Object.entries(state.assets).some(([id, count]) => !/^\d+$/.test(id) || !Number.isSafeInteger(count) || count < 0)
      || Object.values(state.assets).reduce((sum, count) => sum + count, 0) !== state.total
      || (state.excludedAssets !== undefined && (!Array.isArray(state.excludedAssets)
        || new Set(state.excludedAssets).size !== state.excludedAssets.length
        || state.excludedAssets.some(id => typeof id !== 'string' || !Object.hasOwn(state.assets, id)))))
    throw new Error('Download ledger is invalid; refusing to reset it or replay old downloads.');
  return state;
}

function assetPlatform(name) {
  if (/\.(exe|msi)$/i.test(name) || (/windows/i.test(name) && /\.zip$/i.test(name))) return 'Windows';
  if (/\.(app|dmg|pkg)$/i.test(name) || (/macos/i.test(name) && /\.zip$/i.test(name))) return 'Mac';
  return null;
}

function excludedAssets(state, assets) {
  // Preserve the raw ledger and remember exclusions even after an asset is removed.
  return new Set([...(state.excludedAssets ?? []),
    ...assets.filter(asset => !assetPlatform(asset.name)).map(asset => String(asset.id))]);
}

function appTotal(state, excluded) {
  return state.total - [...excluded].reduce((sum, id) => sum + (state.assets[id] ?? 0), 0);
}

export function notification(repository, state, assets, previous = null) {
  const format = count => count.toLocaleString('en-US');
  const excluded = excludedAssets(state, assets);
  const releases = new Map();
  for (const asset of assets) {
    const platform = assetPlatform(asset.name);
    if (!platform || excluded.has(String(asset.id))) continue;
    const count = state.assets[asset.id] ?? 0;
    const added = previous ? Math.max(0, count - (previous.assets[asset.id] ?? 0)) : 0;
    const release = releases.get(asset.tag) ?? { total: 0, added: 0, platforms: new Map() };
    release.total += count;
    release.added += added;
    if (added) release.platforms.set(platform, (release.platforms.get(platform) ?? 0) + added);
    releases.set(asset.tag, release);
  }
  const total = appTotal(state, excluded);
  const added = previous ? total - appTotal(previous, excluded) : 0;
  const updates = [...releases].reverse().filter(([, release]) => release.added > 0).map(([tag, release]) =>
    `**${tag.slice(0, 100)} — +${format(release.added)}** (${format(release.total)} total)\n`
      + ['Windows', 'Mac'].filter(platform => release.platforms.has(platform))
        .map(platform => `${platform} +${format(release.platforms.get(platform))}`).join(' / '));
  const lines = [];
  for (const line of updates) {
    const remaining = updates.length - lines.length - 1;
    const suffix = remaining ? `\n\n… ${format(remaining)} more updated versions` : '';
    if ([...lines, line].join('\n\n').length + suffix.length > 4096) break;
    lines.push(line);
  }
  if (lines.length < updates.length) lines.push(`… ${format(updates.length - lines.length)} more updated versions`);
  return {
    username: 'Aurex Downloads', avatar_url: avatar, allowed_mentions: { parse: [] },
    embeds: [{
      title: previous ? `+${format(added)} new download${added === 1 ? '' : 's'}` : 'Download counter started',
      url: `https://github.com/${repository}/releases`,
      description: previous
        ? lines.join('\n\n')
        : 'Counter started from the current app downloads. Future increases will be grouped into timestamped updates.',
      color: 0xE8B947,
      footer: { text: `${format(total)} total app downloads · All versions` },
      timestamp: new Date().toISOString(),
    }],
  };
}

async function resendLatest({ repository, github, discord, state, assets, log }) {
  const excluded = excludedAssets(state, assets);
  const snapshot = async ref => readState((await github(`contents/${stateFile}?ref=${ref}`)).content);
  for (let page = 1; ; ++page) {
    const commits = await github(`commits?sha=${stateBranch}&path=${stateFile}&per_page=100&page=${page}`);
    if (!commits.length) throw new Error('No previous download update was found; nothing was resent.');
    for (const commit of commits) {
      const after = await snapshot(commit.sha);
      const before = commit.parents.length ? await snapshot(commit.parents[0].sha) : null;
      const total = appTotal(after, excluded);
      // Exclusion-only migrations and silent checksum updates are not messages.
      if (before && total <= appTotal(before, excluded)) continue;
      const body = notification(repository, { ...after, excludedAssets: [...excluded] }, assets, before);
      if (!body.embeds[0].description)
        throw new Error('The last update references removed release assets; nothing was resent.');
      body.embeds[0].timestamp = commit.commit.committer.date;
      const message = await discord(body);
      log(`Resent download update ${commit.sha}. Message: ${message}. Counter unchanged.`);
      return { total, sent: 1 };
    }
  }
}

export async function runCounter({ repository, github, discord, initialize = false, resend = false, log = console.log }) {
  if (initialize && resend) throw new Error('Choose initialization or resend, not both.');
  const stored = await github(`contents/${stateFile}?ref=${stateBranch}`, { allowMissing: true });
  if (!stored && !initialize)
    throw new Error('Download ledger is missing. Run this workflow once with Initialize enabled.');
  const assets = await releaseAssets(github);
  if (resend) return resendLatest({ repository, github, discord, state: readState(stored.content), assets, log });
  if (!stored) {
    const state = { version: 1, total: 0, assets: {} };
    for (const asset of assets) {
      state.assets[asset.id] = asset.download_count;
      state.total += asset.download_count;
    }
    const excluded = excludedAssets(state, assets);
    if (excluded.size) state.excludedAssets = [...excluded];
    const tree = await github('git/trees', { method: 'POST', body: {
      tree: [{ path: stateFile, mode: '100644', type: 'blob', content: JSON.stringify(state, null, 2) + '\n' }],
    } });
    const commit = await github('git/commits', { method: 'POST', body: {
      message: 'Initialize download counter', tree: tree.sha, parents: [],
    } });
    await github('git/refs', { method: 'POST', body: { ref: `refs/heads/${stateBranch}`, sha: commit.sha } });
    await discord(notification(repository, state, assets));
    const total = appTotal(state, excluded);
    log(`Initialized at ${total} app downloads. Past downloads will not be replayed.`);
    return { total, sent: 0 };
  }

  const state = readState(stored.content);
  const next = { ...state, assets: { ...state.assets } };
  const excluded = excludedAssets(state, assets);
  if (excluded.size) next.excludedAssets = [...excluded];
  for (const asset of assets) {
    // Retain removed assets and ignore temporary downward API corrections.
    // New asset IDs start at zero, so new releases are included automatically.
    const count = Math.max(state.assets[asset.id] ?? 0, asset.download_count);
    next.total += count - (state.assets[asset.id] ?? 0);
    next.assets[asset.id] = count;
  }
  const total = appTotal(next, excluded);
  // Apply newly learned exclusions to both sides, so migration cannot replay
  // old downloads or count checksum growth as app downloads.
  const added = total - appTotal(state, excluded);
  const sent = added > 0 ? 1 : 0;
  if (added) await discord(notification(repository, next, assets, state));
  if (JSON.stringify(next) !== JSON.stringify(state)) {
    // App increases are saved only after delivery. Exclusions and checksum-only
    // changes are saved silently without creating another history entry.
    await github(`contents/${stateFile}`, { method: 'PUT', body: {
      branch: stateBranch, sha: stored.sha, message: `App download total: ${total}`,
      content: Buffer.from(JSON.stringify(next, null, 2) + '\n').toString('base64'),
    } });
  }
  log(`Total: ${total}. New downloads: ${added}. Messages: ${sent}.`);
  return { total, sent };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    await runCounter({
      repository: process.env.GITHUB_REPOSITORY,
      github: githubClient(process.env.GITHUB_REPOSITORY, process.env.GITHUB_TOKEN),
      discord: discordClient(process.env.DOWNLOAD_WEBHOOK_URL),
      initialize: process.env.INITIALIZE === 'true',
      resend: process.env.RESEND_LATEST === 'true',
    });
  } catch (error) {
    // Never print request URLs, response bodies or stacks containing the webhook token.
    console.error(`Download counter stopped: ${error instanceof SyntaxError ? 'Invalid JSON response or ledger.' : error.message}`);
    process.exitCode = 1;
  }
}
