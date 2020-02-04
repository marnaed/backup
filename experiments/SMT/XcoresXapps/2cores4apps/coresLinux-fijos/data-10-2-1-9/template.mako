<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

<%!
## dictionary with cores allocated to each app
lc = {0:10, 1:2, 2:1, 3:9}

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
    event: ["instructions,cycles,cpu_clk_unhalted.thread,'cycles,cpu/umask=0x01,event=0x0E,name=UOPS_ISSUED.ANY/'"]
    cpu_affinity: [3]
    cat-impl: linux

sched:
    kind: linux
    allowed_cpus: [1,2,9,10]

