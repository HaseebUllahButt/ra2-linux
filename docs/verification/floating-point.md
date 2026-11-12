# Exact x87 floating point

The first native comparison checked 876 functions at two x87 precisions. Of those, 102 match exactly, 126 fault the same way, and 645 fall outside the test's limits. Three functions produce different fault behavior. Across 93,200 state checks there are zero returned-value differences.

The native runtime preserves the x87 stack, tag word, status and control words. It executes the original opcodes on the 32-bit x86 host. There is no approximation through double precision. The function check covers results and fault behavior for its chosen input states. It does not compare every guest instruction pointer or opcode field.

After the later grouping changes, the standalone old-versus-new x87 check compares 1,951,744 executions per run. The [CPU report](performance.md) gives the timing and gameplay checks for that change.
