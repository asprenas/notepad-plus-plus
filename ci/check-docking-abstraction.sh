#!/usr/bin/env bash
# Enforces docs/plan/qt-cross-platform-port-plan.md appendix B.4 rule 1:
# ADS headers/types may only be used inside app/ui/docking/ads/.
#
# Convention: ADS headers are included with <...>, project headers with "...".
# (app/ui/docking/DockManager.h shares its file name with ADS' DockManager.h.)
set -euo pipefail
cd "$(dirname "$0")/.."

pattern='#include[[:space:]]*<(DockManager|DockWidget|DockAreaWidget|DockContainerWidget|FloatingDockContainer|DockComponentsFactory|DockSplitter|ads_globals)\.h>|\bads::'

violations=$(grep -rnE "$pattern" app --include='*.h' --include='*.cpp' \
    | grep -v '^app/ui/docking/ads/' \
    | grep -v '^app/thirdparty/' \
    || true)

if [[ -n "$violations" ]]; then
    echo "ADS usage outside app/ui/docking/ads/ (use npp::ui::DockManager instead):"
    echo "$violations"
    exit 1
fi
echo "Docking abstraction check passed."
