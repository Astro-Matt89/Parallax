#!/usr/bin/env python3
"""
prepare_hipparcos.py — Convert raw Hipparcos catalog (hip_main.dat) to Parallax CSV format.

Input:  hip_main.dat  (fixed-width format from CDS/ESA)
        Download from: https://cdsarc.cds.unistra.fr/ftp/cats/I/239/hip_main.dat

Output: data/catalogs/hipparcos.csv
        Columns: HIP,RA_deg,Dec_deg,Vmag,BV

Usage:
    python3 tools/prepare_hipparcos.py <input_path> [output_path]

    python3 tools/prepare_hipparcos.py hip_main.dat
    python3 tools/prepare_hipparcos.py hip_main.dat data/catalogs/hipparcos.csv

The hip_main.dat fixed-width field layout (1-indexed columns):
    Field H1:  HIP number          bytes   9-14
    Field H5:  V magnitude (Vmag)  bytes  42-46
    Field H8:  RA in degrees       bytes  52-63
    Field H9:  Dec in degrees      bytes  65-76
    Field H37: B-V color index     bytes 246-251

Stars are filtered out if:
    - HIP number is missing or invalid
    - RA or Dec is missing (blank field)
    - Vmag is missing
    - B-V is missing

Stars with blank B-V get a default of 0.65 (solar-type, G2V) if --fill-bv is given.
By default, stars with missing B-V are skipped.
"""

import sys
import os
import argparse


def parse_float(s: str) -> float | None:
    """Parse a float from a fixed-width field. Returns None if blank or invalid."""
    stripped = s.strip()
    if not stripped:
        return None
    try:
        return float(stripped)
    except ValueError:
        return None


def parse_int(s: str) -> int | None:
    """Parse an int from a fixed-width field. Returns None if blank or invalid."""
    stripped = s.strip()
    if not stripped:
        return None
    try:
        return int(stripped)
    except ValueError:
        return None


def convert_hipparcos(input_path: str, output_path: str, fill_bv: bool = False) -> None:
    """
    Read hip_main.dat and write hipparcos.csv.

    Parameters
    ----------
    input_path : str
        Path to the raw hip_main.dat file.
    output_path : str
        Path to write the output CSV.
    fill_bv : bool
        If True, fill missing B-V with 0.65 instead of skipping.
    """
    if not os.path.isfile(input_path):
        print(f"ERROR: Input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    total_lines = 0
    written = 0
    skipped_no_hip = 0
    skipped_no_pos = 0
    skipped_no_mag = 0
    skipped_no_bv = 0
    filled_bv = 0

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    with open(input_path, "r", encoding="latin-1") as fin, \
         open(output_path, "w", encoding="utf-8") as fout:

        # Write CSV header
        fout.write("HIP,RA_deg,Dec_deg,Vmag,BV\n")

        for line in fin:
            total_lines += 1

            # Skip lines that are too short (comments, blank lines)
            if len(line) < 252:
                continue

            # -------------------------------------------------------
            # Extract fixed-width fields (1-indexed in docs, 0-indexed here)
            # H1:  HIP identifier          cols  9-14  → [8:14]
            # H5:  Vmag                     cols 42-46  → [41:46]
            # H8:  RA (degrees, J1991.25)   cols 52-63  → [51:63]
            # H9:  Dec (degrees, J1991.25)  cols 65-76  → [64:76]
            # H37: B-V color index          cols 246-251 → [245:251]
            # -------------------------------------------------------
            hip_id = parse_int(line[8:14])
            vmag = parse_float(line[41:46])
            ra_deg = parse_float(line[51:63])
            dec_deg = parse_float(line[64:76])
            bv = parse_float(line[245:251])

            # --- Validation ---

            if hip_id is None:
                skipped_no_hip += 1
                continue

            if ra_deg is None or dec_deg is None:
                skipped_no_pos += 1
                continue

            if vmag is None:
                skipped_no_mag += 1
                continue

            if bv is None:
                if fill_bv:
                    bv = 0.65
                    filled_bv += 1
                else:
                    skipped_no_bv += 1
                    continue

            # --- Write CSV row ---
            # RA and Dec in hip_main.dat are ICRS (essentially J2000) in degrees.
            # We output degrees; the C++ loader converts to radians.
            fout.write(f"{hip_id},{ra_deg:.8f},{dec_deg:.8f},{vmag:.4f},{bv:.4f}\n")
            written += 1

    # --- Summary ---
    print(f"Hipparcos Catalog Conversion Complete")
    print(f"  Input:           {input_path}")
    print(f"  Output:          {output_path}")
    print(f"  Total lines:     {total_lines}")
    print(f"  Stars written:   {written}")
    print(f"  Skipped (no HIP):  {skipped_no_hip}")
    print(f"  Skipped (no pos):  {skipped_no_pos}")
    print(f"  Skipped (no mag):  {skipped_no_mag}")
    print(f"  Skipped (no B-V):  {skipped_no_bv}")
    if fill_bv:
        print(f"  Filled B-V:        {filled_bv}")
    print()

    if written == 0:
        print("WARNING: No stars written! Check input file format.", file=sys.stderr)
        sys.exit(1)

    # Sanity check: Hipparcos should have ~118,218 entries
    if written < 100000:
        print(f"WARNING: Only {written} stars written. Expected ~118,218 for full Hipparcos.",
              file=sys.stderr)
    elif written > 120000:
        print(f"NOTE: {written} stars written (slightly above expected 118,218 — may include supplements).")
    else:
        print(f"OK: {written} stars — consistent with full Hipparcos catalog.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert Hipparcos hip_main.dat to Parallax CSV format.",
        epilog="Download hip_main.dat from: https://cdsarc.cds.unistra.fr/ftp/cats/I/239/hip_main.dat"
    )
    parser.add_argument(
        "input",
        help="Path to hip_main.dat (fixed-width format)"
    )
    parser.add_argument(
        "output",
        nargs="?",
        default="data/catalogs/hipparcos.csv",
        help="Output CSV path (default: data/catalogs/hipparcos.csv)"
    )
    parser.add_argument(
        "--fill-bv",
        action="store_true",
        help="Fill missing B-V with 0.65 (solar type) instead of skipping"
    )

    args = parser.parse_args()
    convert_hipparcos(args.input, args.output, fill_bv=args.fill_bv)


if __name__ == "__main__":
    main()