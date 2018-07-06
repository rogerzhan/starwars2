set FEEDBACK OFF
set LINESIZE 32767
set PAGESIZE 0
set underline off
set COLSEP |
set TRIM ON
set TRIMS ON
set HEADING OFF
set TERMOUT OFF
spool ssoftcrew.out
select
    upper(ei.zbadgeid) AS zbadgeid,
    d.zdeptdesc,
    to_char(ej.dtdate,'yyyy/mm/dd hh24:mi') AS dtdate,
    ei.zlname,
    ei.zfname,
    ei.zmname,
    e.lempid,
    e.lempinfoid,
    ej.lpostid,
    p.zpostdesc,
    sh.zshiftdesc,
    ac.zacccodeid,
    ac.zacccodedesc,
    rawtohex(edn.znote)
from
	ss_test6.empjobs ej,
	ss_test6.empsched es,
	ss_test6.emp e,
	ss_test6.empinfo ei,
	ss_test6.dept d,
	ss_test6.posts p,
	ss_test6.shift sh,
	ss_test6.empdaynote edn,
	ss_test6.acccode ac
where
	es.dtdate >= to_date(to_char(sysdate -12,'mm-dd-yyyy'), 'MM-DD-YYYY')
	and es.dtdate < to_date(to_char(sysdate +12,'mm-dd-yyyy'), 'MM-DD-YYYY')
	and  ej.lempid=es.lempid
	and ej.dtdate=es.dtdate
	and sh.lshiftid(+) = ej.lshiftid
	and es.lempid=e.lempid
	and e.lempinfoid=ei.lempinfoid
	and e.ldeptid=d.ldeptid
	and ej.lpostid=p.lpostid
	and edn.lempid(+) = ej.lempid
	and edn.dtdate(+) = ej.dtdate
	and ac.lacccodeid(+) = ej.lacccodeid 
order by
    upper(ei.zbadgeid), d.zdeptdesc, ej.dtdate;
spool off;
quit
