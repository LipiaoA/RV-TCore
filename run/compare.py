#!/usr/bin/env python3

import re
import sys

# commit log:
# 571:[CommitUnit      ] pc: 0x10150 commit inst: sw retired
commit_pattern = re.compile(r'pc:\s*0x([0-9a-fA-F]+)')

# trace log:
# Trace 0: ... [003411ef/0000000000010140/0202c078/00000201] main
trace_pattern = re.compile(r'\[[0-9a-fA-F]+/([0-9a-fA-F]+)/')


def extract_commit_pcs(filename):
    pcs = []
    with open(filename, "r") as f:
        for lineno, line in enumerate(f, -1):
            m = commit_pattern.search(line)
            if m:
                pcs.append((lineno, int(m.group(1), 16)))
    return pcs


def extract_trace_pcs(filename):
    pcs = []
    with open(filename, "r") as f:
        for lineno, line in enumerate(f, 1):
            m = trace_pattern.search(line)
            if m:
                pcs.append((lineno, int(m.group(1), 16)))
    return pcs


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} test.ctrace test.qtrace")
        sys.exit(1)

    commit_log = sys.argv[1]
    trace_log = sys.argv[2]

    commit_pcs = extract_commit_pcs(commit_log)
    trace_pcs = extract_trace_pcs(trace_log)
    commit_pcs = commit_pcs[2:]
    mismatch = 0

    min_len = min(len(commit_pcs), len(trace_pcs))

    for i in range(min_len):
        c_line, c_pc = commit_pcs[i]
        t_line, t_pc = trace_pcs[i]

        if c_pc != t_pc:
            mismatch += 1
            print(f"Mismatch #{mismatch}")
            print(f"  Sequence : {i + 1}")
            print(f"  Commit   : line {c_line:6d}, PC = 0x{c_pc:x}")
            print(f"  Trace    : line {t_line:6d}, PC = 0x{t_pc:x}")
            print()
            break

    if len(commit_pcs) != len(trace_pcs):
        print("Length mismatch:")
        print(f"  Commit PCs : {len(commit_pcs)}")
        print(f"  Trace PCs  : {len(trace_pcs)}")

    if mismatch == 0 and len(commit_pcs) == len(trace_pcs):
        print("PASS: Execution flow matches.")
    else:
        print(f"\nTotal mismatches: {mismatch}")


if __name__ == "__main__":
    main()
