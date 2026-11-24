"""Builds data/headphones.tsv from AutoEQ results and oratory1990's own presets.

Inputs:
  * a local checkout of AutoEQ (only the EQ text files are needed):
        git clone --depth 1 --filter=blob:none --sparse https://github.com/jaakkopasanen/AutoEq autoeq
        git -C autoeq sparse-checkout set --no-cone '/results/**/*ParametricEQ.txt' \
            '/results/**/*GraphicEQ.txt' '/results/README.md'
  * OPRA's database (https://github.com/opra-project/OPRA, CC BY-SA 4.0), which
    holds oratory1990's own hand-made presets transcribed from his PDFs:
        curl -O https://raw.githubusercontent.com/opra-project/OPRA/main/dist/database_v1.jsonl

Usage:
    python tools/build_library.py autoeq database_v1.jsonl data/headphones.tsv

Output format (UTF-8, tab separated, one profile per line):
    BTEQLIB <version> <autoeq commit> <opra entry count>
    FREQS   <comma separated GraphicEQ frequencies>
    <name> <source> <rig> <form> <recommended 0|1> <origin> <target> <preamp dB> <parametric> <graphic>

origin: A = computed by AutoEQ from the source's measurement,
        O = the source's own preset, used exactly as published (no AutoEQ processing).
parametric: filters joined by ';', each "T,fc,gain,q" with T = P (peaking),
L (low shelf), H (high shelf), W (12 dB/oct low pass). Empty if none.
graphic: gains in dB matching FREQS, joined by ','. Empty if missing.
"""
import json
import os
import subprocess
import sys
import urllib.parse
from pathlib import Path

FORMAT_VERSION = 2
FORMS = ('over-ear', 'in-ear', 'earbud')
TYPE_CODES = {'PK': 'P', 'LSC': 'L', 'HSC': 'H'}
OPRA_TYPES = {'peak_dip': 'P', 'low_shelf': 'L', 'high_shelf': 'H', 'low_pass': 'W'}
OPRA_FORMS = {'over_the_ear': 'over-ear', 'on_ear': 'over-ear', 'in_ear': 'in-ear', 'earbuds': 'earbud'}
AUTOEQ_TARGET = 'AutoEQ default target'


def parse_parametric(path):
    preamp, filters = 0.0, []
    for line in path.read_text(encoding='utf-8').splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == 'Preamp:':
            preamp = float(t[1])
        elif t[0] == 'Filter':
            if t[2] != 'ON':
                continue
            if t[3] not in TYPE_CODES:
                raise ValueError(f'{path}: unsupported filter type {t[3]}')
            # e.g. "Fc 105 Hz Gain 6.4 dB Q 0.70": the value follows each keyword.
            vals = {k: t[i + 1] for i, k in enumerate(t[:-1]) if k in ('Fc', 'Gain', 'Q')}
            filters.append(f"{TYPE_CODES[t[3]]},{float(vals['Fc']):g},{float(vals['Gain']):g},{float(vals['Q']):g}")
    return preamp, ';'.join(filters)


def parse_graphic(path):
    body = path.read_text(encoding='utf-8').strip().split(':', 1)[1]
    pts = [p.split() for p in body.split(';') if p.strip()]
    return [p[0] for p in pts], ','.join(f'{float(p[1]):g}' for p in pts)


def recommended_paths(autoeq):
    """Folders listed in results/README.md (AutoEQ's pick of the best source per headphone)."""
    rec = set()
    readme = autoeq / 'results' / 'README.md'
    if not readme.exists():
        return rec
    for line in readme.read_text(encoding='utf-8').splitlines():
        if line.startswith('- [') and '](./' in line:
            rel = line.split('](./', 1)[1].rsplit(')', 1)[0]
            rec.add(urllib.parse.unquote(rel))
    return rec


def split_rig(folder):
    """'GRAS 43AG-7 over-ear' -> ('GRAS 43AG-7', 'over-ear'); 'in-ear' -> ('', 'in-ear')."""
    for form in FORMS:
        if folder == form:
            return '', form
        if folder.endswith(' ' + form):
            return folder[: -len(form) - 1], form
    return folder, ''


def search_key(name):
    return ''.join(ch for ch in name.casefold() if ch.isalnum())


def opra_oratory_presets(jsonl_path):
    """oratory1990's own presets from OPRA, as (name, form, target, preamp, parametric)."""
    vendors, products, eqs = {}, {}, []
    with open(jsonl_path, encoding='utf-8') as f:
        for line in f:
            x = json.loads(line)
            if x['type'] == 'vendor':
                vendors[x['id']] = x['data']['name']
            elif x['type'] == 'product':
                products[x['id']] = x['data']
            elif x['type'] == 'eq' and x['data'].get('author') == 'oratory1990':
                eqs.append(x)
    out = []
    for x in eqs:
        d = x['data']
        if d.get('type') != 'parametric_eq':
            continue
        product_id = d.get('product_id') or x['id'].split('::')[0]
        prod = products.get(product_id)
        if not prod:
            continue
        name = f"{vendors.get(prod['vendor_id'], '')} {prod['name']}".strip()
        bands = []
        for b in d['parameters']['bands']:
            code = OPRA_TYPES.get(b['type'])
            if code is None:
                raise ValueError(f"{x['id']}: unsupported band type {b['type']}")
            if code == 'W':
                if b.get('slope', 12) != 12:
                    raise ValueError(f"{x['id']}: unsupported low pass slope {b.get('slope')}")
                bands.append(f"W,{float(b['frequency']):g},0,0.707107")
            else:
                bands.append(f"{code},{float(b['frequency']):g},{float(b['gain_db']):g},{float(b['q']):g}")
        target = d.get('details') or 'oratory1990 preset'
        target = target.replace(' \u2022 ', ', ')
        out.append((name, OPRA_FORMS.get(prod.get('subtype'), ''), target,
                    float(d['parameters'].get('gain_db', 0)), ';'.join(bands)))
    return out


def main(autoeq_dir, opra_path, out_path):
    autoeq = Path(autoeq_dir)
    results = autoeq / 'results'
    try:
        commit = subprocess.run(['git', '-C', str(autoeq), 'rev-parse', '--short', 'HEAD'],
                                capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        commit = 'unknown'
    rec = recommended_paths(autoeq)

    rows, freqs = [], None
    for source_dir in sorted(p for p in results.iterdir() if p.is_dir()):
        for rig_dir in sorted(p for p in source_dir.iterdir() if p.is_dir()):
            rig, form = split_rig(rig_dir.name)
            for model_dir in sorted(p for p in rig_dir.iterdir() if p.is_dir()):
                name = model_dir.name
                peq = model_dir / f'{name} ParametricEQ.txt'
                geq = model_dir / f'{name} GraphicEQ.txt'
                if not peq.exists() and not geq.exists():
                    continue
                preamp, parametric = parse_parametric(peq) if peq.exists() else (0.0, '')
                graphic = ''
                if geq.exists():
                    f, graphic = parse_graphic(geq)
                    if freqs is None:
                        freqs = f
                    elif f != freqs:
                        raise ValueError(f'{geq}: GraphicEQ frequencies differ from the rest')
                rel = model_dir.relative_to(results).as_posix()
                for field in (name, source_dir.name, rig):
                    if '\t' in field or '\n' in field:
                        raise ValueError(f'bad characters in {field!r}')
                rows.append((name, source_dir.name, rig, form, '1' if rel in rec else '0', 'A',
                             AUTOEQ_TARGET, f'{preamp:g}', parametric, graphic))

    # oratory1990's own presets: attach to the AutoEQ headphone with the same name
    # (ignoring case, spaces and punctuation), else add them as a new headphone.
    known = {}
    for r in rows:
        known.setdefault(search_key(r[0]), r)
    opra = opra_oratory_presets(opra_path)
    matched = 0
    for name, form, target, preamp, parametric in opra:
        hit = known.get(search_key(name))
        if hit:
            matched += 1
            name, form = hit[0], hit[3] or form
        rows.append((name, 'oratory1990', '', form, '0', 'O', target, f'{preamp:g}', parametric, ''))

    rows = list(dict.fromkeys(rows))  # OPRA lists some presets under two product ids
    # Per headphone: oratory1990's own presets first, then AutoEQ's recommended source, then the rest.
    rows.sort(key=lambda r: (r[0].casefold(), r[0], r[5] != 'O', r[4] != '1', r[1].casefold(), r[2], r[6]))
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'BTEQLIB\t{FORMAT_VERSION}\t{commit}\t{len(opra)}\n')
        f.write('FREQS\t' + ','.join(freqs or []) + '\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    models = len({r[0] for r in rows})
    print(f'{len(rows)} profiles for {models} headphones from AutoEQ {commit}; '
          f'{len(opra)} oratory1990 presets ({matched} matched to AutoEQ names) -> {out_path}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
