#!/bin/sh
# Launch naming batch 2 (job H).
cd /tmp/r2
nohup codex exec \
  --skip-git-repo-check \
  --sandbox workspace-write \
  -c sandbox_workspace_write.network_access=true \
  -c 'model_reasoning_effort="high"' \
  - < prompts/job12.txt > /tmp/codex12.log 2>&1 &
echo "jobH pid $!"
