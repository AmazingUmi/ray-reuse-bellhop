#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
input_dir="$script_dir/inputs"
output_dir="$script_dir/output"

bellhop_bin=${1:-"$repo_dir/Bellhop_origin/bin/bellhop"}
requested_case=${2:-all}

if [ ! -x "$bellhop_bin" ]; then
    echo "Bellhop executable is missing or not executable: $bellhop_bin" >&2
    echo "Build it with: make -C Bellhop_origin" >&2
    exit 2
fi

case_names="constant_speed_direct munk_cerveny_cc"
if [ "$requested_case" != all ]; then
    case_names=$requested_case
fi

read_i32_at() {
    file=$1
    offset=$2
    od -An -t d4 -j "$offset" -N 4 "$file" | tr -d ' '
}

read_f64_at() {
    file=$1
    offset=$2
    od -An -t fD -j "$offset" -N 8 "$file" | tr -d ' '
}

validate_shd_header() {
    case_name=$1
    shd_file=$2

    recl_words=$(read_i32_at "$shd_file" 0)
    record_bytes=$((4 * recl_words))
    dimensions_offset=$((2 * record_bytes))

    nfreq=$(read_i32_at "$shd_file" "$dimensions_offset")
    ntheta=$(read_i32_at "$shd_file" $((dimensions_offset + 4)))
    nsz=$(read_i32_at "$shd_file" $((dimensions_offset + 16)))
    nrz=$(read_i32_at "$shd_file" $((dimensions_offset + 20)))
    nrr=$(read_i32_at "$shd_file" $((dimensions_offset + 24)))
    freq0=$(read_f64_at "$shd_file" $((dimensions_offset + 28)))

    case "$case_name" in
        constant_speed_direct)
            expected="1 1 1 21 51"
            ;;
        munk_cerveny_cc)
            expected="1 1 1 201 501"
            ;;
        *)
            echo "Unknown standard case: $case_name" >&2
            exit 2
            ;;
    esac

    actual="$nfreq $ntheta $nsz $nrz $nrr"
    if [ "$actual" != "$expected" ]; then
        echo "$case_name: unexpected SHD dimensions: $actual (expected $expected)" >&2
        exit 1
    fi

    if ! awk -v value="$freq0" 'BEGIN { exit !(value > 49.999 && value < 50.001) }'; then
        echo "$case_name: unexpected SHD center frequency: $freq0" >&2
        exit 1
    fi
}

mkdir -p "$output_dir"

for case_name in $case_names; do
    input_file="$input_dir/$case_name.env"
    case_dir="$output_dir/$case_name"

    if [ ! -f "$input_file" ]; then
        echo "Unknown standard case or missing input: $case_name" >&2
        exit 2
    fi

    mkdir -p "$case_dir"
    cp "$input_file" "$case_dir/$case_name.env"

    echo "Running $case_name"
    (
        cd "$case_dir"
        "$bellhop_bin" "$case_name"
    )

    prt_file="$case_dir/$case_name.prt"
    shd_file="$case_dir/$case_name.shd"

    test -s "$prt_file"
    test -s "$shd_file"
    grep -q "Coherent TL calculation" "$prt_file"
    grep -q "Cartesian beams" "$prt_file"
    grep -q "Rectilinear receiver grid" "$prt_file"
    if grep -q "FATAL ERROR" "$prt_file"; then
        echo "$case_name: Bellhop reported a fatal error" >&2
        exit 1
    fi

    validate_shd_header "$case_name" "$shd_file"
    echo "$case_name: PASS"
done
