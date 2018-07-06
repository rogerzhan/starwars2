set FEEDBACK OFF
set LINESIZE 32767
set PAGESIZE 0
set underline off
set COLSEP |
set TRIM ON
set TRIMS ON
set HEADING OFF
set TERMOUT OFF
spool ratios.out
SELECT distinct
  b.bitwise_number as contractid,
  d.aircrafttypeid as contract_ac_typeid,
  d.name as contract_plane_name,
  e.aircrafttypeid as ac_typeid,
  e.name plane_flown_name,
  c.RATIO
FROM 
  okc_k_articles_b@bwtoapps a,
  xref_contract b,
  cs_conversion_ratio c,
  aircrafttype d,
  aircrafttype e,
  contract f,
  fractionalprogram g
WHERE 
  a.dnz_chr_id = b.oracle_number and
  a.attribute1 is not null and
  upper(attribute_category) = upper('program hours') and
  b.BITWISE_NUMBER in(
    SELECT
        distinct r.contractid
      FROM
        demand d,
        ownerreservation r
      WHERE
        d.outtime > (new_time(sysdate,'EST','GMT')) and
        d.outtime < (new_time(sysdate + 4,'EST','GMT')) and
        r.ownerreservationid = d.reservationid
      ) and
  c.CONTRACT_TYPE = a.attribute1 and
  c.plane_own = d.aircrafttypeid and
  c.PLANE_FLOWN = e.AIRCRAFTTYPEID and
  b.BITWISE_NUMBER = f.contractid and
  f.FRACTIONALPROGRAMID = g.FRACTIONALPROGRAMID and
  g.AIRCRAFTTYPEID = c.PLANE_OWN and
  e.aircrafttypeid not in(2,8) and
  d.aircrafttypeid not in(2,8)
order by b.bitwise_number;
spool off;
quit
