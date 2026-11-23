#!/usr/bin/env python3
"""Check complete original regular paths and both postprocessing passes around resolved obstacles."""
import sys
import original_path_check as p
walls=((),((54,68),),((54,68),(55,68)),((53,68),(54,68),(55,68)),
       ((54,68),(53,68),(54,67)),((54,68),(55,68),(55,69)),
       ((53,68),(54,68),(55,68),(53,67),(55,67)))
for scenario,blocked in enumerate(walls):
    for y in range(66,71):
        for x in range(52,57):
            result=p.run((x,y),walls=blocked)
            if '--vectors' in sys.argv:
                count,directions,cost=result or (0,(),0)
                print('{%d,%d,%d,%d,0x%08xu,{%s}},'%(x,y,scenario,count,cost,','.join(map(str,directions))))
            else: print('original postprocessed path',scenario,(x,y),result)
