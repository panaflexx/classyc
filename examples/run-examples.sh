#!/bin/bash

# Run all examples in the examples directory
echo "Running all ClassyC examples..."

# Find all .cy files in examples directory
for example in examples/*.cy; do
    if [ -f "$example" ]; then
        echo "Running $example..."
        bin/classyc "$example" -eg
        echo "Finished $example"
        echo "---"
    fi
done

echo "All examples completed."
