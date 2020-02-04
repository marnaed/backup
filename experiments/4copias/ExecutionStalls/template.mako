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
    event: ["instructions,cycles,'cpu/umask=0x01,event=0xB1,cmask=0x01,name=UOPS_EXECUTED.THREAD/','cpu/umask=0x04,event=0xA3,name=CYCLE_ACTIVITY.CYCLES_NO_EXECUTE,cmask=4/',rs_events.empty_cycles,intel_cqm/llc_occupancy/"]
    cat-impl: linux
