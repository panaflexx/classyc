#!/bin/bash

# Run all examples in the examples directory
echo "Running all ClassyC examples..."

SEGFAULTS=()

# Find all .cy files in examples directory
for example in examples/*.cy; do
    if [ -f "$example" ]; then
        echo "Running $example..."
        bin/classyc -I include -g "$example" -eg
        status=$?
        if [ $status -gt 133 ]; then
            SEGFAULTS+=("$example")
        fi
        echo "Finished $example (exit $status)"
        echo "---"
    fi
done

echo "All examples completed."

if [ ${#SEGFAULTS[@]} -gt 0 ]; then
    echo ""
    echo "Samples that segfaulted:"
    for example in "${SEGFAULTS[@]}"; do
        echo "  $example"
    done
else
    echo ""
    echo "No samples segfaulted."
fi
