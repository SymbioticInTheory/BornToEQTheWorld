"""Builds data/curves.tsv: measurements and target curves for the advanced graph.

For every profile in data/headphones.tsv this records the headphone's raw
measurement and the target curve the EQ was made for, both on the library's
127 GraphicEQ frequencies, so the plugin can draw target, measured and
corrected curves like autoeq.app.

Sources:
- AutoEQ's published results: "<model>.csv" next to each ParametricEQ.txt has
  the raw measurement and the target AutoEQ used (bass shelf included).
- Extra targets (tools/compute_targets.py): AutoEQ's target file plus the
  same 105 Hz / Q 0.7 bass shelf AutoEQ applies.
- oratory1990's own presets: his measurement from AutoEQ, with the Harman or
  oratory1990 target named in the preset. Others get no target.

It needs a checkout of AutoEQ with its result CSVs and targets:
    git clone --depth 1 --filter=blob:none --sparse https://github.com/jaakkopasanen/AutoEq autoeq
    git -C autoeq sparse-checkout set --no-cone '/results/**/*.csv' '/targets/*'

Usage:
    python tools/build_curves.py autoeq data/headphones.tsv data/curves.tsv

Output (tab separated, values in tenths of a dB, each curve 0 dB at 1 kHz):
    BTEQCURVES  1
    T  <target id>  <127 values>
    M  <measurement id>  <127 values>
    P  <name>  <source>  <rig>  <A|O>  <target label>  <measurement id or ->  <target id or ->
"""
import csv
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from compute_targets import EXTRA_TARGETS, default_target, rig_family
from build_library import default_target  # noqa: E402

FORMS = ('over-ear', 'in-ear', 'earbud')


def read_csv(path, columns):
    with open(path, encoding='utf-8') as f:
        rows = list(csv.DictReader(f))
    freqs = [float(r['frequency']) for r in rows]
    return freqs, {c: [float(r[c]) if r.get(c) not in (None, '') else math.nan for r in rows] for c in columns}


def resample(freqs, values, at):
    """Average of each curve over the log-frequency bin around each output point."""
    logs = [math.log(f) for f in freqs]
    out = []
    for i, f in enumerate(at):
        lo = math.log(at[i - 1]) if i > 0 else math.log(f) - 0.02
        hi = math.log(at[i + 1]) if i + 1 < len(at) else math.log(f) + 0.02
        lo, hi = (lo + math.log(f)) / 2, (hi + math.log(f)) / 2
        sel = [v for x, v in zip(logs, values) if lo <= x <= hi and not math.isnan(v)]
        out.append(sum(sel) / len(sel) if sel else interp(logs, values, math.log(f)))
    return out


def interp(xs, ys, x):
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    for i in range(1, len(xs)):
        if xs[i] >= x:
            t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
            return ys[i - 1] + t * (ys[i] - ys[i - 1])
    return ys[-1]


def low_shelf_db(f, gain, fc=105.0, q=0.7, fs=44100.0):
    """RBJ low shelf magnitude, as AutoEQ uses for its bass boost."""
    if gain == 0:
        return 0.0
    a = 10 ** (gain / 40)
    w0 = 2 * math.pi * fc / fs
    alpha = math.sin(w0) / (2 * q)
    cw = math.cos(w0)
    sa = 2 * math.sqrt(a) * alpha
    b = [a * ((a + 1) - (a - 1) * cw + sa), 2 * a * ((a - 1) - (a + 1) * cw), a * ((a + 1) - (a - 1) * cw - sa)]
    d = [(a + 1) + (a - 1) * cw + sa, -2 * ((a - 1) + (a + 1) * cw), (a + 1) + (a - 1) * cw - sa]
    w = 2 * math.pi * f / fs
    z = complex(math.cos(w), -math.sin(w))
    h = (b[0] + b[1] * z + b[2] * z * z) / (d[0] + d[1] * z + d[2] * z * z)
    return 20 * math.log10(abs(h))


def centre(values, freqs):
    ref = [v for f, v in zip(freqs, values) if 800 <= f <= 1250]
    off = sum(ref) / len(ref) if ref else 0.0
    return [v - off for v in values]


def encode(values):
    return ','.join(str(int(round(v * 10))) for v in values)


def split_rig(folder):
    for form in FORMS:
        if folder == form:
            return '', form
        if folder.endswith(' ' + form):
            return folder[: -len(form) - 1], form
    return folder, ''


def oratory_target(label, form):
    t = label.casefold()
    in_ear = form in ('in-ear', 'earbud')
    if 'harman' in t:
        return 'Harman in-ear 2019' if in_ear else 'Harman over-ear 2018'
    if 'oratory1990' in t or 'optimum' in t:
        return 'oratory1990 in-ear' if in_ear else 'oratory1990 optimum hifi over-ear'
    return None


def main(autoeq_dir, library_path, out_path):
    autoeq = Path(autoeq_dir)
    with open(library_path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    freqs = [float(x) for x in lines[1].split('\t')[1].split(',')]
    profiles = [ln.split('\t') for ln in lines[2:] if ln]

    # Measurements and published targets from AutoEQ's result CSVs.
    measurements, published_target = {}, {}
    for path in sorted((autoeq / 'results').glob('*/*/*/*.csv')):
        model_dir = path.parent
        if path.stem != model_dir.name:
            continue
        source, (rig, _form) = model_dir.parent.parent.name, split_rig(model_dir.parent.name)
        f, cols = read_csv(path, ['raw', 'target'])
        key = (model_dir.name, source, rig)
        measurements[key] = encode(centre(resample(f, cols['raw'], freqs), freqs))
        published_target[key] = encode(centre(resample(f, cols['target'], freqs), freqs))

    target_files = {}

    def target_from_file(name, bass):
        k = (name, bass)
        if k not in target_files:
            p = autoeq / 'targets' / f'{name}.csv'
            if not p.exists():
                target_files[k] = None
            else:
                f, cols = read_csv(p, ['raw'])
                vals = resample(f, cols['raw'], freqs)
                vals = [v + low_shelf_db(fr, bass) for v, fr in zip(vals, freqs)]
                target_files[k] = encode(centre(vals, freqs))
        return target_files[k]

    extra = {}
    for (fam, form), items in EXTRA_TARGETS.items():
        for label, file, bass in items:
            extra[(fam, form, label)] = (file, bass)

    oratory_measurement = {}
    for (name, source, rig) in measurements:
        if source == 'oratory1990':
            oratory_measurement.setdefault(name, (name, source, rig))

    target_ids, meas_ids, out_profiles, missing = {}, {}, [], 0

    def tid(values):
        if values is None:
            return '-'
        return target_ids.setdefault(values, str(len(target_ids)))

    def mid(key):
        if key is None or key not in measurements:
            return '-'
        return meas_ids.setdefault(key, str(len(meas_ids)))

    for p in profiles:
        name, source, rig, form, _rec, origin, label = p[:7]
        if origin == 'O':
            mkey = oratory_measurement.get(name)
            tname = oratory_target(label, form)
            target = target_from_file(tname, 0.0) if tname else None
        else:
            mkey = (name, source, rig)
            target = published_target.get(mkey)
            fam_key = (rig_family(source, rig, form), form, label)
            if label != default_target(source, rig, form) and fam_key in extra:
                target = target_from_file(*extra[fam_key])  # an extra-target row
            if target is None:
                target = target_from_file(label, 0.0)  # sources without raw data (crinacle)
        m = mid(mkey)
        if m == '-':
            missing += 1
        out_profiles.append((name, source, rig, origin, label, m, tid(target)))

    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('BTEQCURVES\t1\n')
        for values, i in target_ids.items():
            f.write(f'T\t{i}\t{values}\n')
        for key, i in meas_ids.items():
            f.write(f'M\t{i}\t{measurements[key]}\n')
        for row in out_profiles:
            f.write('P\t' + '\t'.join(row) + '\n')
    print(f'{len(out_profiles)} profiles, {len(meas_ids)} measurements, {len(target_ids)} targets, '
          f'{missing} profiles without a measurement -> {out_path}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
