<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

<%!
## dictionary with cores allocated to each app
lc = {0:1, 1:9, 2:2, 3:10, 4:3 , 5:11, 6:4, 7:12 }

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

cmd:
    ti: 0.5
    mi: 20000
    event: ["instructions,cycles,cpu_clk_unhalted.thread,uops_retired.retire_slots,idq_uops_not_delivered.core,'cycles,cpu/umask=0x01,event=0x0E,name=UOPS_ISSUED.ANY/',intel_cqm/llc_occupancy/"]
    cat-impl: linux
