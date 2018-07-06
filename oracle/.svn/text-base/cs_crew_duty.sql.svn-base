set FEEDBACK OFF
set LINESIZE 32767
set PAGESIZE 0
set underline off
set COLSEP |
set TRIM ON
set TRIMS ON
set HEADING OFF
set TERMOUT OFF
spool cs_crew_duty.out
select
    cd.crewid,
    to_char(ca.starttime,'YYYY/MM/DD HH24:MI') as starttime,
    to_char(ca.endtime,'YYYY/MM/DD HH24:MI') as endtime,
    ca.aircraftid as ca_aircraftid,
    cd.aircraftid as cd_aircraftid,
    aa.registration as ca_registration,
    ad.registration as cd_registration,
    ca.position,
    to_char(cd.scheduled_on,'YYYY/MM/DD HH24:MI') as scheduled_on,
    to_char(cd.scheduled_off,'YYYY/MM/DD HH24:MI') as scheduled_off,
    to_char(cd.actual_on,'YYYY/MM/DD HH24:MI') as actual_on,
    to_char(cd.actual_off,'YYYY/MM/DD HH24:MI') as actual_off,
    to_char(cd.currdate,'YYYY/MM/DD HH24:MI') as currdate,
    to_char(cd.lastupdated,'YYYY/MM/DD HH24:MI') as lastupdated,
    cd.scenarioid
from
    cs_crew_duty cd,
    crewassignment ca,
    aircraft aa,
    aircraft ad
where
    cd.currdate >= sysdate - 12
    and (cd.scheduled_on is not null OR cd.actual_on is not null)
    and cd.scenarioid in(
        select
            distinct sv.masterscenarioid
        from
            schedulingview sv
        where
            masterscenarioid is not null
    )
    and ca.crewid(+) = cd.crewid
    and ( ca.scenarioid is null OR (
            ca.scenarioid = cd.scenarioid and (
                (cd.scheduled_on is not null and
                 to_char(cd.scheduled_on,'YYYY/MM/DD') <> '1901/01/01' and
                 (ca.starttime <= cd.scheduled_on and ca.endtime >= cd.scheduled_on)
                )
                OR
                (cd.actual_on is not null and
                 to_char(cd.actual_on,'YYYY/MM/DD') <> '1901/01/01' and
                 (ca.starttime <= cd.actual_on and ca.endtime >= cd.actual_on)
                )
            )
        )
    )
    and aa.aircraftid = ca.aircraftid
    and ad.aircraftid = cd.aircraftid
order by
    cd.crewid, cd.scheduled_on, ca.starttime;
spool off;
quit
