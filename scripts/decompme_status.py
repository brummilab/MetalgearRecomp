#!/usr/bin/env python3
"""
Build docs/decompme-status.md from decomp.me scratch listings of the
"Metal Gear Solid (overlays)" preset (https://decomp.me/preset/19).

decomp.me sits behind a Cloudflare check, so the pages are fetched with a
browser-based scraper and saved to files first; this script only parses them.
Accepted inputs: plain API JSON, the API's HTML page (Django REST framework),
or a scraper result (JSON with a "rawHtml" field).

Usage: decompme_status.py <out.md> <page> [<page> ...]
  page = https://decomp.me/api/scratch?preset=19&page_size=100[&cursor=...]
"""
import collections
import datetime
import html
import json
import re
import sys

PRESET_URL = 'https://decomp.me/preset/19'


def load_page(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    try:
        data = json.loads(text)
        if isinstance(data, dict) and 'rawHtml' in data:
            text = data['rawHtml']
        elif isinstance(data, dict) and 'results' in data:
            return data
    except ValueError:
        pass
    pres = re.findall(r'<pre class="prettyprint">(.*?)</pre>', text, re.S)
    if not pres:
        raise SystemExit('%s: no scratch list found' % path)
    body = html.unescape(re.sub(r'<[^>]+>', '', pres[-1]))
    return json.loads(body[body.index('{'):])


def owner(r):
    o = r.get('owner')
    if isinstance(o, dict):
        return o.get('username') or 'anonym'
    return o or 'anonym'


def overlay_of(name):
    """Stage overlays prefix their function names (s11g_..., brf_...)."""
    m = re.match(r'^([a-z]+\d*[a-z]*r?)_', name or '')
    return m.group(1) if m else '(Hauptprogramm/sonstige)'


def matched(r):
    return r.get('score') == 0 or bool(r.get('match_override'))


def main():
    out, pages = sys.argv[1], sys.argv[2:]
    scratches, seen = [], set()
    for p in pages:
        for r in load_page(p)['results']:
            if r['slug'] not in seen:
                seen.add(r['slug'])
                scratches.append(r)
    scratches.sort(key=lambda r: r['last_updated'], reverse=True)
    if not scratches:
        raise SystemExit('no scratches')

    newest = datetime.datetime.fromisoformat(scratches[0]['last_updated'])
    week = newest - datetime.timedelta(days=7)
    recent = [r for r in scratches if datetime.datetime.fromisoformat(r['last_updated']) >= week]

    # best result per function name
    best = {}
    for r in scratches:
        n = r['name']
        if n not in best or (r['score'] or 0) < (best[n]['score'] or 0) or matched(r):
            if n not in best or not matched(best[n]):
                best[n] = r

    per_overlay = collections.defaultdict(lambda: [0, 0])
    for n, r in best.items():
        per_overlay[overlay_of(n)][0] += 1
        per_overlay[overlay_of(n)][1] += matched(r)

    people = collections.Counter(owner(r) for r in recent)

    lines = [
        '# decomp.me: Metal Gear Solid (overlays)',
        '',
        'Übersicht über [%s](%s), das decomp.me-Preset des Upstream-Projekts.' % (PRESET_URL, PRESET_URL),
        'Wird täglich automatisch aktualisiert (`scripts/decompme_status.py`).',
        '',
        '- Stand: %s (letzte Änderung auf decomp.me)' % newest.strftime('%d.%m.%Y %H:%M'),
        '- Ausgewertet: %d Scratches, %d verschiedene Funktionen, davon %d gematcht' % (
            len(scratches), len(best), sum(matched(r) for r in best.values())),
        '- Letzte 7 Tage: %d Scratches von %d Mitwirkenden' % (len(recent), len(people)),
        '',
        '> Score 0 = Funktion ist exakt gematcht. Gematchte Overlay-Funktionen landen',
        '> per Pull Request im Upstream-Repo; sobald ein Overlay vollständig ist, baut',
        '> der PC-Port es nach einem Submodul-Update automatisch ein.',
        '',
        '## Fortschritt pro Overlay',
        '',
        '| Overlay | Funktionen | gematcht |',
        '| --- | ---: | ---: |',
    ]
    for ov, (total, done) in sorted(per_overlay.items(), key=lambda kv: (-kv[1][1], kv[0])):
        lines.append('| %s | %d | %d |' % (ov, total, done))

    lines += ['', '## Aktive Mitwirkende (7 Tage)', '']
    for who, n in people.most_common(10):
        lines.append('- %s: %d Scratches' % (who, n))

    lines += ['', '## Neueste Scratches', '', '| Funktion | Score | Stand | von | Link |', '| --- | ---: | --- | --- | --- |']
    for r in scratches[:40]:
        when = datetime.datetime.fromisoformat(r['last_updated']).strftime('%d.%m.%Y')
        score = '✔️ 0' if matched(r) else '%d / %d' % (r['score'], r['max_score'])
        lines.append('| %s | %s | %s | %s | [%s](https://decomp.me/scratch/%s) |' % (
            r['name'], score, when, owner(r), r['slug'], r['slug']))
    lines.append('')

    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print('decompme_status: %d scratches -> %s' % (len(scratches), out))


if __name__ == '__main__':
    main()
