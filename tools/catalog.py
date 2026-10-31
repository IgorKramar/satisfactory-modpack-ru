"""Inspect and rebuild reviewed localization catalogs; never modifies the game."""
from pathlib import Path
import argparse
import collections
import gzip
import hashlib
import json
import re
import struct
from locres import read, write

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'data'
BUILD = ROOT / 'build'
NAMES = ('translations', 'planner', 'editorial')
TOKENS = re.compile(r'\{[^{}]*\}|<(?:/?[A-Za-z_][^<>]*|/)>')

def load(name):
    return json.loads(gzip.decompress((DATA / (name+'.json.gz')).read_bytes()))

def active_rows(rows):
    active = {}
    for row in rows:
        if not row.get('deployed') or row.get('replacement_namespace'):
            continue
        identity = (row['namespace'], row['key'])
        if identity in active and (active[identity]['hash'], active[identity]['translation']) != (row['hash'], row['translation']):
            raise ValueError(f'Unresolved active identity: {identity}')
        active[identity] = row
    return list(active.values())

def check():
    rows, planner, editorial = (load(n) for n in NAMES)
    active = active_rows(rows)
    expected = json.loads((ROOT/'evidence/catalog-2026-10-04.json').read_text(encoding='utf-8'))
    if len(active) != expected['strings']:
        raise ValueError(f"Active key count differs from recorded catalog: {len(active)}")
    if len(editorial) != expected['unique_reviewed_sources']:
        raise ValueError('Editorial source count differs from recorded catalog')
    for row in rows:
        if not row['translation'].strip() and row['source'].strip():
            raise ValueError(f"Empty translation: {row['key']}")
        if row['status'] != 'literary-reviewed':
            raise ValueError(f"Unreviewed entry: {row['key']}")
        if collections.Counter(TOKENS.findall(row['source'])) != collections.Counter(TOKENS.findall(row['translation'])):
            raise ValueError(f"Changed format tokens: {row['key']}")
    table = planner['tables']
    if len(table) != 820 or len({r['english'] for r in table}) != 798:
        raise ValueError('Unexpected Planner catalog dimensions')
    for row in table:
        if row['english'].strip() and not row['russian'].strip():
            raise ValueError('Empty Planner translation')
    mods = json.loads((DATA/'mods.lock.json').read_text(encoding='utf-8'))['mods']
    if len(mods) != 49: raise ValueError('Expected the recorded 49-module snapshot')
    print(json.dumps({'active_keys':len(active),'reviewed_sources':len(editorial),'planner_rows':len(table),'modules':len(mods),'check':'passed'},ensure_ascii=False))
    return active

def build():
    rows = check()
    dest = BUILD/'Localization/SFPackRU'
    report = {}
    for culture in ('en','ru','ru-RU'):
        result = [dict(r,value=r['source'] if culture=='en' else r['translation']) for r in rows]
        path = dest/culture/'SFPackRU.locres'
        write(path,result)
        parsed = read(path)[1]
        actual = {(r['namespace'],r['key']):(r['hash'],r['value']) for r in parsed}
        expected = {(r['namespace'],r['key']):(r['hash'],r['value']) for r in result}
        if actual != expected: raise ValueError(f'locres round-trip failed: {culture}')
        report[culture] = {'entries':len(parsed),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    def ue_string(s):
        encoded=(s+'\0').encode()
        return struct.pack('<i',len(encoded))+encoded
    meta=bytes.fromhex('4fee4ca1684855836c4c46bd70da507c')+b'\x01'+ue_string('en')+ue_string('en/SFPackRU.locres')+struct.pack('<i',3)+b''.join(ue_string(c) for c in ('en','ru','ru-RU'))
    (dest/'SFPackRU.locmeta').write_bytes(meta)
    (BUILD/'catalog-build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('unpack','pack','check','build-locres'))
    command=parser.parse_args().command
    if command=='check': check()
    elif command=='build-locres': build()
    elif command=='unpack':
        (BUILD/'data').mkdir(parents=True,exist_ok=True)
        for name in NAMES:
            target=BUILD/'data'/f'{name}.json'
            if target.exists(): raise FileExistsError(f'Refusing to overwrite edits: {target}')
        for name in NAMES:
            (BUILD/'data'/f'{name}.json').write_text(json.dumps(load(name),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Editable JSON written to build/data/')
    elif command=='pack':
        # Validate all JSON inputs before changing any tracked file.
        values={name:json.loads((BUILD/'data'/f'{name}.json').read_text(encoding='utf-8-sig')) for name in NAMES}
        for name,value in values.items():
            payload=json.dumps(value,ensure_ascii=False,separators=(',',':')).encode()
            (DATA/f'{name}.json.gz').write_bytes(gzip.compress(payload,mtime=0))
        print('Updated compressed catalogs; run check and review the diff.')

if __name__=='__main__': main()
