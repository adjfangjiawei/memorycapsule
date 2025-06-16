#!/bin/bash

# --- Configuration ---
CLANG_TIDY_EXECUTABLE="clang-tidy-21"
COMPILE_COMMANDS_FILE="Build/compile_commands.json" # Relative to PROJECT_ROOT
PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"     # Assumes script is in project root

# Compiler paths will be determined dynamically below
GCC_COMPILER_FROM_JSON_IN_CC_DB="" 
GCC_COMPILER_FOR_INCLUDES=""       

TARGET_STD_CLANG="c++2b" # C++ standard for Clang
OUTPUT_FILE="clang_tidy_report.txt"
NUM_JOBS=$(nproc)          

# --- Helper Functions ---
# Function to resolve symlinks to their target file
resolve_symlink() {
    local path_to_resolve="$1"; local resolved_path
    resolved_path=$(realpath -e "$path_to_resolve" 2>/dev/null)
    if [ -z "$resolved_path" ]; then
        local current_path="$path_to_resolve"; local target_path="$path_to_resolve"; local depth=0
        while [ -L "$current_path" ] && [ "$depth" -lt 10 ]; do
            link_target=$(readlink "$current_path")
            if [[ "$link_target" == /* ]]; then current_path="$link_target"; else current_path="$(dirname "$target_path")/$link_target"; fi
            target_path="$current_path"; depth=$((depth + 1))
        done; resolved_path="$current_path"
    fi
    if [ ! -x "$resolved_path" ]; then echo "$1"; return 1; fi # Fallback to original if not executable
    echo "$resolved_path"; return 0
}

# --- Initial Checks ---
if [ ! -f "$PROJECT_ROOT/$COMPILE_COMMANDS_FILE" ]; then
    echo "CRITICAL ERROR: Compile commands file not found: '$PROJECT_ROOT/$COMPILE_COMMANDS_FILE'" >&2; exit 1; fi
if ! command -v jq &> /dev/null; then
    echo "CRITICAL ERROR: 'jq' not found. Please install jq." >&2; exit 1; fi
if ! command -v realpath &> /dev/null; then
    echo "CRITICAL ERROR: 'realpath' not found. Please install coreutils." >&2; exit 1; fi
if ! command -v "$CLANG_TIDY_EXECUTABLE" &> /dev/null; then
    echo "CRITICAL ERROR: clang-tidy: '$CLANG_TIDY_EXECUTABLE' not found." >&2; exit 1; fi

# --- Clean old report ---
>"$PROJECT_ROOT/$OUTPUT_FILE"
echo "Starting Clang-Tidy analysis..." > "$PROJECT_ROOT/$OUTPUT_FILE" # Initial message in report

# --- Determine Compiler Paths ---
FIRST_COMMAND_COMPILER_RAW=$(jq -r '(.[0].command // "") | split(" ")[0] // ""' "$PROJECT_ROOT/$COMPILE_COMMANDS_FILE")
if [ -z "$FIRST_COMMAND_COMPILER_RAW" ] || [ "$FIRST_COMMAND_COMPILER_RAW" == "null" ]; then
    echo "CRITICAL ERROR: Could not extract compiler from '$PROJECT_ROOT/$COMPILE_COMMANDS_FILE'" >&2; exit 1; fi
GCC_COMPILER_FROM_JSON_IN_CC_DB="$FIRST_COMMAND_COMPILER_RAW"

GCC_COMPILER_FOR_INCLUDES=$(resolve_symlink "$GCC_COMPILER_FROM_JSON_IN_CC_DB")
if [ $? -ne 0 ] || [ -z "$GCC_COMPILER_FOR_INCLUDES" ] || ! command -v "$GCC_COMPILER_FOR_INCLUDES" &>/dev/null ; then
    echo "Warning: Using '$GCC_COMPILER_FROM_JSON_IN_CC_DB' for include detection (symlink resolution failed or target not executable)." >&2
    GCC_COMPILER_FOR_INCLUDES="$GCC_COMPILER_FROM_JSON_IN_CC_DB"
fi
if ! command -v "$GCC_COMPILER_FOR_INCLUDES" &> /dev/null; then
    echo "CRITICAL ERROR: Effective GCC compiler '$GCC_COMPILER_FOR_INCLUDES' not found." >&2; exit 1; fi

# --- Determine G++ Standard Library Include Paths ---
GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY=() 
# !!! IMPORTANT: MANUALLY VERIFY AND SET GCC 15.1.0 INCLUDE PATHS HERE !!!
# These paths are crucial for finding <format> and other C++20/23 headers.
# Run: echo | /path/to/your/gcc-15.1.0/bin/g++ -std=gnu++23 -E -v -x c++ -
# And find the C++ include paths from the '#include <...> search starts here:' section.
MANUAL_GCC_INCLUDES=(
    "/usr/local/gcc-15.1.0/include/c++/15.1.0"
    "/usr/local/gcc-15.1.0/include/c++/15.1.0/x86_64-linux-gnu" # Adjust triplet if needed
    "/usr/local/gcc-15.1.0/lib/gcc/x86_64-linux-gnu/15.1.0/include" # Check this path carefully
)
for path in "${MANUAL_GCC_INCLUDES[@]}"; do
    if [ -d "$path" ]; then GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY+=("-isystem" "$path"); fi
done
USR_INCLUDE_PATH="/usr/include"; already_has_usr_include=false
for ((i=0; i<${#GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY[@]}; i+=2)); do
    if [[ "${GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY[i+1]}" == "$USR_INCLUDE_PATH" ]]; then already_has_usr_include=true; break; fi
done
if [ "$already_has_usr_include" = false ] && [ -d "$USR_INCLUDE_PATH" ]; then
    GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY+=("-isystem" "$USR_INCLUDE_PATH"); fi

if [ ${#GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY[@]} -eq 0 ]; then
    echo "CRITICAL WARNING: No G++ standard library include paths determined. Clang-tidy will likely fail." >> "$PROJECT_ROOT/$OUTPUT_FILE"
fi
GCC_CXX_INCLUDE_PATHS_ARGS_STR="${GCC_CXX_INCLUDE_PATHS_ARGS_ARRAY[*]}" # For export

# --- Define function to process a single file ---
run_tidy_for_file() {
    local command_entry_json_str="$1"; local raw_file_from_json_arg="$2"
    if [ -z "$command_entry_json_str" ]||[ "$command_entry_json_str" == "null" ]||[ -z "$raw_file_from_json_arg" ]||[ "$raw_file_from_json_arg" == "null" ]; then return 0; fi

    local original_command; original_command=$(echo "$command_entry_json_str" | jq -r '.command')
    if [ -z "$original_command" ]||[ "$original_command" == "null" ]; then return 1; fi
    local compile_directory; compile_directory=$(echo "$command_entry_json_str" | jq -r '.directory')
    if [ -z "$compile_directory" ]||[ "$compile_directory" == "null" ]; then return 1; fi

    local ifs_backup="$IFS"; IFS=' ' read -ra CMD_ARGS <<< "$original_command"; IFS="$ifs_backup"
    local project_specific_compile_args=(); local i=0
    while [ "$i" -lt "${#CMD_ARGS[@]}" ]; do
        local arg="${CMD_ARGS[$i]}"; local current_arg_is_project_specific=false
        case "$arg" in
            -I*) if [[ "${#arg}" -gt 2 ]]; then project_specific_compile_args+=("$arg"); else i=$((i+1)); if [ "$i" -lt "${#CMD_ARGS[@]}" ]; then project_specific_compile_args+=("$arg" "${CMD_ARGS[$i]}"); fi; fi; current_arg_is_project_specific=true ;;
            -isystem) i=$((i+1)); if [ "$i" -lt "${#CMD_ARGS[@]}" ]; then project_specific_compile_args+=("$arg" "${CMD_ARGS[$i]}"); fi; current_arg_is_project_specific=true ;;
            -D*) project_specific_compile_args+=("$arg"); current_arg_is_project_specific=true ;;
        esac
        if [ "$current_arg_is_project_specific" = false ]; then
            case "$arg" in # Args to skip
                "$GCC_COMPILER_FROM_JSON_IN_CC_DB"|"-std="*|"-o"|"-c"|"-fPIC"|"-ffunction-sections"|"-fdata-sections"|"-MF"|"-MT"|"-MD")
                    if [[ "$arg" == "-o" ]]||[[ "$arg" == "-MF" ]]||[[ "$arg" == "-MT" ]]; then i=$((i+1));fi ;;
                *) if [[ "$arg" == *.cpp || "$arg" == *.c || "$arg" == *.cc || "$arg" == *.hpp || "$arg" == *.h || "$arg" == *.hh || "$arg" == *.S ]]; then : ; else : ; fi ;; # Skip source files and other unknown
            esac
        fi; i=$((i + 1))
    done

    local absolute_file_path_to_tidy
    if [[ "$raw_file_from_json_arg" == /* ]]; then absolute_file_path_to_tidy="$raw_file_from_json_arg"; else absolute_file_path_to_tidy="$compile_directory/$raw_file_from_json_arg"; fi
    absolute_file_path_to_tidy=$(realpath -m "$absolute_file_path_to_tidy" 2>/dev/null)
    if [ -z "$absolute_file_path_to_tidy" ]; then return 0; fi
    
    local relative_path_from_project_root
    relative_path_from_project_root=$(realpath --relative-to="$PROJECT_ROOT" -m "$absolute_file_path_to_tidy" 2>/dev/null)
    if [[ "$absolute_file_path_to_tidy" == "$compile_directory/"*"/QuanMinXiYouClient_autogen/"* ]]; then return 0; fi
    if ! [[ "$relative_path_from_project_root" == Base/* || "$relative_path_from_project_root" == src/* || "$relative_path_from_project_root" == Include/* || "$relative_path_from_project_root" == include/* ]]; then return 0; fi

    local final_tidy_args_for_clang_frontend=() 
    final_tidy_args_for_clang_frontend+=("-v") 
    final_tidy_args_for_clang_frontend+=("-std=$TARGET_STD_CLANG")
    # shellcheck disable=SC2086 # We want word splitting for GCC_CXX_INCLUDE_PATHS_ARGS_STR
    final_tidy_args_for_clang_frontend+=($GCC_CXX_INCLUDE_PATHS_ARGS_STR)
    final_tidy_args_for_clang_frontend+=("${project_specific_compile_args[@]}")

    # Capture clang-tidy output
    local tidy_output
    local tidy_cmd_str # For logging if needed
    tidy_cmd_str=$(printf '%q ' "$CLANG_TIDY_EXECUTABLE" "$absolute_file_path_to_tidy" "--" "${final_tidy_args_for_clang_frontend[@]}")
    
    # Only write to report if there is actual error/warning output from clang-tidy
    # Clang's -v output goes to stderr, clang-tidy warnings/errors also to stderr
    tidy_output=$("$CLANG_TIDY_EXECUTABLE" "$absolute_file_path_to_tidy" "--" "${final_tidy_args_for_clang_frontend[@]}" 2>&1)
    local tidy_status=$?

    # Filter out the verbose -v output from Clang, keep only clang-tidy's actual findings
    # Clang-tidy errors/warnings usually contain the filename.
    # Clang's own verbose output (like search paths) usually doesn't start with the filename in the same way.
    local filtered_tidy_output
    filtered_tidy_output=$(echo "$tidy_output" | grep -E "($absolute_file_path_to_tidy:|warning:|error:)")

    if [ $tidy_status -ne 0 ] || [ -n "$filtered_tidy_output" ] ; then
        echo "--- Issues for $absolute_file_path_to_tidy ---" >> "$OUTPUT_FILE"
        # echo "Executed: $tidy_cmd_str" >> "$OUTPUT_FILE" # Optional: log the command
        echo "$filtered_tidy_output" >> "$OUTPUT_FILE"
        if [ $tidy_status -ne 0 ] && ! echo "$filtered_tidy_output" | grep -q "error:"; then
             # If exit status is non-zero but no "error:" in filtered output, something else happened
             echo "Clang-tidy exited with status $tidy_status (Full -v output might be above if not filtered, or check raw output)" >> "$OUTPUT_FILE"
             echo "Raw clang-tidy output for $absolute_file_path_to_tidy:" >> "$OUTPUT_FILE"
             echo "$tidy_output" >> "$OUTPUT_FILE" # Log raw output for inspection
        fi
        echo "----------------------------------------" >> "$OUTPUT_FILE"
    fi
}
# Export all functions and variables needed by the subshells spawned by xargs
export -f run_tidy_for_file resolve_symlink debug_echo # Export all functions
export CLANG_TIDY_EXECUTABLE TARGET_STD_CLANG OUTPUT_FILE PROJECT_ROOT DEBUG_MODE
export GCC_CXX_INCLUDE_PATHS_ARGS_STR GCC_COMPILER_FROM_JSON_IN_CC_DB

# --- Main Processing Logic ---
echo "Processing files listed in '$PROJECT_ROOT/$COMPILE_COMMANDS_FILE' using $NUM_JOBS jobs..."
# jq filter script to extract full JSON entry and the raw .file string, NUL separated
jq_filter_script='
.[] | select(.file and (.file | (endswith(".cpp") or endswith(".h") or endswith(".hpp") or endswith(".cc") or endswith(".hh")))) | 
( (. | tojson) + "\u0000" + .file + "\u0000" )'

if jq -e 'any(.[] | select(.file and (.file | (endswith(".cpp") or endswith(".h") or endswith(".hpp") or endswith(".cc") or endswith(".hh")))); "notempty")' "$PROJECT_ROOT/$COMPILE_COMMANDS_FILE" > /dev/null 2>&1; then
    jq -r "$jq_filter_script" "$PROJECT_ROOT/$COMPILE_COMMANDS_FILE" | \
    xargs -0 -P "$NUM_JOBS" -n 2 \
    bash -c 'run_tidy_for_file "$1" "$2"' _ 
else
    echo "No matching C/C++ source/header files found by jq filter in '$PROJECT_ROOT/$COMPILE_COMMANDS_FILE' to process." >> "$OUTPUT_FILE"
fi

echo "Clang-Tidy analysis complete. Report saved to $PROJECT_ROOT/$OUTPUT_FILE"
# Check if the report file is nearly empty (only contains initial message)
if [ "$(wc -l < "$PROJECT_ROOT/$OUTPUT_FILE")" -le 2 ]; then
    echo "No issues found by Clang-Tidy or script encountered issues preventing analysis."
fi