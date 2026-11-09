# RA2 QA kit (virtual player)

Use ./qa from /path/to/user/Projects/ra2/ra2-test-host/qa. Each QA agent uses its OWN display number (given in its task).

- ./qa start N            start a hidden 1024x768 screen :N and the game (main menu) on it
- ./qa start N --skirmish start straight into a skirmish match
- ./qa shot N FILE.png    screenshot; then open FILE.png with the Read tool to see it
- ./qa in N click left X Y | click right X Y | dclick X Y | drag X1 Y1 X2 Y2 | move X Y
- ./qa in N key KEYNAME   (X keysym names: d, s, Escape, F1, Tab, Left, Right, Up, Down, Return)
- ./qa in N hold KEYNAME MS   hold a key (for arrow-key scrolling)
- ./qa wait MS            let the game run (game time passes in real time)
- ./qa alive N            is the game still running?
- ./qa log N              last lines of the game output (crash messages, debug-log path)
- ./qa stop N             stop game and screen

Screen: 1024x768. Battle view is x 0..~805, sidebar on the right, minimap bottom right.
Debug loop log path is printed in ./qa log N (RA2_DEBUG_LOOP=1): input and present timestamps; use it to prove a freeze.
Tip: resize screenshots for a quick look: magick in.png -resize 60% out.png

Timing note: the game runs at normal priority (chrt -o 0) even if the agent runs at idle priority.
When you judge speed (build time, unit speed), also check how many game frames passed: the debug-loop log has one present per frame.
Report times in game frames when you can, not only real seconds.
