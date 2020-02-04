<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

cos:
  - schemata: 0xfffff
  - schemata: 0xfffff
    cpus: [1]
  - schemata: 0xfffff


tasks:
  % for app in apps:
  - app: *${app}
    max_instr: *${app}_mi
    cpus: [1]
  % endfor

cmd:
    ti: 0.5
    mi: 20000
    event: ["instructions,cycles,uops_retired.retire_slots,cpu_clk_unhalted.thread,intel_cqm/llc_occupancy/"]
    cat-impl: linux
