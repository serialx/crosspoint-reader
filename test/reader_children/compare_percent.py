"""Compare percent input outcomes with current native activity + pinned SDK routing."""
import argparse
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--native', required=True)
p.add_argument('--rust', required=True)
a = p.parse_args()
commands = []

def frame(pressed=0, released=0, held=0, now=0, duration=0, x=-1, y=-1, touch=0, swipe=0):
    commands.append(f'input {pressed} {released} {held} {now} {duration} {x} {y} {touch} {swipe}')

# Exact front/side repeat ordering and 100-value ring, including simultaneous edges.
for edge in [0, 1]:
    for initial in [0, 1, 50, 90, 99, 100]:
        for keys in [16, 32, 64, 128, 48, 192, 240]:
            commands.append(f'new {initial} {edge}')
            frame(pressed=keys, held=keys, now=1000)
            for now, duration in [(1500,500),(1501,501),(2001,1001),(2002,1002),(2600,1600)]:
                frame(held=keys, now=now, duration=duration)
            frame(released=keys, held=keys, now=2700, duration=1700)
            frame(released=2)
            frame(released=1)
            frame(swipe=3)
            frame(swipe=4)

# Touch controls before publication, across repaint and relocated generations.
for edge in [0, 1]:
    for initial in [0, 50, 100]:
        for x, y in [(15,65),(40,65),(60,65),(50,25),(95,95),(150,150),(-1,-1)]:
            for flags in [1, 2, 4, 5]:
                commands.append(f'new {initial} {edge}')
                frame(x=x, y=y, touch=flags)
                commands.append('paint 0')
                frame(x=x, y=y, touch=flags)
                commands.append('paint 0')
                frame(x=110, y=200, touch=4)
                commands.append('paint 10')
                frame(x=5, y=-1, touch=4)
                frame(x=x, y=y, touch=2, swipe=4)
                frame(released=1, swipe=3)
                frame(released=1)
                commands.append('paint 0')
                frame(x=60, y=65, touch=2)

# Route from inside native screen construction after each registered control.
# The route must use the old complete generation, including an active drag index.
for ready in [False, True]:
    for drag in [False, True]:
        for stage in range(6):
            for x, y, touch in [(15,65,2),(40,65,2),(60,65,2),(50,25,4),(110,25,2)]:
                commands.append('new 50 1')
                if ready:
                    commands.append('paint 0')
                if drag:
                    frame(x=30, y=25, touch=4)
                commands.append(f'during 100 {stage} 0 0 0 0 0 {x} {y} {touch} 0')
                frame(x=140, y=65, touch=2)

payload = ('\n'.join(commands) + '\n').encode()
outputs = [subprocess.check_output([binary], input=payload).splitlines() for binary in [a.native, a.rust]]
inputs = [(i, command) for i, command in enumerate(commands) if command.startswith(('input ', 'during '))]
assert len(outputs[0]) == len(outputs[1]) == len(inputs)
for (line, command), native, rust in zip(inputs, *outputs):
    if native != rust:
        start = max(i for i in range(line + 1) if commands[i].startswith('new '))
        raise AssertionError(f'Native {native!r} != Rust {rust!r} at command {line}: {command}\n' + '\n'.join(commands[start:line + 1]))
print(f'{len(inputs)} native/Rust percent input observations equal')
