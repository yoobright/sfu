import sys
import re

text = sys.stdin.read()

block_re = re.compile(
    r"--- Testing (\w+)\s+\(DUT:\s*([^,]+),\s*REF:\s*([^)]+)\)---"
    r".*?"
    r"AvgAbsErr:\s*([0-9.eE+-]+),\s*MaxAbsErr:\s*([0-9.eE+-]+)\n"
    r"AvgRelErr:\s*([0-9.eE+-]+),\s*MaxRelErr:\s*([0-9.eE+-]+)\n"
    r"AvgULP:\s*([0-9.eE+-]+),\s*MaxULP:\s*([0-9.eE+-]+)",
    re.S,
)

rows = []
dut = ref = None

for m in block_re.finditer(text):
    func = m.group(1)
    dut = m.group(2)
    ref = m.group(3)

    rows.append(
        {
            "func": func,
            "AvgAbsErr": m.group(4),
            "MaxAbsErr": m.group(5),
            "AvgRelErr": m.group(6),
            "MaxRelErr": m.group(7),
            "AvgULP": m.group(8),
            "MaxULP": m.group(9),
        }
    )

if not rows:
    print("No valid result blocks found.", file=sys.stderr)
    sys.exit(1)

print(f"DUT: {dut}    REF: {ref}\n")

print("| Function | AvgAbsErr | MaxAbsErr | AvgRelErr | MaxRelErr | AvgULP | MaxULP |")
print("|----------|-----------|-----------|-----------|-----------|--------|--------|")

for r in rows:
    print(
        f"| {r['func']:<8} "
        f"| {r['AvgAbsErr']} "
        f"| {r['MaxAbsErr']} "
        f"| {r['AvgRelErr']} "
        f"| {r['MaxRelErr']} "
        f"| {r['AvgULP']} "
        f"| {r['MaxULP']} |"
    )
