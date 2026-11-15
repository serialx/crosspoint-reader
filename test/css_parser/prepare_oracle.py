#!/usr/bin/env python3
"""Recreate oracle inputs from the pre-port repository (206b4259).

Run at that revision with this script and the golden test copied in. This only
creates inputs; CSS_CAPTURE_ORACLE=1 captures outputs from the C++ implementation.
"""
from pathlib import Path
import re,zipfile,json
root=Path('test/css_parser'); out=root/'goldens'; out.mkdir(exist_ok=True)
cases=[]
def sample_queries(name, queries):
    """Keep cap boundaries, growth boundaries and every 64th identifier.

    All case/extra-class variants of a selected identifier remain. Generic
    nonnumeric probes remain too; stylesheet inputs and cache records are never
    sampled. The selector-byte corpus accepts 146 rules in the C++ oracle.
    """
    caps = {
        'edge-rule-cap': (1520, 1500),
        'edge-style-cap': (276, 256),
        'edge-selector-cap': (200, 146),
    }
    if name not in caps:
        return queries
    total, accepted = caps[name]
    selected = set(range(0, total, 64)) | {0, 1, total - 2, total - 1}
    boundaries = [accepted]
    power = 16
    while power < total:
        boundaries.append(power)
        power *= 2
    for boundary in boundaries:
        selected.update(range(max(0, boundary - 2), min(total, boundary + 2)))
    return {
        query for query in queries
        if not (match := re.match(r'(?:class-|unique-)?(\d+)', query[1], re.I))
        or int(match[1]) in selected
    }

def add(name, sources):
 d=out/name;d.mkdir(exist_ok=True)
 for i,s in enumerate(sources): (d/f'{i:02}.css').write_bytes(s.encode() if isinstance(s,str) else s)
 cases.append(name)
 text='\n'.join(s.decode(errors='replace') if isinstance(s,bytes) else s for s in sources)
 queries={('p','','',''),('div','a b c d e f g h i','id',''),('span','note','target','')}
 for group in re.findall(r'([^{};]+)\{',re.sub(r'/\*.*?\*/','',text,flags=re.S)):
  for sel in group.split(','):
   parts=re.split(r'[\s>]+',sel.strip().replace('::first-letter','').replace(':first-letter',''))
   chain=[]
   for part in parts:
    tag=re.match(r'^[\w-]+',part); ids=re.findall(r'#([\w-]+)',part); classes=re.findall(r'\.([\w-]+)',part)
    chain.append((tag.group() if tag else 'p',' '.join(classes),ids[0] if ids else ''))
   if chain:
    tag,cl,ident=chain[-1];anc='|'.join('~'.join(x) for x in chain[:-1]);queries.add((tag,cl,ident,anc));queries.add((tag,cl,ident,''));queries.add((tag.upper(),cl.upper(),ident.upper(),anc));queries.add((tag,cl+' unrelated',ident,anc))
 queries=sample_queries(name, queries)
 (d/'queries.tsv').write_text(''.join('\t'.join(q)+'\n' for q in sorted(queries)))
 inline=set(re.findall(r'\{([^{}]*)\}',text))
 inline={x.replace('\n',' ').replace('\r',' ').replace('\t',' ') for x in inline}
 (d/'inline.txt').write_text('\n'.join(sorted(inline))+'\n')
for epub in sorted(Path('test/epubs').glob('*.epub')):
 with zipfile.ZipFile(epub) as z:
  files=[(n,z.read(n)) for n in sorted(z.namelist()) if n.lower().endswith('.css')]
  if files: add('epub-'+epub.stem,[data for n,data in files])
# Existing tests use string literals rather than standalone fixtures.
t=Path('test/css_parser/CssParserTest.cpp').read_text()
for i,m in enumerate(re.finditer(r'(?:"(?:\\.|[^"\\])*"\s*)+',t)):
 try: s=''.join(json.loads(x) for x in re.findall(r'"(?:\\.|[^"\\])*"',m.group()))
 except ValueError: continue
 if '{' in s and ':' in s: add(f'suite-{i:03}',[s])
add('edge-order',['.a{margin:1px;font-weight:bold}.b{margin:2px}.a{font-style:italic} p.a.b#id{padding:1px 2em 3pt 4%;} div > p.a {text-align:right} div section > p.a::first-letter{font-size:250%;float:left;initial-letter:2}', '.b{margin:3px} .a{margin:4px} p.a{font-weight:normal!important}'])
add('edge-rule-cap',[''.join(f'.class-{i}{{font-weight:bold}}' for i in range(1520))+'.class-1499{font-style:italic}'])
add('edge-style-cap',[''.join(f'.unique-{i}{{text-indent:{i+1}px}}' for i in range(276))])
add('edge-selector-cap',[''.join('.'+str(i)+'x'*220+'{font-weight:bold}' for i in range(200))])
add('edge-large',['.'+'a'*1100+'{font-weight:bold}.valid{font-style:italic}.long{font-weight:'+'x'*1100+';text-indent:1px}'])
add('edge-floats',[''.join(f'.f{i}{{margin:{s};font-size:{s};border-width:{s};initial-letter:{s}}}' for i,s in enumerate(['NaN','Inf','-Inf','+0','-0','0.49999997px','0.5px','1.000000059604644775390625em','1.000000178813934326171875pt','0.000000000000000000000000000000000000000000001','340282346638528859811704183484516925440','999999999999999999999999999999999999999999','2.4999999','2.5','12pt','+2.5em','--1','1e2','0x1','1garbage']))])
add('edge-grammar',['@import "x";@media screen{p{font-weight:bold}} /*comment*/ P.a.b#id > div.x p.b.a {white-space:pre-wrap;unknown:value;background:#ddd;border:1.5pt dashed} *{text-align:right} .a:hover{font-style:italic} p::first-letter{float:left} p:first-letter{initial-letter:3} .a{margin:1px !IMPORTANT; padding:1px\t2px\r3px\n4px\f5px; text-decoration:underline line-through}'])
for i,s in enumerate(['.a{font-weight:bold;','@media screen {','/* unfinished','.unfinished','/']):add(f'edge-incomplete-{i}',[s])
(out/'cases.txt').write_text('\n'.join(cases)+'\n')
print(len(cases),'cases')
