#!/usr/bin/env python3
"""Summarize strip proof records; times are host microseconds, not C3 measurements."""
import argparse
import csv
import json
import pathlib
import statistics


def collect(work):
    for path in sorted(work.glob('case-*/records.tsv')):
        page = None
        costs = []
        elements = 0
        for line in path.open():
            f = line.rstrip('\n').split('\t')
            if f[0] == 'begin':
                page, costs = f[1], []
            elif f[0] == 'page':
                elements = int(f[2].split(',')[0])
            elif f[0] == 'cost':
                key = list(map(int, f[1].split(',')))
                costs.append((key, list(map(int, f[2].split(',')))))
            elif f[0] == 'end':
                base = {tuple(k[:3] + k[4:]): v[0] for k, v in costs if k[3] == 0}
                for k, v in costs:
                    full = base[tuple(k[:3] + k[4:])]
                    yield [path.parent.name, page, elements, *k, *v, full,
                           v[0] / full if full else None,
                           v[4] / v[6] if v[6] else None]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('work', type=pathlib.Path)
    ap.add_argument('output', type=pathlib.Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    columns = ['case', 'page', 'elements', 'orientation', 'placeholders', 'absolute', 'height', 'plane',
               'host_us', 'prepare_us', 'record_checks', 'records_drawn', 'glyph_visits',
               'prepare_records', 'prepare_glyphs', 'full_us', 'time_ratio', 'glyph_ratio']
    groups = {}
    count, largest = 0, 0
    with (args.output / 'per-page.csv').open('w', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        for values in collect(args.work):
            writer.writerow(values)
            row = dict(zip(columns, values))
            count += 1
            largest = max(largest, row['elements'] * 8)
            # Use actual pages, overlay BW; include every orientation. Exclude
            # sub-20us references from timing ratios to limit timer quantization.
            if row['placeholders'] or row['absolute'] or row['plane']:
                continue
            groups.setdefault(row['height'], []).append(row)
    summary = {'cases': len(list(args.work.glob('case-*/records.tsv'))), 'render_cases': count,
               'max_bounds_bytes': largest, 'timing_filter': 'full_us >= 20; overlay BW, no placeholders',
               'heights': {}}
    for height, rows in sorted(groups.items()):
        timing = [r for r in rows if r['full_us'] >= 20]
        def median(key):
            return statistics.median(r[key] for r in rows if r[key] is not None)
        summary['heights'][height] = {
            'pages_orientations': len(rows), 'timed_pages_orientations': len(timing),
            'median_host_us': median('host_us'), 'median_prepare_us': median('prepare_us'),
            'median_time_ratio': statistics.median(r['time_ratio'] for r in timing),
            'median_with_prepare_ratio': statistics.median((r['host_us'] + r['prepare_us']) / r['full_us'] for r in timing),
            'median_x3_two_draw_ratio': statistics.median((2*r['host_us'] + r['prepare_us']) / r['full_us'] for r in timing),
            'median_record_checks': median('record_checks'), 'median_records_drawn': median('records_drawn'),
            'median_glyph_visits': median('glyph_visits'), 'median_glyph_ratio': median('glyph_ratio'),
            'orientation_time_ratio': {
                o: statistics.median(r['time_ratio'] for r in timing if r['orientation'] == o)
                for o in range(4) if any(r['orientation'] == o for r in timing)},
            'orientation_glyph_ratio': {
                o: statistics.median(r['glyph_ratio'] for r in rows if r['orientation'] == o and r['glyph_ratio'] is not None)
                for o in range(4) if any(r['orientation'] == o and r['glyph_ratio'] is not None for r in rows)},
        }
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
