#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Media contract gate. Requires the existing WebDriver and ffmpeg for test films."""
import argparse, base64, functools, gzip, http.server, io, json, pathlib, struct
import shutil, subprocess, threading, urllib.request, zipfile, zlib, hashlib


def prepare(root):
    def png(width, height, color):
        def chunk(kind, data):
            return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
        rows = b''.join(b'\0' + bytes(color) * width for _ in range(height))
        return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    # Intentionally simple synthetic subjects; no stock/project claims.
    for home in root.glob('*/index.html'):
        assets = home.parent / 'assets'
        assets.mkdir(exist_ok=True)
        for name, dimensions, color in [('room', (1600, 1000), (47, 112, 129)), ('detail', (1000, 1200), (189, 117, 50)), ('proof', (1600, 1000), (52, 109, 67))]:
            (assets / f'{name}.jpg').write_bytes(png(*dimensions, color))
        for name in ['mark', 'bench']:
            source = pathlib.Path('tests/fixtures/composed') / f'{name}.svg'
            (assets / source.name).write_bytes(source.read_bytes())
    first = next(root.glob('*/assets'))
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=320x240:rate=12', '-t', '2', '-c:v', 'libvpx-vp9', '-pix_fmt', 'yuv420p', str(first / 'film.webm')], check=True)
    film = (first / 'film.webm').read_bytes()
    for assets in root.glob('*/assets'):
        (assets / 'film.webm').write_bytes(film)
        (assets / 'phone.webm').write_bytes(film)
    low = root / 'lowdata'
    shutil.copytree(first.parent, low, dirs_exist_ok=True)
    home = low / 'index.html'
    home.write_text(home.read_text().replace('<head>', '<head><script>Object.defineProperty(navigator,"connection",{value:Object.assign(new EventTarget(),{saveData:true})});</script>'))


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


def check(driver, root):
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'
    results = []
    def call(method, path, data=None):
        request = urllib.request.Request(driver.rstrip('/') + path, method=method, data=json.dumps(data).encode() if data is not None else None, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=60) as response:
            value = json.load(response)['value']
        if isinstance(value, dict) and value.get('error'):
            raise RuntimeError(value)
        return value
    try:
        for reduced in [False, True]:
            profile = io.BytesIO()
            with zipfile.ZipFile(profile, 'w') as archive:
                archive.writestr('user.js', 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' + f'user_pref("ui.prefersReducedMotion", {int(reduced)});')
                archive.writestr('chrome/userChrome.css', '#main-window, #navigator-toolbox { min-width: 0 !important; }')
            sid = call('POST', '/session', {'capabilities': {'alwaysMatch': {'browserName': 'firefox', 'moz:firefoxOptions': {'args': ['-headless'], 'profile': base64.b64encode(profile.getvalue()).decode()}}}})['sessionId']
            def execute(script, *args):
                return call('POST', f'/session/{sid}/execute/async', {'script': script, 'args': list(args)})
            try:
                for width in [390, 768, 1440]:
                    call('POST', f'/session/{sid}/window/rect', {'width': width, 'height': 900})
                    for home in sorted(root.glob('*/index.html')):
                        call('POST', f'/session/{sid}/url', {'url': f'{base}/{home.parent.name}/index.html'})
                        report = execute('''const done=arguments[arguments.length-1];
(async()=>{await document.fonts.ready;await new Promise(r=>setTimeout(r,200));
 const v=document.querySelector('video'), p=document.querySelector('.scene picture img'), h=document.querySelector('h1'), b=document.querySelector('.scene__play');
 const rect=h.getBoundingClientRect(), style=getComputedStyle(h), lines=rect.height/parseFloat(style.lineHeight);
 const credit=document.querySelector('.scene figcaption'), copy=document.querySelector('.scene__copy');
 if(credit)credit.textContent='Illustrative photo attribution with licensing details. '.repeat(5);
 const errors=[];
 if(credit&&copy.getBoundingClientRect().bottom>credit.getBoundingClientRect().top+1)errors.push('credit overlaps opening copy');
 if(innerWidth!==arguments[0])errors.push('viewport');
 const brand=document.querySelector('.brand'), menu=document.querySelector('.nav-toggle');
 if(innerWidth<768&&menu&&brand&&Math.abs(brand.getBoundingClientRect().top-menu.getBoundingClientRect().top)>24)errors.push('mobile navigation wrapped');
 if(document.documentElement.scrollWidth>innerWidth+1)errors.push('horizontal scroll');
 if(!p.complete||p.naturalWidth===0)errors.push('poster absent');
 if(getComputedStyle(v).display!=='none'&&Math.abs(v.getBoundingClientRect().height-p.getBoundingClientRect().height)>1)errors.push('film does not fit poster');
 if(v.currentSrc||v.getAttribute('src')||performance.getEntriesByType('resource').some(r=>/\\.(mp4|webm)/.test(r.name)))errors.push('film loaded before intent');
 if(innerWidth>=768&&p.getBoundingClientRect().height<innerHeight*.75)errors.push('opening photo capped by legacy style');
 if(innerWidth<768&&document.querySelector('.scene--full')&&p.getBoundingClientRect().height<innerHeight*.6)errors.push('full opening reduced to a thumbnail');
 if(lines>4.1||parseFloat(style.fontSize)<32)errors.push('display line budget');
 if((arguments[1]||location.pathname.includes('/lowdata/'))&&!b.hidden)errors.push('reduced motion control');
 if(innerWidth<768&&!p.currentSrc.includes('detail.jpg'))errors.push('phone crop');
 done({width:innerWidth,reduced:arguments[1],lines,errors});})();''', width, reduced)
                        report['site'] = home.parent.name
                        results.append(report)
                        # Each layout at every width exercises playback, pause,
                        # offscreen pause and a failed source without layout shifts.
                        if home.parent.name.startswith('chalk-') and not reduced:
                            report = execute('''const done=arguments[arguments.length-1];
(async()=>{const v=document.querySelector('video'),b=document.querySelector('.scene__play'),p=document.querySelector('.scene picture img');
 const before=p.getBoundingClientRect().toJSON(),errors=[];b.click();
 const waitFor=async test=>{const end=performance.now()+5000;while(!test()&&performance.now()<end)await new Promise(r=>setTimeout(r,50));};
 await waitFor(()=>!v.paused&&v.classList.contains('is-playing'));
 if(v.paused||!v.classList.contains('is-playing'))errors.push('play failed');
 if(innerWidth<768&&!v.currentSrc.includes('phone.webm'))errors.push('wrong film crop');
 b.click();if(!v.paused)errors.push('pause failed');
 b.click();await waitFor(()=>!v.paused);scrollTo(0,document.body.scrollHeight);await waitFor(()=>v.paused);
 if(!v.paused)errors.push('offscreen playback');scrollTo(0,0);
 const after=p.getBoundingClientRect().toJSON();if(Math.abs(before.height-after.height)>1||Math.abs(before.y-after.y)>1)errors.push('film layout shift');
 v.src='missing-film.webm';b.click();await waitFor(()=>v.error!==null);
 if(v.classList.contains('is-playing')||!p.complete||p.naturalWidth===0)errors.push('failure fallback');
 done({errors,playback:true,width:innerWidth});})();''')
                            report['site'] = home.parent.name
                            results.append(report)
            finally:
                call('DELETE', f'/session/{sid}')
        for home in root.glob('*/index.html'):
            code = b''.join((home.parent / name).read_bytes() for name in ['index.html', 'styles.css', 'site.js'])
            posters = list((home.parent / 'assets').glob('*.jpg'))
            errors = []
            if len(code) > 150 * 1024 or len(gzip.compress(code)) > 40 * 1024:
                errors.append('code weight')
            if any(p.stat().st_size > 300 * 1024 for p in posters):
                errors.append('poster weight')
            results.append({'site': home.parent.name, 'code_bytes': len(code), 'gzip_bytes': len(gzip.compress(code)), 'errors': errors})
    finally:
        server.shutdown()
        server.server_close()
    (root / 'media-gate.json').write_text(json.dumps(results, indent=2))
    failures = [r for r in results if r['errors']]
    print(json.dumps({'checks': len(results), 'failures': failures}, indent=2))
    return bool(failures)


def review(root):
    """Fail closed on missing live output or an unreviewed visual sequence.

    This complements the synthetic browser checks; they cannot judge taste.
    Approval binds the review to the actual capture bytes, including motion.
    """
    failures = []
    records = json.loads((root / 'provenance.json').read_text())
    verdict_path = root / 'review.json'
    verdict = json.loads(verdict_path.read_text()) if verdict_path.exists() else {}
    slugs = [r.get('slug', '') for r in records]
    if len(records) != 6 or len(set(slugs)) != 6:
        failures.append('Expected six distinct live-content sites')
    for record in records:
        slug = record.get('slug', '')
        if not slug or pathlib.Path(slug).name != slug or slug in ('.', '..'):
            failures.append('Invalid site name')
            continue
        if record.get('fresh_run') is not True:
            failures.append(f'{slug}: generation failed; recorded replacement is diagnostic only')
        for width in (390, 1440):
            directory = root / 'captures' / slug
            names = [f'{width}-first.jpg'] + [f'{width}-scroll-{n}.jpg' for n in (1, 2, 3)] + [f'{width}-scroll.webm']
            digest = hashlib.sha256()
            for name in names:
                path = directory / name
                if not path.is_file() or path.stat().st_size == 0:
                    failures.append(f'{slug}/{name}: missing capture')
                    continue
                digest.update(name.encode())
                digest.update(path.read_bytes())
                probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                    '-show_entries', 'stream=width,height', '-of', 'json', str(path)], capture_output=True, text=True)
                try:
                    stream = json.loads(probe.stdout)['streams'][0]
                    if stream['width'] != width or stream['height'] < 600:
                        failures.append(f'{slug}/{name}: wrong capture dimensions')
                except (ValueError, KeyError, IndexError):
                    failures.append(f'{slug}/{name}: unreadable capture')
            key = f'{slug}/{width}'
            entry = verdict.get(key, {})
            if entry.get('sha256') != digest.hexdigest():
                failures.append(f'{key}: review missing or does not match capture bytes')
            if entry.get('approved') is not True or not entry.get('reference') or not entry.get('judgment'):
                failures.append(f'{key}: comparative visual review not approved')
    report = {'sites': len(records), 'failures': failures, 'passed': not failures}
    (root / 'visual-review-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return bool(failures)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=pathlib.Path)
    parser.add_argument('--webdriver')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--review', action='store_true', help='Check six real runs and their comparative visual review')
    args = parser.parse_args()
    root = args.directory.resolve()
    if args.review:
        raise SystemExit(review(root))
    if args.prepare:
        prepare(root)
    if args.webdriver:
        raise SystemExit(check(args.webdriver, root))
