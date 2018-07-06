set FEEDBACK OFF
set LINESIZE 32767
set PAGESIZE 0
set underline off
set COLSEP |
set TRIM ON
set TRIMS ON
set HEADING OFF
set TERMOUT OFF
spool upgradedowngrade.out
select a.*, dmd.outaptid, dmd.inaptid, dmd.numberofpassengers from (
  SELECT 
    o.ownerid,
    c.contractid,
    to_char(d.outtime,'YYYY/MM/DD HH24:MI') as dmd_outtime,
    to_char(ml.schedout,'YYYY/MM/DD HH24:MI') as outtime,
    d.demandid,
    ml.managedlegid as otherid,
    'demand' as type,
    o.shortname,
    kw.legkeywordid,
    kw.keyword as keyword,
    fc.SEQUENCEPOSITION as contract_seqpos,
    fc.name as contract_actype,
    fc.aircrafttypeid as contract_actypeid,
    fd.SEQUENCEPOSITION as seqpos,
    fd.name as actype,
    fd.aircrafttypeid as actypeid
  FROM 
    demand d, ownerreservation r, contract c, owner o, fractionalprogram fd, fractionalprogram fc,
    demandlegkeywordusage kwu, legkeyword kw, managedleg ml
  WHERE 
    d.outtime > (new_time(sysdate,'EST','GMT')) and
    d.outtime < (new_time(sysdate + 4,'EST','GMT')) and
    r.ownerreservationid = d.reservationid and
    c.contractid = r.CONTRACTID and
    o.ownerid = r.ownerid and
    fd.fractionalprogramid = d.fractionalprogramid and
    fc.fractionalprogramid = c.fractionalprogramid and
    kwu.demandid(+) = d.demandid and
    kw.legkeywordid(+) = kwu.legkeywordid and
    ml.demandid(+) = d.demandid
  union
  SELECT 
    o.ownerid,
    c.contractid,
    to_char(d.outtime,'YYYY/MM/DD HH24:MI') as dmd_outtime,
    to_char(ml.schedout,'YYYY/MM/DD HH24:MI') as outtime,
    d.demandid,
    ml.managedlegid as otherid,
    'mgdleg' as type,
    o.shortname,
    kw.legkeywordid,
    kw.keyword as keyword,
    fc.SEQUENCEPOSITION as contract_seqpos,
    fc.name as contract_actype,
    fc.aircrafttypeid as contract_actypeid,
    fm.SEQUENCEPOSITION as seqpos,
    fm.name as actype,
    fm.aircrafttypeid as actypeid
  FROM 
    managedleg ml, demand d, managedaircraft ma, ownerreservation r, contract c, owner o, fractionalprogram fc,
    fractionalprogram fm, managedlegkeywordusage kwu, legkeyword kw
  WHERE 
    ml.schedout > (new_time(sysdate,'EST','GMT')) and
    ml.schedout < (new_time(sysdate + 4,'EST','GMT')) and
    d.demandid = ml.demandid and
    ma.aircraftid = ml.aircraftid and
    r.ownerreservationid = d.reservationid and
    c.contractid = r.CONTRACTID and
    o.ownerid = r.ownerid and
    fc.fractionalprogramid = c.fractionalprogramid and
    fm.fractionalprogramid = ma.fractionalprogramid and
    kwu.managedlegid(+) = ml.managedlegid and
    kw.legkeywordid(+) = kwu.legkeywordid
  union
  SELECT 
    o.ownerid,
    c.contractid,
    to_char(d.outtime,'YYYY/MM/DD HH24:MI') as dmd_outtime,
    to_char(lpl.actualout,'YYYY/MM/DD HH24:MI') as outtime,
    d.demandid,
    lpl.logpagelegid as otherid,
    'lpgleg' as type,
    o.shortname,
    kw.legkeywordid,
    kw.keyword as keyword,
    fc.SEQUENCEPOSITION as contract_seqpos,
    fc.name as contract_actype,
    fc.aircrafttypeid as contract_actypeid,
    fm.SEQUENCEPOSITION as seqpos,
    fm.name as actype,
    fm.aircrafttypeid as actypeid
  FROM 
    logpageleg lpl, demand d, logpage lp, aircraft ac, managedaircraft ma, ownerreservation r, contract c, owner o,
    fractionalprogram fc, fractionalprogram fm, logpagelegkeywordusage kwu, legkeyword kw
  WHERE 
    lpl.scheduledout > (new_time(sysdate,'EST','GMT')) and
    lpl.scheduledout < (new_time(sysdate + 4,'EST','GMT')) and
    d.demandid = lpl.demandid and
    lp.logpageid = lpl.logpageid and
    ac.registration = lp.registration and
    ma.aircraftid = ac.aircraftid and
    r.ownerreservationid = d.reservationid and
    c.contractid = r.CONTRACTID and
    o.ownerid = r.ownerid and
    fc.fractionalprogramid = c.fractionalprogramid and
    fm.fractionalprogramid = ma.fractionalprogramid and
    kwu.logpagelegid(+) = lpl.logpagelegid and
    kw.legkeywordid(+) = kwu.legkeywordid
) a,
demand dmd
where dmd.demandid = a.demandid
order by a.demandid;
spool off;
quit
