#!/usr/bin/bash
ORACLE_HOME=c:/oracle/ora81
cd c:/optimizer/oracle
export ORACLE_HOME
for table in cs_crew_duty timezone upgradedowngrade ratios cs_peak_days_contract_rates
do
sqlplus -S "csbitwise/csbitwise@csfltops_bk" "@${table}.sql" &
done

for table in ssoftcrew
do
sqlplus -S "csreadonly/deeppurp1e@ssoft" "@${table}.sql" &
done

wait

for table in cs_crew_duty timezone upgradedowngrade ratios cs_peak_days_contract_rates ssoftcrew
do
	#tr -d "\r" <"${table}.out" | white '|' | sed -e 's/||/|\\N|/g' -e 's/||/|\\N|/g' -e 's/||/|\\N|/g' -e 's/||/|\\N|/g' -e 's/|/	/g' >"${table}.txt"
	tr -d "\r" <"${table}.out" | white '|' | sed -e 's/|/	/g' >"${table}.txt"
done
exit 0
