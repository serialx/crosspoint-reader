import struct

def pack(fmt,*a): return struct.pack('<'+fmt,*a)
def page(tag, data): return pack('HB',1,tag)+data+b'\0'*4
def line(wc=2,text=b'a\0b\0', offsets=(0,2), fonts=b'\0'):
    arena=b'\0'*(wc*8)+pack('H'*len(offsets),*offsets)+b'\0'*(wc*3)+text
    return page(1, b'\0'*4+pack('HBHH',wc,0,len(text),65535)+arena+b'\0'*(wc*4)+b'\0'*28+fonts)
def cases():
    out=[]
    def add(name,data):out.append(dict(name=name,hex=data.hex()))
    add('valid-empty',b'\0'*6)
    add('valid-line',line())
    add('word-count',page(1,b'\0'*4+pack('HBHH',10001,0,10001,65535)))
    add('paragraph-start',page(1,b'\0'*4+pack('HBHH',2,0,4,2)))
    add('text-size',page(1,b'\0'*4+pack('HBHH',2,0,1,65535)))
    add('arena-read',line()[:20])
    add('arena-first-offset',line(offsets=(1,2)))
    add('arena-last-terminator',line(text=b'a\0bx'))
    add('word-offset',line(offsets=(0,0)))
    add('font-flag',line(fonts=b'\2'))
    add('word-font-read',line(fonts=b'\1')[:-4])
    add('word-font-slot',line(fonts=b'\1'+pack('iii',1,2,3)+b'\0\4'))
    add('word-font-slot-high',line(fonts=b'\1'+pack('iii',1,2,3)+b'\0\x84'))
    add('word-font-missing',line(fonts=b'\1'+pack('iii',0,2,3)+b'\1\0'))
    add('unknown-tag',page(255,b''))
    add('rule-width',page(3,pack('hhHB',0,0,0,1)))
    add('rule-thickness',page(3,pack('hhHB',0,0,50,0)))
    add('drop-scale',page(4,pack('hhiHBB',0,0,1,0,0,1)+b'A'))
    add('drop-length',page(4,pack('hhiHBB',0,0,1,256,0,0)))
    add('drop-read',page(4,pack('hhiHBB',0,0,1,256,0,12))[:-4])
    add('border-style',page(5,pack('hhHH',0,0,1,1)+bytes([0,5,0,6,0,0,0,0,0])))
    add('border-empty',page(5,pack('hhHH',0,0,0,1)+b'\0'*9))
    add('footnote-count',pack('HHH',0,17,0))
    add('footnote-read',pack('HH',0,2)+b'\0'*300)
    add('link-count',pack('HHH',0,0,33))
    add('link-read',pack('HHH',0,0,2)+b'a'*270)
    add('link-empty',pack('HHH',0,0,1)+b'\0'*256+pack('hhhh',0,0,1,1))
    add('link-width',pack('HHH',0,0,1)+b'a'+b'\0'*255+pack('hhhh',0,0,0,1))
    return out

def run(binary, inputs, mode='memory'):
    import subprocess
    wire=b''.join(pack('IB',len(bytes.fromhex(c['hex'])),c.get('oom',0))+bytes.fromhex(c['hex']) for c in inputs)
    p=subprocess.run([str(binary),mode],input=wire,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
    rows=[]
    for l in p.stdout.decode().splitlines():
        if l.startswith('BEGIN '): rows.append({'logs':[]})
        elif l.startswith('RESULT '):
            fields=l.split();_,accepted,attempts,size=fields[:4]
            rows[-1].update(accepted=int(accepted),attempts=int(attempts),requested=int(size))
            if len(fields)>4: rows[-1]['io']=list(map(int,fields[4:]))
        else: rows[-1]['logs'].append(l)
    assert len(rows)==len(inputs)
    return rows

if __name__=='__main__':
    import json,sys
    data=cases();rows=run(sys.argv[1],data)
    for c,r in zip(data,rows): c.update(r)
    print(json.dumps(data,indent=2))
