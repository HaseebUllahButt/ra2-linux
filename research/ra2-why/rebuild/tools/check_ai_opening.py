#!/usr/bin/env python3
"""Check a stationary Allied opening at each game speed, using the live battle."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game_dir', type=Path)
parser.add_argument('--map', default='MP02T2.MAP')
parser.add_argument('--speeds', default='0,1,2,3,4,5,6')
parser.add_argument('--frames', type=int, default=30000)
parser.add_argument('--jobs', type=int, default=4)
args = parser.parse_args()
binary = Path(__file__).resolve().parents[1] / 'build/ra2-native'
def probe(level, speed):
    assert speed in tuple(str(n) for n in range(7)), speed
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               RA2_AI_OPENING_CHECK=level, RA2_AI_OPENING_SPEED=speed)
    with tempfile.TemporaryFile(mode='w+') as log:
        result = subprocess.run([str(binary), '--game-dir', str(args.game_dir.resolve()),
            '--skirmish', '--map', args.map, '--headless', '--autoplay', '--frames', str(args.frames)],
            env=env, stdout=log, stderr=subprocess.STDOUT, check=False)
        log.seek(0)
        lines = log.read().splitlines()
    records = [line for line in lines if line.startswith('AI_OPENING ')]
    if result.returncode or len(records) != 1:
        raise RuntimeError(f'{level} speed {speed}: exit {result.returncode}\n' + '\n'.join(lines[-30:]))
    record = dict(field.split('=', 1) for field in records[0].split()[1:])
    assert int(record['first_shot']) > 0
    return records[0], record

baselines = {}
with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
    jobs = [pool.submit(probe, level, speed)
            for level in ('easy', 'medium', 'brutal') for speed in args.speeds.split(',')]
    for job in as_completed(jobs):
        line, record = job.result()
        print(line, flush=True)
        record.pop('speed')
        level = record['difficulty']
        if level not in baselines:
            baselines[level] = record
        else:
            assert record == baselines[level], (level, baselines[level], record)
print('Opening checks passed for all requested difficulties and game speeds.')
