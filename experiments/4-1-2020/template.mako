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

cat_policy:
    kind: np
    every: 1
    stats: interval
    firstInterval: 10

cmd:
    ti: 0.5
    mi: 20000
    event: ["instructions,cycles"]
    cat-impl: linux
