<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

<%!
lc = {0:1, 1:9, 2:2, 3:10}
%>

cos:
  - schemata: 0xfffff
  - schemata: 0xfffff
    cpus: [1]
  - schemata: 0xfffff


tasks:
  % for app in apps:
  - app: *${app}
    max_instr: *${app}_mi
    cpus: ${lc[loop.index]}
  % endfor

cat_policy:
    kind: np
    every: 1
    stats: interval
    firstInterval: 10

cmd:
    ti: 0.5
    mi: 20000
    event: ["instructions,cycles,cpu_clk_unhalted.thread,uops_retired.retire_slots,idq_uops_not_delivered.core,'cycles,cpu/umask=0x01,event=0x0E,name=UOPS_ISSUED.ANY/',int_misc.recovery_cycles"]
    cpu_affinity: [3]
    cat-impl: linux

sched:
    kind : linux
    allowed_cpus: [1,2,9,10]

