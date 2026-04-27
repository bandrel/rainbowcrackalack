#!/usr/bin/env bash
#
# nvme_pipeline.sh — Unified NVMe staging pipeline for rainbow table operations
#
# Subcommands:
#   sort     — Copy .rt tables to NVMe, sort with rtsort, write back
#   compact  — Compress sorted .rt → .rtc via rt2rtc with double-buffering
#   lookup   — Run crackalack_lookup against hash file with double-buffer staging
#   all      — Run sort → compact → lookup in sequence
#   status   — Show per-stage progress and cracked counts
#
# Config resolution order (every setting):
#   1. CLI flag
#   2. Environment variable (RC_* prefix)
#   3. scripts/pipeline.conf (shell-sourced if present)
#   4. Hardcoded default
#
# Requires bash 4+ (associative arrays). Linux only.
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Locate script and project directories
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# ---------------------------------------------------------------------------
# Defaults — overridable via config / env / CLI
# ---------------------------------------------------------------------------
RC_RTSORT="${RC_RTSORT:-}"
RC_RT2RTC="${RC_RT2RTC:-}"
RC_LOOKUP="${RC_LOOKUP:-}"
RC_NVME_ROOT="${RC_NVME_ROOT:-/mnt/nvme}"
RC_RT_SOURCE="${RC_RT_SOURCE:-}"
RC_RTC_DEST="${RC_RTC_DEST:-}"
RC_SORT_BATCH_SIZE="${RC_SORT_BATCH_SIZE:-50}"
RC_COMPACT_QUEUE_MAX="${RC_COMPACT_QUEUE_MAX:-2}"
RC_LOOKUP_BATCH_SIZE="${RC_LOOKUP_BATCH_SIZE:-115}"
RC_STATE_DIR="${RC_STATE_DIR:-}"
RC_RTC_DEST_OVERFLOW="${RC_RTC_DEST_OVERFLOW:-}"
RC_RTC_DEST_OVERFLOW_2="${RC_RTC_DEST_OVERFLOW_2:-}"
RC_OVERFLOW_MIN_GB="${RC_OVERFLOW_MIN_GB:-400}"
RC_RT2RTC_FLAGS="${RC_RT2RTC_FLAGS:--s 32 -e 48 -c 512 -p}"
RC_COMPACT_TORRENT_DIRS="${RC_COMPACT_TORRENT_DIRS:-}"

# ---------------------------------------------------------------------------
# Shared utilities
# ---------------------------------------------------------------------------
log()  { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"; }
die()  { echo "ERROR: $*" >&2; exit 1; }

# eta_str ELAPSED DONE TOTAL — prints "Xh Ym" ETA remaining
eta_str() {
    local elapsed=$1 done=$2 total=$3
    if (( done <= 0 )); then
        echo "unknown"
        return
    fi
    local remaining=$(( total - done ))
    local eta_secs=$(( remaining * elapsed / done ))
    echo "${eta_secs}s ($(( eta_secs / 3600 ))h $(( (eta_secs % 3600) / 60 ))m)"
}

# nvme_free_gb DIR — df free GB on mount containing DIR
nvme_free_gb() {
    local dir="$1"
    df --output=avail "$dir" 2>/dev/null | tail -1 | tr -d ' ' | awk '{print int($1/1048576)}' || echo 0
}

# clean_stage DIR — remove all .rt and .rtc files from DIR
clean_stage() {
    local dir="$1"
    rm -f "$dir"/*.rt "$dir"/*.rtc 2>/dev/null || true
}

# load_done DONE_FILE — loads file into declare -gA DONE assoc array
load_done() {
    local done_file="$1"
    declare -gA DONE=()
    if [[ -f "$done_file" ]]; then
        while IFS= read -r line; do
            [[ -n "$line" ]] && DONE["$line"]=1
        done < "$done_file"
    fi
}

# mark_done DONE_FILE FILENAME — flock-protected append
mark_done() {
    local done_file="$1"
    local filename="$2"
    (
        flock 200
        echo "$filename" >> "$done_file"
    ) 200>"${done_file}.lock"
}

# is_nested_layout DIR — returns 0 (true) if DIR contains subdirectories with .rtc files
is_nested_layout() {
    local dir="$1"
    local found
    found=$(find "$dir" -mindepth 2 -maxdepth 2 -name "*.rtc" -type f 2>/dev/null | head -1)
    [[ -n "$found" ]]
}

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage_main() {
    cat <<'EOF'
Usage: nvme_pipeline.sh SUBCOMMAND [FLAGS]

Subcommands:
  sort     [flags]               Sort .rt tables via NVMe staging
  compact  [flags]               Compress .rt → .rtc via NVMe staging
  lookup   --hashes FILE [flags] Run crackalack_lookup with NVMe staging
  all      --hashes FILE [flags] Run sort + compact + lookup in sequence
  status   [--state-dir DIR]     Show per-stage progress

Common flags (all subcommands):
  --source DIR       Directory containing .rt files (RC_RT_SOURCE)
  --dest DIR         Destination for .rtc files (RC_RTC_DEST)
  --nvme DIR         NVMe mount point (RC_NVME_ROOT, default: /mnt/nvme)
  --state-dir DIR    State directory (RC_STATE_DIR)
  --batch-size N     Tables per batch (stage-specific default)
  --start-index N    First table index (default: 0)
  --end-index N      Last table index, inclusive (default: all)
  --dry-run          Print actions without executing
  --config FILE      Source config file (default: scripts/pipeline.conf)
  --continue-on-error  (all subcommand only) Continue past stage failures

Environment variables (RC_*) override pipeline.conf but lose to CLI flags.
EOF
    exit 1
}

usage_sort() {
    cat <<'EOF'
Usage: nvme_pipeline.sh sort [FLAGS]

Sort .rt tables via NVMe staging (NFS → NVMe → rtsort → NFS).

Required (via config or flag):
  --source DIR       .rt source directory
  RC_RTSORT          Path to rtsort binary (required)

Optional:
  --nvme DIR         NVMe root (default: /mnt/nvme)
  --state-dir DIR    State directory
  --batch-size N     Tables per batch (default: 50)
  --start-index N    First table index (default: 0)
  --end-index N      Last table index (default: all)
  --dry-run
  --config FILE
EOF
    exit 1
}

usage_compact() {
    cat <<'EOF'
Usage: nvme_pipeline.sh compact [FLAGS]

Compress .rt → .rtc via double-buffered NVMe staging.

Required (via config or flag):
  --source DIR       Sorted .rt source directory
  --dest DIR         .rtc output directory
  RC_RT2RTC          Path to rt2rtc binary (required)

Optional:
  --nvme DIR         NVMe root (default: /mnt/nvme)
  --state-dir DIR    State directory
  --start-index N    First table index (default: 0)
  --end-index N      Last table index (default: all)
  --torrent-dirs DIRS  Colon-separated dirs with complete torrent .rtc subdirs to skip (RC_COMPACT_TORRENT_DIRS)
  --dry-run
  --config FILE
EOF
    exit 1
}

usage_lookup() {
    cat <<'EOF'
Usage: nvme_pipeline.sh lookup --hashes FILE [FLAGS]

Run crackalack_lookup with double-buffered NVMe staging.

Required:
  --hashes FILE      Hash file to crack (pwdump format)
  --dest DIR         Source of .rtc (or .rt) tables (RC_RTC_DEST)

Optional:
  --nvme DIR         NVMe root (default: /mnt/nvme)
  --state-dir DIR    State directory
  --batch-size N     Tables per batch (default: 115)
  --start-index N    First table index (default: 0)
  --end-index N      Last table index (default: all)
  --dry-run
  --config FILE
EOF
    exit 1
}

usage_all() {
    cat <<'EOF'
Usage: nvme_pipeline.sh all --hashes FILE [FLAGS]

Run sort → compact → lookup in sequence.

Required:
  --hashes FILE      Hash file to crack
  --source DIR       .rt source directory
  --dest DIR         .rtc output directory

Optional:
  --nvme DIR         NVMe root (default: /mnt/nvme)
  --state-dir DIR    State directory
  --dry-run
  --continue-on-error  Continue past stage failures
  --config FILE
EOF
    exit 1
}

# ---------------------------------------------------------------------------
# Config loading
# ---------------------------------------------------------------------------
load_config() {
    local config_file="$1"
    if [[ -f "$config_file" ]]; then
        # shellcheck source=/dev/null
        source "$config_file"
        log "Loaded config: $config_file"
    fi
}

# ---------------------------------------------------------------------------
# Resolve effective settings after CLI parsing
# ---------------------------------------------------------------------------
resolve_settings() {
    # Apply state dir default (no mkdir here — each command does it after CLI overrides)
    if [[ -z "$RC_STATE_DIR" ]]; then
        RC_STATE_DIR="$RC_NVME_ROOT/.pipeline-state"
    fi

    # Auto-detect lookup binary
    if [[ -z "$RC_LOOKUP" ]]; then
        if [[ -x "$PROJECT_DIR/crackalack_lookup" ]]; then
            RC_LOOKUP="$PROJECT_DIR/crackalack_lookup"
        elif command -v crackalack_lookup &>/dev/null; then
            RC_LOOKUP="$(command -v crackalack_lookup)"
        fi
    fi
}

# ---------------------------------------------------------------------------
# cmd_sort
# ---------------------------------------------------------------------------
cmd_sort() {
    local source="" nvme="" state_dir="" batch_size="" start_index=0 end_index=-1
    local dry_run=0 config_file="$SCRIPT_DIR/pipeline.conf"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --source)      source="$2";      shift 2 ;;
            --nvme)        nvme="$2";        shift 2 ;;
            --state-dir)   state_dir="$2";   shift 2 ;;
            --batch-size)  batch_size="$2";  shift 2 ;;
            --start-index) start_index="$2"; shift 2 ;;
            --end-index)   end_index="$2";   shift 2 ;;
            --dry-run)     dry_run=1;        shift ;;
            --config)      config_file="$2"; shift 2 ;;
            --help|-h)     usage_sort ;;
            *)             die "Unknown flag: $1" ;;
        esac
    done

    load_config "$config_file"
    resolve_settings

    # CLI overrides env/config
    [[ -n "$source"     ]] && RC_RT_SOURCE="$source"
    [[ -n "$nvme"       ]] && RC_NVME_ROOT="$nvme"
    [[ -n "$state_dir"  ]] && RC_STATE_DIR="$state_dir"
    [[ -n "$batch_size" ]] && RC_SORT_BATCH_SIZE="$batch_size"

    # After CLI overrides, re-apply state dir default if it was based on nvme
    [[ -z "$RC_STATE_DIR" ]] && RC_STATE_DIR="$RC_NVME_ROOT/.pipeline-state"
    mkdir -p "$RC_STATE_DIR"

    [[ -z "$RC_RT_SOURCE" ]] && die "--source (or RC_RT_SOURCE) is required for sort"
    [[ -d "$RC_RT_SOURCE" ]] || die "Source directory not found: $RC_RT_SOURCE"
    [[ -d "$RC_NVME_ROOT" ]] || die "NVMe mount not found: $RC_NVME_ROOT"
    [[ -n "$RC_RTSORT"    ]] || die "RC_RTSORT is required for sort (set via config or environment)"
    [[ -x "$RC_RTSORT"    ]] || die "rtsort binary not found or not executable: $RC_RTSORT"

    local sort_done="$RC_STATE_DIR/sort.done"
    local sort_log="$RC_STATE_DIR/sort.log"
    local stage_dir="$RC_NVME_ROOT/rt_stage_sort"
    mkdir -p "$stage_dir"

    {
        log "=== sort stage ==="
        log "Source:     $RC_RT_SOURCE"
        log "rtsort:     $RC_RTSORT"
        log "Stage:      $stage_dir"
        log "Batch size: $RC_SORT_BATCH_SIZE"
        log "State dir:  $RC_STATE_DIR"

        # Load done set
        load_done "$sort_done"

        # Build pending list
        log "Scanning for pending tables in: $RC_RT_SOURCE"
        local all_tables=()
        mapfile -t all_tables < <(find "$RC_RT_SOURCE" -maxdepth 1 -name '*.rt' -printf '%f\n' | sort)

        local pending=()
        for t in "${all_tables[@]}"; do
            [[ -z "${DONE[$t]+x}" ]] && pending+=("$t")
        done

        local num_pending=${#pending[@]}
        if [[ $num_pending -eq 0 ]]; then
            log "sort: nothing to do (all tables in sort.done)"
            return 0
        fi

        log "Found $num_pending pending tables (${#all_tables[@]} total, $((${#all_tables[@]} - num_pending)) already done)"

        # Apply index range
        if [[ $end_index -eq -1 ]]; then
            end_index=$((num_pending - 1))
        fi
        [[ $start_index -gt $end_index ]] && die "Invalid range: $start_index-$end_index"
        [[ $end_index -ge $num_pending  ]] && end_index=$((num_pending - 1))

        local tables=("${pending[@]:$start_index:$((end_index - start_index + 1))}")
        local num_tables=${#tables[@]}
        local num_batches=$(( (num_tables + RC_SORT_BATCH_SIZE - 1) / RC_SORT_BATCH_SIZE ))

        log "Will sort $num_tables tables in $num_batches batches of $RC_SORT_BATCH_SIZE"

        if [[ $dry_run -eq 1 ]]; then
            log "=== DRY RUN ==="
            for ((b=0; b<num_batches; b++)); do
                local bstart=$((b * RC_SORT_BATCH_SIZE))
                local bend=$((bstart + RC_SORT_BATCH_SIZE - 1))
                [[ $bend -ge $num_tables ]] && bend=$((num_tables - 1))
                log "Batch $((b+1))/$num_batches: ${tables[$bstart]}..${tables[$bend]} ($((bend - bstart + 1)) tables)"
            done
            return 0
        fi

        local total_start sorted_count=0
        total_start=$(date +%s)

        for ((b=0; b<num_batches; b++)); do
            local bstart=$((b * RC_SORT_BATCH_SIZE))
            local bend=$((bstart + RC_SORT_BATCH_SIZE - 1))
            [[ $bend -ge $num_tables ]] && bend=$((num_tables - 1))
            local bcount=$((bend - bstart + 1))

            log "=== Batch $((b+1))/$num_batches ($bcount tables) ==="

            clean_stage "$stage_dir"

            # Copy batch to NVMe
            log "Copying $bcount tables to NVMe..."
            local copy_start
            copy_start=$(date +%s)
            for ((i=bstart; i<=bend; i++)); do
                rsync -a --inplace "$RC_RT_SOURCE/${tables[$i]}" "$stage_dir/"
                if (( (i - bstart + 1) % 10 == 0 )); then
                    log "  Copied $((i - bstart + 1))/$bcount..."
                fi
            done
            log "Copy to NVMe: $(( $(date +%s) - copy_start ))s"

            # Sort each table in-place on NVMe
            log "Sorting $bcount tables on NVMe..."
            local sort_start
            sort_start=$(date +%s)
            for ((i=bstart; i<=bend; i++)); do
                "$RC_RTSORT" "$stage_dir/${tables[$i]}" 2>&1 | grep -v "^$" || true
                if (( (i - bstart + 1) % 10 == 0 )); then
                    log "  Sorted $((i - bstart + 1))/$bcount..."
                fi
            done
            log "Sort complete: $(( $(date +%s) - sort_start ))s"

            # Write sorted tables back to source
            log "Writing sorted tables back to source..."
            local write_start
            write_start=$(date +%s)
            for ((i=bstart; i<=bend; i++)); do
                rsync -a --inplace "$stage_dir/${tables[$i]}" "$RC_RT_SOURCE/${tables[$i]}.sorted"
                mv "$RC_RT_SOURCE/${tables[$i]}.sorted" "$RC_RT_SOURCE/${tables[$i]}"
                mark_done "$sort_done" "${tables[$i]}"
                ((sorted_count++)) || true
                if (( (i - bstart + 1) % 10 == 0 )); then
                    log "  Written $((i - bstart + 1))/$bcount..."
                fi
            done
            log "Write back: $(( $(date +%s) - write_start ))s"

            local elapsed=$(( $(date +%s) - total_start ))
            local eta
            eta=$(eta_str "$elapsed" "$sorted_count" "$num_tables")
            local rate=$(( sorted_count * 3600 / (elapsed > 0 ? elapsed : 1) ))
            log "Progress: $sorted_count/$num_tables sorted (~${rate}/hr, ETA ~$eta)"

            clean_stage "$stage_dir"
        done

        local duration=$(( $(date +%s) - total_start ))
        log "=========================================="
        log "Sort complete!"
        log "Tables sorted: $sorted_count"
        log "Total time:    $(( duration / 3600 ))h $(( (duration % 3600) / 60 ))m"
        log "=========================================="
    } 2>&1 | tee -a "$sort_log"
}

# ---------------------------------------------------------------------------
# cmd_compact
# ---------------------------------------------------------------------------
cmd_compact() {
    local source="" dest="" nvme="" state_dir="" start_index=0 end_index=-1
    local dry_run=0 config_file="$SCRIPT_DIR/pipeline.conf"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --source)         source="$2";                    shift 2 ;;
            --dest)           dest="$2";                      shift 2 ;;
            --nvme)           nvme="$2";                      shift 2 ;;
            --state-dir)      state_dir="$2";                 shift 2 ;;
            --start-index)    start_index="$2";               shift 2 ;;
            --end-index)      end_index="$2";                 shift 2 ;;
            --torrent-dirs)   RC_COMPACT_TORRENT_DIRS="$2";   shift 2 ;;
            --dry-run)        dry_run=1;                      shift ;;
            --config)         config_file="$2";               shift 2 ;;
            --help|-h)        usage_compact ;;
            *)                die "Unknown flag: $1" ;;
        esac
    done

    load_config "$config_file"
    resolve_settings

    [[ -n "$source"    ]] && RC_RT_SOURCE="$source"
    [[ -n "$dest"      ]] && RC_RTC_DEST="$dest"
    [[ -n "$nvme"      ]] && RC_NVME_ROOT="$nvme"
    [[ -n "$state_dir" ]] && RC_STATE_DIR="$state_dir"

    [[ -z "$RC_STATE_DIR" ]] && RC_STATE_DIR="$RC_NVME_ROOT/.pipeline-state"
    mkdir -p "$RC_STATE_DIR"

    [[ -z "$RC_RT_SOURCE" ]] && die "--source (or RC_RT_SOURCE) is required for compact"
    [[ -z "$RC_RTC_DEST"  ]] && die "--dest (or RC_RTC_DEST) is required for compact"
    [[ -d "$RC_RT_SOURCE" ]] || die "Source directory not found: $RC_RT_SOURCE"
    [[ -d "$RC_NVME_ROOT" ]] || die "NVMe mount not found: $RC_NVME_ROOT"
    [[ -n "$RC_RT2RTC"    ]] || die "RC_RT2RTC is required for compact (set via config or environment)"
    [[ -x "$RC_RT2RTC"    ]] || die "rt2rtc binary not found or not executable: $RC_RT2RTC"
    mkdir -p "$RC_RTC_DEST"

    local compact_done="$RC_STATE_DIR/compact.done"
    local compact_log="$RC_STATE_DIR/compact.log"
    local stage_a="$RC_NVME_ROOT/rt_compact_a"
    local stage_b="$RC_NVME_ROOT/rt_compact_b"
    mkdir -p "$stage_a" "$stage_b"

    {
        log "=== compact stage ==="
        log "Source:   $RC_RT_SOURCE"
        log "Dest:     $RC_RTC_DEST"
        log "rt2rtc:   $RC_RT2RTC"
        log "StageA:   $stage_a"
        log "StageB:   $stage_b"
        log "State:    $RC_STATE_DIR"
        log "rt2rtc flags: $RC_RT2RTC_FLAGS"
        [[ -n "$RC_COMPACT_TORRENT_DIRS" ]] && log "Torrent dirs: $RC_COMPACT_TORRENT_DIRS"

        load_done "$compact_done"

        # Mark tables as done if their index already has .rtc files in torrent dirs
        if [[ -n "$RC_COMPACT_TORRENT_DIRS" ]]; then
            log "Scanning torrent dirs for already-downloaded tables..."
            local torrent_marked=0
            IFS=: read -ra torrent_dirs <<< "$RC_COMPACT_TORRENT_DIRS"
            for tdir in "${torrent_dirs[@]}"; do
                [[ -d "$tdir" ]] || continue
                while IFS= read -r idx_dir; do
                    local idx
                    idx=$(basename "$idx_dir")
                    # Find the matching .rt filename for this index
                    for rt_candidate in "$RC_RT_SOURCE"/*_"${idx}".rt; do
                        [[ -f "$rt_candidate" ]] || continue
                        local rt_name
                        rt_name=$(basename "$rt_candidate")
                        if [[ -z "${DONE[$rt_name]+x}" ]]; then
                            # Check that the torrent dir has at least one .rtc file
                            local rtc_count
                            rtc_count=$(find "$idx_dir" -maxdepth 1 -name '*.rtc' -type f 2>/dev/null | wc -l)
                            if (( rtc_count >= 4 )); then
                                mark_done "$compact_done" "$rt_name"
                                DONE["$rt_name"]=1
                                ((torrent_marked++)) || true
                            fi
                        fi
                    done
                done < <(find "$tdir" -maxdepth 1 -mindepth 1 -type d)
            done
            (( torrent_marked > 0 )) && log "Marked $torrent_marked tables done from torrent dirs"
        fi

        log "Scanning for pending tables in: $RC_RT_SOURCE"
        local all_tables=()
        mapfile -t all_tables < <(find "$RC_RT_SOURCE" -maxdepth 1 -name '*.rt' -printf '%f\n' | sort -r)

        local pending=()
        for t in "${all_tables[@]}"; do
            [[ -z "${DONE[$t]+x}" ]] && pending+=("$t")
        done

        local num_pending=${#pending[@]}
        if [[ $num_pending -eq 0 ]]; then
            log "compact: nothing to do (all tables in compact.done)"
            return 0
        fi

        log "$num_pending tables pending (${#all_tables[@]} total, $((${#all_tables[@]} - num_pending)) already done)"

        if [[ $end_index -eq -1 ]]; then
            end_index=$((num_pending - 1))
        fi
        [[ $start_index -gt $end_index ]] && die "Invalid range: $start_index-$end_index"
        [[ $end_index -ge $num_pending  ]] && end_index=$((num_pending - 1))

        local tables=("${pending[@]:$start_index:$((end_index - start_index + 1))}")
        local num_tables=${#tables[@]}

        log "Processing $num_tables tables (index $start_index-$end_index)"

        if [[ $dry_run -eq 1 ]]; then
            log "=== DRY RUN ==="
            log "Would compress ${tables[0]} .. ${tables[$((num_tables-1))]}"
            log "Pipeline: copy next to NVMe while compressing current"
            log "Staging dirs: $stage_a, $stage_b"
            return 0
        fi

        local total_start compressed=0 copy_pid=""
        total_start=$(date +%s)
        local cur_stage="$stage_a"
        local next_stage="$stage_b"

        clean_stage "$stage_a"
        clean_stage "$stage_b"

        # Pre-copy first table synchronously
        log "[1/$num_tables] Copying ${tables[0]} to NVMe..."
        rsync -a --inplace "$RC_RT_SOURCE/${tables[0]}" "$cur_stage/"

        for ((i=0; i<num_tables; i++)); do
            local table="${tables[$i]}"

            # Background copy of next table
            if (( i + 1 < num_tables )); then
                local next_table="${tables[$((i+1))]}"
                rsync -a --inplace "$RC_RT_SOURCE/$next_table" "$next_stage/" &
                copy_pid=$!
            else
                copy_pid=""
            fi

            # Compress current table on NVMe
            log "[$((i+1))/$num_tables] Compressing $table..."
            local comp_start
            comp_start=$(date +%s)
            # shellcheck disable=SC2086
            (cd "$cur_stage" && "$RC_RT2RTC" "$cur_stage/" $RC_RT2RTC_FLAGS) 2>&1 | tail -2

            local comp_secs=$(( $(date +%s) - comp_start ))

            # Extract table index from filename (last _NUM before .rt)
            local table_index="${table%.rt}"
            table_index="${table_index##*_}"

            # Choose destination — waterfall: primary → overflow → overflow2
            local write_dest="$RC_RTC_DEST"
            local free_gb
            free_gb=$(nvme_free_gb "$RC_RTC_DEST")
            if (( free_gb < RC_OVERFLOW_MIN_GB )) && [[ -n "$RC_RTC_DEST_OVERFLOW" ]]; then
                local free_gb2
                free_gb2=$(nvme_free_gb "$RC_RTC_DEST_OVERFLOW" 2>/dev/null || echo 0)
                if (( free_gb2 >= RC_OVERFLOW_MIN_GB )) || [[ -z "$RC_RTC_DEST_OVERFLOW_2" ]]; then
                    write_dest="$RC_RTC_DEST_OVERFLOW"
                    mkdir -p "$write_dest"
                    log "  Primary <${RC_OVERFLOW_MIN_GB}GB free (${free_gb}GB), spilling to overflow: $write_dest"
                else
                    write_dest="$RC_RTC_DEST_OVERFLOW_2"
                    mkdir -p "$write_dest"
                    log "  Primary (${free_gb}GB) and overflow (${free_gb2}GB) both <${RC_OVERFLOW_MIN_GB}GB free, spilling to overflow2: $write_dest"
                fi
            fi

            # Move .rtc files to destination subdir (one per table index)
            local dest_subdir="$write_dest/$table_index"
            mkdir -p "$dest_subdir"
            local rtc_found=0
            for rtc_file in "$cur_stage"/*.rtc; do
                [[ -f "$rtc_file" ]] || continue
                rsync -a --inplace "$rtc_file" "$dest_subdir/"
                rtc_found=1
            done

            if [[ $rtc_found -eq 1 ]]; then
                mark_done "$compact_done" "$table"
                ((compressed++)) || true
            else
                log "  WARNING: no .rtc created for $table!"
            fi

            clean_stage "$cur_stage"

            # Wait for background copy
            if [[ -n "$copy_pid" ]]; then
                wait "$copy_pid" || die "Failed to copy next table"
                copy_pid=""
            fi

            local elapsed=$(( $(date +%s) - total_start ))
            local eta
            eta=$(eta_str "$elapsed" "$compressed" "$num_tables")
            log "  Compressed in ${comp_secs}s | $compressed/$num_tables done | ETA: $eta"

            # Swap buffers
            local tmp="$cur_stage"
            cur_stage="$next_stage"
            next_stage="$tmp"
        done

        local duration=$(( $(date +%s) - total_start ))
        log "=========================================="
        log "Compression complete!"
        log "Tables compressed: $compressed"
        log "Destination:       $RC_RTC_DEST"
        log "Total time:        $(( duration / 3600 ))h $(( (duration % 3600) / 60 ))m"
        log "=========================================="
    } 2>&1 | tee -a "$compact_log"
}

# ---------------------------------------------------------------------------
# cmd_lookup
# ---------------------------------------------------------------------------
cmd_lookup() {
    local dest="" nvme="" state_dir="" batch_size="" start_index=0 end_index=-1
    local hashes="" dry_run=0 config_file="$SCRIPT_DIR/pipeline.conf"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --hashes)      hashes="$2";      shift 2 ;;
            --dest)        dest="$2";        shift 2 ;;
            --nvme)        nvme="$2";        shift 2 ;;
            --state-dir)   state_dir="$2";   shift 2 ;;
            --batch-size)  batch_size="$2";  shift 2 ;;
            --start-index) start_index="$2"; shift 2 ;;
            --end-index)   end_index="$2";   shift 2 ;;
            --source)      shift 2 ;;
            --dry-run)     dry_run=1;        shift ;;
            --config)      config_file="$2"; shift 2 ;;
            --help|-h)     usage_lookup ;;
            *)             die "Unknown flag: $1" ;;
        esac
    done

    load_config "$config_file"
    resolve_settings

    [[ -n "$dest"       ]] && RC_RTC_DEST="$dest"
    [[ -n "$nvme"       ]] && RC_NVME_ROOT="$nvme"
    [[ -n "$state_dir"  ]] && RC_STATE_DIR="$state_dir"
    [[ -n "$batch_size" ]] && RC_LOOKUP_BATCH_SIZE="$batch_size"

    [[ -z "$RC_STATE_DIR" ]] && RC_STATE_DIR="$RC_NVME_ROOT/.pipeline-state"
    mkdir -p "$RC_STATE_DIR"

    [[ -z "$hashes"      ]] && die "--hashes FILE is required for lookup"
    [[ -f "$hashes"      ]] || die "Hash file not found: $hashes"
    [[ -z "$RC_RTC_DEST" ]] && die "--dest (or RC_RTC_DEST) is required for lookup"
    [[ -d "$RC_RTC_DEST" ]] || die "Table directory not found: $RC_RTC_DEST"
    [[ -d "$RC_NVME_ROOT" ]] || die "NVMe mount not found: $RC_NVME_ROOT"
    [[ -n "$RC_LOOKUP"   ]] || die "Cannot find crackalack_lookup. Set RC_LOOKUP or ensure it is in PATH."
    [[ -x "$RC_LOOKUP"   ]] || die "crackalack_lookup not executable: $RC_LOOKUP"

    # Derive hashkey
    local hashes_real
    hashes_real="$(realpath "$hashes")"
    local hashkey
    hashkey="$(basename "$hashes").$(sha256sum "$hashes_real" | cut -c1-12)"

    local lookup_done="$RC_STATE_DIR/lookup.${hashkey}.done"
    local lookup_log="$RC_STATE_DIR/lookup.log"
    local cracked_file="$RC_STATE_DIR/cracked.${hashkey}.txt"
    local stage_a="$RC_NVME_ROOT/rt_stage_a"
    local stage_b="$RC_NVME_ROOT/rt_stage_b"
    mkdir -p "$stage_a" "$stage_b"

    {
        log "=== lookup stage ==="
        log "Hashes:     $hashes"
        log "Hashkey:    $hashkey"
        log "Table src:  $RC_RTC_DEST"
        log "Lookup:     $RC_LOOKUP"
        log "Batch size: $RC_LOOKUP_BATCH_SIZE"
        log "Cracked:    $cracked_file"
        log "State:      $RC_STATE_DIR"

        # Collect tables from primary dest and overflow (if set), dedup by table key
        local table_ext="rtc"
        local nested_mode=0
        local -A seen_table=()
        local all_tables=()
        local all_table_dirs=()
        for scan_dir in "$RC_RTC_DEST" ${RC_RTC_DEST_OVERFLOW:+"$RC_RTC_DEST_OVERFLOW"} ${RC_RTC_DEST_OVERFLOW_2:+"$RC_RTC_DEST_OVERFLOW_2"}; do
            [[ -d "$scan_dir" ]] || continue
            if is_nested_layout "$scan_dir"; then
                nested_mode=1
                local subdir_count
                subdir_count=$(find "$scan_dir" -mindepth 1 -maxdepth 1 -type d | wc -l)
                log "Scanning $scan_dir (nested): $subdir_count index subdirs"
                while IFS= read -r idx; do
                    if [[ -z "${seen_table[$idx]+x}" ]]; then
                        seen_table["$idx"]=1
                        all_tables+=("$idx")
                        all_table_dirs+=("$scan_dir")
                    fi
                done < <(find "$scan_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -n)
            else
                local rtc_count rt_count
                rtc_count=$(find "$scan_dir" -maxdepth 1 -name '*.rtc' | wc -l)
                rt_count=$(find  "$scan_dir" -maxdepth 1 -name '*.rt'  | wc -l)
                local ext=""
                if   [[ $rtc_count -gt 0 ]]; then ext="rtc"
                elif [[ $rt_count  -gt 0 ]]; then ext="rt"
                else continue
                fi
                log "Scanning $scan_dir (flat): $rtc_count .rtc, $rt_count .rt"
                while IFS= read -r fname; do
                    if [[ -z "${seen_table[$fname]+x}" ]]; then
                        seen_table["$fname"]=1
                        all_tables+=("$fname")
                        all_table_dirs+=("$scan_dir")
                    fi
                done < <(find "$scan_dir" -maxdepth 1 -name "*.${ext}" -printf '%f\n' | sort)
            fi
        done

        [[ ${#all_tables[@]} -eq 0 ]] && die "No .rt or .rtc files found in $RC_RTC_DEST${RC_RTC_DEST_OVERFLOW:+ or $RC_RTC_DEST_OVERFLOW}${RC_RTC_DEST_OVERFLOW_2:+ or $RC_RTC_DEST_OVERFLOW_2}"
        log "Layout: $([ $nested_mode -eq 1 ] && echo 'nested (index subdirs)' || echo 'flat')"

        load_done "$lookup_done"

        local pending=()
        local pending_dirs=()
        for i in "${!all_tables[@]}"; do
            local t="${all_tables[$i]}"
            if [[ -z "${DONE[$t]+x}" ]]; then
                pending+=("$t")
                pending_dirs+=("${all_table_dirs[$i]}")
            fi
        done

        local num_pending=${#pending[@]}
        if [[ $num_pending -eq 0 ]]; then
            log "lookup: nothing to do (all tables in $lookup_done)"
            return 0
        fi

        log "$num_pending tables pending (${#all_tables[@]} total, $((${#all_tables[@]} - num_pending)) already done)"

        if [[ $end_index -eq -1 ]]; then
            end_index=$((num_pending - 1))
        fi
        [[ $start_index -gt $end_index ]] && die "Invalid range: $start_index-$end_index"
        [[ $end_index -ge $num_pending  ]] && end_index=$((num_pending - 1))

        local tables=("${pending[@]:$start_index:$((end_index - start_index + 1))}")
        local table_dirs=("${pending_dirs[@]:$start_index:$((end_index - start_index + 1))}")
        local num_tables=${#tables[@]}
        local num_batches=$(( (num_tables + RC_LOOKUP_BATCH_SIZE - 1) / RC_LOOKUP_BATCH_SIZE ))

        log "Processing $num_tables tables in $num_batches batches of $RC_LOOKUP_BATCH_SIZE"

        # NVMe space check
        local nvme_free_gb
        nvme_free_gb=$(nvme_free_gb "$RC_NVME_ROOT")
        log "NVMe free: ~${nvme_free_gb}GB"

        if [[ $dry_run -eq 1 ]]; then
            log "=== DRY RUN ==="
            for ((b=0; b<num_batches; b++)); do
                local bs=$((b * RC_LOOKUP_BATCH_SIZE))
                local be=$((bs + RC_LOOKUP_BATCH_SIZE - 1))
                [[ $be -ge $num_tables ]] && be=$((num_tables - 1))
                local bc=$((be - bs + 1))
                local stage
                (( b % 2 == 0 )) && stage="$stage_a" || stage="$stage_b"
                log "Batch $((b+1))/$num_batches: ${tables[$bs]}..${tables[$be]} ($bc files) -> $stage"
                log "  Then: $RC_LOOKUP $stage $hashes"
            done
            return 0
        fi

        local pipeline_start total_cracked_file copy_pid=""
        pipeline_start=$(date +%s)
        total_cracked_file="$(mktemp)"
        echo 0 > "$total_cracked_file"

        copy_batch_to_stage() {
            local batch_num=$1
            local stage_dir=$2
            local bs=$((batch_num * RC_LOOKUP_BATCH_SIZE))
            local be=$((bs + RC_LOOKUP_BATCH_SIZE - 1))
            [[ $be -ge $num_tables ]] && be=$((num_tables - 1))
            local bc=$((be - bs + 1))

            log "Copying batch $((batch_num+1))/$num_batches ($bc tables) to $stage_dir"
            clean_stage "$stage_dir"

            local copied=0 failed=0
            for ((j=bs; j<=be; j++)); do
                local src_base="${table_dirs[$j]}"
                local key="${tables[$j]}"
                local ok=0

                if [[ $nested_mode -eq 1 ]]; then
                    local src_subdir="$src_base/$key"
                    if rsync -a "$src_subdir/"*.rtc "$stage_dir/" 2>/dev/null; then
                        ok=1
                    else
                        log "  WARNING: failed to copy from $src_subdir/"
                    fi
                else
                    if rsync -a --inplace "$src_base/$key" "$stage_dir/"; then
                        ok=1
                    else
                        log "  WARNING: failed to copy $key"
                    fi
                fi

                if (( ok )); then
                    ((copied++)) || true
                else
                    ((failed++)) || true
                fi

                if (( copied % 10 == 0 && copied > 0 )); then
                    log "  Copied $copied/$bc tables..."
                fi
            done
            log "Batch $((batch_num+1)) copy done: $copied/$bc tables"
            if (( failed > 0 )); then
                return 1
            fi
        }

        # Helper: run lookup on a staged batch
        run_lookup_batch() {
            local stage_dir=$1
            local batch_num=$2
            local bs=$((batch_num * RC_LOOKUP_BATCH_SIZE))
            local be=$((bs + RC_LOOKUP_BATCH_SIZE - 1))
            [[ $be -ge $num_tables ]] && be=$((num_tables - 1))

            local table_count
            table_count=$(find "$stage_dir" -maxdepth 1 \( -name "*.rtc" -o -name "*.rt" \) | wc -l)
            log "Lookup batch $((batch_num+1))/$num_batches ($table_count tables in $stage_dir)"

            local lookup_start
            lookup_start=$(date +%s)
            local batch_output
            batch_output=$(cd "$(dirname "$RC_LOOKUP")" && "$RC_LOOKUP" "$stage_dir" "$hashes" 2>&1) || true
            local lookup_secs=$(( $(date +%s) - lookup_start ))
            log "Lookup batch $((batch_num+1)) complete in ${lookup_secs}s"

            # Extract and save cracked lines
            if [[ -n "$batch_output" ]]; then
                local cracked_lines
                cracked_lines=$(echo "$batch_output" | grep "^[0-9a-fA-F]\{32\}:" || true)
                if [[ -n "$cracked_lines" ]]; then
                    echo "$cracked_lines" >> "$cracked_file"
                    local batch_count
                    batch_count=$(echo "$cracked_lines" | wc -l)
                    local prev
                    prev=$(cat "$total_cracked_file")
                    echo $(( prev + batch_count )) > "$total_cracked_file"
                    log "Found $batch_count cracked hashes (running total: $(cat "$total_cracked_file"))"
                fi
            fi

            # Mark each table in this batch as done
            for ((j=bs; j<=be; j++)); do
                mark_done "$lookup_done" "${tables[$j]}"
            done
        }

        log "=========================================="
        log "NVMe Pipeline Lookup"
        log "=========================================="

        clean_stage "$stage_a"
        clean_stage "$stage_b"

        local cur_stage="$stage_a"
        local other_stage="$stage_b"

        # Copy first batch synchronously
        copy_batch_to_stage 0 "$cur_stage"

        for ((batch=0; batch<num_batches; batch++)); do
            log "--- Batch $((batch+1))/$num_batches ---"

            # Start background copy of next batch
            if (( batch + 1 < num_batches )); then
                copy_batch_to_stage $((batch+1)) "$other_stage" &
                copy_pid=$!
                log "Background copy started (PID $copy_pid)"
            else
                copy_pid=""
            fi

            # Run lookup on current stage
            run_lookup_batch "$cur_stage" "$batch"

            # Wait for background copy
            if [[ -n "$copy_pid" ]]; then
                log "Waiting for background copy (PID $copy_pid)..."
                wait "$copy_pid" || die "Background copy failed"
                copy_pid=""
            fi

            # ETA
            local elapsed=$(( $(date +%s) - pipeline_start ))
            local done_count=$(( (batch + 1) * RC_LOOKUP_BATCH_SIZE ))
            [[ $done_count -gt $num_tables ]] && done_count=$num_tables
            local eta
            eta=$(eta_str "$elapsed" "$done_count" "$num_tables")
            log "Progress: $done_count/$num_tables tables processed | ETA: $eta"

            # Swap buffers
            local tmp="$cur_stage"
            cur_stage="$other_stage"
            other_stage="$tmp"
        done

        local total_cracked
        total_cracked=$(cat "$total_cracked_file")
        rm -f "$total_cracked_file"

        local duration=$(( $(date +%s) - pipeline_start ))
        log "=========================================="
        log "Lookup complete!"
        log "Tables processed: $num_tables"
        log "Hashes cracked:   $total_cracked"
        log "Cracked output:   $cracked_file"
        log "Total time:       $(( duration / 3600 ))h $(( (duration % 3600) / 60 ))m"
        log "=========================================="
    } 2>&1 | tee -a "$lookup_log"
}

# ---------------------------------------------------------------------------
# cmd_all
# ---------------------------------------------------------------------------
cmd_all() {
    local source="" dest="" nvme="" state_dir="" hashes=""
    local dry_run=0 continue_on_error=0 config_file="$SCRIPT_DIR/pipeline.conf"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --hashes)           hashes="$2";    shift 2 ;;
            --source)           source="$2";    shift 2 ;;
            --dest)             dest="$2";      shift 2 ;;
            --nvme)             nvme="$2";      shift 2 ;;
            --state-dir)        state_dir="$2"; shift 2 ;;
            --dry-run)          dry_run=1;      shift ;;
            --continue-on-error) continue_on_error=1; shift ;;
            --config)           config_file="$2"; shift 2 ;;
            --help|-h)          usage_all ;;
            *)                  die "Unknown flag: $1" ;;
        esac
    done

    [[ -z "$hashes" ]] && die "--hashes FILE is required for all"

    # Build forwarded args for each stage
    local common_args=()
    [[ -n "$source"    ]] && common_args+=(--source "$source")
    [[ -n "$dest"      ]] && common_args+=(--dest "$dest")
    [[ -n "$nvme"      ]] && common_args+=(--nvme "$nvme")
    [[ -n "$state_dir" ]] && common_args+=(--state-dir "$state_dir")
    common_args+=(--config "$config_file")
    [[ $dry_run -eq 1  ]] && common_args+=(--dry-run)

    log "=== all: running sort → compact → lookup ==="

    run_stage() {
        local stage_name="$1"
        shift
        log "--- all: starting $stage_name ---"
        if "cmd_$stage_name" "$@"; then
            log "--- all: $stage_name complete ---"
        else
            local rc=$?
            log "--- all: $stage_name FAILED (exit $rc) ---"
            if [[ $continue_on_error -eq 0 ]]; then
                exit $rc
            fi
        fi
    }

    run_stage sort    "${common_args[@]}"
    run_stage compact "${common_args[@]}"
    run_stage lookup  --hashes "$hashes" "${common_args[@]}"

    log "=== all: pipeline complete ==="
}

# ---------------------------------------------------------------------------
# cmd_status
# ---------------------------------------------------------------------------
cmd_status() {
    local state_dir="" nvme="" config_file="$SCRIPT_DIR/pipeline.conf"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --state-dir) state_dir="$2"; shift 2 ;;
            --nvme)      nvme="$2";      shift 2 ;;
            --config)    config_file="$2"; shift 2 ;;
            --help|-h)   cat <<'EOF'
Usage: nvme_pipeline.sh status [--state-dir DIR] [--nvme DIR] [--config FILE]
EOF
                         exit 1 ;;
            *)           die "Unknown flag: $1" ;;
        esac
    done

    load_config "$config_file"
    resolve_settings

    [[ -n "$nvme"      ]] && RC_NVME_ROOT="$nvme"
    [[ -n "$state_dir" ]] && RC_STATE_DIR="$state_dir"

    [[ -z "$RC_STATE_DIR" ]] && RC_STATE_DIR="$RC_NVME_ROOT/.pipeline-state"

    echo "State directory: $RC_STATE_DIR"
    echo ""

    # Sort progress
    local sort_done_file="$RC_STATE_DIR/sort.done"
    local sort_done_count=0
    [[ -f "$sort_done_file" ]] && sort_done_count=$(wc -l < "$sort_done_file")
    echo "Sort:    $sort_done_count done"

    # Compact progress
    local compact_done_file="$RC_STATE_DIR/compact.done"
    local compact_done_count=0
    [[ -f "$compact_done_file" ]] && compact_done_count=$(wc -l < "$compact_done_file")
    echo "Compact: $compact_done_count done"

    # Source totals
    if [[ -n "$RC_RT_SOURCE" ]] && [[ -d "$RC_RT_SOURCE" ]]; then
        local rt_total
        rt_total=$(find "$RC_RT_SOURCE" -maxdepth 1 -name '*.rt' | wc -l)
        echo "RT source ($RC_RT_SOURCE): $rt_total .rt files total"
    fi

    # Dest totals
    if [[ -n "$RC_RTC_DEST" ]] && [[ -d "$RC_RTC_DEST" ]]; then
        local rtc_total
        rtc_total=$(find "$RC_RTC_DEST" -maxdepth 1 -name '*.rtc' | wc -l)
        echo "RTC dest ($RC_RTC_DEST): $rtc_total .rtc files total"
    fi

    echo ""

    # Lookup progress (scan all lookup.*.done files)
    local found_lookup=0
    if [[ -d "$RC_STATE_DIR" ]]; then
        for lookup_file in "$RC_STATE_DIR"/lookup.*.done; do
            [[ -f "$lookup_file" ]] || continue
            found_lookup=1
            local hashkey
            hashkey=$(basename "$lookup_file" .done | sed 's/^lookup\.//')
            local lookup_count
            lookup_count=$(wc -l < "$lookup_file")
            local cracked_count=0
            local cracked_file="$RC_STATE_DIR/cracked.${hashkey}.txt"
            [[ -f "$cracked_file" ]] && cracked_count=$(wc -l < "$cracked_file")
            echo "Lookup [$hashkey]: $lookup_count tables done, $cracked_count cracked"
        done
    fi
    if [[ $found_lookup -eq 0 ]]; then
        echo "Lookup:  0 done (no lookup.*.done files found)"
    fi
}

# ---------------------------------------------------------------------------
# Main dispatch
# ---------------------------------------------------------------------------
if [[ $# -eq 0 ]]; then
    usage_main
fi

SUBCOMMAND="$1"
shift

case "$SUBCOMMAND" in
    sort)    cmd_sort    "$@" ;;
    compact) cmd_compact "$@" ;;
    lookup)  cmd_lookup  "$@" ;;
    all)     cmd_all     "$@" ;;
    status)  cmd_status  "$@" ;;
    --help|-h|help) usage_main ;;
    *) die "Unknown subcommand: $SUBCOMMAND. Run with --help for usage." ;;
esac
