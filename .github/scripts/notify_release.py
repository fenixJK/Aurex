import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def payload(release):
    url = release["html_url"]
    # Only stable major/minor releases ping; patches and previews stay quiet.
    ping_everyone = (
        not release.get("prerelease")
        and not release.get("draft")
        and re.fullmatch(r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.0", release.get("tag_name", "")) is not None
    )
    notes = (release.get("body") or "Download this version from the release page.").strip()
    notes = re.sub(r"^#{1,6}\s+(.+)$", r"**\1**", notes, flags=re.MULTILINE)
    if len(notes) > 4096:
        ending = f"\n\n[Read the full release notes]({url})"
        notes = notes[:4096 - len(ending) - 1].rstrip() + "…" + ending
    embed = {
        "title": (release.get("name") or release["tag_name"])[:256],
        "url": url,
        "description": notes,
        "color": 0xE8B947,
        "footer": {"text": "Aurex · Preview release" if release.get("prerelease") else "Aurex · Release"},
    }
    if release.get("published_at"):
        embed["timestamp"] = release["published_at"]
    return {
        "username": "Aurex",
        "avatar_url": "https://raw.githubusercontent.com/fenixJK/Aurex/main/assets/aurex.png",
        "content": "||@everyone||" if ping_everyone else "",
        "allowed_mentions": {"parse": ["everyone"] if ping_everyone else []},
        "embeds": [embed],
    }


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def send(url, body):
    target = urllib.parse.urlsplit(url.strip())
    if (target.scheme != "https" or target.netloc not in ("discord.com", "discordapp.com")
            or not re.fullmatch(r"/api(?:/v[0-9]+)?/webhooks/[0-9]+/[A-Za-z0-9_-]+", target.path)):
        raise ValueError("WEBHOOK_URL must be a Discord webhook URL.")
    query = urllib.parse.parse_qs(target.query)
    query["wait"] = ["true"]
    target = target._replace(query=urllib.parse.urlencode(query, doseq=True), fragment="")
    request = urllib.request.Request(urllib.parse.urlunsplit(target), json.dumps(body).encode(),
                                    {"Content-Type": "application/json", "User-Agent": "Aurex-Releases/1.0"})
    client = urllib.request.build_opener(NoRedirect())
    for attempt in range(3):
        try:
            with client.open(request, timeout=20) as response:
                if response.status not in (200, 204):
                    raise RuntimeError("Discord did not accept the release announcement.")
            return
        except urllib.error.HTTPError as error:
            if error.code != 429 or attempt == 2:
                raise RuntimeError(f"Discord rejected the announcement (HTTP {error.code}).") from None
            delay = float(json.loads(error.read(8192)).get("retry_after", 1))
            if not 0 <= delay <= 45:
                raise RuntimeError("Discord requested a longer retry; rerun this job later.") from None
            time.sleep(delay)
        except (urllib.error.URLError, TimeoutError):
            raise RuntimeError("Discord delivery was interrupted. Check the channel before retrying.") from None


def main():
    with open(os.environ["GITHUB_EVENT_PATH"], encoding="utf-8") as source:
        event = json.load(source)
    if event.get("action") != "published" or event["release"].get("draft"):
        return
    send(os.environ.get("WEBHOOK_URL", ""), payload(event["release"]))
    print("Release announcement delivered.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, RuntimeError):
        print("Release announcement failed. Check WEBHOOK_URL and the release event; no credentials were logged.", file=sys.stderr)
        sys.exit(1)
