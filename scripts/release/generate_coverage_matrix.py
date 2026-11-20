
import csv
import re

# Parse REDUCED-SET-PROPOSAL.md table
def parse_table():
    rows = []
    with open("docs/release/REDUCED-SET-PROPOSAL.md", "r") as f:
        lines = f.readlines()
        for line in lines[2:]: # skip header and separator
            parts = [p.strip() for p in line.split("|") if p.strip()]
            if len(parts) >= 8:
                rows.append({
                    "name": parts[0],
                    "group": parts[1],
                    "folder": parts[2],
                    "osc": parts[3],
                    "effects": parts[4],
                    "status": parts[7]
                })
    return rows

def generate_matrix():
    rows = parse_table()
    # Simplified matrix generation
    print("# Coverage Matrix")
    print("| Type | Checked | TODO | Gap |")
    print("|---|---|---|---|")
    
    # Types to check (simplified)
    types = ["WAVE", "MULTI", "SAMPLE", "GRAIN", "SPEC", "Convolve"]
    
    for t in types:
        checked = 0
        todo = 0
        for r in rows:
            if t in r["osc"] or (t == "Convolve" and r["effects"] == "Convolve"):
                if r["status"] == "PASSED":
                    checked += 1
                else:
                    todo += 1
        gap = "Yes" if checked == 0 else "No"
        print(f"| {t} | {checked} | {todo} | {gap} |")

if __name__ == "__main__":
    generate_matrix()
