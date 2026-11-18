# Screen fixes

All five corrected behaviors pass their comparison with the original under Wine.

| Screen | Verified behavior |
| --- | --- |
| Skirmish setup | Draws the map preview. |
| Visual Details | A click next to the slider thumb moves it by one page. The tested click changes level 2 to 0. |
| AI drop-down | Shows three rows and a scroll bar in the tested setup. |
| Score screen | Shows the title promptly. Use `--scale integer` to keep the original centered size at 1024×768. |
| Dialog controls | Round both control edges as Windows does. The tested positions match. |

The default sharp scale fills the window. Integer scale uses whole-number multiples when the window is large enough.

An Isle of War win check matches all 2,092 scenario random states, 70 census samples, and both score rows. Skirmish sliders and check boxes save the same tested values.
