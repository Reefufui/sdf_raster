#!/bin/bash

# Function to get the sum of code lines using cloc
# --quiet: suppresses all messages except the final report
# --csv: outputs the results in CSV format (files, language, blank, comment, code)
# --not-match-f='lookup': excludes any file with "lookup" in the name
get_code_lines() {
    local directory=$1
    if [ -d "$directory" ]; then
        # We take the last line (SUM), and extract the 5th column (code lines)
        local result=$(cloc "$directory" --quiet --csv --not-match-f='lookup' | tail -n 1 | cut -d',' -f5)
        
        # If the result is empty (no files found), return 0
        if [ -z "$result" ]; then
            echo 0
        else
            echo "$result"
        fi
    else
        echo 0
    fi
}

echo "Analyzing project (excluding files with 'lookup' in the name)..."

# Calculate counts for both directories
SRC_COUNT=$(get_code_lines "src")
SHADERS_COUNT=$(get_code_lines "shaders")

# Output the results
echo "--------------------------------"
echo "Code lines in 'src/':     $SRC_COUNT"
echo "Code lines in 'shaders/': $SHADERS_COUNT"
echo "--------------------------------"
echo "Total Code Lines:         $((SRC_COUNT + SHADERS_COUNT))"

