#!/usr/bin/env python3
"""Run original regular paths with resolved hierarchy marks and Cell +122 bytes.
The original full Unit entry, height, and regular search decisions execute.
"""
import sys
import original_path_check as p
for profile in (20,21,23,24):
    for goal in ((56,67),(52,67),(54,67),(58,69),(54,69)):
        result=p.run(goal,hierarchy_profile=profile)
        if '--vectors' in sys.argv:
            count,directions,cost=result or (0,(),0)
            print('{%d,%d,%d,%d,0x%08xu,{%s}},'%(goal[0],goal[1],profile,count,cost,','.join(map(str,directions))))
        else: print('original hierarchy-filtered regular path',profile,goal,result)
