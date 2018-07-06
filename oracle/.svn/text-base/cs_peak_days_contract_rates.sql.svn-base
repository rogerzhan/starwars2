set FEEDBACK OFF
set LINESIZE 32767
set PAGESIZE 0
set underline off
set COLSEP |
set TRIM ON
set TRIMS ON
set HEADING OFF
set TERMOUT OFF
spool cs_peak_days_contract_rates.out
select
	c.contractid,
	p.level_id,
	p.hourly_rate,
	p.flex_hours
from
	contract c,
	xref_contract x,
	cs_peak_days_contract_rates@bwtoapps p
where
	x.bitwise_number = c.contractid and
	p.chr_id = x.oracle_number
order by
	contractid, level_id;
spool off;
quit
