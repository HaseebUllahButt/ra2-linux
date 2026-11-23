# CPU cost and game speed

In the tested OpenGL setup, the native build drops from 75.138 million to 28.705 million user-mode CPU cycles per game frame. This is a **61.8% reduction**. The stored original under Wine used 10 million cycles per frame. The final native build still costs **2.87×** that reference.

| Change | Before | After |
| --- | ---: | ---: |
| Group exact x87 instructions | 75.138 | 33.923 |
| Cache indirect calls | 33.923 | 30.063 |
| Convert textures before scaling and separate the display wait | 30.063 | 30.225 |
| Free a register in the fixed-address build | 30.225 | 28.705 |
| Remove the extra software framebuffer, measured separately | 125.335 | 44.910 |

Values are millions of CPU cycles per game frame. The small texture and display change in the OpenGL rows is within measurement noise. The tested system reaches all seven game speed settings. It reaches 208 frames per second uncapped and 90.13 in one late battle with three hard AI players.

The 32-bit x86 host executes the original floating-point instructions on the CPU and preserves the guest's full x87 state. Safe groups of instructions share one save and restore pair. The standalone x87 comparison checks 1,951,744 executions per run. Five skirmishes and three campaigns match 149,746 scenario random states and 1,989 shared census samples in the performance checks.
