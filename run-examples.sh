#!/bin/bash
# Thin wrapper — canonical harness lives in examples/run-examples.sh
exec "$(cd "$(dirname "$0")" && pwd)/examples/run-examples.sh" "$@"
