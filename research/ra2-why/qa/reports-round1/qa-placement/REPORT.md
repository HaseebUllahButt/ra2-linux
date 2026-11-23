# QA report: qa-placement (display 104), build 87292f36
Session was about 12 minutes of play, shorter than the 25 minutes asked. No crash. ./qa alive stayed "running".

## Steps
1. Started skirmish. Help panel opens on start; click closes it. Selected MCV, pressed D: Construction Yard deployed (s3.png).
2. Built Power Plant. Clicked cameo again while building: no effect, no double charge (s5, s6). READY shown (s7).
3. Placement preview: red on units (p1), red too far (p2, p4), green near base (p3). Clicking a too-far spot gives "PLACE THIS BUILDING CLOSER" (c1). Clicking on units gives "CANNOT DEPLOY HERE" (c2).
4. Esc cancels placement, stays in game, cameo stays READY (e1). Right click also cancels (r1, r2).
5. Placed Power Plant (pl1), Barracks on ore (b2, accepted; same as original). Placed Barracks, 2 more Power Plants.
6. Tabs ALL/BLD/INF/VEH/AIR switched while building: build kept going, no loss (t1-t3).
7. Two builds at once (Power Plant + GI): both ran (f1, f2).
8. Water edge: footprint red next to the shore and on water; click refused with "CANNOT DEPLOY HERE" (x_*.png, wc.png).

## Bugs
No blocker or major bug seen.

- minor: Stale status text after cancel. After Esc or right click the bottom-left message still says "PLACE POWER PLANT. ESC CAN..." (r1.png, e1.png: "CANNOT DEPLOY HERE"). Repro: ready building, click cameo, press Esc. Expected: message cleared. Seen: old text stays.
- minor: Truncated text "ESC CAN" in the message line (cut at the sidebar edge) (b1.png).
- polish: No build progress clock or bar on cameos while building. Only the credit number falling and READY at end (s4, s5, s6). Original RA2 shows a clock sweep.
- polish: Footprint is a thin outline, not the filled green/red tiles of RA2, and sits to the lower right of the cursor (p3). Colors are right: green valid, red invalid.
- polish/unclear: Money is taken a little at a time, which matches RA2, but the first GI I queued (f1) never visibly appeared while I watched; the second one showed a soldier at the barracks (gi2.png). I did not confirm the first one. Needs retest.

## Not tested (honest)
- Cliffs: none near base.
- Too little money: credits stayed 5000+, never reached zero.
- Map edge: base is mid-map; not reached.
- Placing on a moving unit (only on idle tanks: refused correctly).

## Worked well
- Esc and right click cancel; build stays READY.
- Distance rule refuses far spots with a clear message.
- Water and shore refused. Units block placement.
- Ore placement allowed.
- Tab switching and parallel builds are safe.
- Re-clicking a cameo while building does nothing harmful.
