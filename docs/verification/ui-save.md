# Menus, saves, and game endings

Compare base Red Alert 2 1.006 under Wine with its native Linux build. Run both on private 1024×768 displays with the same settings and inputs.

| Case | Result |
| --- | --- |
| Skirmish save and reload | Save at frame 10,000. Match 20,000 scenario random states and 667 census samples. |
| Skirmish cross-load | Load each build's save in the other build. Match the next 10,000 frame boundaries and 333 census samples. |
| Eagle Dawn save and reload | Save at frame 10,001. Match 20,000 frame boundaries and 667 census samples. |
| Win and loss | Match outcomes, end frames and both score rows. A test hook triggers the win. |
| Menus | Open the main menu, skirmish, campaign, options, movies, and credits. Check saved settings after restart. |

The two skirmish saves differ on disk. The decoded comparison found 48,600 stable differing bytes after accounting for process pointers and variation between repeated saves. The cross-load tests confirm the tested gameplay outcomes. Campaign cross-load was outside the test.

The first screen checks found differences that the [screen fixes](screens.md) address. Network and online screens were opened as menus. Online gameplay was outside the test.
