#!/usr/bin/env python3
"""Compare C3 strip uploads with framebuffer traces using controller RAM bytes."""
import argparse
import dataclasses
import hashlib
import pathlib
import re


@dataclasses.dataclass
class Command:
    code: int
    data: bytes
    data_transactions: int = 0


def events(text):
    """Decode the O1 bus transcript; never accept hash-only payloads."""
    cs, dc, transaction = 1, 1, False
    code, payload = None, bytearray()
    transaction_id = 0
    data_transactions = set()
    for raw in text.splitlines():
        match = re.fullmatch(r"(.*) repeat=(\d+)", raw)
        line, count = (match[1], int(match[2])) if match else (raw, 1)
        for _ in range(count):
            if line.startswith("pin.write "):
                _, pin, value = line.split()
                if pin == "21":
                    cs = int(value)
                elif pin == "4":
                    dc = int(value)
                elif pin == "5":
                    if code is not None:
                        yield Command(code, bytes(payload), len(data_transactions))
                        code, payload = None, bytearray()
                    data_transactions.clear()
                    yield line
            elif line.startswith("spi.txn.begin"):
                assert not transaction, "nested SPI transaction"
                transaction = True
                transaction_id += 1
            elif line == "spi.txn.end":
                assert transaction and cs == 1, "transaction ends with CS low"
                transaction = False
            elif line.startswith("spi.bytes "):
                assert transaction and cs == 0, "SPI bytes without bus ownership"
                m = re.fullmatch(r"spi.bytes n=(\d+) hex=([0-9a-f]*)", line)
                assert m, "strip comparison requires --full traces"
                data = bytes.fromhex(m[2])
                assert len(data) == int(m[1]), "truncated SPI payload"
                if dc:
                    assert code is not None, "data without command"
                    payload.extend(data)
                    data_transactions.add(transaction_id)
                else:
                    assert len(data) == 1, "multiple command bytes"
                    if code is not None:
                        yield Command(code, bytes(payload), len(data_transactions))
                    code, payload = data[0], bytearray()
                    data_transactions.clear()
            elif line.startswith(("source.read ", "source.failure ", "step ", "wait.", "delay.", "geometry ")):
                if code is not None:
                    yield Command(code, bytes(payload), len(data_transactions))
                    code, payload = None, bytearray()
                    data_transactions.clear()
                if line.startswith(("source.read ", "source.failure ")):
                    assert cs == 1 and not transaction, "source read while display owns SPI"
                yield line
    if code is not None:
        yield Command(code, bytes(payload), len(data_transactions))
    assert not transaction and cs == 1, "unfinished SPI transaction"


def digest(data):
    return hashlib.sha256(bytes(data)).hexdigest()


class Ram:
    def __init__(self, controller):
        self.x3 = controller == "uc8253"
        self.width, self.height = (792, 528) if self.x3 else (800, 480)
        self.stride = self.width // 8
        self.planes = {p: bytearray(self.stride * self.height) for p in ((0x10, 0x13) if self.x3 else (0x24, 0x26))}
        self.defined = {p: bytearray(self.stride * self.height) for p in self.planes}
        self.window = (0, self.width - 1, 0, self.height - 1)
        self.partial = False
        self.entry = 1
        self.counter = (0, self.height - 1)
        self.pending = False
        self.source = None
        self.next_row = 0
        self.upload_plane = None
        self.upload_before = None
        self.policy = []
        self.checkpoints = []
        self.step = "initial"
        self.source_count = 0

    def snapshot(self, name):
        self.checkpoints.append((self.step, name, tuple((p, digest(self.planes[p]), digest(self.defined[p])) for p in self.planes)))

    def command(self, code, data):
        if code in self.planes:
            assert not self.pending, "RAM write while waveform pending"
            xs, xe, ys, ye = self.window
            if self.x3:
                if not self.partial:
                    xs, xe, ys, ye = 0, self.width - 1, 0, self.height - 1
                assert xs == 0 and xe == self.width - 1, "unexpected X3 X window"
                locations = [r * self.stride + c for r in range(ys, ye + 1) for c in range(self.stride)]
            else:
                assert self.entry in (0, 1), "unsupported data-entry mode"
                assert (xs, xe) in ((0, self.width - 1), (self.width - 1, 0)), "unexpected SSD X window"
                assert self.counter == (xs, ys), "wrong RAM counter"
                locations = [r * self.stride + c for r in range(ys, ye - 1, -1)
                             for c in (range(self.stride) if self.entry else range(self.stride - 1, -1, -1))]
            assert len(data) == len(locations), "incomplete plane/window or wrong padding"
            if self.source:
                y, rows = self.source
                gates = (self.height - y - rows, self.height - y - 1)
                assert (ys, ye) == (gates if self.x3 else gates[::-1]), "wrong strip gate range"
                assert y == self.next_row, "missing, repeated or reordered strip"
                if y == 0:
                    self.upload_plane = code
                    self.upload_before = {p: bytes(b) for p, b in self.planes.items()}
                assert self.upload_plane == code, "plane changed during upload"
                self.next_row += rows
                self.source = None
            for offset, value in zip(locations, data):
                self.planes[code][offset] = value
                self.defined[code][offset] = 1
            if self.upload_before:
                for p in self.planes:
                    if p != code:
                        assert self.planes[p] == self.upload_before[p], "unrequested plane changed"
                if self.next_row == self.height:
                    self.next_row = 0
                    self.upload_before = None
                    self.snapshot(f"plane-{code:02x}")
            else:
                self.snapshot(f"plane-{code:02x}")
            return
        if self.x3 and code in (0x90, 0x91, 0x92):
            if code == 0x90:
                assert len(data) == 9 and data[8] == 1, "bad PTL descriptor"
                self.window = tuple(int.from_bytes(data[i:i+2], "big") for i in range(0, 8, 2))
                xs, xe, ys, ye = self.window
                assert 0 <= xs <= xe < self.width and 0 <= ys <= ye < self.height, "PTL outside panel"
            else:
                assert not data
                self.partial = code == 0x91
            return
        if not self.x3 and code in (0x11, 0x44, 0x45, 0x4e, 0x4f):
            if code == 0x11:
                assert len(data) == 1
                self.entry = data[0]
            elif code in (0x44, 0x45):
                assert len(data) == 4
                values = tuple(int.from_bytes(data[i:i+2], "little") for i in (0, 2))
                self.window = values + self.window[2:] if code == 0x44 else self.window[:2] + values
            else:
                assert len(data) == 2
                value = int.from_bytes(data, "little")
                self.counter = (value, self.counter[1]) if code == 0x4e else (self.counter[0], value)
            return
        if not self.x3 and code in (0x46, 0x47):
            assert data == b'\xf7', "unknown automatic RAM fill"
            plane = 0x24 if code == 0x46 else 0x26
            self.planes[plane][:] = b'\xff' * len(self.planes[plane])
            self.defined[plane][:] = b'\x01' * len(self.defined[plane])
        activation = code == (0x12 if self.x3 else 0x20)
        if activation:
            assert self.next_row == 0 and self.source is None, "activation with incomplete strip coverage"
            if self.x3:
                assert not self.partial or self.window == (0, self.width - 1, 0, self.height - 1), "last strip window retained at activation"
            else:
                xs, xe, ys, ye = self.window
                assert {xs, xe} == {0, self.width - 1} and (ys, ye) == (self.height - 1, 0), "last strip window retained at activation"
            self.snapshot("activate")
            self.pending = True
        # Explicit register whitelist: additions require reviewing RAM semantics.
        allowed = ({0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x11, 0x12, 0x30, 0x50, 0x61, 0x65, 0x82, 0xe1} | set(range(0x20, 0x25))) if self.x3 else {0x00, 0x01, 0x03, 0x04, 0x0c, 0x10, 0x12, 0x18, 0x1a, 0x20, 0x21, 0x22, 0x2c, 0x32, 0x37, 0x3c, 0x46, 0x47}
        assert code in allowed, f"unknown command {code:02x}"
        self.policy.append((code, data))

    def run(self, decoded):
        decoded = list(decoded)
        for index, event in enumerate(decoded):
            if isinstance(event, str) and event.startswith("source.read "):
                template = (0x91, 0x90, None, 0x92) if self.x3 else (0x11, 0x44, 0x45, 0x4e, 0x4f, None)
                sequence = decoded[index + 1:index + 1 + len(template)]
                assert len(sequence) == len(template), "incomplete strip transaction template"
                for command, expected in zip(sequence, template):
                    assert isinstance(command, Command), "non-command inside strip transaction template"
                    assert command.code in self.planes if expected is None else command.code == expected, "illegal strip command order"
            if isinstance(event, Command):
                if event.code in self.planes:
                    assert event.data_transactions == 1, "plane payload must use one CS-low burst"
                self.command(event.code, event.data)
            elif event.startswith("source.read "):
                assert self.source is None, "source without upload"
                _, y, rows = event.split()
                self.source = int(y), int(rows)
                assert 0 <= int(y) < self.height and 0 < int(rows) <= self.height - int(y)
                self.source_count += 1
            elif event.startswith("step "):
                assert not self.source and self.next_row == 0, "operation ends before upload complete"
                self.snapshot("end")
                self.step = event
                self.policy.append(event)
            elif event.startswith("wait.end "):
                self.pending = False
                self.policy.append(event)
            elif not event.startswith("geometry "):
                self.policy.append(event)
        assert not self.source and self.next_row == 0, "missing final strip"
        self.snapshot("end")
        return self


def compare(reference, candidate, controller):
    before = Ram(controller).run(events(reference))
    after = Ram(controller).run(events(candidate))
    assert before.policy == after.policy, "waveform, power, reset or wait policy changed"
    assert before.checkpoints == after.checkpoints, "controller RAM or coverage differs at logical checkpoint"
    assert after.source_count, "candidate did not exercise a strip source"
    return len(after.checkpoints), after.source_count


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--controller", required=True, choices=("uc8253", "ssd1677"))
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    args = parser.parse_args()
    points, sources = compare(args.reference.read_text(), args.candidate.read_text(), args.controller)
    print(f"{args.controller}: {points} RAM checkpoints, {sources} source boundaries")


if __name__ == "__main__":
    main()
