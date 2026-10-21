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
      || Object.values(state.assets).reduce((sum, count) => sum + count, 0) !== state.total)
    throw new Error('Download ledger is invalid; refusing to reset it or replay old downloads.');
  return state;
}

export function notification(repository, state, assets, previous = null) {
  const format = count => count.toLocaleString('en-US');
  const releases = new Map();
  for (const asset of assets) {
    const release = releases.get(asset.tag) ?? { total: 0, added: 0, platforms: new Map() };
    const count = state.assets[asset.id] ?? 0;
    const platform = /windows|\.exe$|\.msi$/i.test(asset.name) ? 'Windows'
      : /macos|\.app$|\.dmg$/i.test(asset.name) ? 'Mac' : 'Other files';
    release.total += count;
    release.added += previous ? Math.max(0, count - (previous.assets[asset.id] ?? 0)) : 0;
    release.platforms.set(platform, (release.platforms.get(platform) ?? 0) + count);
    releases.set(asset.tag, release);
  }
  const added = previous ? state.total - previous.total : 0;
  return {
    username: 'Aurex Downloads', avatar_url: avatar, allowed_mentions: { parse: [] },
    embeds: [{
      title: `${format(state.total)} total downloads`,
      url: `https://github.com/${repository}/releases`,
      description: previous
        ? `**${format(added)} new download${added === 1 ? '' : 's'}** since the last update.`
        : 'Counter started from the current total. New downloads will be grouped into one update.',
      fields: [...releases].slice(-25).reverse().map(([tag, release]) => ({
        name: tag.slice(0, 100),
        value: `**${format(release.total)}** downloads${release.added ? ` · **+${format(release.added)}** new` : ''}\n`
          + [...release.platforms].map(([platform, count]) => `${platform} ${format(count)}`).join(' · '),
        inline: true,
      })),
      color: 0xE8B947,
      footer: { text: releases.size > 25 ? 'Aurex · Latest 25 versions shown · Total includes all versions' : 'Aurex · Total across all versions' },
    }],
  };
}

export async function runCounter({ repository, github, discord, initialize = false, log = console.log }) {
  const stored = await github(`contents/${stateFile}?ref=${stateBranch}`, { allowMissing: true });
  if (!stored && !initialize)
    throw new Error('Download ledger is missing. Run this workflow once with Initialize enabled.');
  const assets = await releaseAssets(github);
  if (!stored) {
    const state = { version: 1, total: 0, assets: {} };
    for (const asset of assets) {
      state.assets[asset.id] = asset.download_count;
      state.total += asset.download_count;
    }
    const tree = await github('git/trees', { method: 'POST', body: {
      tree: [{ path: stateFile, mode: '100644', type: 'blob', content: JSON.stringify(state, null, 2) + '\n' }],
    } });
    const commit = await github('git/commits', { method: 'POST', body: {
      message: 'Initialize download counter', tree: tree.sha, parents: [],
    } });
    await github('git/refs', { method: 'POST', body: { ref: `refs/heads/${stateBranch}`, sha: commit.sha } });
    await discord(notification(repository, state, assets));
    log(`Initialized at ${state.total} downloads. Past downloads will not be replayed.`);
    return { total: state.total, sent: 0 };
  }

  const state = readState(stored.content);
  const next = { ...state, assets: { ...state.assets } };
  for (const asset of assets) {
    // Retain removed assets and ignore temporary downward API corrections.
    // New asset IDs start at zero, so new releases are included automatically.
    const count = Math.max(state.assets[asset.id] ?? 0, asset.download_count);
    next.total += count - (state.assets[asset.id] ?? 0);
    next.assets[asset.id] = count;
  }
  const added = next.total - state.total;
  const sent = added > 0 ? 1 : 0;
  if (added) {
    await discord(notification(repository, next, assets, state));
    // Save the whole batch only after Discord confirms the combined message.
    await github(`contents/${stateFile}`, { method: 'PUT', body: {
      branch: stateBranch, sha: stored.sha, message: `Download total: ${next.total}`,
      content: Buffer.from(JSON.stringify(next, null, 2) + '\n').toString('base64'),
    } });
  }
  log(`Total: ${next.total}. New downloads: ${added}. Messages: ${sent}.`);
  return { total: next.total, sent };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    await runCounter({
      repository: process.env.GITHUB_REPOSITORY,
      github: githubClient(process.env.GITHUB_REPOSITORY, process.env.GITHUB_TOKEN),
      discord: discordClient(process.env.DOWNLOAD_WEBHOOK_URL),
      initialize: process.env.INITIALIZE === 'true',
    });
  } catch (error) {
    // Never print request URLs, response bodies or stacks containing the webhook token.
    console.error(`Download counter stopped: ${error instanceof SyntaxError ? 'Invalid JSON response or ledger.' : error.message}`);
    process.exitCode = 1;
  }
}
