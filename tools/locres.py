import struct, io, json, re
from pathlib import Path
MAGIC=bytes.fromhex('0e147475674a03fc4a15909dc3377f1b')
class Reader:
    def __init__(self, data): self.f=io.BytesIO(data)
    def n(self, fmt): return struct.unpack('<'+fmt,self.f.read(struct.calcsize('<'+fmt)))[0]
    def s(self):
        n=self.n('i')
        if n==0:return ''
        assert abs(n)<10000000
        return self.f.read(n if n>0 else -n*2).decode('utf-8' if n>0 else 'utf-16-le').rstrip('\0')
def read(path):
    r=Reader(Path(path).read_bytes())
    if r.f.read(16)==MAGIC:v=r.n('B')
    else:v=0;r.f.seek(0)
    assert v<=3, v
    strings=[]
    if v>=1:
        offset=r.n('q'); pos=r.f.tell();r.f.seek(offset)
        for _ in range(r.n('i')):
            strings.append(r.s())
            if v>=2:r.n('i')
        r.f.seek(pos)
    if v>=2:total=r.n('i')
    rows=[]
    for _ in range(r.n('i')):
        nh=r.n('I') if v>=2 else None
        ns=r.s()
        for _ in range(r.n('i')):
            kh=r.n('I') if v>=2 else None
            key=r.s();h=r.n('I');value=strings[r.n('i')] if v>=1 else r.s()
            rows.append(dict(namespace=ns,key=key,hash=h,value=value,nh=nh,kh=kh))
    return v,rows
def write(path,rows):
    # Compact format preserves original namespaces, keys and source hashes.
    w=io.BytesIO();w.write(MAGIC);w.write(b'\x01');w.write(b'\0'*8)
    def n(fmt,x):w.write(struct.pack('<'+fmt,x))
    def s(x):
        b=(x+'\0').encode('utf-16-le');n('i',-len(b)//2);w.write(b)
    groups={};texts={}
    for row in rows:groups.setdefault(row['namespace'],[]).append(row)
    n('i',len(groups))
    for ns,items in groups.items():
        s(ns);n('i',len(items))
        for row in items:
            s(row['key']);n('I',row['hash']);idx=texts.setdefault(row['value'],len(texts));n('i',idx)
    offset=w.tell();n('i',len(texts))
    for value in texts:s(value)
    w.seek(17);n('q',offset)
    Path(path).parent.mkdir(parents=True,exist_ok=True);Path(path).write_bytes(w.getvalue())
