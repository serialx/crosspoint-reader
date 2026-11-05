"""Sum Instruments CPU Counters MetricTable cycles for a known word count."""
import argparse
import json
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("xml", help="xctrace export of the MetricTable schema")
parser.add_argument("words", type=int)
args = parser.parse_args()
root = ET.parse(args.xml)
ids = {element.get("id"): element for element in root.iter() if element.get("id")}


def resolve(element):
    return ids[element.get("ref")] if element.get("ref") else element


cycles = 0
samples = 0
for row in root.findall(".//row"):
    cells = list(row)
    if resolve(cells[2]).text == "cycle":
        cycles += int(float(resolve(cells[6]).text))
        samples += 1
if not samples or args.words <= 0:
    raise SystemExit("Missing cycle samples or invalid word count")
print(json.dumps({"samples": samples, "cycles": cycles, "words": args.words,
                  "cycles_per_word": cycles / args.words}, indent=2))
