"""Computes extra-target EQ profiles with AutoEQ's own optimiser.

AutoEQ publishes one result per headphone and source, tuned to a single
default target. This script reruns AutoEQ on the raw measurements for the
other targets below, using exactly the settings AutoEQ uses for its published
results (see AutoEQ's dbtools/db.ipynb, `update_results`): the same filter
configuration, 105 Hz / Q 0.7 bass shelf, and fs 44100.

It needs a checkout of AutoEQ with its code, targets and measurements:
    git clone --depth 1 --filter=blob:none --sparse https://github.com/jaakkopasanen/AutoEq autoeq
    git -C autoeq sparse-checkout set --no-cone '/autoeq/**' '/targets/**' \
        '/measurements/**/*.csv' '/results/**/*ParametricEQ.txt' '/results/**/*GraphicEQ.txt' \
        '/results/README.md'
    pip install numpy scipy matplotlib pillow tabulate soundfile pyyaml tqdm

Usage:
    python tools/compute_targets.py autoeq extra_results [--jobs N] [--only NAME_SUBSTRING]

Output goes to extra_results/<target label>/<source>/<rig form>/<model>/ with
AutoEQ's usual "<model> ParametricEQ.txt" and "<model> GraphicEQ.txt" files,
which tools/build_library.py then picks up with --extra.

crinacle's raw measurements are not in AutoEQ's repository, so his profiles
only get AutoEQ's published default target.
"""
import argparse
import os
import sys
import time
from multiprocessing import Pool
from pathlib import Path

FORMS = ('over-ear', 'in-ear', 'earbud')


def rig_family(source, rig, form):
    if rig == 'HMS II.3' or source in ('Innerfidelity', 'Headphone.com Legacy'):
        return 'HMS'
    if '5128' in rig or (source == 'HypetheSonics' and form in ('over-ear', 'earbud')):
        return '5128'
    if rig in ('EARS + 711', 'Bruel & Kjaer 4620'):
        return 'other'
    return 'GRAS'


def default_target(source, rig, form):
    """Label for the target AutoEQ used for its published result (from db.ipynb)."""
    fam = rig_family(source, rig, form)
    if rig == 'EARS + 711':
        return 'Harman over-ear 2018'
    if rig == 'Bruel & Kjaer 4620':
        return 'JM-1 with Harman treble filter'
    if fam == '5128':
        return 'JM-1 with Harman treble filter' if form == 'in-ear' else 'LMG 5128 0.6'
    if (source == 'HypetheSonics' and rig == 'GRAS RA0045') or \
            (source in ('Regan Cipher', 'Super Review') and form == 'earbud'):
        return 'Harman over-ear 2018'
    if form == 'over-ear':
        return 'Harman over-ear 2018'
    return 'AutoEq in-ear'


# (label shown in the plugin, target file in AutoEQ's targets/, bass shelf gain dB)
EXTRA_TARGETS = {
    ('GRAS', 'over-ear'): [
        ('Harman over-ear 2018 without bass', 'Harman over-ear 2018 without bass', 0.0),
        ('oratory1990 Optimum HiFi', 'oratory1990 optimum hifi over-ear', 0.0),
        ('Diffuse field (GRAS KEMAR)', 'Diffuse field GRAS KEMAR', 0.0),
    ],
    ('GRAS', 'in-ear'): [
        ('Harman in-ear 2019', 'Harman in-ear 2019 without bass', 9.5),
        ('Harman in-ear 2019 without bass', 'Harman in-ear 2019 without bass', 0.0),
        ('oratory1990 in-ear', 'oratory1990 in-ear', 0.0),
    ],
    ('HMS', 'over-ear'): [
        ('Harman over-ear 2018 without bass', 'HMS II.3 Harman over-ear 2018 without bass', 0.0),
    ],
    ('HMS', 'in-ear'): [
        ('Harman in-ear 2019', 'HMS II.3 Harman in-ear 2019 without bass', 9.5),
        ('Harman in-ear 2019 without bass', 'HMS II.3 Harman in-ear 2019 without bass', 0.0),
    ],
    ('5128', 'over-ear'): [
        ('LMG 5128 0.6 without bass', 'LMG 5128 0.6 without bass', 0.0),
        ('Diffuse field (B&K 5128)', 'Diffuse field 5128', 0.0),
    ],
    ('5128', 'in-ear'): [
        ('Diffuse field (B&K 5128, -1 dB/oct)', 'Diffuse field 5128 -1dB per octave', 0.0),
    ],
}
EXTRA_TARGETS[('GRAS', 'earbud')] = EXTRA_TARGETS[('GRAS', 'in-ear')]
EXTRA_TARGETS[('HMS', 'earbud')] = EXTRA_TARGETS[('HMS', 'in-ear')]
EXTRA_TARGETS[('5128', 'earbud')] = EXTRA_TARGETS[('5128', 'over-ear')]


def split_rig(folder):
    for form in FORMS:
        if folder == form:
            return '', form
        if folder.endswith(' ' + form):
            return folder[: -len(form) - 1], form
    return folder, ''


def jobs_for(autoeq):
    """One job per (published result with a raw measurement, extra target)."""
    jobs = []
    results = autoeq / 'results'
    for source_dir in sorted(p for p in results.iterdir() if p.is_dir()):
        for rig_dir in sorted(p for p in source_dir.iterdir() if p.is_dir()):
            rig, form = split_rig(rig_dir.name)
            targets = EXTRA_TARGETS.get((rig_family(source_dir.name, rig, form), form), [])
            if not targets:
                continue
            # Some sources keep measurements under a differently-cased folder ("rtings").
            meas_dirs = []
            for src in sorted({source_dir.name, source_dir.name.lower()}):
                d = autoeq / 'measurements' / src / 'data' / form
                meas_dirs.append(d / rig if rig and (d / rig).is_dir() else d)
            for model_dir in sorted(p for p in rig_dir.iterdir() if p.is_dir()):
                csv = next((d / f'{model_dir.name}.csv' for d in meas_dirs
                            if (d / f'{model_dir.name}.csv').exists()), None)
                if csv is None:
                    continue
                for label, target_file, bass in targets:
                    jobs.append((str(csv), label, str(autoeq / 'targets' / f'{target_file}.csv'), bass,
                                 source_dir.name, rig_dir.name, model_dir.name))
    return jobs


_targets_cache = {}


def run_job(args):
    csv, label, target_path, bass, source, rig_folder, model, out_root = args
    from autoeq.constants import PEQ_CONFIGS
    from autoeq.frequency_response import FrequencyResponse
    out_dir = Path(out_root) / label / source / rig_folder / model
    peq_path = out_dir / f'{model} ParametricEQ.txt'
    if peq_path.exists():
        return 'skipped'
    if target_path not in _targets_cache:
        _targets_cache[target_path] = FrequencyResponse.read_csv(target_path)
    try:
        fr = FrequencyResponse.read_csv(csv)
        fr.process(target=_targets_cache[target_path].copy(), min_mean_error=True,
                   bass_boost_gain=bass, bass_boost_fc=105, bass_boost_q=0.7, fs=44100)
        out_dir.mkdir(parents=True, exist_ok=True)
        fr.write_eqapo_graphic_eq(str(out_dir / f'{model} GraphicEQ.txt'), normalize=True)
        peqs = fr.optimize_parametric_eq(
            [PEQ_CONFIGS['4_PEAKING_WITH_LOW_SHELF'], PEQ_CONFIGS['4_PEAKING_WITH_HIGH_SHELF']], 44100)
        fr.write_eqapo_parametric_eq(str(peq_path) + '.tmp', peqs)
        os.replace(str(peq_path) + '.tmp', peq_path)
        return 'ok'
    except Exception as e:  # keep going; report at the end
        return f'error {csv} [{label}]: {e}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('autoeq')
    ap.add_argument('out')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--only', default=None, help='only models whose name contains this')
    a = ap.parse_args()
    autoeq = Path(a.autoeq).resolve()
    sys.path.insert(0, str(autoeq))
    os.environ['PYTHONPATH'] = str(autoeq) + os.pathsep + os.environ.get('PYTHONPATH', '')
    jobs = [j + (str(Path(a.out).resolve()),) for j in jobs_for(autoeq)
            if a.only is None or a.only in j[6]]
    print(f'{len(jobs)} jobs on {a.jobs} processes', flush=True)
    t0, done, errors = time.time(), 0, []
    with Pool(a.jobs) as pool:
        for r in pool.imap_unordered(run_job, jobs, chunksize=8):
            done += 1
            if r.startswith('error'):
                errors.append(r)
            if done % 500 == 0 or done == len(jobs):
                el = time.time() - t0
                print(f'{done}/{len(jobs)} in {el / 60:.1f} min, {len(errors)} errors', flush=True)
    for e in errors:
        print(e)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
