<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

<%!
## dictionary with cores allocated to each app
lc = {0:1, 1:9, 2:2, 3:10}

%>

tasks:
  % for app in apps:
  - app: *${app}
    max_instr: *${app}_mi
    cpus: [${lc[loop.index]}]
  % endfor

cmd:
    ti: 0.5
    mi: 20000
    event: ["instructions,cycles,cpu_clk_unhalted.thread"]
    cpu_affinity: [3]
    cat-impl: linux

sched:
    kind: linux
    allowed_cpus: [1,2,9,10]
