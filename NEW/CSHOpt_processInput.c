#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include "datetime.h"
#include "my_mysql.h"
#include "params.h"
#include "split.h"
#include "logMsg.h"
#include "localTime.h"
#include "airportLatLon.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_processInput.h"
#include "fuelStop.h"
#include "doFlightCalc.h"
#include "CSHOpt_output.h"

extern FILE *logFile;
extern OptParameters optParam;
extern time_t tp;
extern time_t run_time_t;
extern int maxDemandID;
int actualMaxDemandID;

extern AircraftType *acTypeList; //sorted by sequencePosition
extern Aircraft *acList;
extern Crew *crewList;
extern CrewPair *crewPairList;
extern Demand *demandList;
extern Exclusion *exclusionList;
extern Leg *legList;
extern MaintenanceRecord *maintList;
extern int numAcTypes;
extern int numAircraft;
extern int numCrew;
extern int numCrewPairs;
extern int numDemand;
extern int numFakeDemand;
int numOptDemand;
extern int numExclusion;
extern int numLegs;
extern int numMaintenanceRecord;
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;
extern CrewEndTourRecord *crewEndTourList; // 11/14/07 ANG
extern int crewEndTourCount; // 11/14/07 ANG
extern int numFakeMaintenanceRecord; // 11/14/07 ANG
//extern McpAircraft *mcpAircraftList; //03/12/08 ANG
extern double contingency_prob[ACTYPE_NUM];
extern int numMacInfo;

int multiCrewAC = 0;

time_t firstMidnight;
time_t firstEndOfDay;
time_t endOfToday;

int month;
int lastTripOfDay[MAX_WINDOW_DURATION]; //index in demandList of last trip of each day
int lastTripToday, lastTripTomorrow;
int **aptExcl; //stores fleet-airport exclusions (first index is airport, second index is fleet)
int **aptCurf; //stores airport curfews (first (row) index is airport, first column is number of curfews, remaining cols are start and end of curfews

static int testDSTChange(void);
static int getAddlDemandData1(void);
static int calcFirstMidnightAndMonth(void);
static int processLegs(void);
static int createAppointDemand(void);
static int processCrews(void);
static int sortDemand(void);
static int findlastTripOfDay(void);
static int getAddlACData(void);
static int storeAircraftAndCrewPairInclusions(void);
static int processExclusionList(void);
static int getAddlDemandData2(void);
static void getEarlyAndLateDepartTimes(int i, int j);
static int getInternationalExclusions(void);


/********************************************************************************
*	Function  initializeStructs    Written BGC, Date last modified: 11/30/06 SWO*
*	Purpose:  Initialize all computed variables.								*
********************************************************************************/
static int initializeStructs (void)
{
	int i, j, a;
	for (i=0; i<(2*numDemand) + (2*numMaintenanceRecord); i++)
	{
		demandList[i].feasible = 0;
		demandList[i].aircraftID = 0;
		demandList[i].acInd = -1;
		demandList[i].crewPairID = 0;
		demandList[i].isAppoint = 0; 
		demandList[i].changePenalty = 0;
		demandList[i].redPenACID = 0;
		demandList[i].redPenCrewPairID = 0;
		demandList[i].inLockedTour = 0;
		for (j=0; j<numAcTypes; j++)
		{
			demandList[i].blockTm[j] = INFINITY;
			demandList[i].elapsedTm[j] = INFINITY;
			demandList[i].cost[j] = INFINITY;
			//demandList[i].macOprCost[j] = INFINITY; //MAC - 08/19/08 ANG
			//demandList[i].macOwnerCost[j] = INFINITY; //MAC - 08/19/08 ANG
		}
		for (a=0; a<numMacInfo; a++){
			demandList[i].macOprCost[a] = INFINITY; //MAC - 08/19/08 ANG
		}
		demandList[i].predDemID = 0;
		demandList[i].succDemID = 0;
		demandList[i].puSArcList = NULL;
		demandList[i].puEArcList = NULL;
		demandList[i].puSDual = NULL;
		demandList[i].puEDual = NULL;
		demandList[i].dual = 0;
		for (j=0; j<MAX_AC_TYPES; j++)
		{
			demandList[i].numPUSArcs[j] = 0;
			demandList[i].numPUEArcs[j] = 0;
		}
		demandList[i].maintenanceRecordID = 0;
	}


	for (i=0; i<numCrew; i++)
	{
		crewList[i].acTypeIndex = -1;
		crewList[i].numExcl = 0;
		for (j=0; j<MAX_LEG_EXCL; j++)
			crewList[i].exclDemandInd[j] = -1;
	}

	for (i=0; i<numCrewPairs; i++)
	{
		crewPairList[i].crewListInd[0] = -1; 
		crewPairList[i].crewListInd[1] = -1; 
		crewPairList[i].optAircraftID = 0;
		crewPairList[i].acInd = -1;
		crewPairList[i].acTypeIndex = -1;
		crewPairList[i].availAptID = 0;
		crewPairList[i].availDT = 0;
		crewPairList[i].activityCode = -1;
		crewPairList[i].dutyTime = 0;
		crewPairList[i].blockTm = 0;
		crewPairList[i].numIncl = 0;
		for (j=0; j<MAX_LEGS; j++)
		{
			crewPairList[i].schedLegIndList[j] = -1;
			crewPairList[i].schedLegACIndList[j] = -1;
		}
		for(j = 0; j<MAX_LEG_INCL; j++)
			crewPairList[i].inclDemandInd[j] = -1;
		crewPairList[i].crewPlaneList = NULL;
		crewPairList[i].crewPUSList = NULL;
		crewPairList[i].crewPUEList = NULL;
		crewPairList[i].numPlaneArcs = 0;
		crewPairList[i].numPUStartArcs = 0;
		crewPairList[i].numPUEndArcs = 0;
		crewPairList[i].getHomeCost = NULL;
		crewPairList[i].nodeStartIndex = -1;
		crewPairList[i].dual = 0;
	}
	
	for(i = 0; i<numAircraft; i++){	
		acList[i].firstCrPrID = 0;
		acList[i].firstCrPrInd = -1; 
		acList[i].applyCPACbonus = 0; //CPAC - 06/17/09 ANG
		acList[i].cpIndCPACbonus = -1; //CPAC - 06/17/09 ANG
	}
	return 0;
}

/********************************************************************************
*	Function   processInputData              Date last modified:   3/27/06 SWO	*
*	Purpose:   Initialize and populate structures and global variables			*
*				with fields that are derived from the raw input data.			*
********************************************************************************/
int processInputData(void)
{
	initializeStructs ();
	getAddlDemandData1();
	calcFirstMidnightAndMonth();
	processLegs(); 
	processCrews();
	createAppointDemand();
	sortDemand();
	findlastTripOfDay();
	getAddlACData();
	storeAircraftAndCrewPairInclusions();
	processExclusionList(); 
	getAddlDemandData2();
	getInternationalExclusions();
	//printAcList();
	return 0;
}



/********************************************************************************
*	Function   getAddlDemandData1              Date last modified:  2/26/07 SWO	*
*	Purpose:	Update reqOut for trips in recovery, and populate turnTime		*
********************************************************************************/
static int getAddlDemandData1(void)
{
	int i, j, k;
	int days, hours, minutes, seconds, msecs, timediff; //05/28/08 ANG

	for(i=0; i<numDemand; i++)
	{
		if(demandList[i].reqOut < optParam.windowStart){//if trip is "in recovery"
			//demandList[i].reqOut = optParam.windowStart; // replaced by codes below - 05/28/08 ANG
			dt_dateTimeDiff(dt_time_tToDateTime(demandList[i].reqOut), dt_time_tToDateTime(demandList[i].reqIn), &days, &hours, &minutes, &seconds, &msecs);
			timediff = (24 * 60 * 60 * days) + (60 * 60 * hours) + (60 * minutes); //in seconds
			demandList[i].reqOut = optParam.windowStart;
			demandList[i].reqIn = demandList[i].reqOut + timediff;
		}//this will be updated to schedOut if scheduler has planned and locked the leg
		demandList[i].turnTime = optParam.turnTime;
	
		//TEMPZHAN
		if (demandList[i].recoveryFlag){
			for (j=0; j<numAcTypes; j++){
				if (demandList[i].aircraftTypeID == acTypeList[j].aircraftTypeID){
					demandList[i].downgradeRecovery = acTypeList[j].downgradeRecovery;
					demandList[i].upgradeRecovery = acTypeList[j].upgradeRecovery;
					demandList[i].elapsedTm[j] = (int)(demandList[i].reqIn - demandList[i].reqOut)/60; //will be repopulated by new flightTime
					for (k = 1; k <= acTypeList[j].downgradeRecovery; k++){
						//demandList[i].incRev[j-k] = - optParam.downgradePenaltyRatio * demandList[i].elapsedTm[j]/60 * max(0, acTypeList[j].operatingCost - acTypeList[j-k].operatingCost);
						demandList[i].incRev[j-k] = - (optParam.downgradePenaltyRatio + k * 0.1) * demandList[i].elapsedTm[j]/60 * acTypeList[j].charterCost;
					}
					break;
				}
			}
		}

	}

	return 0;
}


/****************************************************************************************************
*	Function   calcFirstMidnightAndMonth						Date last modified:  1/26/07 SWO	*
*	Purpose:	Calculate endOfToday, firstMidnight after window start time							*
*		and firstEndOfDay after window start time. These are used for penalties, airport curfews,	*
*		and dividing up trips by day. Also determine month for FlightTime Calculator.				*																	*
****************************************************************************************************/
static int calcFirstMidnightAndMonth(void)
{
	DateTime currentDT, winStartDT; 
	DateTime priorMidnightDT, firstMidnightDT, firstEndOfDayDT, endOfTodayDT;
	DateTimeParts dtparts;
	time_t winStart;
	time_t tempTime;
	struct tm *tmStruct;
//	char tbuf1[32]; // ANG

	winStart = optParam.windowStart;
	winStartDT = dt_time_tToDateTime(winStart);
	//currentDT = dt_time_tToDateTime(tp); //tp is current time_t

	//fprintf(logFile, "winStartDT = %s\n\n", dt_DateTimeToDateTimeString(winStartDT, tbuf1, "%Y/%m/%d %H:%M")); // ANG

	currentDT = dt_time_tToDateTime(run_time_t); //run_time_t is fake runtime, if provided, else current time_t (tp)

	//fprintf(logFile, "currentDT = %s\n\n", dt_DateTimeToDateTimeString(currentDT, tbuf1, "%Y/%m/%d %H:%M")); // ANG

	// get prior midnight (prior to window start time)
	dt_DateTimeToDateTimeParts(winStartDT, &dtparts);
	if(! (dtparts.tparts.hour == 0 && dtparts.tparts.min == 0 && dtparts.tparts.sec == 0))
		priorMidnightDT = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, 0, 0, 0, 0);
	else
		priorMidnightDT = winStartDT;

	//fprintf(logFile, "priorMidnightDT = %s\n\n", dt_DateTimeToDateTimeString(priorMidnightDT, tbuf1, "%Y/%m/%d %H:%M")); // ANG

	// get firstMidnight and convert to time_t GMT time
	firstMidnightDT = dt_addToDateTime(Hours, 24, priorMidnightDT);
	firstMidnight = DateTimeToTime_t(firstMidnightDT);

	//fprintf(logFile, "firstMidnight = %s\n\n", dt_DateTimeToDateTimeString(dt_time_tToDateTime(firstMidnight), tbuf1, "%Y/%m/%d %H:%M")); // ANG

	// get firstEndOfDay (used for dividing up demand legs into days)
	firstEndOfDayDT = dt_addToDateTime(Minutes, optParam.dayEndTime, priorMidnightDT);
	firstEndOfDay = DateTimeToTime_t(firstEndOfDayDT);
	while(firstEndOfDay <= winStart)
		firstEndOfDay += 24*60*60;

	//fprintf(logFile, "firstEndOfDay = %s\n\n", dt_DateTimeToDateTimeString(dt_time_tToDateTime(firstEndOfDay), tbuf1, "%Y/%m/%d %H:%M")); // ANG

	// get endOfToday (used to calculate penalties for changing assignments today and tomorrow) 
	dt_DateTimeToDateTimeParts(currentDT, &dtparts);
	//first get prior midnight (prior to current datetime)
	if(! (dtparts.tparts.hour == 0 && dtparts.tparts.min == 0 && dtparts.tparts.sec == 0))
		priorMidnightDT = dt_MDYHMSMtoDateTime((int) dtparts.dparts.month, (int) dtparts.dparts.day, (int) dtparts.dparts.year, 0, 0, 0, 0);
	else
		priorMidnightDT = currentDT;
	
	//fprintf(logFile, "priorMidnightDT = %s\n\n", dt_DateTimeToDateTimeString(priorMidnightDT, tbuf1, "%Y/%m/%d %H:%M")); // ANG

	endOfTodayDT = dt_addToDateTime(Minutes, optParam.dayEndTime, priorMidnightDT);
	endOfToday = DateTimeToTime_t(endOfTodayDT);
	//while(endOfToday < tp)
	while(endOfToday <= run_time_t)
		endOfToday += 24*3600;

	//fprintf(logFile, "End of Today = %s\n\n", dt_DateTimeToDateTimeString(dt_time_tToDateTime(endOfToday), tbuf1, "%Y/%m/%d %H:%M")); // ANG

	//get month for use in flight time calculator
	tempTime = winStart + (int)((optParam.planningWindowDuration - 0.5)*24*3600*0.5);
	tmStruct = gmtime(&tempTime);
	month = tmStruct->tm_mon + 1;  //tm_mon ranges 0-11, month ranges from 1-12
	
	return 0;
}


/************************************************************************************************
*	Function   processLegs									Date last modified:  8/16/07 SWO	*		
*	Purpose:	Find acInd for legs.															*
*				Create demand legs for any locked repositioning legs.							*		
*				Update reqOut = schedOut on locked demand legs, and earlyAdj= lateAdj = 0.		*
*				Add plane and crew IDs to locked demand legs.									*
*				Add crew and plane IDs for reduced penalties on demand legs, and				*
*				calculate default penalty for changing assignment on leg.						*	
************************************************************************************************/	
static int processLegs(void)  
{
	int lg, i, p;
	int stops, numPax, fltTm, blkTm, elapsedTm;
    char writetodbstring1[200];
	actualMaxDemandID = maxDemandID;

	//note that legs are sorted in order of increasing scheduled out time
	for(lg = 0; lg<numLegs; lg++){
		//initialize indices
		legList[lg].crewPairInd = -1;
		legList[lg].acInd = -1;
		legList[lg].demandInd = -1;
		legList[lg].dropped = 0;
		
		//set acInd
		if(legList[lg].aircraftID > 0){
			for(p=0; p<numAircraft; p++){
				if(acList[p].aircraftID == legList[lg].aircraftID){
					legList[lg].acInd = p;
					break;
				}
			}
			//exit with error message if acInd was not found
			if(legList[lg].acInd == -1){
				logMsg(logFile,"%s Line %d, aircraftID %d on legList[%d] not found\n", __FILE__,__LINE__, legList[lg].aircraftID, lg);
				sprintf(writetodbstring1, "%s Line %d, aircraftID %d on legList[%d] not found", __FILE__,__LINE__, legList[lg].aircraftID, lg);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
			    else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				errorinfoList[errorNumber].aircraftid=legList[lg].aircraftID;
				errorinfoList[errorNumber].legindx=lg;
				sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			    errorinfoList[errorNumber].format_number=28;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
		}
		//Unpair invalid, infeasible,  leg-crewPairID
		if ( acList[legList[lg].acInd].legCrewPairFlag == 0 && legList[lg].crewPairID ){
			legList[lg].crewPairID = 0;
		    fprintf(logFile,"Warning: Leg out from airportid = %d on aircraftid = %d or its previos leg is not fully crewed. \n", legList[lg].outAirportID, legList[lg].aircraftID);
			sprintf(writetodbstring1, "Warning: Leg out from airportid = %d on aircraftid = %d or its previos leg is not fully crewed.", legList[lg].outAirportID, legList[lg].aircraftID);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
			    else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				errorinfoList[errorNumber].airportid=legList[lg].outAirportID;
				errorinfoList[errorNumber].aircraftid=legList[lg].aircraftID;
			    errorinfoList[errorNumber].format_number=38;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
		}
		else
			acList[legList[lg].acInd].legCrewPairFlag = legList[lg].crewPairID;

		//if a leg is locked...
		if(legList[lg].planeLocked == 1){
			//exit with error message if user tries to lock a leg with no acInd
			if(legList[lg].acInd == -1){
				logMsg(logFile,"%s Line %d, can't lock plane on leg with acInd == %d\n", __FILE__,__LINE__, legList[lg].acInd);
                sprintf(writetodbstring1, "%s Line %d, can't lock plane on leg with acInd == %d", __FILE__,__LINE__, legList[lg].acInd);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				errorinfoList[errorNumber].acidx=legList[lg].acInd;
				sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			    errorinfoList[errorNumber].format_number=39;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
			//if this is a repositioning leg, create a demand leg
			if(legList[lg].demandID == 0){
				i = numDemand;
				demandList[i].demandID = maxDemandID + 1;
				legList[lg].demandID = demandList[i].demandID;
				legList[lg].demandInd = i;
				maxDemandID++;
				demandList[i].contractID = 0;
				demandList[i].ownerID = 0;
				demandList[i].outAirportID = legList[lg].outAirportID;
				demandList[i].outFboID = legList[lg].outFboID;
				demandList[i].inAirportID = legList[lg].inAirportID;
				demandList[i].inFboID = legList[lg].inFboID;
				demandList[i].reqOut = legList[lg].schedOut;
				demandList[i].reqIn = legList[lg].schedIn;
				demandList[i].earlyAdj = 0;
				demandList[i].lateAdj = 0;
				demandList[i].contractFlag = 0;
				demandList[i].isAppoint = 0;
				demandList[i].turnTime = optParam.turnTime;
				demandList[i].aircraftID = legList[lg].aircraftID;
				demandList[i].acInd = legList[lg].acInd;
				demandList[i].sequencePosn = acList[legList[lg].acInd].sequencePosn;
				demandList[i].aircraftTypeID = acList[legList[lg].acInd].aircraftTypeID;
				//if leg leaves today or tomorrow and BOTH the crew and plane are not locked
				//(if just the plane is locked we could still have a penalty for changing crew, and we can't just lock crew)
				if(legList[lg].crewLocked == 0){
					if(demandList[i].reqOut < endOfToday){
						demandList[i].redPenACID = legList[lg].aircraftID;
						demandList[i].redPenCrewPairID = legList[lg].crewPairID;
						demandList[i].changePenalty = optParam.changeTodayPenalty;
					}
					else if(demandList[i].reqOut < (endOfToday + 24*3600)){
						demandList[i].redPenACID = legList[lg].aircraftID;
						demandList[i].redPenCrewPairID = legList[lg].crewPairID;
						demandList[i].changePenalty = optParam.changeNxtDayPenalty;
					}
				}
				numDemand++;
			}
			else //this is a demand leg.... 
			{
				for(i = 0; i<numDemand; i++){
					if(demandList[i].demandID == legList[lg].demandID){
						legList[lg].demandInd = i;
						//if this is the first leg for the demand...
						if(legList[lg].outAirportID == demandList[i].outAirportID){
							//update reqOut on demand leg to schedOut, and update contract flag to 0
							demandList[i].reqOut = legList[lg].schedOut;
							demandList[i].reqIn = legList[lg].schedIn; // 05/28/08 ANG
							demandList[i].earlyAdj = 0;
							demandList[i].lateAdj = 0;
							//populate aircraftID on demand leg
							demandList[i].aircraftID = legList[lg].aircraftID;
							demandList[i].acInd = legList[lg].acInd;
							//if leg leaves today or tomorrow and BOTH the crew and plane are not locked
							//(if just the plane is locked we could still have a penalty for changing crew, and we can't just lock crew)
							if(legList[lg].crewLocked == 0){
								if(demandList[i].reqOut < endOfToday){
									demandList[i].redPenACID = legList[lg].aircraftID;
									demandList[i].redPenCrewPairID = legList[lg].crewPairID;
									demandList[i].changePenalty = optParam.changeTodayPenalty;
								}
								else if(demandList[i].reqOut < (endOfToday + 24*3600)){
									demandList[i].redPenACID = legList[lg].aircraftID;
									demandList[i].redPenCrewPairID = legList[lg].crewPairID;
									demandList[i].changePenalty = optParam.changeNxtDayPenalty;
								}
							}
						}
						break;
					}
				}

				//exit with error message if demand leg was not found
				if(legList[lg].demandInd == -1){
					logMsg(logFile,"%s Line %d, demandID %d on legList[%d] not valid\n", __FILE__,__LINE__, legList[lg].demandID, lg);
					sprintf(writetodbstring1, "%s Line %d, demandID %d on legList[%d] not valid", __FILE__,__LINE__, legList[lg].demandID, lg);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
			    else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].demandid=legList[lg].demandID;
                   errorinfoList[errorNumber].legindx=lg;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=29;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                   errorNumber++;
				   writeWarningData(myconn); exit(1);
				}
			}
			//for either a locked repo (which becomes demand) or locked demand leg, populate the crewPairID if crewPair is locked
			if(legList[lg].crewLocked == 1){
				//exit with error message if user tries to lock a leg with no crewPairID
				if(legList[lg].crewPairID == 0){
					logMsg(logFile,"%s Line %d, can't lock crew on leg with crewPairID == %d?\n", __FILE__,__LINE__, legList[lg].crewPairID);
					sprintf(writetodbstring1, "%s Line %d, can't lock crew on leg with crewPairID == %d?", __FILE__,__LINE__, legList[lg].crewPairID);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
			    else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].crewpairid=legList[lg].crewPairID;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=30;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                   errorNumber++;
					writeWarningData(myconn); exit(1);
				}
				demandList[i].crewPairID = legList[lg].crewPairID;
			}
			else
				demandList[i].crewPairID = 0;
		} // end of "if a leg is locked" loop
		else //leg is not locked.  If leg is a demand leg, we store reduced penalty information with demand leg 
			//for calculation in change penalties. 
		{
			if(legList[lg].demandID > 0){
				for(i = 0; i<numDemand; i++){
					if(demandList[i].demandID == legList[lg].demandID){
						legList[lg].demandInd = i;
						//if leg leaves today or tomorrow...
						if(demandList[i].reqOut < endOfToday){
							demandList[i].redPenACID = legList[lg].aircraftID;
							demandList[i].redPenCrewPairID = legList[lg].crewPairID;
							demandList[i].changePenalty += optParam.changeTodayPenalty;
						}
						else if(demandList[i].reqOut < (endOfToday + 24*3600)){
							demandList[i].redPenACID = legList[lg].aircraftID;
							demandList[i].redPenCrewPairID = legList[lg].crewPairID;
							demandList[i].changePenalty += optParam.changeNxtDayPenalty;
						}
						break;
					}				}
				//exit with error message if demand leg was not found
				if(legList[lg].demandInd == -1){
					logMsg(logFile,"%s Line %d, demandID %d on legList[%d] not valid\n", __FILE__,__LINE__, legList[lg].demandID, lg);
					sprintf(writetodbstring1, "%s Line %d, demandID %d on legList[%d] not valid", __FILE__,__LINE__, legList[lg].demandID, lg);
				if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
				      }
			      }
			    else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processLegs().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
				  }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				errorinfoList[errorNumber].demandid=legList[lg].demandID;
                errorinfoList[errorNumber].legindx=lg;
				sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			    errorinfoList[errorNumber].format_number=29;
                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
				}
			}	
		}
	} 
	//Populate adjSchedIn using flight time calculator.
	for (i=0; i<numLegs; i++){
		if (legList[i].acInd >= 0){
			if (legList[i].demandInd > -1)
			{// Demand leg
				numPax = demandList[legList[i].demandInd].numPax;
			}
			else // Positioning leg
				numPax = 0;
			
			getFlightTime(legList[i].outAirportID, legList[i].inAirportID, acList[legList[i].acInd].aircraftTypeID, 
				month, numPax, &fltTm, &blkTm, &elapsedTm, &stops);
			legList[i].adjSchedIn = legList[i].schedOut + 60 * elapsedTm;
		}
		else
			legList[i].adjSchedIn = legList[i].schedIn;
	}
	return 0;
}
	

/**********************************************************************************************
*	Function   processCrews								Date last modified:  08/10/07 SWO	  *
*	Purpose:	Find acTypeIndex for crew members and (existing) crewPairs.					  *	
*				Find crewListIndices for crew members of (existing) crewPairs.				  *
*				Store scheduled legs for existing crewPairs in current scenario.			  *								
*				For lockTour, remove demand legs from consideration in duty generation		  *
*					(add tours later) and update plane  and crew availability.				  *
*				Set optAircraftID, the plane to which the crewPair is locked if applicable.	  *
*				Find acInd (if applicable) for(exg) crewPairs.							  *
*				Populate availDT and availAptID for crewPairs that are already together		  *
***********************************************************************************************/	
static int processCrews(void)
{
	int lg, cp, p, c, j, x, y, lg2, cpInd, cpInd2;
	int crInd[2];
//	int *acPtr;
	int flightTm=0, elapsedTm=0, blockTm = 0, numStops = 0;
	int *mustRest;
	int crewInd, day, windowEnd;


	for(c = 0; c<numCrew; c++){
		// Find acTypeIndex for crew members 
		for(j = 0; j<numAcTypes; j++){
			if(acTypeList[j].aircraftTypeID == crewList[c].aircraftTypeID){
				crewList[c].acTypeIndex = j;
				break;
			}
		}
		//exit with error message if acTypeIndex not found for crew member
		if(crewList[c].acTypeIndex == -1){
			logMsg(logFile,"%s Line %d, acTypeIndex not found for crewList[%d]\n", __FILE__,__LINE__, c);
			writeWarningData(myconn); exit(1);
		}
		//store original available information for crew member in "orig" fields (it will be overwritten below if crew member is in a locked tour)
		crewList[c].origActCode = crewList[c].activityCode;
		crewList[c].origAvailDT = crewList[c].availDT;
		crewList[c].origBlockTm = crewList[c].blockTm;
		crewList[c].origDutyTm = crewList[c].dutyTime;
	}	
	// For each (existing) crewPair 
	for(cp = 0; cp<numCrewPairs; cp++){
		//find crewListIndices for crew members
		for(c = 0; c<numCrew; c++){
			if(crewPairList[cp].captainID == crewList[c].crewID){
				crewPairList[cp].crewListInd[0] = c;
				break;
			}
		}
		for(c = 0; c<numCrew; c++){
			if(crewPairList[cp].flightOffID == crewList[c].crewID){
				crewPairList[cp].crewListInd[1] = c;
				break;
			}
		}
		//exit with error message if crewListInd not found for crew member
		for(c = 0; c < 2; c++){
			if(crewPairList[cp].crewListInd[c] == -1){
				logMsg(logFile,"%s Line %d, crewListInd not found for crewPairList[%d].crewListInd[%d]\n", __FILE__,__LINE__, cp, c);
				writeWarningData(myconn); exit(1);
			}
		}
		//set acTypeIndex
		crewPairList[cp].acTypeIndex = crewList[crewPairList[cp].crewListInd[0]].acTypeIndex;
	}
	//Store scheduled legs for existing crewPairs in current scenario, along with aircraft. 
	//Store (temporary) crew Pair index  with legs. Indicate whether leg is in a locked tour.
	//Legs are in order of increasing schedOut.
	for(cp = 0; cp<numCrewPairs; cp++){
		crewPairList[cp].schedLegIndList[0] = -1;
		y = 0;
		for(lg = 0; lg < numLegs; lg++){
			if(legList[lg].crewPairID > 0 && legList[lg].crewPairID == crewPairList[cp].crewPairID){
				x = 0;
				while(crewPairList[cp].aircraftID[x] > 0){
					if(crewPairList[cp].aircraftID[x]== legList[lg].aircraftID){
						// if crewPair has lockTour = 1 with that plane, indicate leg is inLockedTour
						if(crewPairList[cp].lockTour[x] == 1)
							legList[lg].inLockedTour = 1;
						break;
					}
					x++;
				}
				legList[lg].crewPairInd = cp;
				crewPairList[cp].schedLegIndList[y] = lg;
				crewPairList[cp].schedLegACIndList[y] = legList[lg].acInd;
				//exit with error message if acInd not found for scheduled leg for crewPair
				if(legList[lg].acInd == -1){
					logMsg(logFile,"%s Line %d, no acInd for crewPairList[%d].schedLegACIndList[%d]]\n", __FILE__,__LINE__, cp, y);
					writeWarningData(myconn); exit(1);
				}
				y++;
			}
		}
		//put -1 in array after last scheduled leg index to mark end of list
		crewPairList[cp].schedLegIndList[y] = -1;
	}
	/*If a scheduler sets lockTour = 1 for a crewPair X and plane Y, they should set lockTour = 1 for any other crewPair that 
	flies the plane before crewPair X, and lockTour = 1 for any earlier crewPair that includes one of the crew members of crewPair X.
	We will check if a crew member or plane is used on a leg in an unlocked tour prior to a lockTour; 
	if so, we'll update lockTour = 0 (cancel/ignore lock).*/
	for(lg = 0; lg<numLegs; lg++){
		if(legList[lg].inLockedTour == 1){
			cpInd = legList[lg].crewPairInd; // this should be okay from above: inLockedTour is set in cp loop along with crewPairInd
			for(lg2 = 0; lg2<lg; lg2++){
				if(legList[lg2].inLockedTour == 0){
					if(legList[lg2].crewPairInd >-1){
						cpInd2 = legList[lg2].crewPairInd;
						if(crewPairList[cpInd2].crewListInd[0]== crewPairList[cpInd].crewListInd[0]
							|| crewPairList[cpInd2].crewListInd[1] == crewPairList[cpInd].crewListInd[0]
							|| crewPairList[cpInd2].crewListInd[0] == crewPairList[cpInd].crewListInd[1]
							|| crewPairList[cpInd2].crewListInd[1] == crewPairList[cpInd].crewListInd[1]){
								legList[lg].inLockedTour = 0;
								break;
							}
					}
					if(legList[lg2].aircraftID > 0){
						if(legList[lg].aircraftID == legList[lg2].aircraftID){
							legList[lg].inLockedTour = 0;
							break;
						}
					}
				}
			}
		}
	}

	//Set crewPair.optAircraftID if crewPair is already flying a plane at window start, tour on this plane is not locked, 
	//AND crewPair is next available within the planning window (and not locked to home).
	//Also set firstCrPrID for aircraft in this case.
	for(cp = 0; cp<numCrewPairs; cp++){
		if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd
			|| crewList[crewPairList[cp].crewListInd[0]].lockHome == 1 || crewList[crewPairList[cp].crewListInd[1]].lockHome ==1)
			continue;
		if(crewPairList[cp].hasFlownFirst == 1 && crewPairList[cp].lockTour[0] == 0){
			crewPairList[cp].optAircraftID = crewPairList[cp].aircraftID[0];
			if(crewPairList[cp].schedLegACIndList[0]>-1 && acList[crewPairList[cp].schedLegACIndList[0]].aircraftID == crewPairList[cp].optAircraftID){
				p = crewPairList[cp].schedLegACIndList[0]; //we have already verified that this is not -1				
				crewPairList[cp].acInd = p;
				acList[p].firstCrPrID = crewPairList[cp].crewPairID;
				acList[p].firstCrPrInd = cp;  //crew Pair index must be repopulated after crew pairing
			}
			else{
				for(p = 0; p<numAircraft; p++){
					if(acList[p].aircraftID == crewPairList[cp].aircraftID[0]){
						acList[p].firstCrPrID = crewPairList[cp].crewPairID;
						acList[p].firstCrPrInd = cp;  //crew Pair index must be repopulated after crew pairing
						crewPairList[cp].acInd = p;
						break;
					}
				}
			}
			//exit with error message if acInd not found for crewPairList[cp].optAircraftID
			if(crewPairList[cp].acInd == -1){
				logMsg(logFile,"%s Line %d, no acInd for crewPairList[%d].optAircraftID\n", __FILE__,__LINE__, cp);
				writeWarningData(myconn); exit(1);
			}
		}

		//CPAC - 06/17/09 ANG
		if(crewPairList[cp].hasFlownFirst == 0 && optParam.exgCPACBonus){
			for(p = 0; p<numAircraft; p++){
				if(acList[p].aircraftID == crewPairList[cp].aircraftID[0]){
					acList[p].applyCPACbonus = 1;
					acList[p].cpIndCPACbonus = cp;
					break;
				}
			}
		}
	}
	//allocate memory for mustRest, which is used below when a pilot is locked to a Tour for which they haven't yet left home (unusual case)
	mustRest = (int *) calloc(numCrew, sizeof(int));

	// Pass through all scheduled legs in order of increasing sched.out... 
	for(lg = 0; lg<numLegs; lg++){
		//if leg is in a lockedTour, mark associated demand leg as inLockedTour (can be removed from optimization)
		//and update available dt and locn for crew and plane
		if(legList[lg].inLockedTour == 1){
			if(legList[lg].demandInd > -1)
				demandList[legList[lg].demandInd].inLockedTour = 1;
			p = legList[lg].acInd; //no error check needed - inLockedTour is set in loop that will give error if p == -1
			acList[p].maintFlag = 0;
			if(legList[lg].schedIn > acList[p].availDT){
				acList[p].availDT = legList[lg].schedIn;
				acList[p].availAirportID = legList[lg].inAirportID;
				acList[p].availFboID = demandList[legList[lg].demandInd].inFboID;
			}
			cpInd = legList[lg].crewPairInd; //no error check needed - inLockedTour is set in crewPair loop
			crInd[0] = crewPairList[cpInd].crewListInd[0];
			crInd[1] = crewPairList[cpInd].crewListInd[1];
			
			for(x = 0; x<2; x++){
				if(crewList[crInd[x]].lockHome == 0){ //don't bother updating if crew member is going home after locked tours anyway
					getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, acTypeList[crewPairList[cpInd].acTypeIndex].aircraftTypeID,
							month, (legList[lg].demandInd > -1? demandList[legList[lg].demandInd].numPax : 0),&flightTm,&blockTm,&elapsedTm,&numStops);
				
					//check if crew member is resting and can start later OR hasn't started tour OR can rest before doing this leg
					//assume preFlightTm, not firstPreFltTm, because will typically be correct and not worth overhead of calculating
					if(crewList[crInd[x]].activityCode > 0 ||
						(legList[lg].schedOut - 60*(crewList[crInd[x]].blockTm > 0? optParam.postFlightTm : 0) - 60*optParam.minRestTm
						- 60*optParam.preFlightTm) >= crewList[crInd[x]].availDT- 60){ //one minute tolerance
						
						//Check if pilot is located at plane. 
						if(crewList[crInd[x]].availAirportID == legList[lg].outAirportID)
							mustRest[crInd[x]] = 0;
						 /*If a pilot is locked to a tour for which he still must travel to the plane, 
						then we assume that his duty hours will be maxed out on the first day of this tour 
						and he must rest before performing another tour that we generate.*/
						else
							mustRest[crInd[x]] = 1;

						crewList[crInd[x]].dutyTime = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + optParam.preFlightTm;
						crewList[crInd[x]].blockTm = blockTm;
						
					}

					//else update fields assuming no rest before leg
					else{
						crewList[crInd[x]].dutyTime += (int)((legList[lg].schedIn - crewList[crInd[x]].availDT)/60);
						crewList[crInd[x]].blockTm += blockTm;
					}
					crewList[crInd[x]].availAirportID = legList[lg].inAirportID;
					crewList[crInd[x]].activityCode = 0;
					crewList[crInd[x]].availDT = legList[lg].schedIn;

				}
			}
		}//end if(legList[lg].inLockedTour == 1)
		else{ //leg is not part of a locked tour.  
			//If leg is locked to crewPair AND this is the first locked leg we have found for this crew pair, then we
			// populate optAircraftID with this first locked leg (may override an aircraft the crew was already flying)
			if(legList[lg].crewLocked == 1){
				//exit with error message if crewPairInd not populated
				if(legList[lg].crewPairInd == -1){
					logMsg(logFile,"%s Line %d, no crewPairInd found for legList[%d] which has locked crew.\n", __FILE__,__LINE__, lg);
					writeWarningData(myconn); exit(1);
				}
				//continue if we have already considered a locked leg for this crew pair
				y = 0;
				while(crewPairList[legList[lg].crewPairInd].schedLegIndList[y] < lg){
					if(legList[crewPairList[legList[lg].crewPairInd].schedLegIndList[y]].crewLocked == 1)
						continue;
					y++;
				}
				//if crew pair with locked leg was already flying a different plane, no longer consider them
				//the first crew for the other plane
				if(crewPairList[legList[lg].crewPairInd].optAircraftID != legList[lg].aircraftID){
					if(acList[crewPairList[legList[lg].crewPairInd].acInd].firstCrPrID == legList[lg].crewPairID){
						acList[crewPairList[legList[lg].crewPairInd].acInd].firstCrPrID = 0;
						acList[crewPairList[legList[lg].crewPairInd].acInd].firstCrPrInd = -1;
					}
				}
				//exit with error message if acInd not found for crewLocked leg
				if(legList[lg].acInd == -1){
					logMsg(logFile,"%s Line %d, no acInd for legList[%d] which has locked crew.\n", __FILE__,__LINE__, lg);
					writeWarningData(myconn); exit(1);
				}
				//update optAircraftID and acInd for crew pair based on locked leg
				crewPairList[legList[lg].crewPairInd].optAircraftID = legList[lg].aircraftID;
				crewPairList[legList[lg].crewPairInd].acInd = legList[lg].acInd;
			}
		}
	} //end for(lg = 0...

	//add rest time to any pilot that must rest at the end of their locked tour (see above)
	for(c = 0; c< numCrew; c++){
		if(mustRest[c] == 1){
			crewList[c].activityCode = 1;
			crewList[c].dutyTime = 0;
			crewList[c].blockTm = 0;
			crewList[c].availDT += 60*(optParam.postFlightTm + optParam.minRestTm);
		}
	}

	for(cp = 0; cp<numCrewPairs; cp++){
		// Populate available information for crewPairs that are already together (crew members available at same time and place with same duty hours)
/*	//Replaced by RLZ 04/29/2008
		if(crewList[crewPairList[cp].crewListInd[0]].availAirportID == crewList[crewPairList[cp].crewListInd[1]].availAirportID &&
			crewList[crewPairList[cp].crewListInd[0]].availDT == crewList[crewPairList[cp].crewListInd[1]].availDT && 
			crewList[crewPairList[cp].crewListInd[0]].dutyTime == crewList[crewPairList[cp].crewListInd[1]].dutyTime){
				//it is probably safe to assume block time and activity code are the same in this case
				crewPairList[cp].availAptID = crewList[crewPairList[cp].crewListInd[0]].availAirportID;
				crewPairList[cp].availDT = crewList[crewPairList[cp].crewListInd[0]].availDT;
				crewPairList[cp].blockTm =  crewList[crewPairList[cp].crewListInd[0]].blockTm;
				crewPairList[cp].dutyTime = crewList[crewPairList[cp].crewListInd[0]].dutyTime;
				crewPairList[cp].activityCode = crewList[crewPairList[cp].crewListInd[0]].activityCode;
		}
*/

		if(crewList[crewPairList[cp].crewListInd[0]].availAirportID == crewList[crewPairList[cp].crewListInd[1]].availAirportID &&
			crewList[crewPairList[cp].crewListInd[0]].blockTm == crewList[crewPairList[cp].crewListInd[1]].blockTm 
			&& crewList[crewPairList[cp].crewListInd[0]].dutyTime == crewList[crewPairList[cp].crewListInd[1]].dutyTime){
				//it is probably safe to assume block time and activity code are the same in this case
				//NO. NOT the case, added dutyTime condition on 03/17/2009 RLZ;
                //case: One pilot has already with the plane, the other one is travelling to the availAirportID, they can not be considered as a pair
				//this piece of info is used in getCrewArcTimeAndCost function
				crewPairList[cp].availAptID = crewList[crewPairList[cp].crewListInd[0]].availAirportID;
				crewPairList[cp].availDT = max(crewList[crewPairList[cp].crewListInd[0]].availDT, crewList[crewPairList[cp].crewListInd[1]].availDT);
				crewPairList[cp].blockTm =  crewList[crewPairList[cp].crewListInd[0]].blockTm;
				//crewPairList[cp].dutyTime = max(crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime) ;
				//No MAX is required, since we add the equality condtion in dutyTime.
				crewPairList[cp].dutyTime = crewList[crewPairList[cp].crewListInd[0]].dutyTime;
				crewPairList[cp].activityCode = min(crewList[crewPairList[cp].crewListInd[0]].activityCode,crewList[crewPairList[cp].crewListInd[1]].activityCode);
		}			
		else if (crewPairList[cp].hasFlownFirst && crewList[crewPairList[cp].crewListInd[0]].availAirportID == crewList[crewPairList[cp].crewListInd[1]].availAirportID && 
			    crewList[crewPairList[cp].crewListInd[0]].blockTm == crewList[crewPairList[cp].crewListInd[1]].blockTm ) {
				//RLZ: 06/01/2009: To populate the crewPair info in addition to the IF part. one particular case, pic and sic has a slightly different start time, then they will not qualify for the above condtion
				crewPairList[cp].availAptID = crewList[crewPairList[cp].crewListInd[0]].availAirportID;
				crewPairList[cp].availDT = max(crewList[crewPairList[cp].crewListInd[0]].availDT, crewList[crewPairList[cp].crewListInd[1]].availDT);
				crewPairList[cp].blockTm = max(crewList[crewPairList[cp].crewListInd[0]].blockTm, crewList[crewPairList[cp].crewListInd[1]].blockTm) ;
				crewPairList[cp].dutyTime = max(crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime);
				crewPairList[cp].activityCode = min(crewList[crewPairList[cp].crewListInd[0]].activityCode,crewList[crewPairList[cp].crewListInd[1]].activityCode);
		}


	}

	// Determine the earliest day on which the regular tour ends for the members of an existing crew pair.														*



	windowEnd = optParam.planningWindowDuration - 1;

	for(cp = 0; cp<numCrewPairs; cp++){
		for(c = 0; c<2; c++){
			crewInd = crewPairList[cp].crewListInd[c];
			crewList[crewInd].endRegDay = PAST_WINDOW;
			for(day = 0; day <=windowEnd; day++)
			{
				if(crewList[crewInd].tourEndTm < firstEndOfDay + day*24*3600){
					crewList[crewInd].endRegDay = day;
					break;
				}
			}
		}
		//find the earlier endRegDay of the two crew members and populate for crewPair
		if(crewList[crewPairList[cp].crewListInd[0]].endRegDay < crewList[crewPairList[cp].crewListInd[1]].endRegDay)
			crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[0]].endRegDay;
		else
			crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[1]].endRegDay;

		//find the start and end days of the tour (in terms of the planning window days)
        //copy from dutynodes: tabulateCrewStartAndEndDays
		crewPairList[cp].startDay = PAST_WINDOW;
		for(day = 0; day <=windowEnd; day++){
			// we add a few hours to pairStartTm so that if a tour starts at 4:30AM and day ends at 5:30AM, 
			//we don't consider the 1 hour day	
			if((crewPairList[cp].pairStartTm + 4*3600) < (firstEndOfDay + day*24*3600) &&
				crewList[crewPairList[cp].crewListInd[0]].availDT < (firstEndOfDay + day*24*3600) &&
				crewList[crewPairList[cp].crewListInd[1]].availDT < (firstEndOfDay + day*24*3600)){ 
				crewPairList[cp].startDay = day;
				break;
			}
		}
		
		crewPairList[cp].endDay = PAST_WINDOW;
		for(day = 0; day <= windowEnd; day++){
			if((crewPairList[cp].pairEndTm - 12*3600)< (firstEndOfDay + day*24*3600)){ //we subtract half-a-day from pairEndTm so that for crews willing to stay 1/2 day OT, we don't generate duties on last 1/2 day (used for travel home only)
				crewPairList[cp].endDay = day;
				break;
			}
		}



	}

	return 0;
}



/********************************************************************************
*	Function   createAppointDemand             Date last modified: 08/15/07 SWO	*
*	Purpose:	Create demand legs from maintenance/appointments records to 	*
*				insure they are covered.										*
********************************************************************************/

static int createAppointDemand(void)
{
	int d, i, x, k, j, tt;	
	extern Demand *testFlightList; //05/23/08 ANG
	extern int countTestFlights; //05/23/08 ANG
	int m, combine, update; //05/23/08 ANG

	d = numDemand;  //d = number of non-appointment demand legs = temporary (before sort) index of first appointment demand leg

	for(x = 0; x < numMaintenanceRecord; x++)
	{	
		for(k = 0; k<numAircraft; k++)
		{
			if(acList[k].aircraftID == maintList[x].aircraftID)
				break;
		}	
		//exit with error message if acInd not found for maintenance/appt record
		if(k == numAircraft){
			logMsg(logFile,"%s Line %d, no acInd for maintList[%d]\n", __FILE__,__LINE__, x);
			writeWarningData(myconn); exit(1);
		}
		//because some planes are "locked to home", it is possible that plane is not available 
		//until after the appointment/maintenance leg is completed - don't create demand
		if(maintList[x].endTm <= acList[k].availDT)
			continue;

		i = numDemand;
		combine = 1; // 05/23/08 ANG

		//if maintenance or appointment record is an extension of a previous record, 
		//just change length of previously created demand leg
		//(if there is less time than can be used between the appointments, assume it is an extension)
		for(j = d; j < i; j++){
			if(k == demandList[j].acInd && maintList[x].airportID == demandList[j].outAirportID 
				&& maintList[x].startTm < (demandList[j].reqIn + 60*demandList[j].turnTime + 60*30 + 2*optParam.turnTime*60)){

				//START - if there is a test flight can be scheduled in between, do not combine legs - 05/23/08 ANG

				//demandList[j].reqIn = maintList[x].endTm;
				////temporarily store elapsed time in demandList[i].elapsedTm[0] because we don't yet have acList[k].acTypeListInd populated
				////will store properly in getAddlDemandData2
				//demandList[j].elapsedTm[0] =(int)difftime(demandList[j].reqIn, demandList[j].reqOut)/60;
				////if appointment extension is a sales / signing appt, update isAppoint to indicate so. Otherwise, DON'T 
				////update field, as we don't want to overwrite apptType for previous sales/signing (we use this info later)
				//if(maintList[x].apptType == 2)
				//	demandList[j].isAppoint = 3;
				//if(maintList[x].apptType == 3)// 11/05/07 ANG
				//	demandList[j].isAppoint = 4; 
				//if(maintList[x].apptType == 0)
				//	demandList[j].turnTime = optParam.maintTurnTime;
				//else //maintList[x].apptType = 1 or 2 
				//	demandList[j].turnTime = 1; //buffer time to avoid promote overlap.
				//break;

				for(m = 0; m < countTestFlights; m++){
					if(testFlightList[m].reqOut > demandList[j].reqIn && testFlightList[m].reqIn < maintList[x].startTm){
						combine = 0;
						break;
					}
				}
				if (combine == 1){
					demandList[j].reqIn = maintList[x].endTm;
					//temporarily store elapsed time in demandList[i].elapsedTm[0] because we don't yet have acList[k].acTypeListInd populated
					//will store properly in getAddlDemandData2
					demandList[j].elapsedTm[0] =(int)difftime(demandList[j].reqIn, demandList[j].reqOut)/60;
					//if appointment extension is a sales / signing appt, update isAppoint to indicate so. Otherwise, DON'T 
					//update field, as we don't want to overwrite apptType for previous sales/signing (we use this info later)
					if(maintList[x].apptType == 2)
						demandList[j].isAppoint = 3;
					if(maintList[x].apptType == 3)// 11/05/07 ANG
						demandList[j].isAppoint = 4; 
					if(maintList[x].apptType == 0){
						//demandList[j].turnTime = optParam.maintTurnTime; - Replaced below

						//START - 01/15/09 ANG
						//If there is a test flight shortly after out from combined MX (so here we extend previous record),
						//we still want to cover the test flight, even if it is not scheduled in exg tour
						update = 0;
						//First check test flight list
						for(m = 0; m < countTestFlights; m++){
							if( //testFlightList[m].aircraftID == acList[p].aircraftID &&
								testFlightList[m].outAirportID == maintList[x].airportID &&
								testFlightList[m].reqOut > maintList[x].endTm &&
								difftime(testFlightList[m].reqOut, maintList[x].endTm) < 60*optParam.maintTurnTime){
								demandList[j].turnTime += difftime(testFlightList[m].reqOut, maintList[x].endTm) - 60; //1 minute allowance to avoid "<" sign, if any
								update = 1;
								break;
							}
						}
						if(update == 0)//not yet updated
							demandList[j].turnTime = optParam.maintTurnTime;
						//END - 01/15/09 ANG

					}
					else //maintList[x].apptType = 1 or 2 
						demandList[j].turnTime = 1; //buffer time to avoid promote overlap.
					break;	
				} else
					j = i;
					//break;				
				//END - if there is a test flight can be scheduled in between, do not combine legs - 05/23/08 ANG
			}
		}
		if(j < i) //if we have used record to extend a previous appointment leg, move to next record (fields are already populated)
			continue;

		//START - if there is a test flight can be scheduled in between, do not combine legs - 05/23/08 ANG

		//if plane is available at appointment location, and appointment occurs soon after plane is available (less time than 
		//can be used), then simply update available info on plane and don't create a demand leg
		//if(maintList[x].airportID == acList[k].availAirportID){
		//	if(acList[k].maintFlag == 0)
		//		tt = optParam.turnTime;
		//	else if(acList[k].maintFlag == 1)
		//		tt = optParam.maintTurnTime;
		//	else 
		//		tt = 0;
		//	if(maintList[x].startTm < (acList[k].availDT + 60*(tt + 15 + optParam.turnTime + 15))){
		//		acList[k].availDT = maintList[x].endTm;
		//		//if maintList[x].apptType == 0, plane is available after maintenance; if maintList[x].apptType == (1 || 2), plane avail. after airport appointment
		//		acList[k].maintFlag = (maintList[x].apptType == 0? 1 : 2);
		//		continue;
		//	}
		//}

		if(combine == 1 && maintList[x].airportID == acList[k].availAirportID){ // 05/27/08 ANG
			for(m = 0; m < countTestFlights; m++){
                //if(testFlightList[m].reqOut > demandList[j].reqIn && testFlightList[m].reqIn < maintList[x].startTm){
               if(testFlightList[m].reqOut > acList[k].availDT && testFlightList[m].reqIn < maintList[x].startTm){ // 06/20/08 ANG
					combine = 0;
					break;
				}
			}
			if (combine == 1){ //else create new demand leg for maint/appoint record
				if(acList[k].maintFlag == 0)
					tt = optParam.turnTime;
				else if(acList[k].maintFlag == 1)
					tt = optParam.maintTurnTime;
				else if(acList[k].maintFlag == 2)
					tt = 1; 
				else
					tt = 0;
				if(maintList[x].startTm < (acList[k].availDT + 60*(tt + 15 + optParam.turnTime + 15))){
					acList[k].availDT = maintList[x].endTm;
					//if maintList[x].apptType == 0, plane is available after maintenance; if maintList[x].apptType == (1 || 2), plane avail. after airport appointment
					acList[k].maintFlag = (maintList[x].apptType == 0? 1 : 2);
					continue;
				}
			}
		}
		//END - 05/23/08 ANG

		//else create new demand leg for maint/appoint record
		demandList[i].acInd = k;
		demandList[i].demandID = maxDemandID + 1;
		maxDemandID++;
		demandList[i].contractID = 0;
		demandList[i].ownerID = 0;
		demandList[i].outAirportID = maintList[x].airportID;
		demandList[i].outFboID = maintList[x].fboID;
		demandList[i].inAirportID = maintList[x].airportID;
		demandList[i].inFboID = maintList[x].fboID;
		//temporarily store elapsed time in demandList[i].elapsedTm[0] because we don't yet have acList[k].acTypeListInd populated
		//will store properly in getAddlDemandData2
		demandList[i].elapsedTm[0] =(int)difftime(maintList[x].endTm, maintList[x].startTm)/60;	
		demandList[i].reqOut_actual = maintList[x].startTm;

		if(maintList[x].apptType == 0){
			if(difftime(maintList[x].endTm,maintList[x].startTm) > (optParam.turnTime + 2)*60 
			&& difftime(optParam.windowEnd, maintList[x].startTm) > optParam.turnTime*60){ 
				demandList[i].reqOut = maintList[x].startTm + (optParam.turnTime - 1)*60; //delay the maint, offset the previous turn time
			}
			else{
				demandList[i].reqOut = maintList[x].startTm;				
			}
			demandList[i].reqIn = maintList[x].endTm;
			//Adjust elapsedTm when changing reqOut time - 12/30/08 ANG
			demandList[i].elapsedTm[0] =(int)difftime(demandList[i].reqIn, demandList[i].reqOut)/60;
		}
		//else this is an airport appointment, not a maintenance record, and we don't need turntime before appt.
		//Since we automatically put in turntime after preceding leg, we will adjust start of demand leg.
		//RLZ 10/29/2007: BUT can not kick it out of planning window
		else if(difftime(optParam.windowEnd, maintList[x].startTm) > optParam.turnTime*60){				
			if(difftime(maintList[x].endTm,maintList[x].startTm) > (optParam.turnTime + 2)*60){  //change 5 to 2
				demandList[i].reqOut = maintList[x].startTm + (optParam.turnTime - 1)*60; //RLZ 10/29/2007 1 minutes (buffer) torance for not overlapping in promote.
				demandList[i].reqIn = maintList[x].endTm;
			}
			else{
				//demandList[i].reqIn = maintList[x].endTm + (optParam.turnTime + 5)*60 -  (int)difftime(maintList[x].endTm,maintList[x].startTm);
				demandList[i].reqIn = maintList[x].endTm + (optParam.turnTime + 2)*60 - demandList[i].elapsedTm[0]*60;
				demandList[i].reqOut = demandList[i].reqIn - (2 + 1)*60; 
			}
		}
		else{
			demandList[i].reqOut = optParam.windowEnd - 60; // 1 minute before the pwEnd 
			demandList[i].reqIn = demandList[i].reqOut + 5*60; // Make it 5 minutes airport appointment. 
		}
		//Adjust elapsedTm when changing reqOut time - 12/30/08 ANG
		demandList[i].elapsedTm[0] =(int)difftime(demandList[i].reqIn, demandList[i].reqOut)/60;

		
		demandList[i].contractFlag = 0;
		demandList[i].earlyAdj = 0;
		demandList[i].lateAdj = 0;
		demandList[i].changePenalty = 0;
		demandList[i].feasible = 1;
		demandList[i].aircraftID = maintList[x].aircraftID;
		demandList[i].crewPairID = 0;
		demandList[i].sequencePosn = acList[k].sequencePosn;
		demandList[i].aircraftTypeID = acList[k].aircraftTypeID;
		//demandList[j].isAppoint = maintList[x].apptType + 1; - Why j? Should be i - 01/23/09 ANG
		demandList[i].isAppoint = maintList[x].apptType + 1;
		if(optParam.autoFlyHome == 1 && demandList[i].isAppoint == 4){ // 03/24/08 ANG
			demandList[i].maintenanceRecordID = maintList[x].maintenanceRecordID;
		}
		if(maintList[x].apptType == 0)
			demandList[i].turnTime = optParam.maintTurnTime;
		else //maintList[x].apptType = 1 or 2 for airport appointment
			demandList[i].turnTime = 1; //buffer time to avoid promote overlap.


		//START - 11/13/07 ANG
		//To capture demandID assigned to every fake maintenance record
		if(optParam.autoFlyHome == 1){
			int y;
			CrewEndTourRecord *tPtr;
			tPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));// 11/13/07 ANG
			if(! tPtr) {
				logMsg(logFile,"%s Line %d, Out of Memory in createAppointDemand().\n", __FILE__,__LINE__);
				exit(1);
			}

			//if(maintList[x].apptType == 3){
			if(maintList[x].apptType >= 0){// 11/15/07 ANG
				for(y = 0, tPtr = crewEndTourList; y < crewEndTourCount; ++y, ++tPtr){
					if( //x >= numMaintenanceRecord - numFakeMaintenanceRecord && 
						maintList[x].maintenanceRecordID > 0 && 
						tPtr->recordID == maintList[x].maintenanceRecordID){
						tPtr->assignedDemandID = demandList[i].demandID;
						tPtr->assignedDemandInd = i;
						if (maintList[x].apptType == 3){ //11/20/07 ANG
							//get crewPairID
							int tempPairID;
							int tempAcID;
							int *aircraftPtr;
							CrewPair *cpPtr;
							cpPtr = (CrewPair *) calloc((size_t) 1, (size_t) sizeof(CrewPair));
							if(! cpPtr) {
								logMsg(logFile,"%s Line %d, Out of Memory in createAppointDemand().\n", __FILE__,__LINE__);
								exit(1);
							}
							cpPtr = crewPairList;
							while (cpPtr->crewPairID){
								aircraftPtr = cpPtr->aircraftID;
								tempAcID = *aircraftPtr;
								if((cpPtr->captainID == tPtr->crewID1 || cpPtr->flightOffID == tPtr->crewID1) &&
									tempAcID == tPtr->aircraftID){
									tempPairID = cpPtr->crewPairID;
									break;
								}//end if
								++cpPtr;
							}//end while

							//then store crewPairID
							if(tempPairID > 0){
								//demandList[i].crewPairID = tempPairID;
								tPtr->crewPairID = tempPairID;
							}
							else {
								fprintf(logFile, "WARNING: Could not find crewPairID for crewID = %d. \n", tPtr->crewID1);
							}
						}
						//printCrewEndTourList();
						break;
					}
				}
			}

		}//end if optParam.autoFlyHome == 1
		//END - 11/13/07 ANG

		//START - adjust turn time for MX only (no OS) if there is a test flight afterwards - 01/15/09 ANG
		//This is to enable Optimizer create duty node with test flight shortly after MX, 
		//even if the test flight is not in the existing tour
		if(demandList[i].isAppoint == 1){
			//Search through test flight
			for(m = 0; m < countTestFlights; m++){
				if( //testFlightList[m].aircraftID == demandList[i].aircraftID &&
					testFlightList[m].outAirportID == demandList[i].inAirportID &&
					testFlightList[m].reqOut > demandList[i].reqIn &&
					difftime(testFlightList[m].reqOut, demandList[i].reqIn) < 60*demandList[i].turnTime){
					demandList[i].turnTime = difftime(testFlightList[m].reqOut, demandList[i].reqIn)/60 - 1; //1 minute allowance to avoid "<" sign, if any
					break;
				}
			}
		}
		//END - 01/15/09 ANG

		numDemand++;
	}	
	printCrewEndTourList(); // 03/24/08 ANG
	return 0;
}


/********************************************************************************************************
*	Function   sortDemand									Date last modified:  9/12/06 SWO			*
*	Purpose:	Sort demand legs by reqOut now that we've added appointment and locked repo	legs and	*
*				have set reqOut = schedOut for locked legs.  Demand legs that are in a lockedTour		*
*				are placed at the end and not included in numOptDemand (the demand for optimization)	*
********************************************************************************************************/	
static int sortDemand(void)
{
	Demand temp;
	int i, j, lg;

	for(i=1; i < numDemand; i++){
		temp = demandList[i];
		j = i;
		while((j>0) && (demandList[j-1].inLockedTour > temp.inLockedTour || 
			(demandList[j-1].inLockedTour == temp.inLockedTour && demandList[j-1].reqOut > temp.reqOut))){
			demandList[j] = demandList[j-1];
			j--;
		}
		demandList[j] = temp;
	}

	//determine numOptDemand, the number of demand legs that are NOT in a locked tour and should be considered by optimization
	i = numDemand-1; 
	while(demandList[i].inLockedTour)
		i--;
	numOptDemand = i+1;
	
	//NOTE:  demandInd on legList must now be repopulated
	for(lg = 0; lg < numLegs; lg++){
		if(legList[lg].demandInd > -1){
			for(i = 0; i<numDemand; i++){
				if(demandList[i].demandID == legList[lg].demandID){
					legList[lg].demandInd = i;
					break;
				}
			}
		}
	}
	return 0;
}

/********************************************************************************
*	Function   findlastTripOfDay              Date last modified:  4/03/06 SWO	*
*	Purpose:  																	*
********************************************************************************/
static int findlastTripOfDay(void)
{
	int i, j;
	time_t endOfDay;
	
	//find last trip of each day in the planning window so that we can divide up trips by days
	j=0;
	for(i= 0; i< optParam.planningWindowDuration; i++)
	{
		endOfDay = firstEndOfDay + 24*3600*i;
		while(j < numOptDemand){
			if(demandList[j].reqOut<=endOfDay)
				j++;
			else
				break;
		}
		/*while(j < numOptDemand && demandList[j].reqOut<=endOfDay)
			j++;*/
		lastTripOfDay[i]= j-1;
	}
	return 0;
}


/********************************************************************************
*	Function   getAddlACData	              Date last modified:  03/07/07 SWO	*
*	Purpose:  																	*
********************************************************************************/
static int getAddlACData(void)
{
	int p, j, d;
	int m, update; //01/15/09 ANG
	extern int countTestFlights;
	extern Demand *testFlightList;

	for(p=0; p<numAircraft; p++)
	{
		//find acTypeList index
		for(j=0; j<numAcTypes; j++)
		{
			if(acTypeList[j].aircraftTypeID == acList[p].aircraftTypeID)
			{
				acList[p].acTypeIndex = j;
				j = numAcTypes;
			}
		}
		//exit with error message if no acTypeIndex found for plane
		if(acList[p].acTypeIndex == -1){
			logMsg(logFile,"%s Line %d, no acType Index found for aircraft ID %d\n", __FILE__,__LINE__, acList[p].aircraftID);
			writeWarningData(myconn); exit(1);
		}
		//Update available datetime of aircraft to include turntime, and to be no earlier than the 
		// start of the planning window (can't move the aircraft earlier than that)

		if (acList[p].maintFlag == 0){
			acList[p].availDT += optParam.turnTime*60;
			//START - 02/28/08 ANG
			//remove nextLegStartTm: RLZ 06/23/08
			/*
			if(acList[p].nextLegStartTm < acList[p].availDT) 
				acList[p].availDT += optParam.turnTime*60;
			else
				acList[p].availDT = min(acList[p].availDT+optParam.turnTime*60, acList[p].nextLegStartTm);
			*/
			//END - 02/28/08 ANG*/
		}
		else if(acList[p].maintFlag == 1){//coming out of maintenance
			//acList[p].availDT += optParam.maintTurnTime*60; //Replaced by codes below

			//START - 01/15/09 ANG
			//If there is a test flight shortly after out from MX, don't adjust availDT too far
			//We still want to cover the test flight, even if it is not scheduled in exg tour
			update = 0;
			//First check test flight list
			for(m = 0; m < countTestFlights; m++){
				if( //testFlightList[m].aircraftID == acList[p].aircraftID &&
					testFlightList[m].outAirportID == acList[p].availAirportID &&
					testFlightList[m].reqOut > acList[p].availDT &&
					difftime(testFlightList[m].reqOut, acList[p].availDT) < 60*optParam.maintTurnTime){
					acList[p].availDT += difftime(testFlightList[m].reqOut, acList[p].availDT) - 60; //1 minute allowance to avoid "<" sign, if any
					update = 1;
					break;
				}
			}
			if(update == 0)//not yet updated
				acList[p].availDT += optParam.maintTurnTime*60;
			//END - 01/15/09 ANG
		}
		//else acList[p].maintFlag == 2 - coming from airport appointment, so no turnTime is needed
		else if(acList[p].maintFlag == 2)//coming out of airport appointment
			acList[p].availDT += 60; // one minute turn time

		//if(acList[p].availDT < optParam.windowStart)
		//	acList[p].availDT = optParam.windowStart;
		//initialize lastExcl and lastIncl
		for(d=0; d<optParam.planningWindowDuration; d++){
			acList[p].lastExcl[d] = -1;
			acList[p].lastIncl[d] = -1;
		}
		//initialize inclDemandInd and exclDemandInd
		for(d = 0; d<MAX_LEG_INCL; d++)
			acList[p].inclDemandInd[d] = -1;
		for(d = 0; d<MAX_LEG_EXCL; d++)
			acList[p].exclDemandInd[d] = -1;
		//initialize sepNW field
		acList[p].sepNW = 0;
	}
	
	return 0;
}

/****************************************************************************************************
*	Function   storeAircraftAndCrewPairInclusions	     Date last modified:  11/21/06 SWO			*
*	Purpose:  	Store demand indices of required appointment/maintenance and locked legs with crews *	
*				and planes.  Also store locked crew assoc. with inclusions (if any) for planes		*																		*
****************************************************************************************************/
static int storeAircraftAndCrewPairInclusions(void)
{
	int i, j, k, day, d;
	//store demand index for (required) appoint/maint legs and locked legs, in order of increasing schedOut, with the aircrafts and crewPairs, and increment number of inclusions
	for(day = 0; day < optParam.planningWindowDuration; day++){
		for(i=(day == 0? 0 : (lastTripOfDay[day-1]+1)); i<=lastTripOfDay[day]; i++){
			if(demandList[i].acInd > -1 && demandList[i].inLockedTour == 0){
				j = demandList[i].acInd;
				//update lastIncl (last inclusion) for this day and all days that follow
				for(d=day; d<optParam.planningWindowDuration; d++)
					acList[j].lastIncl[d]++;
				acList[j].inclDemandInd[acList[j].lastIncl[day]] = i;
				acList[j].inclCrewID[acList[j].lastIncl[day]] = demandList[i].crewPairID;
				if(demandList[i].crewPairID > 0){
					for(k = 0; k<numCrewPairs; k++){//find (temporary) index k of crewPairList
						if(crewPairList[k].crewPairID == demandList[i].crewPairID){
							crewPairList[k].inclDemandInd[crewPairList[k].numIncl] = i;
							crewPairList[k].numIncl++;
							break;
						}
					}
					//exit with error message if crewPairList index not found for inclusion with a crewPairID
					if(k == numCrewPairs){
						logMsg(logFile,"%s Line %d, no crewPairList index found for inclusion with a crewPairID:  demandList[%d]\n", __FILE__,__LINE__, i);
						writeWarningData(myconn); exit(1);
					}
				}
			}
		}
	}
	return 0;
}

/********************************************************************************
*	Function   processExclusionList			  Date last modified:  7/09/07 SWO	*
*	Purpose:																	*
*	    Store fleet-airport exclusions in a matrix where the first index is the	*
*	 airportID, and the second index is the aircraftType index.					*
*		Store curfews in a matrix where the first (row) index is the airportID,	*
*	the first column stores the number of curfews at the airport, and the		*
*	remaining columns are allocated as necessary to store the start and end of	*
*	each curfew.																*
*		Store demand-aircraft exclusions with aircraft.							*
********************************************************************************/

//ISSUE:  INPUT DATA CANNOT CURRENTLY ACCOMODATE MORE THAN ONE CURFEW AT AN AIRPORT
static int processExclusionList(void)
{
	int i, j, k, day, d, found;
	char writetodbstring1[200];

	int *intPtr;
	//harry int numCurfews[TOTAL_AIRPORTS_NUM + 1];
	int *numCurfews = NULL;

	if(!(numCurfews = calloc(TOTAL_AIRPORTS_NUM + 1, sizeof(int)))) {
		logMsg(logFile,"%s Line %d: out of memory.\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	
	//allocate memory for aptExcl. and initialize numCurfews
	aptExcl = (int **) calloc((TOTAL_AIRPORTS_NUM + 1), sizeof(int *));
	for(i = 0; i<= TOTAL_AIRPORTS_NUM; i++)
	{
		if ((intPtr = (int *)calloc(numAcTypes,sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d: Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		aptExcl[i] = intPtr;
		numCurfews[i] = 0;
	}

	//pass through list of exclusions, storing fleet-airport and demand-aircraft exclusions, and counting curfews at each airport
	for(i=0; i < numExclusion; i++)
	{
		if(exclusionList[i].typeID == 1){ //airport curfew start
			//exit with error message if airport ID not correct
			if(exclusionList[i].firstID <=0 || exclusionList[i].secondID > TOTAL_AIRPORTS_NUM)
			{
				logMsg(logFile,"%s Line %d, airport ID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, airport ID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
					  }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }
					initializeWarningInfo(&errorinfoList[errorNumber]);	    
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=31;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
			numCurfews[exclusionList[i].secondID] ++;
		}
		else if(exclusionList[i].typeID == 3) //aircraftType - airport
		{
			//exit with error message if airport ID not correct
			if(exclusionList[i].secondID <=0 || exclusionList[i].secondID > TOTAL_AIRPORTS_NUM){
				logMsg(logFile,"%s Line %d, airport ID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, airport ID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
					  }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }	
				initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=31;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
			for(j= 0;j < numAcTypes; j++){
				if(acTypeList[j].aircraftTypeID == exclusionList[i].firstID){
					aptExcl[exclusionList[i].secondID][j]= 1;
					break;
				}
			}
			//exit with error message if aircraftType not found
			if(j == numAcTypes){
				logMsg(logFile,"%s Line %d, aircraftTypeID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, aircraftTypeID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
					  }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }	
				initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=32;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
				writeWarningData(myconn); exit(1);
			}
		}
		else if(exclusionList[i].typeID == 4) //demand - aircraft
		{	//find acList index for aircraftID
			for(j= 0;j<numAircraft; j++){
				if(acList[j].aircraftID == exclusionList[i].secondID){
					//find and store demand index for exclusions, in order of increasing reqOut, with the aircrafts, and increment number of exclusions
					found = 0;
					for(day = 0; day < optParam.planningWindowDuration; day++){
						for(k=(day == 0? 0 : (lastTripOfDay[day-1]+1)); k<=lastTripOfDay[day]; k++){
							if(demandList[k].demandID == exclusionList[i].firstID){
								found = 1;
								//update lastExcl for this day and all days that follow
								//for(d=day; d<optParam.planningWindowDuration; d++) - moved to insertExclusedDemandList() - 11/13/09 ANG
								//	acList[j].lastExcl[d]++;
								insertExcludedDemandList(k, j, day); //10/13/09 ANG
								//acList[j].exclDemandInd[acList[j].lastExcl[day]] = k;
								k = numDemand; //to terminate loop
								day = optParam.planningWindowDuration;//to terminate loop
							}
						}
					}
					//exit with error message if demand index is not found
					if(found == 0){
						logMsg(logFile,"%s Line %d, demandID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
                        sprintf(writetodbstring1, "%s Line %d, demandID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                        if(errorNumber==0)
			              {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                     {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		                      writeWarningData(myconn); exit(1);
					         }
				          }
				        else
				          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                      {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		                       writeWarningData(myconn); exit(1);
	                          }
			               }
						initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=33;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
						writeWarningData(myconn); exit(1);
					}
					break;
				}
			}
			//exit with error message if acIndex is not found
			if(j == numAircraft){
				logMsg(logFile,"%s Line %d, aircraftID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, aircraftID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		             {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		              writeWarningData(myconn); exit(1);
					 }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		             {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		              writeWarningData(myconn); exit(1);
	                 }
			       }
				initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=34;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
				writeWarningData(myconn); exit(1);
			}
		}
		else if(exclusionList[i].typeID == 5) //aircraft - state exclusion - 08/27/09 ANG
		{	//find acList index for aircraftID
			for(j= 0;j<numAircraft; j++){
				if(acList[j].aircraftID == exclusionList[i].firstID){
					//find and store demand index for exclusions, in order of increasing reqOut, with the aircrafts, and increment number of exclusions
					found = 0;
					for(day = 0; day < optParam.planningWindowDuration; day++){
						for(k=(day == 0? 0 : (lastTripOfDay[day-1]+1)); k<=lastTripOfDay[day]; k++){
							//go through ALL demands
							if(demandList[k].outCountryID == -exclusionList[i].secondID || demandList[k].inCountryID == -exclusionList[i].secondID){
								found = 1;
								//update lastExcl for this day and all days that follow
								//for(d=day; d<optParam.planningWindowDuration; d++) - moved to insertExclusedDemandList() - 11/13/09 ANG
								//	acList[j].lastExcl[d]++;
								insertExcludedDemandList(k, j, day); //10/13/09 ANG
								//acList[j].exclDemandInd[acList[j].lastExcl[day]] = k;
							}
						}
					}
					//exit with error message if demand index is not found
					if(found == 0){
						logMsg(logFile,"%s Line %d, demandIDs related to for exclusionList[%d] cannot be found.\n", __FILE__,__LINE__, i);
					}
					break;
				}
			}
			//exit with error message if acIndex is not found
			if(j == numAircraft){
				logMsg(logFile,"%s Line %d, aircraftID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
			}
		}
	}
	//allocate memory for aptCurf and store numCurfews with each airport
	aptCurf = (int **) calloc((TOTAL_AIRPORTS_NUM+1), sizeof(int *));
	for(i = 0; i<= TOTAL_AIRPORTS_NUM; i++)
	{
		if ((intPtr = (int *)calloc((1+numCurfews[i]*2),sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d: Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		aptCurf[i] = intPtr;
		aptCurf[i][0] = numCurfews[i];
	}
	//pass through list of exclusions, storing curfews at each airport
	//decrement numCurfews to track how many we have stored thus far
//ISSUE:  CURRENTLY CODE IS RELYING ON CURFEWS AT EACH AIRPORT TO BE SORTED BY PAIR:  start, associated end, start, associated end, start, associated end ETC
	for(i=0; i < numExclusion; i++){
		if(exclusionList[i].typeID == 1){ //airport curfew start
			//exit with error message if airport ID not correct
			if(exclusionList[i].firstID <=0 || exclusionList[i].secondID > TOTAL_AIRPORTS_NUM){
				logMsg(logFile,"%s Line %d, airport ID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, airport ID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
					  }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }
				   initializeWarningInfo(&errorinfoList[errorNumber]);	    
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=31;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
			j = 2*(aptCurf[exclusionList[i].secondID][0] - numCurfews[exclusionList[i].secondID])+ 1; //find appropriate column in aptCurf
			aptCurf[exclusionList[i].secondID][j] = exclusionList[i].firstID;
		}
		else if(exclusionList[i].typeID ==2){ //airport curfew end
			//exit with error message if airport ID not correct
			if(exclusionList[i].firstID <=0 || exclusionList[i].secondID > TOTAL_AIRPORTS_NUM){
				logMsg(logFile,"%s Line %d, airport ID for exclusionList[%d] is invalid.\n", __FILE__,__LINE__, i);
				sprintf(writetodbstring1, "%s Line %d, airport ID for exclusionList[%d] is invalid.", __FILE__,__LINE__, i);
                if(errorNumber==0)
			      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
					  }
				  }
				else
				  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		              {logMsg(logFile,"%s Line %d, Out of Memory in processExclusionList().\n", __FILE__,__LINE__);
		               writeWarningData(myconn); exit(1);
	                  }
			      }
					initializeWarningInfo(&errorinfoList[errorNumber]);	    
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging");
				   errorinfoList[errorNumber].exclusionindex=i;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=31;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				writeWarningData(myconn); exit(1);
			}
			j = 2*(aptCurf[exclusionList[i].secondID][0] - numCurfews[exclusionList[i].secondID])+ 1; //find appropriate column in aptCurf
			aptCurf[exclusionList[i].secondID][j+1] = exclusionList[i].firstID;
			numCurfews[exclusionList[i].firstID]--;
		}
	}

	if(numCurfews)
		free(numCurfews);
	return 0;
}


/********************************************************************************
*	Function   getAddlDemandData2             Date last modified:  4/12/07 SWO	*
*	Purpose:	Get elapsed time, block time and cost for demand legs.			*
********************************************************************************/
static int getAddlDemandData2(void)
{
	int i, j, temp, startTypeIndex, maxTypeIndex, i2, a;
	int flightTm=0, elapsedTm=0, blockTm = 0, numStops = 0;
//	double incRev;
	extern int numMacInfo;
	extern MacInfo *macInfoList;

	for(i = 0; i < numOptDemand; i++)
	{	
		//if demand is a maintenance/appointment
		if(demandList[i].isAppoint > 0){   
			demandList[i].feasible = 1;
			temp = demandList[i].elapsedTm[0];
			demandList[i].elapsedTm[0] = INFINITY;
			j = acList[demandList[i].acInd].acTypeIndex;
			demandList[i].blockTm[j]= 0;
			demandList[i].elapsedTm[j]=temp;
			demandList[i].early[j] = (int)(demandList[i].reqOut / 60);
			demandList[i].late[j] = (int)(demandList[i].reqOut / 60);
			demandList[i].cost[j] = 0.0;
			//demandList[i].macOprCost[j] = 0.0; //MAC - 08/19/08 ANG
			//demandList[i].macOwnerCost[j] = 0.0; //MAC - 08/19/08 ANG
		}
		//else if demand is not tied to an aircraft (not appointment and not locked leg)
		else if(demandList[i].aircraftID == 0)
		{
			j = 0;
			//block time is infinite (infeasible) for aircraft types with lower sequence positions than demand sequence position
			while(acTypeList[j].sequencePosn < (demandList[i].sequencePosn - demandList[i].downgradeRecovery) && j<numAcTypes)//acTypeList is sorted by seqencePosn
				j++;
			startTypeIndex = j;

			//remove CJ1 from the consideration: RLZ: 
			startTypeIndex = max(startTypeIndex, 1);
			
			//block time is infinite (infeasible) for aircraft types that involve an upgrade greater than the max allowed
			maxTypeIndex = numAcTypes - 1;
			while(maxTypeIndex > -1 && acTypeList[maxTypeIndex].sequencePosn > (demandList[i].sequencePosn + max(demandList[i].maxUpgradeFromRequest, max(demandList[i].upgradeRecovery, acTypeList[startTypeIndex].maxUpgrades))))
				maxTypeIndex--;
			//for sales demos, MUST use requested type //RLZ: may need different condition to tell sales demo
			//No contractID for sales demo
			if((demandList[i].outAirportID == demandList[i].inAirportID || demandList[i].contractID < 0)
				&& demandList[i].contingecnyfkdmdflag!=1){
					//if (demandList[i].ownerID != 87359)  //RLZ 07/31/08 Exclude the contigency demand
					//Also need to exclude test flights that is not yet locked to aircraft - some test flights are not built under correct contract - 01/23/09 ANG
					//Later, if test flights are built under correct contract, we can rollback to use the line commented out above
					if (demandList[i].ownerID != 87359 && demandList[i].ownerID != 24299)  
 						maxTypeIndex = startTypeIndex;
				}
		}
		else //if locked leg
		{
			startTypeIndex = acList[demandList[i].acInd].acTypeIndex;
			maxTypeIndex = startTypeIndex;
		}

		//for all non-appointment legs, populate block time, elapsed time, early and late times, cost, and feasible indicator
		if(!demandList[i].isAppoint){//if NOT a maintenance or appointment leg
			demandList[i].feasible = 0;
			for(j = startTypeIndex; j <= maxTypeIndex; j++)
			{
				//PER BILL HALL (CSH CIO) 4/12/06:  assume that demand aircraftType is NOT excluded from airports; 
				//only worry about aircraftType -airport exclusions for complementary upgrades
				if( (demandList[i].aircraftTypeID != acTypeList[j].aircraftTypeID && 
					(aptExcl[demandList[i].inAirportID][j] ==1 || aptExcl[demandList[i].outAirportID][j]==1))					
					|| 	demandList[i].numPax > acTypeList[j].capacity)  //RLZ 021008 consider the number of seats.
						demandList[i].blockTm[j] = INFINITY;
				else if(demandList[i].outAirportID == demandList[i].inAirportID){  //for sales demos
					demandList[i].elapsedTm[j] = (int)(demandList[i].reqIn - demandList[i].reqOut)/60;
					demandList[i].blockTm[j] = demandList[i].elapsedTm[j];  //conservatively assume
					demandList[i].early[j] = (int)(demandList[i].reqOut / 60);  
					demandList[i].late[j] = (int)(demandList[i].reqOut / 60); 
					demandList[i].cost[j] = ((demandList[i].blockTm[j] - optParam.taxiInTm - optParam.taxiOutTm)*acTypeList[j].operatingCost)/60 + acTypeList[j].taxiCost;
					demandList[i].feasible = 1;
					//MAC - 08/19/08 ANG, 05/27/09 ANG
					if(optParam.withMac == 1){
						//demandList[i].macOprCost[j] = ((demandList[i].blockTm[j] - optParam.taxiInTm - optParam.taxiOutTm)*acTypeList[j].macOprCost)/60 + acTypeList[j].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
						for(a = 0; a < numMacInfo; a++){
							if(macInfoList[a].aircraftTypeID == acTypeList[j].aircraftTypeID){
								demandList[i].macOprCost[a] = ((demandList[i].blockTm[j] - optParam.taxiInTm - optParam.taxiOutTm)* macInfoList[a].macDOC)/60 + acTypeList[j].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
							}
						}
					}
					//if(demandList[i].isMacDemand == 1)
					//	demandList[i].macOwnerCost[j] = ((demandList[i].blockTm[j] - optParam.taxiInTm - optParam.taxiOutTm)*acTypeList[j].macOwnerCost)/60 + acTypeList[j].taxiCost;
					//else
					//	demandList[i].macOwnerCost[j] = demandList[i].cost[j];
					//
				}
				else //get flight and elapsed times from flight calculator, including taxi times
				{
					getFlightTime(demandList[i].outAirportID, demandList[i].inAirportID, acTypeList[j].aircraftTypeID,
						month, demandList[i].numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
					demandList[i].blockTm[j]= blockTm;
					demandList[i].elapsedTm[j]= elapsedTm;
					getEarlyAndLateDepartTimes(i, j);
					demandList[i].cost[j] = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost; 

					//MAC - 08/19/08 ANG
					if(optParam.withMac == 1){
						//demandList[i].macOprCost[j] = (flightTm*acTypeList[j].macOprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
						for(a = 0; a < numMacInfo; a++){
							if(macInfoList[a].aircraftTypeID == acTypeList[j].aircraftTypeID){
								demandList[i].macOprCost[a] = (flightTm*macInfoList[a].macDOC)/60 + (numStops+1)*acTypeList[j].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
							}
						}
					}
					//if(demandList[i].isMacDemand == 1)
					//	demandList[i].macOwnerCost[j] = (flightTm*acTypeList[j].macOwnerCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
					//else
					//	demandList[i].macOwnerCost[j] = demandList[i].cost[j];
					//

					if(demandList[i].incRevStatus[j] != -1){
						demandList[i].cost[j] -= demandList[i].incRev[j]; //RLZ: Revenue to cost
						if(optParam.withMac == 1){
							//demandList[i].macOprCost[j] -= demandList[i].incRev[j]; //MAC - 08/26/08 ANG//MacDOC per MAC - 05/20/2009 ANG
							for(a = 0; a < numMacInfo; a++){
								if(macInfoList[a].aircraftTypeID == acTypeList[j].aircraftTypeID){
									demandList[i].macOprCost[a] -= demandList[i].incRev[j];
								}
							}
						}
						//demandList[i].macOwnerCost[j] -= demandList[i].incRev[j]; //MAC - 08/26/08 ANG
					}
					if(blockTm < optParam.maxFlightTm && elapsedTm < optParam.maxDutyTm)
						demandList[i].feasible = 1;
					
				}
			}
		}
		//populate earlyMPM:  earliest departure time in minutes past midnight, local time (used to check against cutoffForFinalRepo)
		demandList[i].earlyMPM = minutesPastMidnight((demandList[i].reqOut - 60*demandList[i].earlyAdj), demandList[i].outAirportID);
	}
	
	for(i = 0; i<numOptDemand; i++){
		if(demandList[i].contractID > 0){ //if demand is not appointment or a demo
			i2 = i + 1;
			//for all later demand legs that start within 90 minutes after first leg ends
			while((demandList[i2].reqOut - demandList[i].reqIn) <= 90*60 && i2<numOptDemand){
				 //if same owner and legs connect
				if(demandList[i].contractID == demandList[i2].contractID && demandList[i].inAirportID == demandList[i2].outAirportID){
					//we will tie / fly legs together
					demandList[i].succDemID = demandList[i2].demandID;
					demandList[i2].predDemID = demandList[i].demandID;
					break;
				}
				i2++;
			}
		}
	}
		
	return 0;
}


/****************************************************************************************
*	Function   getInternationalExclusions		Date last modified:  3/09/07 SWO	    *
*	Purpose: Store international demand legs as exclusions for uncertified aircraft		*
****************************************************************************************/
static int getInternationalExclusions(void)
{
	int k, j, x, day, endDay, startDay, i, d;
	int uncertStart, uncertEnd; //as (int)time_t/60, i.e. minutes
	
	for(k = 0; k<numAircraft; k++){
		uncertStart = 0;
		uncertEnd = 0;
		j = acList[k].acTypeIndex;
		if(acList[k].intlCert == 0){
			uncertStart = (int)optParam.windowStart/60 - 12*60; //conservatively put 12 hours before planning window start
			startDay = 0;
		}
		x = 0;
		while(x <= acList[k].lastIncl[optParam.planningWindowDuration - 1]){
			if(uncertStart == 0){//we are looking for the start of (the next) uncertified window
				if(demandList[acList[k].inclDemandInd[x]].isAppoint == 3){
					//we have found the start of an uncertified window - a sales/signing appointment
					uncertStart = demandList[acList[k].inclDemandInd[x]].early[j];
					for(day = 0; day < optParam.planningWindowDuration; day++){
						if(acList[k].inclDemandInd[x]<= lastTripOfDay[day]){
							startDay = day;
							break;
						}
					}
				}
			}
			//else look for the end of the current uncertified window
			else if(demandList[acList[k].inclDemandInd[x]].isAppoint == 0 && 
				(demandList[acList[k].inclDemandInd[x]].outCountryID > 1 || demandList[acList[k].inclDemandInd[x]].inCountryID > 1)){
				//we have found the end of an uncertified window - a locked international leg that must be connected to a flywire;
				//any international demands within window are exclusions for the aircraft
				uncertEnd = demandList[acList[k].inclDemandInd[x]].late[j];
				for(day = 0; day < optParam.planningWindowDuration; day++){
					if(acList[k].inclDemandInd[x]<= lastTripOfDay[day]){
						endDay = day;
						break;
					}
				}
				for(day = startDay; day <=endDay; day++){
					for(i=(day == 0? 0 : (lastTripOfDay[day-1]+1)); i<=lastTripOfDay[day]; i++){
						if(demandList[i].inLockedTour == 1)
							break; //all remaining demand legs are in locked tours and need not be considered as exclusions
						if(demandList[i].acInd > -1) //if demand is tied to a plane, it is an inclusion (required leg) 
							continue; // for a particular plane and need not be considered as an exclusion
						if(demandList[i].outCountryID > 1 || demandList[i].inCountryID > 1){ //if demand goes out of USA
							//if demand could be flown on a plane from this fleet and plane is uncertified at time of demand
							if(demandList[i].blockTm[j]!= INFINITY &&
								(demandList[i].late[j] >= uncertStart && demandList[i].early[j] <= uncertEnd)){

								//update lastExcl for this day and all days that follow
								//for(d=day; d<optParam.planningWindowDuration; d++) - moved to insertExclusedDemandList() - 11/13/09 ANG
								//	acList[k].lastExcl[d]++;
								insertExcludedDemandList(i, k, day); //10/13/09 ANG
								//acList[k].exclDemandInd[acList[k].lastExcl[day]] = i;
							}
						}
					}
				}
				//reset uncertStart
				uncertStart = 0;
			} //end else if(loop 
			x++;
		} //end while(x loop

		if(uncertStart > 0){  //plane is uncertified at the end of the planning window
			endDay = optParam.planningWindowDuration - 1;
			for(day = startDay; day <=endDay; day++){
				for(i=(day == 0? 0 : (lastTripOfDay[day-1]+1)); i<=lastTripOfDay[day]; i++){
					if(demandList[i].inLockedTour == 1)
						break; //all remaining demand legs are in locked tours and need not be considered as exclusions
					if(demandList[i].acInd > -1) //if demand is tied to a plane, it is an inclusion (required leg) 
						continue; // for a particular plane and need not be considered as an exclusion
					if(demandList[i].outCountryID > 1 || demandList[i].inCountryID > 1){ //if demand goes out of USA
						//if demand could be flown on a plane from this fleet and plane is uncertified at time of demand
						if(demandList[i].blockTm[j]!= INFINITY && demandList[i].late[j] >= uncertStart){

							//update lastExcl for this day and all days that follow
							//for(d=day; d<optParam.planningWindowDuration; d++) - moved to insertExclusedDemandList() - 11/13/09 ANG
							//	acList[k].lastExcl[d]++;
							insertExcludedDemandList(i, k, day); //10/13/09 ANG
							//acList[k].exclDemandInd[acList[k].lastExcl[day]] = i;
							//What about those exclusions that are stored already? - 10/13/09 ANG
						}
					}
				}
			}
		} //end if(uncertStart > 0) //plane is uncertified at end of the planning window loop
	}  //end for(k loop

	return 0;
}

/****************************************************************************************
*	Function   getEarlyAndLateDepartTimes		Date last modified:  2/28/07 SWO	    *
*	Purpose: Set early and late demand departure times using adjustments based on		*
*			contract, peak day, etc.  Limit early and late adjustments of				*
*			demand departures if necessary due to curfews.								*
*			Demand departing at schedOut will be considered feasible					*
*			regardless of curfews.  Modify early and late no more than once each		*
*			to avoid curfews.  If additional modifications are needed, simply set early	*
*			or late equal to the requested outtime (prohibit adjustment).				*
****************************************************************************************/
static void getEarlyAndLateDepartTimes(int i, int j)
{	
	//i == demandList index and j == acTypeList index
	
	int startAptID, endAptID, startTm, depMPM, arrMPM, x, outMPM;
	int newEarly = 0;
	int newLate = 0;

	demandList[i].early[j] = (int)(demandList[i].reqOut/60) - demandList[i].earlyAdj;
	demandList[i].late[j] = (int)(demandList[i].reqOut/60) + demandList[i].lateAdj;

	startTm = (int)demandList[i].reqOut/60;
	startAptID = demandList[i].outAirportID;
	endAptID = demandList[i].inAirportID;
	
	//first, check for early departure....
	depMPM = minutesPastMidnight(demandList[i].early[j]*60, startAptID);
	outMPM = minutesPastMidnight(demandList[i].reqOut, startAptID);

	//RLZ: 6AM FLEX
	if (depMPM < optParam.noEarlyThanFlex && outMPM > depMPM ){
		demandList[i].early[j] = demandList[i].early[j] + (optParam.noEarlyThanFlex - depMPM);
		depMPM = minutesPastMidnight(demandList[i].early[j]*60, startAptID);
		logMsg(logFile, "\n demandID flex limit to %d/60 AM: \n", optParam.noEarlyThanFlex);
		logMsg(logFile, "%d ,", demandList[i].demandID);
	}




	x = 0;
	while(x < aptCurf[startAptID][0]){
		//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
		if(aptCurf[startAptID][2*x+1] > aptCurf[startAptID][2*x+2]){
			if(depMPM > aptCurf[startAptID][2*x+1]){
				demandList[i].early[j] += (1440 - depMPM + aptCurf[startAptID][2*x+2]);
				break;
			}
			if(depMPM < aptCurf[startAptID][2*x+2]){
				demandList[i].early[j] += (aptCurf[startAptID][2*x+2] - depMPM);
				break;
			}
		}
		else if(depMPM > aptCurf[startAptID][2*x+1] && depMPM < aptCurf[startAptID][2*x+2]){ //curfew doesn't cross midnight
			demandList[i].early[j] += (aptCurf[startAptID][2*x+2] - depMPM);
			break;
		}
		x++;
	}
	
	if(demandList[i].early[j] > startTm)
		demandList[i].early[j] = startTm;

	arrMPM = minutesPastMidnight((demandList[i].early[j] + demandList[i].elapsedTm[j])*60, endAptID);
	x=0;
	while(x < aptCurf[endAptID][0]){
		//if start of curfew (in minutes past Midnight) is greater than end of curfew, start is < midnite and end is after
		if(aptCurf[endAptID][2*x+1] > aptCurf[endAptID][2*x+2]){
			if(arrMPM > aptCurf[endAptID][2*x+1]){
				demandList[i].early[j] += (1440 - arrMPM + aptCurf[endAptID][2*x+2]);
				newEarly = 1; //departure time must be checked (again)
				break;
			}
			if(arrMPM < aptCurf[endAptID][2*x+2]){
				demandList[i].early[j] += (aptCurf[endAptID][2*x+2] - arrMPM);
				newEarly = 1; //departure time must be checked (again)
				break;
			}
		}
		else if(arrMPM > aptCurf[endAptID][2*x+1] && arrMPM < aptCurf[endAptID][2*x+2]){  //curfew doesn't cross midnight
			demandList[i].early[j] += (aptCurf[endAptID][2*x+2] - arrMPM);
			newEarly = 1; //departure time must be checked (again)
			break;
		}
		x++;
	}
	
	
	if(demandList[i].early[j] > startTm){
		demandList[i].early[j] = startTm;
		newEarly = 0;
	}
	




	//if we have modified departure time due to endApt curfew, and we need to modify again due to startApt curfew, prohibit early adjustment
	//(adjustment caused us to hit curfew in this case)
	if(newEarly == 1){
		depMPM = minutesPastMidnight(demandList[i].early[j]*60, startAptID);
		x = 0;
		while(x < aptCurf[startAptID][0]){
			//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
			if(aptCurf[startAptID][2*x+1] > aptCurf[startAptID][2*x+2]){
				if(depMPM > aptCurf[startAptID][2*x+1] || depMPM < aptCurf[startAptID][2*x+2]){
					demandList[i].early[j] = startTm;	
					break;
				}
			}
			else if(depMPM > aptCurf[startAptID][2*x+1] && depMPM < aptCurf[startAptID][2*x+2]){ //curfew doesn't cross midnight
				demandList[i].early[j] = startTm;	
				break;
			}
			x++;
		}
	}

	
	
	
	
	//now, check for late departure / arrival...
	arrMPM = minutesPastMidnight((demandList[i].late[j] + demandList[i].elapsedTm[j])*60, endAptID);
	x=0;
	while(x < aptCurf[endAptID][0]){
		//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
		if(aptCurf[endAptID][2*x+1] > aptCurf[endAptID][2*x+2]){
			if(arrMPM > aptCurf[endAptID][2*x+1]){
				demandList[i].late[j] -= (arrMPM - aptCurf[endAptID][2*x+1]);
				break;
			}
			if(arrMPM < aptCurf[endAptID][2*x+2]){
				demandList[i].late[j] -= (arrMPM + 1440 - aptCurf[endAptID][2*x+1]);
				break;
			}
		}
		else if(arrMPM > aptCurf[endAptID][2*x+1] && arrMPM < aptCurf[endAptID][2*x+2]){//curfew doesn't cross midnight
			demandList[i].late[j] -= (arrMPM - aptCurf[endAptID][2*x+1]);
			break;
		}
		x++;
	}
	if(demandList[i].late[j] < startTm)
		demandList[i].late[j] = startTm;

	depMPM = minutesPastMidnight(demandList[i].late[j]*60, startAptID);
	x=0;
	while(x < aptCurf[startAptID][0]){
		//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
		if(aptCurf[startAptID][2*x+1] > aptCurf[startAptID][2*x+2]){
			if(depMPM > aptCurf[startAptID][2*x+1]){
				demandList[i].late[j] -= (depMPM - aptCurf[startAptID][2*x+1]);
				newLate = 1; //arrival time must be checked (again)
				break;
			}
			if(depMPM < aptCurf[startAptID][2*x+2]){
				demandList[i].late[j] -= (depMPM + 1440 - aptCurf[startAptID][2*x+1]);
				newLate = 1; //arrival time must be checked (again)
				break;
			}
		}
		else if(depMPM > aptCurf[startAptID][2*x+1] && depMPM < aptCurf[startAptID][2*x+2]){ //curfew doesn't cross midnight
			demandList[i].late[j] -= (depMPM - aptCurf[startAptID][2*x+1]);
			newLate = 1; //arrival time must be checked (again)
			break;
		}
		x++;
	}
	if(demandList[i].late[j] < startTm){
		demandList[i].late[j] = startTm;
		newLate = 0;
	}

	//if we have modified arrival time due to startApt curfew, and we need to modify again due to endApt curfew, prohibit late adjustment
	//(adjustment caused us to hit curfew in this case)
	if(newLate == 1){
		arrMPM = minutesPastMidnight((demandList[i].late[j] + demandList[i].elapsedTm[j])*60, endAptID);
		x = 0;
		while(x < aptCurf[endAptID][0]){
			//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
			if(aptCurf[endAptID][2*x+1] > aptCurf[endAptID][2*x+2]){
				if(arrMPM > aptCurf[endAptID][2*x+1] || arrMPM < aptCurf[endAptID][2*x+2]){
					demandList[i].late[j] = startTm;	
					break;
				}
			}
			else if(arrMPM > aptCurf[endAptID][2*x+1] && arrMPM < aptCurf[endAptID][2*x+2]){//curfew doesn't cross midnight
				demandList[i].late[j] = startTm;	
				break;
			}
			x++;
		}
	}
	return;
}

/********************************************************************************
*	Function   getFlightTime					Date last modified:  3/27/06 SWO
********************************************************************************/
int taxiOutTm;
int taxiInTm;
int fuelStopTime;

int getFlightTime(int outAirportID, int inAirportID, int aircraftTypeID, int month, int numPax, int *flightTm, int *blockTm, int *elapsedTm, int *numStops)				
{
	struct acTypeXlatTab {
		int aircraftCd;
		int aircraftType;
	} xref[] = {
		{ 1,  6 }, // CJ1
		{ 2,  5 }, // BRAVO
		{ 3, 11 }, // EXCEL
		{ 4, 52 }, // SOVEREIGN
		{ 5, 54 },  // CJ3
		{ 6, 20 }  // CX - 12/04/2009 ANG
	};

	//Fuel stop table maxinum pax cutoff for different aircraftType RLZ: 11/15/2007
	struct maxPaxXacType {
		int aircraftCd;
		int maxPax;
	} paxXref[] = {
		{ 0,  0 }, // Dummy
		{ 1,  5 }, // CJ1
		{ 2,  8 }, // BRAVO
		{ 3,  9 }, // EXCEL
		{ 4,  9 }, // SOVEREIGN
		{ 5,  7 },  // CJ3
		{ 6,  9 }  // CX - 12/04/2009 ANG
	};


	int x;
	int aircraftCd;
	FlightCalcOutput *fcoPtr, fcoBuf;
	AirportLatLon *orig_all, *dst_all;
	char writetodbstring1[200];

	taxiOutTm = optParam.taxiOutTm;
	taxiInTm = optParam.taxiInTm;
	fuelStopTime = optParam.fuelStopTm;

	if((orig_all = getAirportLatLonInfoByAptID(outAirportID)) == NULL) {
		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, outAirportID);
		sprintf(writetodbstring1, "%s Line %d, airportID %d not found", __FILE__,__LINE__, outAirportID);
		if(errorNumber==0)
		  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getFlightTime().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
			   }
		  }
	    else
		  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getFlightTime().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
	          }
	      }	    
		initializeWarningInfo(&errorinfoList[errorNumber]);
	    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
        strcpy(errorinfoList[errorNumber].group_name,"group_airport");
	    errorinfoList[errorNumber].airportid=outAirportID;
		sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
	    errorinfoList[errorNumber].format_number=21;
        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
        errorNumber++;
		writeWarningData(myconn); exit(1);
	}
	if((dst_all = getAirportLatLonInfoByAptID(inAirportID)) == NULL) {
		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, inAirportID);
		sprintf(writetodbstring1, "%s Line %d, airportID %d not found", __FILE__,__LINE__, inAirportID);
		if(errorNumber==0)
		  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		       {logMsg(logFile,"%s Line %d, Out of Memory in getFlightTime().\n", __FILE__,__LINE__);
		        writeWarningData(myconn); exit(1);
			   }
		  }
	    else
		  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getFlightTime().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
	          }
	      }	    
		initializeWarningInfo(&errorinfoList[errorNumber]);
	    errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
        strcpy(errorinfoList[errorNumber].group_name,"group_airport");
	    errorinfoList[errorNumber].airportid=inAirportID;
	    sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
	    errorinfoList[errorNumber].format_number=21;
        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
        errorNumber++;
		writeWarningData(myconn); exit(1);
	}

	aircraftCd = -1;
	//for(x = 0; x < 5; ++x) {
	for(x = 0; x < 6; ++x) { // CX - 12/04/2009 ANG
		if(aircraftTypeID == xref[x].aircraftType)
			aircraftCd = xref[x].aircraftCd;
		    //break;
	}

	if(aircraftCd == -1) {
		logMsg(logFile,"%s Line %d, aircraftTypeID == %d?\n", __FILE__,__LINE__, aircraftTypeID);
		writeWarningData(myconn); exit(1);
	}

    if (numPax > paxXref[aircraftCd].maxPax){
        numPax = paxXref[aircraftCd].maxPax;
		logMsg(logFile,"Exceed the seat capacity, out: %d, in: %d \n", outAirportID,inAirportID);
	}

	fcoPtr = doFlightCalc(orig_all->lat, orig_all->lon, dst_all->lat, dst_all->lon, month, aircraftCd, numPax, &fcoBuf);
	if(fcoPtr) {
		*flightTm = fcoPtr->adjFlightDuration;
		*elapsedTm = fcoPtr->totalElapsedTime;
		*blockTm = fcoPtr->blockTime;
		//RLZ 08/01/2008 Artificial fix
		if ( fcoPtr->adjFlightDuration < 0 || fcoPtr->totalElapsedTime < 0 || fcoPtr->blockTime < 0){
			*flightTm = 480;
			*elapsedTm = 480;
		    *blockTm = 480;	
		}
		*numStops = fcoPtr->nbrFuelStops;
		return 0;
	}
	else
		return(-1);
}


/****************************************************************************************************
*	Function	populateAcSchedLegIndList							Date last modified:  03/06/08 ANG
*	Purpose:	Populate schedLegIndList for all aircraft 
*   Purpose:    Data for locking the crew assignment                                     06/06/08 RLZ
****************************************************************************************************/
//static void
int
populateAcSchedLegIndList(void)
{
	int k, y, lg, x, p, cp, found, tempCrPr, countCrPr;
	int *acPtr;
	CrewPair *cpPtr;
	//McpAircraft *mcpAircraftList, *mcpPtr;
	extern time_t endOfToday;
	extern int numCrewPairs;

	cpPtr = (CrewPair *) calloc((size_t) 1, (size_t) sizeof(CrewPair));
	if(! cpPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in populateAcSchedLegIndList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	//mcpAircraftList = (McpAircraft *) calloc(numAircraft, sizeof(McpAircraft));
	//if(! mcpAircraftList) {
	//	logMsg(logFile,"Out of Memory in populateAcSchedLegIndList().\n");
	//	writeWarningData(myconn); exit(1);
	//}

	//mcpPtr = (McpAircraft *) calloc((size_t) 1, (size_t) sizeof(McpAircraft));
	//if(! mcpPtr) {
	//	logMsg(logFile,"%s Line %d, Out of Memory in populateAcSchedLegIndList().\n", __FILE__,__LINE__);
	//	writeWarningData(myconn); exit(1);
	//}

	//initialize acList remaining structure
	for(x = 0; x < numAircraft; x++){
	//	mcpAircraftList[x].numCrPairs = 0;
		acList[x].numCrPairs = 0;
		for(y = 0; y < MAX_CRPR_PER_AC; y++){
	//		mcpAircraftList[x].cprInd[y] = -1;
			acList[x].cprInd[y] = -1;
		}
	}

	//choose only aircraft having 2 or more crewPairs
	//first, count crewpairs for each aircraft
	cpPtr = crewPairList;
	
	while(cpPtr->crewPairID) {
		//if(cpPtr->pairStartTm >= optParam.windowEnd || cpPtr->pairEndTm <= optParam.windowStart)
		//if(cpPtr->pairEndTm <= optParam.windowStart || cpPtr->pairStartTm >= DateTimeToTime_t(dt_addToDateTime(Hours, 14, dt_time_tToDateTime(optParam.windowStart))))
		//if(cpPtr->availDT < DateTimeToTime_t(dt_addToDateTime(Hours, 14, dt_time_tToDateTime(optParam.windowStart)))){
		//if(cpPtr->pairStartTm <= endOfToday && cpPtr->pairEndTm >= optParam.windowStart){
		if(cpPtr->pairStartTm <= optParam.windowEnd && cpPtr->pairEndTm >= optParam.windowStart){ //Retain more exg sol - 08/11/08 ANG
			acPtr = cpPtr->aircraftID;
			while(*acPtr) {
				//fprintf(logFile," %d", *acPtr);
				for(p = 0; p<numAircraft; p++){
					if(acList[p].aircraftID == *acPtr){
						//if (mcpAircraftList[p].numCrPairs >= MAX_CRPR_PER_AC)
						if (acList[p].numCrPairs >= MAX_CRPR_PER_AC)
							break;
						//else if (mcpAircraftList[p].numCrPairs == 0){
						//	mcpAircraftList[p].numCrPairs++;
						//	mcpAircraftList[p].cprInd[mcpAircraftList[p].numCrPairs-1] = cpPtr->crewPairID;
						else if (acList[p].numCrPairs == 0){
							acList[p].numCrPairs++;
							acList[p].cprInd[acList[p].numCrPairs-1] = cpPtr->crewPairID;
						}
						else {
							found = 0;
							//for(x = 0; x < mcpAircraftList[p].numCrPairs; x++){
							//	if(mcpAircraftList[p].cprInd[x] == cpPtr->crewPairID)
							for(x = 0; x < acList[p].numCrPairs; x++){
								if(acList[p].cprInd[x] == cpPtr->crewPairID)
									found = 1;
							}
							if(found == 0){
								//mcpAircraftList[p].numCrPairs++;
								//mcpAircraftList[p].cprInd[mcpAircraftList[p].numCrPairs-1] = cpPtr->crewPairID;
								acList[p].numCrPairs++;
								acList[p].cprInd[acList[p].numCrPairs-1] = cpPtr->crewPairID;
							}
						}
						break;
					}
				}
				++acPtr;
			}
		}
		++cpPtr;
	}

	//print mcpAircraftList
	fprintf(logFile,"Multi-crew Aircraft:\n");
	fprintf(logFile,"+---------+---------+------------+---------------+\n");
		for(x = 0; x < numAircraft; ++x) {
			if (acList[x].numCrPairs >= 2){
				fprintf(logFile,"AcInd %d (ID %d) has %d crewPairIDs : ",
				x, acList[x].aircraftID, acList[x].numCrPairs);
				for(y = 0; y < acList[x].numCrPairs; y++) {
					fprintf(logFile," %d", acList[x].cprInd[y]);
				}
				fprintf(logFile,"\n");
				}
		}
	fprintf(logFile,"+---------+---------+------------+---------------+\n\n");
	fflush(logFile);


	//populate schedLegIndList (only for those aircraft with multi-crewpairs)
	for(k = 0; k<numAircraft; ++k){
		//if(acList[k].numCrPairs >= 2){
		if(acList[k].numCrPairs >= 1){ //Retain more exg sol - 08/11/08 ANG
			acList[k].schedLegIndList[0] = -1;
			y = 0;
			for(lg = 0; lg < numLegs; lg++){
				if(legList[lg].aircraftID > 0 && legList[lg].aircraftID == acList[k].aircraftID){
					if(legList[lg].inLockedTour == 1){
						fprintf(logFile, "For aircraft %d, legList[%d].inLockedTour = 1, so break...\n", acList[k].aircraftID, lg);
						break; //continue; //Don't want to deal with locked aircraft now.  
					}
					if(legList[lg].crewPairInd == -1){
						fprintf(logFile, "For aircraft %d, legList[%d].crewPairInd = -1, so break...\n", acList[k].aircraftID, lg);
						break; //continue; //Don't want to deal with leg without crewPairInd.  
					}
					if(legList[lg].acInd == -1){
						fprintf(logFile, "For aircraftID %d, legList[%d].acInd = -1, so break...\n", acList[k].aircraftID, lg);
						break; //continue; //Don't want to deal with missing acInd.
					}
					//if(legList[lg].schedOut > endOfToday){ // if the leg is for the next day, break; we only consider enter new column for today
					if(legList[lg].schedOut > optParam.windowEnd){//To include all legs within planning window - 04/29/08 ANG
						break;
					}
					//fprintf(logFile, "Adding demandID %d for aircraft %d as schedLegIndList[%d]\n", legList[lg].demandID, acList[k].aircraftID, y);
					acList[k].schedLegIndList[y] = lg;
					y++;
				}//end if
			}//end for lg
			//put -1 in array after last scheduled leg index to mark end of list
			acList[k].schedLegIndList[y] = -1;
		}//end if
		//else if(acList[k].numCrPairs == 1)
		//	fprintf(logFile, "schedLegIndList for AircraftID %d is not populated.\n", acList[k].aircraftID);
		//else
		//	fprintf(logFile, "No crewPair found for AircraftID %d.\n", acList[k].aircraftID);
	}//end for k	

	//print schedLegIndList for multi-crewpair aircraft
	fprintf(logFile,"Aircraft list with Legs:\n");
	fprintf(logFile,"+---------+---------+------------+---------------+\n");
	for(k = 0; k<numAircraft; ++k){
		//if(acList[k].schedLegIndList[1] > -1 && mcpAircraftList[k].numCrPairs >= 2 && mcpAircraftList[k].cprInd[1] > -1){
		if(acList[k].schedLegIndList[1] > -1 && acList[k].numCrPairs >= 2 && acList[k].cprInd[1] > -1){
			fprintf(logFile,"Aircraft %d has following leg indices: ", acList[k].aircraftID);
			x = 0;
			while (acList[k].schedLegIndList[x] > -1){
				fprintf(logFile," %d (ID %d)  ", acList[k].schedLegIndList[x], legList[acList[k].schedLegIndList[x]].demandID);
				x++;
				//countAddlExgTours++;
			}
			fprintf(logFile,"\n");
		}
	}
	fprintf(logFile,"+---------+---------+------------+---------------+\n\n");

	//populate schedCrPrIndList (only for those aircraft with multi-crewpairs)
	//populate countCrPrToday
	for(k = 0; k<numAircraft; ++k){
		//if(acList[k].schedLegIndList[1] > -1 && acList[k].numCrPairs >= 2 && acList[k].cprInd[1] > -1){
		if(acList[k].schedLegIndList[0] > -1 && acList[k].numCrPairs >= 2 && acList[k].cprInd[1] > -1){ //Retain more exg sol - 08/11/08 ANG
			x = 0; countCrPr = 0;
			while (acList[k].schedLegIndList[x] > -1){
				tempCrPr = -1;
				for(cp = 0; cp < numCrewPairs; cp++){
					for(lg = 0; lg < MAX_LEGS; lg++){
						if(crewPairList[cp].schedLegIndList[lg] == acList[k].schedLegIndList[x]){
							tempCrPr = cp;
							break;
						}
					}
					if (tempCrPr > -1)
						break;
				}
				if (tempCrPr > -1){
					acList[k].schedCrPrIndList[x] = tempCrPr;
					if(x == 0) 
						countCrPr = 1;
					if(x > 0 && tempCrPr != acList[k].schedCrPrIndList[x-1])
						countCrPr++;
				}
				x++;
			}
			acList[k].countCrPrToday = countCrPr;
		}
	}

	//print schedCrPrIndList for multi-crewpair aircraft
	fprintf(logFile,"Aircraft list with CrewPairs:\n");
	fprintf(logFile,"+---------+---------+------------+---------------+\n");
	for(k = 0; k<numAircraft; ++k){
		//if(acList[k].countCrPrToday > 1){
		if(acList[k].countCrPrToday >= 1){ //Retain more exg sol - 08/11/08 ANG
			fprintf(logFile,"Aircraft %d has following crewpair indices: ", acList[k].aircraftID);
			x = 0;
			while (acList[k].schedLegIndList[x] > -1){
				fprintf(logFile," %d (ID %d)  ", acList[k].schedCrPrIndList[x], crewPairList[acList[k].schedCrPrIndList[x]].crewPairID);
				x++;
				//countAddlExgTours++;
			}
			fprintf(logFile,"\n");
		}
	}
	fprintf(logFile,"+---------+---------+------------+---------------+\n\n");
	fflush(logFile);

	///RLZ: Do not reassign the existing crew pair, search multiCrew for other related changes
	for(cp = 0; cp<numCrewPairs; cp++){
		if (optParam.exgCPACLock && crewPairList[cp].optAircraftID <= 0) {
				if (crewPairList[cp].aircraftID[0] > 0){
					acPtr = crewPairList[cp].aircraftID;
					acPtr++;
					if (*acPtr <= 0){  //only one associated ac for the pair
						crewPairList[cp].optAircraftID = crewPairList[cp].aircraftID[0];
						for(p = 0; p<numAircraft; p++){
							if(acList[p].aircraftID == crewPairList[cp].aircraftID[0]){
								if (acList[p].availDT >= optParam.windowEnd) {
									crewPairList[cp].optAircraftID = 0; //undo the preview assignment if ac not avail in pw. RLZ 09/24							
									break;
								}
								crewPairList[cp].acInd = p;
								if (acList[p].firstCrPrInd < 0){
									if ((crewPairList[cp].availDT - max(optParam.windowStart, acList[p].availDT) <= 86400) 
										&& crewPairList[cp].startDay != PAST_WINDOW){
										acList[p].firstCrPrID = crewPairList[cp].crewPairID;
										acList[p].firstCrPrInd = cp;  //crew Pair index must be repopulated after crew pairing
										fprintf(logFile, "aircraftID %d, first crew pairID %d \n", acList[p].aircraftID, crewPairList[cp].crewPairID); 
									}
									else{
										crewPairList[cp].optAircraftID = 0;
										crewPairList[cp].acInd = -1;
									}
						 		}
								else {
									if (crewPairList[acList[p].firstCrPrInd].endRegDay <= crewPairList[cp].startDay){
										acList[p].multiCrew = 1;
										multiCrewAC++;
										fprintf(logFile, "aircraftID %d, second crew pairID %d \n", acList[p].aircraftID, crewPairList[cp].crewPairID); 
									}
									else{ //If not the crew pair after the first crew pair
										crewPairList[cp].optAircraftID = 0;
										crewPairList[cp].acInd = -1;
									}
								}	
								break;
							}
						}
					}
				}
			}
	}



	return (0);

}//end function




/****************************************************************************************************
*	Function	populateCrewPairCountAcID							Date last modified:  03/06/08 ANG
*	Purpose:	Populate countAircraftID for all crewpair
****************************************************************************************************/
//static void
int
populateCrewPairCountAcID(void)
{
	int x, cp, tempCount, tempAcID;
	CrewPair *cpPtr;
//	char tbuf1[32], tbuf2[32];
//	int errNbr1, errNbr2;
	extern int numCrewPairs;
	//extern CrewPair *crewPairList;

	cpPtr = (CrewPair *) calloc((size_t) 1, (size_t) sizeof(CrewPair));
	if(! cpPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in populateAcSchedLegIndList().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(cp = 0; cp < numCrewPairs; cp++){
		x = 0;
		tempCount = 0;
		tempAcID = -1;
		while(crewPairList[cp].schedLegACIndList[x] > -1){
			if(crewPairList[cp].schedLegACIndList[x] != tempAcID){
				tempCount++;
				tempAcID = crewPairList[cp].schedLegACIndList[x];
			}
			x++;
		}
		crewPairList[cp].countAircraftID = tempCount;
	}

	/*fprintf(logFile,"\n\n\n\nCount AcID in Crew Pair List:\n");
	fprintf(logFile,"+------+--------+--------+------------------+------------------+------+\n");
	fprintf(logFile,"| crew | cap    | flight | pair             | pair             | tot  |\n");
	fprintf(logFile,"| pair | tain   | off    | start            | end              | a/c  |\n");
	fprintf(logFile,"| id   | id     | id     | time             | time             | IDs  |\n");
	fprintf(logFile,"+------+--------+--------+------------------+------------------+------+\n");
	for(cp = 0; cp < numCrewPairs; cp++){
		fprintf(logFile,"| %4d | %6d | %6d | %s | %s | %4d |",
			crewPairList[cp].crewPairID,
			crewPairList[cp].captainID,
			crewPairList[cp].flightOffID,
		    	dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
				asctime(gmtime(&(crewPairList[cp].pairStartTm))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		    	dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
				asctime(gmtime(&(crewPairList[cp].pairEndTm))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"),
			crewPairList[cp].countAircraftID);
		fprintf(logFile,"\n");
	}
	fprintf(logFile,"+------+--------+--------+------------------+------------------+------+\n");
	fflush(logFile);*/
	return (0);

}//end function

/********************************************************************************************************
*	Function   insertExcludedDemandList								Date last modified:  10/13/09 ANG	*
*	Purpose:	Propoerly insert excluded demand list for aircraft										*
********************************************************************************************************/	
static insertExcludedDemandList(int i, int j, int day)
{
	//for acList[j], prepare to insert demandInd = k to acList[j].exclDemandInd[acList[j].lastExcl[day]]
	int d, exclIndToStore, lastExclInd;
	int x, y, z, dup, temp, temp2, storeInd;

	x = 0; y = 0; 
	temp = temp2 = i;
	dup = 0;

	while(x <= MAX_LEG_EXCL && temp > -1){
		if(acList[j].exclDemandInd[x] == temp){
			dup = 1;
			break;
		}
		else if (acList[j].exclDemandInd[x] > temp){
			temp2 = acList[j].exclDemandInd[x];
			acList[j].exclDemandInd[x] = temp;
			temp = temp2;
		} 
		else if (acList[j].exclDemandInd[x] == -1 && temp > -1){
			temp2 = acList[j].exclDemandInd[x];
			acList[j].exclDemandInd[x] = temp;
			temp = temp2;
		}
		x++;
	}

	if (dup == 0){
		for(z=day; z<optParam.planningWindowDuration; z++)
			acList[j].lastExcl[z]++;
	}
		
	return 0;
}