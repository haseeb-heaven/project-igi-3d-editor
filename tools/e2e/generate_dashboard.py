#!/usr/bin/env python3
"""
Generate self-contained HTML5 verification dashboard from e2e batch.json artifacts.
Two evidence layers: source manifest (authored data) + render evidence (JSONL from C++).
Uses Pillow for contact sheets and GIF previews when available.
"""
import sys, os, json, argparse, math, base64
from pathlib import Path

try:
    from PIL import Image, ImageDraw
    HAS_PILLOW = True
except ImportError:
    HAS_PILLOW = False

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def fmt_bytes(n):
    if not n or n <= 0: return '0 B'
    for u in ['B','KB','MB','GB']:
        if abs(n) < 1024: return f'{n:.1f} {u}'
        n /= 1024
    return f'{n:.1f} TB'

def embed_image(path):
    """Return base64 data-URI for an image file, or empty string."""
    p = Path(path)
    if not p.exists(): return ''
    ext = p.suffix.lower()
    mime = {'png':'image/png','jpg':'image/jpeg','jpeg':'image/jpeg',
            'gif':'image/gif','bmp':'image/bmp'}.get(ext.lstrip('.'), 'image/png')
    data = base64.b64encode(p.read_bytes()).decode()
    return f'data:{mime};base64,{data}'

def make_contact_sheet(png_paths, out_path, thumb_w=320):
    """Create a contact sheet PNG from a list of image paths."""
    if not HAS_PILLOW: return False
    images = []
    for p in png_paths:
        if Path(p).exists():
            try: images.append(Image.open(p).convert('RGB'))
            except: pass
    if not images: return False
    cols = 3
    rows = math.ceil(len(images) / cols)
    ratio = images[0].height / images[0].width if images[0].width else 1
    th = int(thumb_w * ratio)
    sheet = Image.new('RGB', (cols * thumb_w, rows * th), (9, 13, 22))
    for i, img in enumerate(images):
        r, c = divmod(i, cols)
        sheet.paste(img.resize((thumb_w, th), Image.LANCZOS), (c * thumb_w, r * th))
    sheet.save(out_path, 'PNG')
    return True

def make_gif(png_paths, out_path, duration_ms=500):
    """Create animated GIF from up to 8 exterior PNGs."""
    if not HAS_PILLOW: return False
    frames = []
    for p in png_paths:
        if Path(p).exists():
            try: frames.append(Image.open(p).convert('RGBA').resize((480, 270), Image.LANCZOS))
            except: pass
    if not frames: return False
    frames[0].save(out_path, save_all=True, append_images=frames[1:],
                   loop=0, duration=duration_ms, optimize=False)
    return True

# ---------------------------------------------------------------------------
# HTML generation
# ---------------------------------------------------------------------------
STYLE = '''
:root{--bg:#090d16;--card:#0f1929;--border:#1e3a5f;--pass:#10b981;--fail:#ef4444;
      --cyan:#06b6d4;--amber:#f59e0b;--text:#e2e8f0;--muted:#64748b}
body{margin:0;padding:0;background:var(--bg);color:var(--text);font-family:monospace;font-size:13px}
.header{background:linear-gradient(135deg,#0a1628,#0f2040);border-bottom:1px solid var(--border);
        padding:20px 32px;display:flex;align-items:center;gap:24px}
.header h1{margin:0;font-size:18px;color:var(--cyan);letter-spacing:2px}
.kpi{background:var(--card);border:1px solid var(--border);border-radius:6px;
     padding:10px 20px;text-align:center;min-width:90px}
.kpi .val{font-size:22px;font-weight:700}
.kpi .lbl{font-size:10px;color:var(--muted);letter-spacing:1px}
.pass-val{color:var(--pass)}.fail-val{color:var(--fail)}.cyan-val{color:var(--cyan)}
.toolbar{padding:12px 32px;display:flex;gap:10px;align-items:center;
         background:#0a1220;border-bottom:1px solid var(--border)}
.toolbar input{background:#0f1929;border:1px solid var(--border);color:var(--text);
               border-radius:4px;padding:6px 12px;font-size:12px;font-family:monospace;flex:1}
.toolbar select{background:#0f1929;border:1px solid var(--border);color:var(--text);
                border-radius:4px;padding:6px 10px;font-size:12px;font-family:monospace}
.cards{padding:24px 32px;display:grid;grid-template-columns:repeat(auto-fill,minmax(760px,1fr));gap:24px}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;overflow:hidden}
.card-header{padding:14px 18px;display:flex;align-items:center;gap:12px;
             background:linear-gradient(90deg,#0a1628,#0f2040);border-bottom:1px solid var(--border)}
.badge{padding:4px 12px;border-radius:4px;font-size:11px;font-weight:700;letter-spacing:1px}
.badge-pass{background:#052e16;color:var(--pass);border:1px solid var(--pass)}
.badge-fail{background:#2d0a0a;color:var(--fail);border:1px solid var(--fail)}
.card-body{display:grid;grid-template-columns:1fr 1fr;gap:0}
.panel{padding:14px 18px;border-right:1px solid var(--border)}
.panel:last-child{border-right:none}
.panel-title{font-size:10px;color:var(--cyan);letter-spacing:2px;text-transform:uppercase;margin-bottom:10px;border-bottom:1px solid var(--border);padding-bottom:6px}
.kv{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #1a2840}
.kv .k{color:var(--muted);font-size:11px}.kv .v{font-size:11px;text-align:right;max-width:60%;word-break:break-all}
.gallery{display:flex;flex-wrap:wrap;gap:4px;padding:14px 18px}
.gallery img{height:80px;border-radius:3px;border:1px solid var(--border);cursor:pointer;
             transition:border-color .15s}
.gallery img:hover{border-color:var(--cyan)}
video{width:100%;border-radius:4px;margin:8px 0}
.contact-sheet{width:100%;border-radius:4px;margin:8px 0}
.gif-preview{max-width:100%;border-radius:4px;margin:4px 0}
table{width:100%;border-collapse:collapse;font-size:11px}
th,td{padding:5px 8px;text-align:left;border-bottom:1px solid var(--border)}
th{color:var(--cyan);font-size:10px;letter-spacing:1px}
.tick{color:var(--pass);font-weight:700}.cross{color:var(--fail);font-weight:700}
.modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,.85);z-index:999;align-items:center;justify-content:center}
.modal-bg.open{display:flex}
.modal-img{max-width:92vw;max-height:92vh;border-radius:6px;border:1px solid var(--cyan)}
.ev-view{font-size:10px;color:var(--muted)}
'''

JS = '''
function filterCards(){
  const q=(document.getElementById('search').value||'').toLowerCase();
  const cat=document.getElementById('cat-filter').value;
  const st=document.getElementById('status-filter').value;
  document.querySelectorAll('.card').forEach(c=>{
    const text=c.dataset.search||'';
    const catMatch=!cat||c.dataset.cat===cat;
    const stMatch=!st||c.dataset.status===st;
    const qMatch=!q||text.includes(q);
    c.style.display=(catMatch&&stMatch&&qMatch)?'':'none';
  });
}
function openModal(src){document.getElementById('modal').classList.add('open');document.getElementById('modal-img').src=src;}
function closeModal(){document.getElementById('modal').classList.remove('open');}
document.addEventListener('keydown',e=>{if(e.key==='Escape')closeModal();});
'''

def bool_badge(ok, label=None):
    cls = 'tick' if ok else 'cross'
    icon = '&#10003;' if ok else '&#10007;'
    txt = (label + ' ') if label else ''
    return f'<span class="{cls}">{icon} {txt}</span>'

def kv(k, v):
    return f'<div class="kv"><span class="k">{k}</span><span class="v">{v}</span></div>'

def asset_row(label, info):
    passed = info.get('passed', False)
    size = fmt_bytes(info.get('sizeBytes', 0))
    fname = info.get('file', label)
    badge = bool_badge(passed)
    return f'<tr><td>{label}</td><td><code>{fname}</code></td><td>{size}</td><td>{badge}</td></tr>'

def build_report(data_sources, output_path, artifact_root):
    all_objs = []
    total_pass = total_fail = 0
    cats = set()

    for lvl, batch, root_dir in data_sources:
        for obj in batch.get('objects', []):
            obj['_level'] = lvl
            obj['_root'] = str(root_dir)
            if obj.get('passed'): total_pass += 1
            else: total_fail += 1
            cats.add(obj.get('category', 'Unknown'))
            all_objs.append(obj)

    overall = 'PASS' if total_fail == 0 and all_objs else 'FAIL'
    ov_cls = 'pass-val' if overall == 'PASS' else 'fail-val'

    # Category options
    cat_opts = ''.join(f'<option value="{c}">{c}</option>' for c in sorted(cats))

    # Build cards
    cards = []
    for idx, obj in enumerate(all_objs):
        is_pass = obj.get('passed', False)
        status_cls = 'badge-pass' if is_pass else 'badge-fail'
        status_icon = '&#10003;' if is_pass else '&#10007;'
        status_txt = 'PASS' if is_pass else 'FAIL'

        model_id = obj.get('modelId', 'N/A')
        task_id  = obj.get('taskId', 'N/A')
        category = obj.get('category', 'Unknown')
        obj_type = obj.get('type', 'Unknown')
        prefix   = obj.get('prefix', f'obj-{idx:04d}')
        lvl      = obj.get('_level', '?')
        root_dir = Path(obj.get('_root', str(artifact_root)))

        # Screenshot dir for this object
        shot_dir = root_dir / 'screenshots' / prefix

        # Position / rotation
        pos = obj.get('authoredPosition', [0, 0, 0]) or [0, 0, 0]
        rot = obj.get('authoredRotation',  [0, 0, 0]) or [0, 0, 0]
        px, py, pz = (pos + [0,0,0])[:3]
        rx, ry, rz = (rot + [0,0,0])[:3]
        mx, my, mz = px/256, py/256, pz/256
        rdx = math.degrees(rx); rdy = math.degrees(ry); rdz = math.degrees(rz)

        # Find PNGs in shot dir
        view_order = ['Ext_000','Ext_060','Ext_120','Ext_180','Ext_240','Ext_300',
                      'Int_000','Int_090','Int_180','Int_270']
        png_files = []
        for v in view_order:
            p = shot_dir / f'Level{lvl:02d}_Model{model_id}_{v}.png'
            if p.exists(): png_files.append(str(p))

        # Contact sheet
        contact_uri = ''
        if png_files:
            cs_path = shot_dir / 'contact_sheet.png'
            if make_contact_sheet(png_files, str(cs_path)):
                contact_uri = embed_image(str(cs_path))

        # GIF from exterior views
        gif_uri = ''
        ext_pngs = [str(shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png')
                    for a in range(0, 360, 45)  # 8 views
                    if (shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png').exists()]
        if not ext_pngs:  # fallback to every 60°
            ext_pngs = [str(shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png')
                        for a in range(0, 360, 60)
                        if (shot_dir / f'Level{lvl:02d}_Model{model_id}_Ext_{a:03d}.png').exists()]
        if ext_pngs:
            gif_path = shot_dir / 'preview.gif'
            if make_gif(ext_pngs, str(gif_path)):
                gif_uri = embed_image(str(gif_path))

        # Orbit video
        video_info = obj.get('video') or {}
        vid_path = shot_dir / 'orbit.mp4'
        vid_rel = f'screenshots/{prefix}/orbit.mp4'
        vid_status = video_info.get('status', 'N/A')

        # Capture evidence (from evidence.jsonl)
        evidence_records = obj.get('captureEvidence') or []
        if not evidence_records:
            ev_jsonl = shot_dir / 'evidence.jsonl'
            if ev_jsonl.exists():
                try:
                    evidence_records = [json.loads(l) for l in ev_jsonl.read_text(encoding='utf-8-sig').splitlines() if l.strip()]
                except: pass

        # Asset lineage
        lineage = obj.get('assetLineage') or {}

        # Failures
        failures = obj.get('failures') or []

        # --- Thumbnail gallery HTML ---
        thumbs_html = ''
        for pf in png_files[:10]:
            uri = embed_image(pf)
            fname = Path(pf).name
            thumbs_html += f'<img src="{uri}" alt="{fname}" onclick="openModal(this.src)" />'

        # --- Contact sheet ---
        cs_html = f'<img class="contact-sheet" src="{contact_uri}" alt="contact sheet" />' if contact_uri else ''
        gif_html = f'<img class="gif-preview" src="{gif_uri}" alt="GIF preview" />' if gif_uri else ''

        # --- Video player ---
        if vid_path.exists():
            vid_html = f'''<video controls autoplay muted loop playsinline>
  <source src="{vid_rel}" type="video/mp4">
</video>'''
        else:
            vid_html = f'<div style="color:var(--muted);padding:8px">orbit.mp4 {vid_status}</div>'

        # --- Evidence table (render layer) ---
        ev_rows = ''
        for ev in evidence_records[:10]:
            view_name = ev.get('view', '?')
            cam = ev.get('camera', {})
            rendered = ev.get('rendered', False)
            ev_rows += f'''<tr>
  <td class="ev-view">{view_name}</td>
  <td class="ev-view">{cam.get("x",0):.0f},{cam.get("y",0):.0f},{cam.get("z",0):.0f}</td>
  <td class="ev-view">{cam.get("yaw",0):.1f}°/{cam.get("pitch",0):.1f}°</td>
  <td>{bool_badge(rendered)}</td>
</tr>'''
        ev_table = f'''<table>
  <thead><tr><th>View</th><th>Camera XYZ</th><th>Yaw/Pitch</th><th>Rendered</th></tr></thead>
  <tbody>{ev_rows}</tbody>
</table>''' if ev_rows else '<div style="color:var(--muted)">No render evidence</div>'

        # --- Asset lineage table ---
        al_rows = ''
        for key in ['dat','mtp','mef','qvm','res']:
            if key in lineage:
                al_rows += asset_row(f'.{key.upper()}', lineage[key])
        al_table = f'''<table>
  <thead><tr><th>File</th><th>Name</th><th>Size</th><th>OK</th></tr></thead>
  <tbody>{al_rows}</tbody>
</table>''' if al_rows else '<div style="color:var(--muted)">No lineage data</div>'

        # --- Failures ---
        fail_html = ''
        if failures:
            fail_html = '<div style="color:var(--fail);font-size:11px;padding:8px 18px">'
            for f in failures:
                fail_html += f'<div>&#9888; {f}</div>'
            fail_html += '</div>'

        search_data = f'{model_id} {task_id} {category} {obj_type}'.lower()

        cards.append(f'''
<div class="card" data-search="{search_data}" data-cat="{category}" data-status="{status_txt}">
  <div class="card-header">
    <span class="badge {status_cls}">{status_icon} {status_txt}</span>
    <span style="color:var(--cyan);font-size:13px">L{lvl} · {model_id}</span>
    <span style="color:var(--muted);font-size:11px">{obj_type} · {category}</span>
    <span style="color:var(--muted);font-size:11px;margin-left:auto">task {task_id}</span>
  </div>
  {fail_html}
  <div class="gallery">{thumbs_html}</div>
  {vid_html}
  {cs_html}
  {gif_html}
  <div class="card-body">
    <div class="panel">
      <div class="panel-title">Source Layer — Authored</div>
      {kv('Position (eng)', f'{px:.0f}, {py:.0f}, {pz:.0f}')}
      {kv('Position (m)', f'{mx:.2f}, {my:.2f}, {mz:.2f}')}
      {kv('Rotation (rad)', f'{rx:.4f}, {ry:.4f}, {rz:.4f}')}
      {kv('Rotation (deg)', f'{rdx:.1f}°, {rdy:.1f}°, {rdz:.1f}°')}
      <div class="panel-title" style="margin-top:10px">Asset Lineage</div>
      {al_table}
    </div>
    <div class="panel">
      <div class="panel-title">Render Layer — Live Evidence</div>
      {ev_table}
      <div class="panel-title" style="margin-top:10px">Orbit Video</div>
      {kv('Status', vid_status)}
      {kv('Frames', str(video_info.get("frames", 0)))}
      {kv('FPS', str(video_info.get("fps", 12)))}
      {kv('Source', video_info.get("source", "N/A"))}
    </div>
  </div>
</div>''')

    cards_html = '\n'.join(cards)

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IGI Editor E2E Report</title>
<style>{STYLE}</style>
</head>
<body>
<div class="header">
  <div><h1>&#9670; IGI EDITOR E2E REPORT</h1><div style="color:var(--muted);font-size:11px;margin-top:4px">Source Manifest + Render Evidence</div></div>
  <div class="kpi"><div class="val cyan-val">{len(all_objs)}</div><div class="lbl">OBJECTS</div></div>
  <div class="kpi"><div class="val pass-val">{total_pass}</div><div class="lbl">PASS</div></div>
  <div class="kpi"><div class="val fail-val">{total_fail}</div><div class="lbl">FAIL</div></div>
  <div class="kpi"><div class="val {ov_cls}">{overall}</div><div class="lbl">OVERALL</div></div>
</div>
<div class="toolbar">
  <input id="search" oninput="filterCards()" placeholder="Search model / task / type..." />
  <select id="cat-filter" onchange="filterCards()">
    <option value="">All Categories</option>{cat_opts}
  </select>
  <select id="status-filter" onchange="filterCards()">
    <option value="">All Status</option>
    <option value="PASS">PASS</option>
    <option value="FAIL">FAIL</option>
  </select>
</div>
<div class="cards">{cards_html}</div>
<div class="modal-bg" id="modal" onclick="closeModal()">
  <img class="modal-img" id="modal-img" src="" alt="full size" />
</div>
<script>{JS}</script>
</body>
</html>
'''
    Path(output_path).write_text(html, encoding='utf-8')
    print(f'[Dashboard] Written: {output_path}')

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description='Generate IGI E2E HTML dashboard')
    parser.add_argument('--artifact-dir', required=True, help='Root artifact dir (contains level*/ subdirs or is itself level dir)')
    parser.add_argument('--output', default='report.html')
    args = parser.parse_args()

    artifact_root = Path(args.artifact_dir)
    data_sources = []

    # Look for batch.json files (may be nested in levelN/ subdirs or at root)
    batch_files = list(artifact_root.rglob('batch.json'))
    if not batch_files:
        print(f'[Dashboard] No batch.json found under {artifact_root}', file=sys.stderr)
        sys.exit(1)

    for bf in batch_files:
        try:
            batch = json.loads(bf.read_text(encoding='utf-8-sig'))
            lvl = batch.get('level', 0)
            data_sources.append((lvl, batch, bf.parent))
        except Exception as e:
            print(f'[Dashboard] Skipping {bf}: {e}', file=sys.stderr)

    output_path = artifact_root / args.output
    build_report(data_sources, str(output_path), artifact_root)

if __name__ == '__main__':
    main()
