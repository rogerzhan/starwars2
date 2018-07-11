CREATE DEFINER=`root`@`127.0.0.1` PROCEDURE `proc_updateCrewdutydate`()
    MODIFIES SQL DATA
BEGIN

	IF (select count(*) from CrewSchedule_KINGAIR)>0 THEN  -- make sure the connection exists before delete the data
    
		SET @numOfDays = 30;
		SET @beginDate = date(sysdate());
		SET @endDate = date(date_add(sysdate(), interval @numOfDays day));

		DELETE FROM crewdutydate WHERE date(dutyDate) between @beginDate and @endDate;
		
		INSERT INTO crewdutydate (crewId, crewCode, regionCode, aircrafttypeid, dutydate, position, gsValue, qualification)
		
		SELECT f.crewId, a.crewCode, regionCode, aircrafttypeid, dutydate, position, gsValue, qualification
		FROM
		(
			-- kingAir
			SELECT
				c.crewCode, r.regionCode, 1 aircrafttypeid, dutyDate, r.position, c.gsValue, c.qualification
			FROM 
			(

				select c.id, c.crewCode, d.dutyDate, d.gsValue, c.qualification
				from
				(
					select id, trim(col_11) as crewCode, 
							case 
								when col_10 like '%dual%'then 3
								when col_10 ='p/l only' or (col_10 like '%p/l%' and col_10 like '%hours%' ) or col_10 like '%p/l qual%' then 1            
								when col_10 like '%fusion%' then 2 
								else 1 
							end as qualification
					from CrewSchedule_KINGAIR where length(trim(col_11)) =4
				) c
				inner join
				(
					select id, date_format(str_to_date((select col_12 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_12 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_13 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_13 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_14 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_14 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_15 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_15 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_16 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_16 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_17 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_17 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_18 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_18 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_19 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_19 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_20 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_20 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_21 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_21 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_22 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_22 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_23 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_23 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_24 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_24 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_25 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_25 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_26 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_26 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_27 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_27 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_28 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_28 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_29 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_29 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_30 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_30 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_31 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_31 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_32 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_32 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_33 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_33 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_34 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_34 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_35 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_35 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_36 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_36 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_37 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_37 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_38 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_38 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_39 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_39 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_40 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_40 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_41 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_41 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_42 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_42 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_43 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_43 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_44 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_44 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_45 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_45 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_46 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_46 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_47 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_47 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_48 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_48 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_49 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_49 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_50 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_50 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_51 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_51 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_52 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_52 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_53 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_53 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_54 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_54 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_55 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_55 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_56 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_56 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_57 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_57 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_58 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_58 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_59 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_59 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_60 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_60 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_61 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_61 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_62 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_62 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_63 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_63 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_64 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_64 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_65 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_65 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_66 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_66 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_67 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_67 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_68 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_68 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_69 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_69 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_70 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_70 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_71 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_71 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_72 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_72 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_73 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_73 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_74 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_74 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_75 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_75 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_76 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_76 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_77 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_77 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_78 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_78 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_79 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_79 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_80 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_80 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_81 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_81 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_82 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_82 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_83 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_83 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_84 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_84 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_85 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_85 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_86 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_86 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_87 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_87 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_88 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_88 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_89 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_89 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_90 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_90 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_91 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_91 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_92 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_92 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_93 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_93 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_94 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_94 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_95 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_95 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_96 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_96 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_97 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_97 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_98 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_98 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_99 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_99 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_100 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_100 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_101 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_101 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_102 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_102 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_103 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_103 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_104 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_104 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_105 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_105 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_106 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_106 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_107 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_107 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_108 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_108 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_109 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_109 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_110 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_110 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_111 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_111 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_112 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_112 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_113 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_113 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_114 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_114 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_115 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_115 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_116 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_116 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_117 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_117 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_118 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_118 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_119 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_119 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_120 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_120 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_121 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_121 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_122 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_122 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_123 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_123 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_124 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_124 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_125 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_125 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_126 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_126 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_127 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_127 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_128 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_128 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_129 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_129 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_130 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_130 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_131 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_131 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_132 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_132 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_133 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_133 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_134 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_134 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_135 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_135 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_136 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_136 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_137 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_137 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_138 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_138 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_139 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_139 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_140 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_140 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_141 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_141 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_142 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_142 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_143 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_143 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_144 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_144 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_145 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_145 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_146 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_146 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_147 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_147 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_148 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_148 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_149 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_149 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_150 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_150 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_151 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_151 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_152 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_152 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_153 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_153 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_154 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_154 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_155 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_155 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_156 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_156 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_157 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_157 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_158 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_158 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_159 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_159 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_160 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_160 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_161 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_161 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_162 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_162 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_163 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_163 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_164 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_164 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_165 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_165 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_166 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_166 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_167 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_167 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_168 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_168 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_169 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_169 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_170 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_170 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_171 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_171 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_172 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_172 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_173 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_173 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_174 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_174 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_175 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_175 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_176 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_176 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_177 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_177 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_178 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_178 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_179 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_179 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_180 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_180 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_181 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_181 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_182 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_182 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_183 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_183 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_184 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_184 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_185 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_185 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_186 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_186 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_187 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_187 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_188 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_188 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_189 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_189 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_190 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_190 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_191 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_191 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_192 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_192 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_193 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_193 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_194 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_194 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_195 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_195 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_196 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_196 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_197 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_197 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_198 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_198 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_199 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_199 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_200 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_200 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_201 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_201 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_202 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_202 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_203 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_203 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_204 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_204 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_205 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_205 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_206 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_206 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_207 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_207 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_208 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_208 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_209 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_209 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_210 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_210 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_211 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_211 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_212 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_212 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_213 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_213 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_214 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_214 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_215 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_215 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_216 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_216 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_217 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_217 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_218 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_218 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_219 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_219 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_220 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_220 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_221 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_221 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_222 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_222 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_223 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_223 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_224 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_224 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_225 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_225 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_226 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_226 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_227 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_227 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_228 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_228 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_229 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_229 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_230 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_230 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_231 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_231 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_232 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_232 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_233 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_233 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_234 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_234 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_235 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_235 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_236 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_236 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_237 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_237 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_238 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_238 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_239 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_239 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_240 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_240 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_241 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_241 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_242 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_242 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_243 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_243 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_244 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_244 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_245 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_245 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_246 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_246 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_247 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_247 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_248 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_248 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_249 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_249 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_250 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_250 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_251 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_251 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_252 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_252 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_253 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_253 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_254 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_254 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_255 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_255 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_256 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_256 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_257 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_257 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_258 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_258 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_259 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_259 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_260 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_260 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_261 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_261 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_262 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_262 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_263 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_263 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_264 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_264 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_265 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_265 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_266 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_266 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_267 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_267 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_268 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_268 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_269 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_269 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_270 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_270 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_271 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_271 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_272 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_272 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_273 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_273 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_274 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_274 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_275 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_275 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_276 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_276 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_277 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_277 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_278 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_278 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_279 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_279 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_280 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_280 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_281 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_281 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_282 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_282 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_283 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_283 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_284 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_284 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_285 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_285 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_286 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_286 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_287 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_287 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_288 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_288 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_289 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_289 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_290 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_290 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_291 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_291 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_292 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_292 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_293 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_293 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_294 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_294 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_295 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_295 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_296 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_296 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_297 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_297 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_298 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_298 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_299 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_299 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_300 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_300 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_301 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_301 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_302 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_302 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_303 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_303 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_304 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_304 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_305 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_305 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_306 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_306 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_307 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_307 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_308 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_308 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_309 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_309 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_310 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_310 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_311 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_311 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_312 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_312 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_313 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_313 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_314 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_314 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_315 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_315 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_316 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_316 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_317 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_317 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_318 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_318 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_319 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_319 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_320 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_320 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_321 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_321 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_322 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_322 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_323 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_323 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_324 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_324 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_325 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_325 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_326 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_326 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_327 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_327 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_328 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_328 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_329 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_329 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_330 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_330 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_331 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_331 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_332 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_332 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_333 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_333 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_334 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_334 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_335 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_335 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_336 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_336 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_337 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_337 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_338 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_338 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_339 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_339 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_340 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_340 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_341 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_341 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_342 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_342 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_343 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_343 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_344 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_344 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_345 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_345 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_346 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_346 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_347 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_347 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_348 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_348 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_349 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_349 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_350 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_350 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_351 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_351 gsValue from CrewSchedule_KINGAIR union all
 					select id, date_format(str_to_date((select col_352 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_352 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_353 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_353 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_354 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_354 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_355 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_355 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_356 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_356 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_357 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_357 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_358 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_358 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_359 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_359 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_360 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_360 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_361 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_361 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_362 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_362 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_363 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_363 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_364 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_364 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_365 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_365 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_366 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_366 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_367 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_367 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_368 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_368 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_369 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_369 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_370 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_370 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_371 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_371 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_372 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_372 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_373 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_373 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_374 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_374 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_375 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_375 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_376 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_376 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_377 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_377 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_378 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_378 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_379 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_379 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_380 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_380 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_381 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_381 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_382 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_382 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_383 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_383 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_384 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_384 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_385 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_385 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_386 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_386 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_387 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_387 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_388 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_388 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_389 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_389 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_390 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_390 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_391 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_391 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_392 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_392 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_393 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_393 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_394 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_394 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_395 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_395 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_396 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_396 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_397 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_397 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_398 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_398 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_399 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_399 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_400 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_400 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_401 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_401 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_402 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_402 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_403 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_403 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_404 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_404 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_405 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_405 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_406 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_406 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_407 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_407 gsValue from CrewSchedule_KINGAIR union all
					select id, date_format(str_to_date((select col_408 from CrewSchedule_KINGAIR where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_408 gsValue from CrewSchedule_KINGAIR                   
				) d
				on c.id = d.id
				where d.gsValue between 1 and 9 OR d.gsValue in ('OT', 'A/OT', 'AOT', 'AOT/VAC')
			) c
			CROSS JOIN
			(
				select a.regionPosition, a.rowId as beginRowId, ifnull(b.rowId, 1000) as nextRowId, 
						case 
							when upper(a.regionPosition)='IOE' then 'IOE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='NORTHEAST' then 'NE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='SOUTHEAST' then 'SE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='MIDWEST' then 'MW'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='WEST' then 'W'
							when a.regionPosition ='CHECKAIRMEN' then 'CA'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='STANDARD' then 'SC'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='FUSION' then 'F'
							else null
						end regionCode,
						case when upper(right(a.regionPosition,3)) ='SIC' or upper(right(a.regionPosition,3)) ='IOE' then 'SIC' else 'PIC' end as position
				from
				(
					select t.*, @rownum := @rownum + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_KINGAIR 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE', 'FUSION ONLY-N857UP AND BEYOND','FUSION ONLY SIC','FUSION ONLY IOE')
						order by id
					) t, ( select @rownum := 0 ) r
				) a
				left join
				(
					select t.*, @rownum2 := @rownum2 + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_KINGAIR 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE', 'FUSION ONLY-N857UP AND BEYOND','FUSION ONLY SIC','FUSION ONLY IOE')
						order by id
					) t, ( select @rownum2 := 0 ) r
				) b
				on a.rowNum+1 = b.rowNum
			) r
			WHERE c.id > r.beginRowId and c.id < r.nextRowId		
			AND date(dutyDate) between @beginDate and @endDate
			
			UNION ALL  

			-- EXCEL
			SELECT
				c.crewCode, r.regionCode, 50 aircrafttypeid, dutyDate, r.position, c.gsValue, c.qualification
			FROM 
			(

				select c.id, c.crewCode, d.dutyDate, d.gsValue, c.qualification
				from
				(
					select id, trim(col_11) as crewCode, null as qualification from CrewSchedule_EXCEL where length(trim(col_11)) =4
				) c
				inner join
				(
					select id, date_format(str_to_date((select col_12 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_12 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_13 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_13 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_14 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_14 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_15 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_15 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_16 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_16 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_17 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_17 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_18 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_18 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_19 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_19 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_20 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_20 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_21 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_21 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_22 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_22 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_23 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_23 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_24 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_24 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_25 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_25 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_26 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_26 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_27 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_27 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_28 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_28 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_29 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_29 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_30 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_30 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_31 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_31 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_32 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_32 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_33 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_33 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_34 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_34 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_35 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_35 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_36 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_36 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_37 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_37 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_38 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_38 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_39 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_39 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_40 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_40 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_41 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_41 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_42 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_42 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_43 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_43 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_44 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_44 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_45 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_45 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_46 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_46 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_47 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_47 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_48 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_48 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_49 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_49 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_50 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_50 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_51 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_51 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_52 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_52 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_53 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_53 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_54 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_54 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_55 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_55 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_56 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_56 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_57 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_57 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_58 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_58 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_59 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_59 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_60 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_60 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_61 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_61 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_62 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_62 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_63 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_63 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_64 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_64 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_65 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_65 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_66 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_66 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_67 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_67 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_68 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_68 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_69 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_69 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_70 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_70 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_71 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_71 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_72 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_72 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_73 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_73 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_74 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_74 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_75 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_75 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_76 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_76 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_77 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_77 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_78 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_78 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_79 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_79 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_80 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_80 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_81 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_81 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_82 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_82 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_83 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_83 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_84 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_84 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_85 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_85 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_86 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_86 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_87 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_87 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_88 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_88 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_89 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_89 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_90 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_90 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_91 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_91 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_92 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_92 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_93 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_93 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_94 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_94 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_95 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_95 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_96 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_96 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_97 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_97 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_98 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_98 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_99 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_99 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_100 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_100 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_101 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_101 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_102 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_102 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_103 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_103 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_104 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_104 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_105 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_105 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_106 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_106 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_107 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_107 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_108 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_108 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_109 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_109 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_110 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_110 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_111 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_111 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_112 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_112 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_113 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_113 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_114 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_114 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_115 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_115 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_116 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_116 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_117 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_117 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_118 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_118 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_119 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_119 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_120 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_120 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_121 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_121 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_122 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_122 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_123 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_123 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_124 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_124 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_125 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_125 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_126 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_126 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_127 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_127 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_128 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_128 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_129 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_129 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_130 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_130 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_131 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_131 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_132 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_132 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_133 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_133 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_134 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_134 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_135 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_135 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_136 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_136 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_137 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_137 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_138 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_138 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_139 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_139 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_140 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_140 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_141 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_141 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_142 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_142 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_143 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_143 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_144 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_144 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_145 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_145 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_146 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_146 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_147 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_147 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_148 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_148 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_149 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_149 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_150 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_150 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_151 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_151 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_152 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_152 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_153 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_153 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_154 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_154 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_155 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_155 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_156 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_156 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_157 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_157 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_158 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_158 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_159 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_159 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_160 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_160 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_161 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_161 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_162 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_162 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_163 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_163 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_164 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_164 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_165 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_165 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_166 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_166 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_167 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_167 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_168 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_168 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_169 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_169 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_170 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_170 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_171 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_171 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_172 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_172 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_173 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_173 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_174 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_174 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_175 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_175 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_176 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_176 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_177 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_177 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_178 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_178 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_179 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_179 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_180 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_180 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_181 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_181 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_182 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_182 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_183 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_183 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_184 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_184 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_185 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_185 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_186 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_186 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_187 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_187 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_188 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_188 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_189 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_189 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_190 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_190 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_191 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_191 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_192 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_192 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_193 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_193 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_194 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_194 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_195 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_195 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_196 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_196 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_197 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_197 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_198 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_198 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_199 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_199 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_200 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_200 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_201 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_201 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_202 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_202 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_203 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_203 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_204 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_204 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_205 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_205 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_206 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_206 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_207 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_207 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_208 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_208 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_209 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_209 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_210 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_210 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_211 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_211 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_212 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_212 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_213 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_213 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_214 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_214 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_215 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_215 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_216 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_216 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_217 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_217 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_218 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_218 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_219 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_219 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_220 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_220 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_221 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_221 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_222 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_222 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_223 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_223 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_224 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_224 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_225 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_225 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_226 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_226 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_227 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_227 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_228 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_228 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_229 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_229 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_230 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_230 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_231 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_231 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_232 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_232 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_233 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_233 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_234 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_234 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_235 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_235 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_236 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_236 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_237 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_237 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_238 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_238 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_239 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_239 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_240 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_240 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_241 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_241 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_242 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_242 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_243 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_243 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_244 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_244 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_245 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_245 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_246 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_246 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_247 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_247 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_248 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_248 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_249 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_249 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_250 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_250 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_251 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_251 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_252 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_252 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_253 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_253 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_254 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_254 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_255 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_255 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_256 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_256 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_257 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_257 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_258 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_258 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_259 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_259 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_260 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_260 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_261 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_261 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_262 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_262 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_263 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_263 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_264 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_264 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_265 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_265 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_266 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_266 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_267 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_267 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_268 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_268 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_269 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_269 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_270 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_270 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_271 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_271 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_272 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_272 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_273 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_273 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_274 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_274 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_275 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_275 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_276 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_276 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_277 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_277 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_278 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_278 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_279 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_279 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_280 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_280 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_281 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_281 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_282 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_282 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_283 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_283 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_284 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_284 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_285 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_285 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_286 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_286 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_287 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_287 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_288 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_288 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_289 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_289 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_290 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_290 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_291 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_291 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_292 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_292 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_293 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_293 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_294 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_294 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_295 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_295 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_296 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_296 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_297 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_297 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_298 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_298 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_299 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_299 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_300 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_300 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_301 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_301 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_302 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_302 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_303 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_303 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_304 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_304 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_305 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_305 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_306 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_306 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_307 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_307 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_308 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_308 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_309 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_309 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_310 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_310 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_311 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_311 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_312 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_312 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_313 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_313 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_314 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_314 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_315 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_315 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_316 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_316 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_317 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_317 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_318 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_318 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_319 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_319 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_320 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_320 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_321 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_321 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_322 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_322 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_323 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_323 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_324 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_324 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_325 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_325 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_326 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_326 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_327 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_327 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_328 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_328 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_329 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_329 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_330 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_330 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_331 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_331 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_332 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_332 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_333 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_333 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_334 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_334 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_335 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_335 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_336 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_336 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_337 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_337 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_338 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_338 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_339 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_339 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_340 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_340 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_341 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_341 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_342 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_342 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_343 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_343 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_344 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_344 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_345 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_345 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_346 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_346 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_347 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_347 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_348 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_348 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_349 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_349 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_350 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_350 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_351 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_351 gsValue from CrewSchedule_EXCEL union all
 					select id, date_format(str_to_date((select col_352 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_352 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_353 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_353 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_354 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_354 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_355 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_355 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_356 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_356 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_357 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_357 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_358 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_358 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_359 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_359 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_360 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_360 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_361 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_361 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_362 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_362 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_363 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_363 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_364 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_364 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_365 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_365 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_366 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_366 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_367 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_367 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_368 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_368 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_369 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_369 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_370 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_370 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_371 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_371 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_372 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_372 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_373 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_373 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_374 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_374 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_375 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_375 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_376 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_376 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_377 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_377 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_378 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_378 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_379 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_379 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_380 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_380 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_381 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_381 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_382 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_382 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_383 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_383 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_384 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_384 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_385 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_385 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_386 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_386 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_387 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_387 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_388 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_388 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_389 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_389 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_390 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_390 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_391 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_391 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_392 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_392 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_393 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_393 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_394 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_394 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_395 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_395 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_396 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_396 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_397 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_397 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_398 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_398 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_399 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_399 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_400 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_400 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_401 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_401 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_402 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_402 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_403 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_403 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_404 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_404 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_405 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_405 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_406 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_406 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_407 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_407 gsValue from CrewSchedule_EXCEL union all
					select id, date_format(str_to_date((select col_408 from CrewSchedule_EXCEL where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_408 gsValue from CrewSchedule_EXCEL				) d
				on c.id = d.id
				where d.gsValue between 1 and 9 OR d.gsValue in ('OT', 'A/OT', 'AOT', 'AOT/VAC')
			) c
			CROSS JOIN
			(
				select a.regionPosition, a.rowId as beginRowId, ifnull(b.rowId, 1000) as nextRowId, 
						case 
							when upper(a.regionPosition)='IOE' then 'IOE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='NORTHEAST' then 'NE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='SOUTHEAST' then 'SE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='MIDWEST' then 'MW'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='WEST' then 'W'
							when a.regionPosition ='CHECKAIRMEN' then 'CA'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='STANDARD' then 'SC'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='FUSION' then 'F'
							else null
						end regionCode,
						case when upper(right(a.regionPosition,3)) ='SIC' or upper(right(a.regionPosition,3)) ='IOE' then 'SIC' else 'PIC' end as position
				from
				(
					select t.*, @rownum := @rownum + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_EXCEL 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE')
						order by id
					) t, ( select @rownum := 0 ) r
				) a
				left join
				(
					select t.*, @rownum2 := @rownum2 + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_EXCEL 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE')
						order by id
					) t, ( select @rownum2 := 0 ) r
				) b
				on a.rowNum+1 = b.rowNum
			) r
			WHERE c.id > r.beginRowId and c.id < r.nextRowId
			AND date(dutyDate) between @beginDate and @endDate
            
            UNION ALL  
			
            -- CITATIONX
			SELECT
				c.crewCode, r.regionCode, 30 aircrafttypeid, dutyDate, r.position, c.gsValue, c.qualification
			FROM 
			(

				select c.id, c.crewCode, d.dutyDate, d.gsValue, c.qualification
				from
				(
					select id, trim(col_11) as crewCode, null as qualification from CrewSchedule_CITATIONX where length(trim(col_11)) =4
				) c
				inner join
				(
					select id, date_format(str_to_date((select col_12 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_12 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_13 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_13 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_14 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_14 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_15 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_15 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_16 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_16 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_17 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_17 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_18 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_18 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_19 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_19 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_20 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_20 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_21 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_21 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_22 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_22 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_23 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_23 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_24 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_24 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_25 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_25 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_26 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_26 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_27 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_27 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_28 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_28 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_29 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_29 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_30 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_30 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_31 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_31 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_32 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_32 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_33 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_33 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_34 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_34 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_35 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_35 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_36 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_36 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_37 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_37 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_38 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_38 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_39 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_39 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_40 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_40 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_41 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_41 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_42 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_42 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_43 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_43 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_44 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_44 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_45 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_45 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_46 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_46 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_47 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_47 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_48 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_48 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_49 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_49 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_50 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_50 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_51 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_51 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_52 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_52 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_53 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_53 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_54 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_54 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_55 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_55 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_56 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_56 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_57 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_57 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_58 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_58 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_59 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_59 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_60 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_60 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_61 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_61 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_62 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_62 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_63 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_63 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_64 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_64 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_65 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_65 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_66 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_66 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_67 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_67 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_68 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_68 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_69 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_69 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_70 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_70 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_71 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_71 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_72 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_72 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_73 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_73 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_74 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_74 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_75 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_75 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_76 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_76 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_77 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_77 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_78 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_78 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_79 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_79 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_80 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_80 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_81 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_81 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_82 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_82 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_83 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_83 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_84 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_84 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_85 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_85 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_86 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_86 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_87 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_87 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_88 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_88 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_89 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_89 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_90 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_90 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_91 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_91 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_92 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_92 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_93 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_93 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_94 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_94 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_95 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_95 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_96 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_96 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_97 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_97 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_98 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_98 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_99 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_99 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_100 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_100 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_101 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_101 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_102 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_102 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_103 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_103 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_104 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_104 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_105 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_105 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_106 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_106 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_107 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_107 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_108 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_108 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_109 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_109 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_110 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_110 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_111 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_111 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_112 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_112 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_113 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_113 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_114 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_114 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_115 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_115 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_116 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_116 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_117 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_117 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_118 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_118 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_119 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_119 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_120 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_120 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_121 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_121 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_122 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_122 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_123 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_123 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_124 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_124 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_125 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_125 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_126 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_126 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_127 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_127 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_128 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_128 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_129 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_129 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_130 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_130 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_131 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_131 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_132 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_132 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_133 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_133 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_134 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_134 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_135 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_135 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_136 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_136 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_137 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_137 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_138 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_138 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_139 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_139 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_140 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_140 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_141 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_141 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_142 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_142 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_143 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_143 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_144 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_144 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_145 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_145 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_146 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_146 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_147 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_147 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_148 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_148 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_149 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_149 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_150 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_150 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_151 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_151 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_152 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_152 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_153 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_153 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_154 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_154 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_155 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_155 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_156 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_156 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_157 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_157 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_158 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_158 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_159 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_159 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_160 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_160 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_161 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_161 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_162 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_162 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_163 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_163 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_164 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_164 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_165 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_165 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_166 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_166 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_167 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_167 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_168 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_168 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_169 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_169 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_170 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_170 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_171 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_171 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_172 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_172 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_173 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_173 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_174 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_174 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_175 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_175 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_176 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_176 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_177 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_177 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_178 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_178 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_179 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_179 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_180 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_180 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_181 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_181 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_182 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_182 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_183 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_183 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_184 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_184 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_185 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_185 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_186 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_186 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_187 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_187 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_188 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_188 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_189 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_189 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_190 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_190 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_191 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_191 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_192 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_192 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_193 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_193 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_194 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_194 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_195 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_195 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_196 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_196 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_197 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_197 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_198 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_198 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_199 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_199 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_200 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_200 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_201 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_201 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_202 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_202 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_203 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_203 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_204 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_204 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_205 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_205 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_206 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_206 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_207 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_207 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_208 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_208 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_209 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_209 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_210 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_210 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_211 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_211 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_212 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_212 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_213 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_213 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_214 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_214 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_215 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_215 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_216 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_216 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_217 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_217 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_218 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_218 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_219 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_219 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_220 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_220 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_221 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_221 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_222 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_222 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_223 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_223 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_224 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_224 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_225 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_225 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_226 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_226 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_227 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_227 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_228 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_228 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_229 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_229 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_230 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_230 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_231 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_231 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_232 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_232 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_233 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_233 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_234 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_234 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_235 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_235 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_236 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_236 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_237 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_237 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_238 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_238 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_239 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_239 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_240 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_240 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_241 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_241 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_242 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_242 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_243 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_243 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_244 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_244 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_245 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_245 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_246 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_246 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_247 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_247 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_248 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_248 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_249 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_249 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_250 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_250 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_251 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_251 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_252 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_252 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_253 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_253 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_254 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_254 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_255 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_255 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_256 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_256 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_257 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_257 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_258 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_258 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_259 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_259 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_260 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_260 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_261 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_261 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_262 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_262 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_263 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_263 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_264 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_264 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_265 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_265 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_266 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_266 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_267 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_267 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_268 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_268 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_269 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_269 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_270 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_270 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_271 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_271 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_272 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_272 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_273 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_273 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_274 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_274 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_275 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_275 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_276 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_276 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_277 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_277 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_278 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_278 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_279 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_279 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_280 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_280 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_281 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_281 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_282 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_282 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_283 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_283 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_284 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_284 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_285 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_285 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_286 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_286 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_287 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_287 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_288 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_288 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_289 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_289 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_290 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_290 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_291 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_291 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_292 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_292 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_293 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_293 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_294 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_294 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_295 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_295 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_296 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_296 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_297 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_297 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_298 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_298 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_299 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_299 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_300 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_300 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_301 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_301 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_302 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_302 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_303 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_303 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_304 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_304 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_305 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_305 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_306 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_306 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_307 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_307 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_308 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_308 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_309 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_309 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_310 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_310 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_311 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_311 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_312 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_312 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_313 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_313 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_314 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_314 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_315 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_315 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_316 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_316 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_317 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_317 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_318 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_318 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_319 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_319 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_320 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_320 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_321 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_321 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_322 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_322 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_323 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_323 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_324 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_324 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_325 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_325 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_326 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_326 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_327 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_327 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_328 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_328 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_329 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_329 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_330 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_330 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_331 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_331 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_332 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_332 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_333 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_333 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_334 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_334 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_335 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_335 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_336 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_336 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_337 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_337 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_338 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_338 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_339 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_339 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_340 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_340 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_341 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_341 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_342 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_342 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_343 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_343 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_344 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_344 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_345 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_345 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_346 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_346 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_347 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_347 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_348 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_348 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_349 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_349 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_350 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_350 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_351 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_351 gsValue from CrewSchedule_CITATIONX union all
 					select id, date_format(str_to_date((select col_352 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_352 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_353 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_353 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_354 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_354 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_355 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_355 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_356 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_356 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_357 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_357 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_358 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_358 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_359 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_359 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_360 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_360 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_361 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_361 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_362 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_362 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_363 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_363 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_364 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_364 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_365 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_365 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_366 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_366 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_367 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_367 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_368 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_368 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_369 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_369 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_370 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_370 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_371 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_371 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_372 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_372 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_373 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_373 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_374 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_374 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_375 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_375 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_376 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_376 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_377 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_377 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_378 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_378 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_379 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_379 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_380 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_380 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_381 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_381 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_382 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_382 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_383 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_383 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_384 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_384 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_385 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_385 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_386 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_386 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_387 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_387 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_388 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_388 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_389 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_389 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_390 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_390 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_391 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_391 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_392 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_392 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_393 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_393 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_394 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_394 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_395 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_395 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_396 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_396 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_397 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_397 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_398 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_398 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_399 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_399 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_400 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_400 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_401 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_401 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_402 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_402 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_403 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_403 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_404 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_404 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_405 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_405 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_406 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_406 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_407 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_407 gsValue from CrewSchedule_CITATIONX union all
					select id, date_format(str_to_date((select col_408 from CrewSchedule_CITATIONX where id = 3),'%m/%d/%Y'), '%Y/%m/%d %H:%i') as dutyDate, col_408 gsValue from CrewSchedule_CITATIONX				) d
				on c.id = d.id
				where d.gsValue between 1 and 9 OR d.gsValue in ('OT', 'A/OT', 'AOT', 'AOT/VAC')
			) c
			CROSS JOIN
			(
				select a.regionPosition, a.rowId as beginRowId, ifnull(b.rowId, 1000) as nextRowId, 
						case 
							when upper(a.regionPosition)='IOE' then 'IOE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='NORTHEAST' then 'NE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='SOUTHEAST' then 'SE'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='MIDWEST' then 'MW'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='WEST' then 'W'
							when a.regionPosition ='CHECKAIRMEN' then 'CA'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='STANDARD' then 'SC'
							when left(a.regionPosition, locate(' ',a.regionPosition) - 1) ='FUSION' then 'F'
							else null
						end regionCode,
						case when upper(right(a.regionPosition,3)) ='SIC' or upper(right(a.regionPosition,3)) ='IOE' then 'SIC' else 'PIC' end as position
				from
				(
					select t.*, @rownum := @rownum + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_CITATIONX 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE')
						order by id
					) t, ( select @rownum := 0 ) r
				) a
				left join
				(
					select t.*, @rownum2 := @rownum2 + 1 AS rowNum
					from
					(
						select id as rowId , trim(col_10) as regionPosition
						from CrewSchedule_CITATIONX 
						where (length(col_11) is null or length(col_11) < 4) -- no crewCode
						and upper(trim(col_10)) in ('NORTHEAST PIC','NORTHEAST SIC','SOUTHEAST PIC','SOUTHEAST SIC','MIDWEST PIC', 'MIDWEST PIC','MIDWEST SIC','WEST PIC','WEST SIC','CHECKAIRMEN','STANDARD CAPT.','IOE')
						order by id
					) t, ( select @rownum2 := 0 ) r
				) b
				on a.rowNum+1 = b.rowNum
			) r
			WHERE c.id > r.beginRowId and c.id < r.nextRowId
			AND date(dutyDate) between @beginDate and @endDate
		) a
		LEFT JOIN flightcrew f
		ON a.crewCode = f.crewCode;
	END IF;
END