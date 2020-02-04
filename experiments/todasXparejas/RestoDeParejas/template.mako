<%include file="applications.mako"/>
<%include file="instr_60s_500ms.mako"/>

<%!
## dictionary with cores allocated to each app
lc = {0:1, 1:9}

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
    event: ["instructions,cycles,idq_uops_not_delivered.core,'cycles,cpu/umask=0x01,event=0x0E,name=UOPS_ISSUED.ANY/'"]
    cat-impl: linux

sched:
    kind: linux
    allowed_cpus: [1,9]
