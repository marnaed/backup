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
    event: ["instructions,cycles,resource_stalls.sb,'cpu/umask=0x02,event=0x48,name=L1D_PEND_MISS.FB_FULL,offcore_rsp=0x00,cmask=1/','cpu/umask=0x01,event=0xb2,name=OFFCORE_REQUESTS_BUFFER.SQ_FULL/',intel_cqm/llc_occupancy/"]
    cat-impl: linux
