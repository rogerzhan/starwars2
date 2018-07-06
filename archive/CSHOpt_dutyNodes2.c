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
#include "CSHOpt_dutyNodes.h"
#include "memory.h"
#include "CSHOpt_output.h"


extern FILE *logFile;
extern Crew *crewList;
extern CrewPair *crewPairList;
extern int numCrew;
extern int numOptCrewPairs;
extern int month, lastTripOfDay[MAX_WINDOW_DURATION];
extern struct optParameters optParam;
extern Demand *demandList;
extern AircraftType *acTypeList;
extern Aircraft *acList;
extern Leg *legList;
extern int numAcTypes;
extern int numDemand;
extern int numOptDemand;
extern int firstMidnight;
extern int lastTripTomorrow;
extern time_t firstEndOfDay;
extern int **aptCurf; //stores airport curfews (first (row) index is airport, first column is number of curfews, remaining cols are start and end of curfews
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;


int potCrewStarts[MAX_AC_TYPES][MAX_WINDOW_DURATION];
int potCrewEnds[MAX_AC_TYPES][MAX_WINDOW_DURATION];
Duty **dutyList; //store duties in arrays - one for each aircraftType.  Use array indices to keep track of duties by day and duty type.
//tabulate number of duties by aircraftType, day, and duty type (1-trip, 1-trip+repo, 2-trip, etc)
struct listMarker dutyTally[MAX_AC_TYPES][MAX_WINDOW_DURATION][9];
/*  There are potentially 9 duty types as follows:
	m=0: one-trip, m=1: two-trips, m=2: three-trips, m=3: four trips,
	m=4: one trip + final repo, m=5: two trips + final repo
	m=6:  three trips + final repo, m=7:  four trips + final repo
	m= 8: no trips + final repo*/
int maxTripsPerDuty = 4;
static int early[4];
static int late[4];
static int inter[4];

int netMaxDutyTm;
int netMaxDutyTmEarly;
int *dutyCountsPerAcType;
int numDutiesByType[10]; //temporary

static int tabulateCrewStartAndEndDays(void);
static int createOneTripNodes(int day);
static int createMultiTripNodes(int day, int m);
static int createFinalRepoNodes(int day);
static int createRepoOnlyNodes(int day);
static int checkTripTimeFeasibility (Duty *exgDuty, int j, Demand *lastTrip, int lastInd, int intTrTm, int *startTm, int m, int *crewStartTm, int *crewEndTm);
static int checkReposAgainstCurfews (Duty *exgDuty, int j, Demand *lastTrip, int intTrTm, int *startTm, int m, int *crewStartTm, int *crewEndTm);
static int checkDutyTimeFeasibility (Duty *duty);
static void dutyAllocInit(int acTypeCount);
static Duty *dutyAlloc(int acTypeIdx);

static int
initializeDuty (Duty *duty)
{
	int i;

	duty->acInd = -1;
	duty->actualCost = 0;
	duty->aircraftID = 0;
	duty->aircraftTypeID = 0;
	duty->arcList = NULL;
	duty->arcTallyByDay = NULL;
	duty->blockTm = 0;
	duty->changePenalty = 0;
	duty->cost = 0;
	duty->tempCostForMac = 0; //MAC - 09/23/08 ANG

	duty->countsPerArcList = NULL;
	duty->crewPairID = 0;
	for (i=0; i<4; i++)
	{
		duty->demandInd[i] = -1;
		duty->redPenaltyList[i] = 0;
		duty->redPenCrewPairList[i] = 0;
		duty->redPenACList[i] = 0;
		duty->startTm[i] = 0;
	}
	for (i=0; i<3; i++)
	{
		duty->intTrTm[i] = INFINITY;
		duty->repoFltTm[i] = 0;//MAC - 09/24/08 ANG
		duty->repoStop[i] = 0;//MAC - 09/24/08 ANG
	}
	duty->earlyLatePen = 0;
	duty->endTm = 0;
	duty->lastDemInd = -1;
	duty->repoDemandInd = -1;
	duty->repoFromAptID = 0; //MAC - 09/19/08 ANG
	duty->macRepoFltTm = 0; //MAC - 09/24/08 ANG
	duty->macRepoStop = 0; //MAC - 09/24/08 ANG
	duty->sumDuals = 0;
	duty->unreachableFlag = NULL;
	duty->isPureAppoint = 0;

	return (0);
}


/********************************************************************************************************************	
*	Function	tabulateCrewStartAndEndDays									Date last modified:  10/29/06 SWO		
*	Purpose:	For each fleet, find start and end days (in terms of planning window) for crew pairs, and the 		
*				  start and end of the regular (non-overtime) tours for crew members.  For crews not locked to a	
*				  plane, tabulate the days on which a crew could pickup up a plane left by another crew																			
*				  and start flying, and the days on which a crew could potentially end its tour (based on
*				  optParam.prohibitStealingPlanes). We do this AFTER crew members have been paired.	Also
*				  populate the field which indicates if we must calculate overtime for a pilot		
********************************************************************************************************************/
static int tabulateCrewStartAndEndDays(void)
{
	int cp, day, c, crewInd, windowEnd, i;

	windowEnd = optParam.planningWindowDuration - 1;

	// Initialize potCrewStarts and potCrewEnds
	for (i=0; i<MAX_AC_TYPES; i++)
	{
		for (day=0; day<MAX_WINDOW_DURATION; day++)
		{
			potCrewStarts[i][day] = 0;
			potCrewEnds[i][day] = 0;
		}
	}

	//for each crew pair considered in optimization....
	for(cp = 0; cp < numOptCrewPairs; cp++){	
		//find the start and end days of the tour (in terms of the planning window days)

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

		//Update potential crew starts table for aircraft type IF crewPair is not already locked to a specific plane. 
		//The crew could pickup a plane and start flying anytime between startDay and max(endDay, end of window).
		if(crewPairList[cp].optAircraftID == 0){
			for(day = crewPairList[cp].startDay; day <= (crewPairList[cp].endDay < windowEnd? crewPairList[cp].endDay : windowEnd); day++)
				potCrewStarts[crewPairList[cp].acTypeIndex][day]= 1;
		}

		//For each crew member, find start and end day of regular tour (prior to overtime, in terms of days of the planning window).
		//add a few hours to tour start time so that if tour start is an hour before day end time, we don't consider as a day
		//Also populate overtimeMatters field
		for(c = 0; c<2; c++){
			crewInd = crewPairList[cp].crewListInd[c];

			crewList[crewInd].startRegDay = PAST_WINDOW;
			for(day = 0; day <= windowEnd; day++){
				if((crewList[crewInd].tourStartTm + 4*3600 < firstEndOfDay + day*24*3600) &&
					(crewList[crewInd].availDT < firstEndOfDay + day*24*3600)){
					crewList[crewInd].startRegDay = day;
					break;
				}
			}
			crewList[crewInd].endRegDay = PAST_WINDOW;
			for(day = 0; day <=windowEnd; day++){
				if(crewList[crewInd].tourEndTm < firstEndOfDay + day*24*3600){
					crewList[crewInd].endRegDay = day;
					break;
				}
			}

			//if crew has already started this tour, or if crew did not volunteer for early overtime, or if we are already into their regular tour, then
			//overtime need not be calculated
			if(crewList[crewInd].activityCode < 2 || crewList[crewInd].startEarly == 0 || crewList[crewInd].availDT >= crewList[crewInd].tourStartTm)
				crewList[crewInd].overtimeMatters = 0;
			else
				crewList[crewInd].overtimeMatters = 1;
		}
		//find the earlier endRegDay of the two crew members and populate for crewPair
		if(crewList[crewPairList[cp].crewListInd[0]].endRegDay < crewList[crewPairList[cp].crewListInd[1]].endRegDay)
				crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[0]].endRegDay;
		else
			crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[1]].endRegDay;

		//Update potential crew ends table for aircraft type if optParam.prohibitStealingPlanes == 1.
		//If optParam.prohibitStealingPlanes == 1, the crewPair could potentially end their tour from the last regular day 
		//of a member's tour up until the endDay (which includes overtime)
		if(optParam.prohibitStealingPlanes == 1){
			for(day = crewPairList[cp].endRegDay; day <=(crewPairList[cp].endDay < windowEnd? crewPairList[cp].endDay : windowEnd); day++)
				potCrewEnds[crewPairList[cp].acTypeIndex][day] = 1;
		}
		//Else if optParam.prohibitStealingPlanes == 0, the crewPair could potentially end their tour any day up until the endDay (which includes overtime)
		else{
			for(day = 0; day<=(crewPairList[cp].endDay < windowEnd? crewPairList[cp].endDay : windowEnd); day++)
				potCrewEnds[crewPairList[cp].acTypeIndex][day] = 1;
		}
	}
	//If optParam.prohibitStealingPlanes = TRUE...   
	//	A crewPair can pick up another crew's plane at the end of a duty (start/end of a trip) on endReg day (end of the regular, non-overtime tour) 
	//		of another crew in the same fleet, or on any following day. We tabulate endDays and endRegDays of crew members 
	//		in same fleet that are being considered in the optimization. Then, we check if there is an available crew that is not yet locked to a plane that day; 
	//		if not, no crew starts flying that day. These checks are used to expedite duty node AND arc generation.
	//If optParam.prohibitStealingPlanes = FALSE...
	//	For pickup at end of a duty (start/end of trip):  Just check if there is an available crew that is not yet locked to a plane that day; 
	//		if not, no crew starts flying that day.  This check is used to expedite duty node AND arc generation.

	return 0;
}


/************************************************************************************************
*	Function   createDutyNodes								Date last modified:  7/17/06 SWO	*
*	Purpose:	Enumerate all feasible duties.  Each duty is a series of 0 to no more than 4	*
*			(maxTripsPerDuty <=4) demand legs that can be covered in a single day by a plane, 	*
*			plus sometimes a final repositioning move.  Store these duties in arrays, one		*
*			array for each aircraft type.														*
*			Use array indices to keep track of duties by day and duty type.						*																			*
************************************************************************************************/
int createDutyNodes(void)
{
	int day, j, m, i, x, infeasible, k, check, numInfeasible=0;
	
	logMemoryStatus(); 
	
	//Following function must be done before creating duty nodes to expedite their generation
	tabulateCrewStartAndEndDays();
	
	//allocate memory for dutyList.  will allocate memory for duty structures as needed below
	dutyList = (Duty **) calloc(numAcTypes, sizeof(Duty *));
	if(! dutyList) {
		logMsg(logFile,"%s Line %d, Out of Memory in createDutyNodes().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numAcTypes; i++)
	{
		dutyList[i] = NULL;
	}

	dutyAllocInit(numAcTypes);

	dutyCountsPerAcType = (int *) calloc(numAcTypes, sizeof(int));

	for (i=0; i<numAcTypes; i++)
	{
		dutyCountsPerAcType[i] = 0;
	}

	netMaxDutyTm = optParam.maxDutyTm - optParam.preFlightTm - optParam.postFlightTm;
	netMaxDutyTmEarly = optParam.shortDutyTm - optParam.preFlightTm - optParam.postFlightTm/2;
	//Initialize dutyTally arrays
	for (i=0; i<MAX_AC_TYPES; i++){
		for (j=0; j<MAX_WINDOW_DURATION; j++){
			for (x=0; x<9; x++){
				dutyTally[i][j][x].startInd = -1;
				dutyTally[i][j][x].endInd = -1;
			}
		}
	}

	for(day = 0; day < optParam.planningWindowDuration; day++) 
	{	
		createOneTripNodes(day);
		//create Duty Nodes with more than one trip 
		for(m=1; m<maxTripsPerDuty; m++)
			createMultiTripNodes(day, m);
		if(day < (optParam.planningWindowDuration-1)) //if not last day in planning window, look at repositioning for next day
		{
			createFinalRepoNodes(day);
			createRepoOnlyNodes(day);
		}
	}
	//for each day of planning window as required, initialize duty type 8 dutyTally indices for simplicity in CHSOpt_arcs loops
	//(we don't generate final repo duty types 4 thru 8 for the last day)
	for(j= 0; j< numAcTypes; j++)
	{
		dutyTally[j][optParam.planningWindowDuration-1][8].startInd = dutyTally[j][optParam.planningWindowDuration-1][maxTripsPerDuty - 1].endInd+1;
		dutyTally[j][optParam.planningWindowDuration-1][8].endInd = dutyTally[j][optParam.planningWindowDuration-1][maxTripsPerDuty - 1].endInd;
	}
	//sum up total duty nodes generated for each duty type and in total
	numDutiesByType[9] = 0;
	for(m = 0; m <= 8; m++)
	{
		numDutiesByType[m] = 0;
		for(j = 0; j<numAcTypes; j++)
			for(day = 0; day < optParam.planningWindowDuration; day++)
				if(dutyTally[j][day][m].endInd > 0)
					numDutiesByType[m]+= (dutyTally[j][day][m].endInd - dutyTally[j][day][m].startInd + 1);
		numDutiesByType[9] += numDutiesByType[m];
	}
	//sum up infeasible trips
	infeasible = 0;
	for(i=0; i<numOptDemand; i++)
	{
		if(demandList[i].feasible == 0)
			infeasible++;
	}


	// -----------DEBUGGING ONLY - CHECKING DUTY FEASIBILITY--------------- //
	for(day = 0; day < optParam.planningWindowDuration; day++)
	{
		for(j = 0; j< numAcTypes; j++)
		{
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++)
			{
				if ((check = checkDutyTimeFeasibility (&dutyList[j][k])) < 0)
				{
					logMsg (logFile, "Error %d\n", check);
					numInfeasible ++;
				}
			}
		}
	}	
	fprintf (logFile, "\n\nNum infeasible duties: %d\n\n", numInfeasible);

	return 0;
}


/********************************************************************************
*	Function   createOneTripNodes             Date last modified:  02/12/07 SWO	*
*	Purpose:  																	*
********************************************************************************/
static int createOneTripNodes(int day)
{
	int i, j, firstdemandInd, firstIncl;
	Duty *newDuty;
	int flexStart, flexEnd, flexInc, intReqOut;
	int apptStartTm, overnightTm, firstCrPrInd, postFlightTm; //RLZ 04/29/2008
	double lateCostPH, earlyCostPH;

	//initialize dutyTally matrix
	if(day ==0)
		for(j=0; j<numAcTypes; j++)
		{
			dutyTally[j][0][0].startInd = 0;
			dutyTally[j][0][0].endInd = -1;
		}
	else
		for(j=0; j<numAcTypes; j++)
		{
				dutyTally[j][day][0].startInd = dutyTally[j][day-1][8].endInd + 1;
				dutyTally[j][day][0].endInd = dutyTally[j][day-1][8].endInd; // ==startInd - 1
		}
	
	if(day ==0)
		firstdemandInd = 0;
	else
		firstdemandInd = lastTripOfDay[day-1]+ 1;
	
	for(i = firstdemandInd; i<=lastTripOfDay[day]; i++)
	{
		//if trip is tied to an earlier trip (demPredID > 0) then it can not be the first trip in a duty node
		if(demandList[i].predDemID > 0)
			continue;
		//if not appointment or locked (not tied to plane)
		if(demandList[i].acInd == -1)
		{	//for each aircraftType
			for(j = 0; j< numAcTypes;j++)
			{	//check if infeasible due to sequence position or fleet exclusion
				if(demandList[i].blockTm[j] ==INFINITY)
					continue;
				//check max flight and duty time.
				if(demandList[i].blockTm[j] < optParam.maxFlightTm && demandList[i].elapsedTm[j] < netMaxDutyTm)
				//create a Duty Node
				{	
					intReqOut = (int)(demandList[i].reqOut)/60;
					
					flexStart = -(int)((intReqOut - max(demandList[i].early[j], (int)optParam.windowStart/60))/optParam.dutyNodeAdjInterval);
					flexEnd = (int)((demandList[i].late[j] - intReqOut)/optParam.dutyNodeAdjInterval);

					//flexStart = flexEnd = 0;


					for (flexInc = flexStart; flexInc <= flexEnd; flexInc++){ 
						//allocate memory for each duty and increment dutyTally
						newDuty = dutyAlloc(j);
						initializeDuty (newDuty);
						dutyTally[j][day][0].endInd ++;
						
						newDuty->startTm[0] = (int)(demandList[i].reqOut)/60 + optParam.dutyNodeAdjInterval*flexInc; 
						newDuty->blockTm = demandList[i].blockTm[j]; 
						newDuty->endTm = newDuty->startTm[0] + demandList[i].elapsedTm[j]; //already includes taxi time
						newDuty->crewStartTm = newDuty->startTm[0];
						newDuty->crewEndTm = newDuty->endTm;
						newDuty->lastDemInd = i;
						newDuty->demandInd[0]= i;
						newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
						newDuty->actualCost=demandList[i].cost[j];
						newDuty->cost = newDuty->actualCost;
						newDuty->changePenalty = demandList[i].changePenalty;

						if(demandList[i].changePenalty != 0)
						{
							newDuty->redPenCrewPairList[0] = demandList[i].redPenCrewPairID;
							newDuty->redPenACList[0] = demandList[i].redPenACID;
							newDuty->redPenaltyList[0] = 0;
						}
						//Adjust penalty for flex
						lateCostPH = optParam.lateCostPH;
						earlyCostPH = optParam.earlyCostPH;
						if (demandList[newDuty->demandInd[0]].recoveryFlag){
							lateCostPH = max(lateCostPH, optParam.lateCostPH_recovery);
							earlyCostPH = max(earlyCostPH, optParam.lateCostPH_recovery);
						}
						if(flexInc < 0)
							newDuty->earlyLatePen = lateCostPH * (-flexInc)*optParam.dutyNodeAdjInterval/60;
						else 
							newDuty->earlyLatePen = earlyCostPH * flexInc*optParam.dutyNodeAdjInterval/60;
						newDuty->cost = newDuty->actualCost + newDuty->earlyLatePen;
						if(optParam.withMac == 1){
							newDuty->tempCostForMac = newDuty->earlyLatePen; //MAC - 09/23/08 ANG
						}

					} //end for loop: flexInc				
				}
			}
		}
		//if locked or maintenance/appointment, create a duty IF this is the first required leg (first inclusion) for the plane that day
		//(assume locked leg is feasible)
		else 
		{		
			firstIncl = (day == 0 ? 0 : acList[demandList[i].acInd].lastIncl[day-1]+1);
			if(acList[demandList[i].acInd].inclDemandInd[firstIncl] != i)
				continue;
			//find acTypeIndex for aircraft to which we are locked
			j = acList[demandList[i].acInd].acTypeIndex;			
			//allocate memory for each duty and increment dutyTally
			newDuty = dutyAlloc(j);
			initializeDuty(newDuty);
			dutyTally[j][day][0].endInd ++;
			newDuty->startTm[0] = (int)(demandList[i].reqOut)/60;
		//	if (demandList[i].isAppoint && demandList[i].aircraftID == 485 )
		//		newDuty->startTm[0] = (int)(demandList[i].reqOut)/60 - optParam.dutyNodeAdjInterval*6;

			// if maintenance leg or airport appt
			if(demandList[i].isAppoint){	
				newDuty->blockTm= 0; 
				newDuty->endTm = (int)(demandList[i].reqIn)/60;
				newDuty->crewEndTm = newDuty->startTm[0];  //WHY?
				newDuty->crewStartTm = newDuty->endTm;
				//newDuty->crewEndTm = newDuty->endTm;
				//newDuty->crewStartTm = newDuty->endTm;
				newDuty->isPureAppoint = 1;
			}
			else{ //if locked leg
				newDuty->blockTm= demandList[i].blockTm[j]; 
				newDuty->endTm = newDuty->startTm[0] + demandList[i].elapsedTm[j]; //already includes taxi time
				newDuty->crewStartTm = newDuty->startTm[0];
				newDuty->crewEndTm = newDuty->endTm;
			}
			//Finish Creating Node
			newDuty->lastDemInd = i;
			newDuty->demandInd[0]= i;
			newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
			newDuty->aircraftID=demandList[i].aircraftID;
			newDuty->acInd = demandList[i].acInd;
			newDuty->crewPairID = demandList[i].crewPairID;
			newDuty->actualCost=demandList[i].cost[j];
			newDuty->cost = newDuty->actualCost;

			newDuty->changePenalty = demandList[i].changePenalty;
			if(demandList[i].changePenalty != 0)
			{
				newDuty->redPenCrewPairList[0] = demandList[i].redPenCrewPairID;
				newDuty->redPenACList[0] = demandList[i].redPenACID;
				newDuty->redPenaltyList[0] = 0;
			}


			//Create multiple copies of pure appointment
			//first for overnight rest
			//second for output convienience 
			if (demandList[i].isAppoint){
				if (day+1 >= optParam.planningWindowDuration)
					continue;

				overnightTm = optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm;
				apptStartTm = (int) (demandList[lastTripOfDay[day]+1].reqOut)/60 -  overnightTm - 1;
	
				if ((int)(demandList[i].reqOut)/60  > apptStartTm && apptStartTm >= (int)(optParam.windowStart)/60 ){ //enough rest to do first trip         
				
					newDuty = dutyAlloc(j);
					initializeDuty (newDuty);
					dutyTally[j][day][0].endInd ++;
					
					newDuty->startTm[0] = apptStartTm;
					newDuty->blockTm = 0; 
					newDuty->endTm = (int)(demandList[i].reqIn)/60;
					newDuty->crewEndTm = newDuty->startTm[0]; 
					newDuty->crewStartTm = newDuty->endTm ; //CHECK
					newDuty->lastDemInd = i;
					newDuty->demandInd[0]= i;
					newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
					newDuty->aircraftID=demandList[i].aircraftID;
					newDuty->acInd = demandList[i].acInd;
					newDuty->crewPairID = demandList[i].crewPairID;
					newDuty->actualCost=demandList[i].cost[j];
					newDuty->cost = newDuty->actualCost;
					newDuty->changePenalty = demandList[i].changePenalty;

					if(demandList[i].changePenalty != 0)
					{
						newDuty->redPenCrewPairList[0] = demandList[i].redPenCrewPairID;
						newDuty->redPenACList[0] = demandList[i].redPenACID;
						newDuty->redPenaltyList[0] = 0;
					}
					newDuty->isPureAppoint = 1;
				}
			}//end for overnight copy

			if (demandList[i].isAppoint){
				if ( day == 0 && demandList[i].acInd >= 0){// || (demandList[i].aircraftID == 485 && demandList[i].inAirportID == 4577) ){
						firstCrPrInd = acList[demandList[i].acInd].firstCrPrInd;
						if (firstCrPrInd < 0){ //another way to get crewPairInd, if hasflownfirst is not 1
							firstCrPrInd = acList[demandList[i].acInd].cprInd[0];
						}

						if ( firstCrPrInd >= 0 && crewPairList[firstCrPrInd].activityCode == 0){
							postFlightTm = optParam.postFlightTm;
							if (crewPairList[firstCrPrInd].endRegDay == day)
								postFlightTm = optParam.finalPostFltTm;
							apptStartTm = (int)(crewPairList[firstCrPrInd].availDT )/60 - crewPairList[firstCrPrInd].dutyTime + optParam.maxDutyTm - postFlightTm + optParam.turnTime - 1;
							if (apptStartTm < (int)(demandList[i].reqOut)/60 && apptStartTm >= (int)(optParam.windowStart)/60 ){ //make a copy with earlier startTm
								newDuty = dutyAlloc(j);
								initializeDuty (newDuty);
								dutyTally[j][day][0].endInd ++;
								
								newDuty->startTm[0] = apptStartTm;
								newDuty->blockTm = 0; 
								newDuty->endTm = (int)(demandList[i].reqIn)/60;
								newDuty->crewEndTm = newDuty->startTm[0] - postFlightTm;  
								newDuty->crewStartTm = newDuty->endTm ; //CHECK
								newDuty->lastDemInd = i;
								newDuty->demandInd[0]= i;
								newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
								newDuty->aircraftID=demandList[i].aircraftID;
								newDuty->acInd = demandList[i].acInd;
								newDuty->crewPairID = demandList[i].crewPairID;
								newDuty->actualCost=demandList[i].cost[j] - SMALL_INCENTIVE;  // -SMALL_INCENTIVE: incentive for choosing this copy of appt
								newDuty->cost = newDuty->actualCost;
								newDuty->changePenalty = demandList[i].changePenalty;
								if(optParam.withMac == 1){
									newDuty->tempCostForMac = - SMALL_INCENTIVE; //MAC - 08/19/08 ANG
								}

								if(demandList[i].changePenalty != 0)
								{
									newDuty->redPenCrewPairList[0] = demandList[i].redPenCrewPairID;
									newDuty->redPenACList[0] = demandList[i].redPenACID;
									newDuty->redPenaltyList[0] = 0;
								}
								newDuty->isPureAppoint = 1;
							}
						}
				}//end for output-use copy
			}//end for adding out put copy
		}//end else for mx and locking
	}
	return 0;
}


/********************************************************************************
*	Function   createMultiTripNodes	          Date last modified:  5/29/07 SWO	*
*	Purpose:  																	*
********************************************************************************/
static int createMultiTripNodes(int day, int m)
{
	int i, j, k, x, y, firstIncl, firstExcl, found, match, a; 
	Demand *lastTrip, *newTrip, *firstTrip;
	Duty *oldDuty, *newDuty;
	int intTrTm, blockTm;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int startTm[4];
	int crewStartTm = 0;
	int crewEndTm = 0;
	double tempPen, lateCostPH, earlyCostPH;
	
	//for each aircraftType, run through duty nodes for the current day and duty type, and look at adding another trip
	for(j = 0; j < numAcTypes; j++)
	{
		//initialize dutyTally Matrix
		dutyTally[j][day][m].startInd = dutyTally[j][day][m-1].endInd + 1;
		dutyTally[j][day][m].endInd = dutyTally[j][day][m-1].endInd; // ==startInd - 1

		//for each existing duty for that aircraftType, day, and duty type
		for(k = dutyTally[j][day][m-1].startInd; k<= dutyTally[j][day][m-1].endInd; k++)
		{
			//NOTE:  oldDuty POINTER MUST BE RESET IF IT IS TO BE REUSED AFTER A CALL TO dutyAlloc!!
			oldDuty = &dutyList[j][k];
			lastTrip = &demandList[oldDuty->demandInd[m-1]];
			firstTrip = &demandList[oldDuty->demandInd[0]];
				
			//check that lastTrip is not a long maintenance trip (which should have no subsequent trips in duty)
			if(lastTrip->isAppoint > 0 && lastTrip->elapsedTm[j] > optParam.maintTmForReassign)
				continue;
			
			//for each trip on the same day as the existing duty node (trip need not start
			//later than last trip of the existing duty node because of flex time)
			for(i = (day == 0? 0 : lastTripOfDay[day-1] + 1); i<=lastTripOfDay[day]; i++)
			{
				//if blockTm for fleet is INFINITY, trip not feasible for that acType (due to sequence or excln)
				if(demandList[i].blockTm[j] == INFINITY)
					continue;
				//quick check that latest (new) trip start is not before (unconservatively) earliest end of exg duty
				
				//if(demandList[i].late[j] < (lastTrip->early[j] + lastTrip->elapsedTm[j]))
				//	continue;

				//RLZ 04/25/2008
				if (!lastTrip->isAppoint){
					if(demandList[i].late[j] < (lastTrip->early[j] + lastTrip->elapsedTm[j]))
						continue;
				}
				else{
					if(demandList[i].late[j] < (int)lastTrip->reqIn/60)
						continue;				
				}

				//check that latest (new) trip is not already part of exg duty
				found = 0;
				for(x = 0; x<m; x++){
					if(i == oldDuty->demandInd[x]){
						found = 1;
						break;
					}
				}
				if(found == 1)
					continue;

				newTrip = &demandList[i];

				//if last trip is tied to a succeeding (following) trip, new trip must be that trip
				if(lastTrip->succDemID > 0 && lastTrip->succDemID != newTrip->demandID)
					continue;
				//if new trip is tied to a preceding trip, then last trip must be that trip
				if(newTrip->predDemID > 0 && newTrip->predDemID != lastTrip->demandID)
					continue;
		
				//if new trip is tied to a planeID (it is a required trip for the plane)...
				if(newTrip->acInd > -1)
				{	
					//if duty is NOT tied to a planeID, check that duty has no exclusions for plane and that the new trip is the first inclusion for plane that day
					if(oldDuty->acInd == -1){
						if(acList[newTrip->acInd].lastExcl[day]>(day == 0 ? -1 : acList[newTrip->acInd].lastExcl[day-1])) {  		
							if(checkPlaneExclusions(oldDuty,&acList[newTrip->acInd], day)) //returns -1 (true) if infeasible
								continue;
						}
						firstIncl = (day == 0 ? 0 : acList[newTrip->acInd].lastIncl[day-1]+1);
						if(acList[newTrip->acInd].inclDemandInd[firstIncl] != i)
							continue;
					}
					//else duty is tied to a planeID: it must be the same plane and duty node must include all inclusions up to this point
					else
					{	if(oldDuty->aircraftID != newTrip->aircraftID)
							continue;
						//if there is more than one inclusion for this plane and day (more inclusions than just the new trip)
						firstIncl = (day == 0 ? 0 : acList[newTrip->acInd].lastIncl[day-1]+1);
						if(acList[newTrip->acInd].lastIncl[day]> firstIncl){
							y = firstIncl;
							//find previous inclusion which should occur prior to this trip and be part of the duty
							while(acList[newTrip->acInd].inclDemandInd[y] != i)
								y++;
							//y-1 is index of previous inclusion
							found = 0;
							for(x=0; x<4; x++){
								if(oldDuty->demandInd[x] == acList[newTrip->acInd].inclDemandInd[y-1]){
									found = 1;
									break;  //RLZ
								}

							}
							if(found == 0)
								continue; //this duty does NOT include all inclusions up to this point
						}
					}
				}
				else if(oldDuty->acInd > -1){ //else if duty is tied to a plane, but new trip isn't, check that trip isn't excluded
					firstExcl = (day == 0 ? 0 : acList[oldDuty->acInd].lastExcl[day-1]+1);
					for(y=firstExcl; y <= acList[oldDuty->acInd].lastExcl[day]; y++){
						if(i == acList[oldDuty->acInd].exclDemandInd[y])
							continue;
					}
				}

				if(oldDuty->crewPairID != 0 && demandList[i].crewPairID != 0 && oldDuty->crewPairID != demandList[i].crewPairID)
					continue;  //KEEP THIS - IF TWO DIFF CREWS LOCKED TO ONE PLANE DURING WINDOW

				//quick check: minimum possible duty time against max net duty
				//If new trip is an appointment leg, there is no need for crew to be on duty for maintenance leg.
				if(!newTrip->isAppoint){ //if new trip is NOT maintenance or appointment
					if((newTrip->early[j] + newTrip->elapsedTm[j] - firstTrip->late[j]) > netMaxDutyTm)
						continue;
				}
				//if no reposition to newTrip
				if(lastTrip->inAirportID == newTrip->outAirportID)
				{
					repoFltTm = 0;
					repoStops = -1;  //to offset  the cost calc term (repoStops + 1) RLZ,Fei 06/13/2008
					repoBlkTm = 0;
					//check total block time against max flight time
					blockTm = oldDuty->blockTm + newTrip->blockTm[j]; 						
					if(blockTm > optParam.maxFlightTm)
						continue;

					//calculate required time between trips
					intTrTm = lastTrip->turnTime;
//					if(lastTrip->inFboID > 0 && lastTrip->outFboID > 0 && lastTrip->inFboID != newTrip->outFboID)
					if(lastTrip->inFboID > 0 && newTrip->outFboID > 0 && lastTrip->inFboID != newTrip->outFboID)
						intTrTm += optParam.fboTransitTm;

					if(newTrip->predDemID == lastTrip->demandID){ //new trip is tied to last trip, and we will assume that scheduled turn time is feasible
						if((int)(newTrip->reqOut - (lastTrip->reqOut + lastTrip->elapsedTm[j]*60))/60 < intTrTm)
							intTrTm = (int)(newTrip->reqOut - (lastTrip->reqOut + lastTrip->elapsedTm[j]*60))/60 - 1;  //allow one minute buffer for rounding
					}
				}
				else //we need repo to new Trip
				{
					getFlightTime(lastTrip->inAirportID, newTrip->outAirportID, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
					//check repo flight time against limit
					if(repoFltTm > optParam.maxRepoTm)
						continue;
					//check total block time against limit
					if(oldDuty->blockTm + repoBlkTm + newTrip->blockTm[j] > optParam.maxFlightTm)
						continue;
					//calculate required time between trips
					intTrTm = lastTrip->turnTime + repoElapsedTm + optParam.turnTime;
				}
				if (a = checkTripTimeFeasibility(oldDuty, j, newTrip, i, intTrTm, startTm, m, &crewStartTm, &crewEndTm))
				{ // Returns -X if infeasible, 0 otherwise.
					continue;
				}
				if (a = checkReposAgainstCurfews(oldDuty, j, newTrip, intTrTm, startTm, m, &crewStartTm, &crewEndTm))
				{ //Returns -1 if infeasible, 0 otherwise.
					continue;
				}

				//RLZ early duty rule
				if (minutesPastMidnight((crewStartTm - optParam.preFlightTm)*60 , firstTrip->outAirportID) <= optParam.cutoffForShortDuty){
					if (crewEndTm - crewStartTm > netMaxDutyTmEarly)
						continue;				
				}
								
				//allocate memory for each duty and increment dutyTally
				newDuty = dutyAlloc(j);
				initializeDuty (newDuty);
				dutyTally[j][day][m].endInd ++;

				//RESET POINTERS FOR RE-USE
				oldDuty = &dutyList[j][k];
				lastTrip = &demandList[oldDuty->demandInd[m-1]];//not needed?
				
				//copy over old (existing)duty, then update for added trip and any repo
				*newDuty = *oldDuty;
				newDuty->demandInd[m] = i;
				//populate start times on duty, and calculate early/late penalties
				newDuty->earlyLatePen = 0.0;
				for(x = 0; x <= m; x++){
					newDuty->startTm[x] = startTm[x];
					lateCostPH = optParam.lateCostPH;
					earlyCostPH = optParam.earlyCostPH;

					if (demandList[newDuty->demandInd[x]].recoveryFlag){
						lateCostPH = max(lateCostPH, optParam.lateCostPH_recovery);
						earlyCostPH = max(earlyCostPH, optParam.lateCostPH_recovery);
					}

					if(newDuty->startTm[x] > (int)demandList[newDuty->demandInd[x]].reqOut/60)
						newDuty->earlyLatePen += lateCostPH*(newDuty->startTm[x] - (int)demandList[newDuty->demandInd[x]].reqOut/60)/60;
					else if(newDuty->startTm[x] < (int)demandList[newDuty->demandInd[x]].reqOut/60)
						newDuty->earlyLatePen += earlyCostPH *((int)demandList[newDuty->demandInd[x]].reqOut/60 - newDuty->startTm[x])/60;
				}
				//newDuty->endTm = startTm[m]+ newTrip->elapsedTm[j];
				//RLZ 04/25/2008
				if (!newTrip->isAppoint)
					newDuty->endTm = startTm[m]+ newTrip->elapsedTm[j];
				else
					newDuty->endTm = (int)newTrip->reqIn/60;

				newDuty->crewStartTm = crewStartTm;
				newDuty->crewEndTm = crewEndTm;
				newDuty->lastDemInd = i;
				newDuty->intTrTm[m-1] = intTrTm;
				if(optParam.withMac == 1){
					newDuty->repoFltTm[m-1] = repoFltTm; //MAC - 09/24/08 ANG
					newDuty->repoStop[m-1] = repoStops; //MAC - 09/24/08 ANG
					newDuty->tempCostForMac = newDuty->earlyLatePen;//MAC - 09/23/08 ANG
				}
				newDuty->actualCost += (newTrip->cost[j] + (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost);
				newDuty->cost = newDuty->actualCost + newDuty->earlyLatePen;

				newDuty->blockTm += (newTrip->blockTm[j] + repoBlkTm);
				newDuty->changePenalty += newTrip->changePenalty;
				//If there is a change penalty on this new trip, update reduced Penalty lists
				if(newTrip->changePenalty > 0)
				{	//if there was no crew on the trip in the existing scenario (but there was a plane in order to have a changePenalty on trip)
					if(newTrip->redPenCrewPairID == 0){
						//add penalty to any existing redPenalty crew/plane combo in list if plane doesn't match
						x=0;
						match=0;
						while(newDuty->redPenACList[x]!= 0 ){//there will not be a redPen crew without a redPen plane
							if(newDuty->redPenACList[x] == newTrip->redPenACID){
								if(newDuty->redPenCrewPairList[x] == 0)
									match = 1;
							}
							else //redPenACList[x] doesn't match newTrip->redPenACID
								newDuty->redPenaltyList[x] += newTrip->changePenalty;
							x++;
						}
						if(match==0){//if we haven't found a redPen crew/plane combo with matching plane and null crew (like trip), then add one
							newDuty->redPenACList[x] = newTrip->redPenACID;
							newDuty->redPenCrewPairList[x] = 0;
							newDuty->redPenaltyList[x] = oldDuty->changePenalty;
						}
					}
					else{ //There is both a crew and a plane on the trip in the existing scenario
						// We don't add penalty to existing (crew+plane) match. If necessary we add 
						//crew/plane combo to redPen list, with penalty = (plane/null crew) redPenalty if found, or else default.
						x=0;
						match=0;
						tempPen = oldDuty->changePenalty;  //this is the default penalty prior to adding the new trip penalty
						while(newDuty->redPenACList[x]!= 0 ){//there will not be a redPen crew without a redPen plane
							//if the redPenAC on duty matches plane on trip in existing scenario
							if(newDuty->redPenACList[x] == newTrip->redPenACID){
								if(newDuty->redPenCrewPairList[x] == newTrip->redPenCrewPairID)//if crew also matches..
									match = 1;
								else if(newDuty->redPenCrewPairList[x] == 0){//else if crew is null (in duty redPen list)
									tempPen = newDuty->redPenaltyList[x]; //this will be the penalty for crew/plane combo if no exact match is found
									newDuty->redPenaltyList[x]+= newTrip->changePenalty;
								}
								else //crew is neither a match nor null; just add trip penalty to redPen
									newDuty->redPenaltyList[x]+= newTrip->changePenalty;
							}
							else //plane doesn't match so add trip penalty to redPen
								newDuty->redPenaltyList[x]+= newTrip->changePenalty;
							x++;
						}
						if(match==0){//if we haven't found a redPen crew/plane combo with matching plane and crew (like trip), then add one
							newDuty->redPenCrewPairList[x]=demandList[i].redPenCrewPairID;
							newDuty->redPenACList[x]=demandList[i].redPenACID;
							newDuty->redPenaltyList[x]=tempPen;//either default change penalty prior to adding trip OR penalty for plane/null crew
						}
					}
				}
				//if trip we are adding has an aircraft or crewPair tied to it (and exg duty didn't), add aircraft or crewPair to new duty
				if(newTrip->aircraftID > newDuty->aircraftID)
				{
					newDuty->aircraftID = newTrip->aircraftID;
					newDuty->acInd = newTrip->acInd;
				}
				if(newTrip->crewPairID > newDuty->crewPairID)
					newDuty->crewPairID = newTrip->crewPairID;
				if (newDuty->isPureAppoint > newTrip->isAppoint)//only true if add one is not appoint; RLZ
                    newDuty->isPureAppoint = newTrip->isAppoint;	

			}
		}
	}
	return 0;
}


/********************************************************************************
*	Function   createFinalRepoNodes          Date last modified:  03/08/07 SWO	*
*	Purpose:  																	*
********************************************************************************/
static int createFinalRepoNodes(int day)
{
	int i, j, k, m, x, y, found; 
	int freshCrew, dutyDest, tripOrig, firstIncl, firstExcl; 
	Duty *newDuty, *oldDuty;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0, repoEndTm;

	//for each aircraftType, run through duty nodes for the current day, and look at adding a final positioning leg to the 
	//start of each of the next day's trips
	for(j = 0; j < numAcTypes; j++)
	{
		//determine if a fresh crew will be able to pick up a plane after the final repo and do the next day's trip.  If not, the first crew must do the repo
		//and sleep before doing the next day's trip
		if(potCrewStarts[j][day + 1] == 1 && potCrewEnds[j][day] == 1)
			freshCrew =1;
		else 
			freshCrew = 0;
		//for each type of exg. duty (1-trip, 2-trip, 3-trip, etc).  m = number of trips minus 1.
		for(m = 0; m < maxTripsPerDuty; m++)
		{
			//initialize dutyTally Matrix
			if(m==0)
			{
				dutyTally[j][day][4].startInd = dutyTally[j][day][maxTripsPerDuty-1].endInd + 1;
				dutyTally[j][day][4].endInd = dutyTally[j][day][maxTripsPerDuty-1].endInd; // ==startInd - 1
			}
			else
			{
				dutyTally[j][day][m+4].startInd = dutyTally[j][day][m+3].endInd + 1;
				dutyTally[j][day][m+4].endInd = dutyTally[j][day][m+3].endInd; // ==startInd - 1
			}
			//for each duty for that day, aircraftType,and dutyType
			for(k = dutyTally[j][day][m].startInd; k<= dutyTally[j][day][m].endInd; k++)
			{
				oldDuty = &dutyList[j][k];  //NOTE THAT THIS POINTER MUST BE RESET AFTER A CALL TO dutyAlloc
				
				//check that lastTrip is not a long maintenance trip (should have no subsequent trips in duty)
				if(demandList[oldDuty->demandInd[m]].isAppoint > 0 && demandList[oldDuty->demandInd[m]].elapsedTm[j] > optParam.maintTmForReassign)
					continue;
				//check that lastTrip is not tied to a succeeding (following) trip that hasn't been included
				if(demandList[oldDuty->demandInd[m]].succDemID > 0)
					continue;
				
				//for each of next day's trips
				for(i = lastTripOfDay[day]+ 1; i<=lastTripOfDay[day+1]; i++)  
				{
					//if blockTm for fleet is INFINITY, trip not feasible for that acType (due to seq or excln) so don't repo
					if(demandList[i].blockTm[j] == INFINITY)
						continue;
					//if earliest trip start is later than optParam.cutoffForFinalRepo, we won't do an evening (final) reposition for it
					if(demandList[i].earlyMPM > optParam.cutoffForFinalRepo)
						continue;
					//if trip has a preceeding trip tied to it, then there is no duty starting with this trip so there should be no repo to it
					if(demandList[i].predDemID > 0)
						continue;
					//if next days trip is not a maintenance/appointment leg or a locked leg
					if(demandList[i].acInd == -1){
						//check next days trip against max flight and duty time (don't repo for it if infeas)
						if(demandList[i].blockTm[j] > optParam.maxFlightTm || demandList[i].elapsedTm[j] > netMaxDutyTm)
							continue;
					}
					dutyDest = demandList[oldDuty->demandInd[m]].inAirportID;
					tripOrig = demandList[i].outAirportID;
					//if the destn of the last trip of the exg duty equals the orig of the next-day trip, no need for final repo node
					if(dutyDest == tripOrig)
						continue;

					//if next day's trip is tied to a planeID (it is a required trip for the plane)...
					if(demandList[i].acInd > -1)
					{	
						//if duty is NOT tied to a planeID, check that duty has no exclusions for plane and that the next day's trip is the first inclusion for plane the next day
						if(oldDuty->acInd == -1){
							if(acList[demandList[i].acInd].lastExcl[day]>(day == 0 ? -1 : acList[demandList[i].acInd].lastExcl[day-1])) {  				
								if(checkPlaneExclusions(oldDuty,&acList[demandList[i].acInd], day)) //returns -1 (true) if infeasible
									continue;
							}
							firstIncl = acList[demandList[i].acInd].lastIncl[day]+1;  //for next day's trip, "day" is the previous day
							if(acList[demandList[i].acInd].inclDemandInd[firstIncl] != i)
								continue;
						}
						//else duty is tied to a planeID: it must be the same plane as for the trip, and duty node must include all inclusions for the day
						else
						{	if(oldDuty->aircraftID != demandList[i].aircraftID)
								continue;
							//it is sufficient to check that the last inclusion for the day is part of the duty (if so, all others must be as well)
							x = acList[demandList[i].acInd].inclDemandInd[acList[demandList[i].acInd].lastIncl[day]];
							found = 0;
							for(y=0; y<4; y++){
								if(oldDuty->demandInd[y] == x)
									found = 1; 
							}
							if(found == 0)
								continue;  //this duty does NOT include all inclusions up to this point
						}
					}
					else if(oldDuty->acInd > -1){ //else if duty is tied to a plane, but next day's trip isn't, check that trip isn't excluded
						firstExcl = acList[oldDuty->acInd].lastExcl[day]+1; //for next day's trip, "day" is the previous day
						for(y=firstExcl; y <= acList[oldDuty->acInd].lastExcl[day+1]; y++){
							if(i == acList[oldDuty->acInd].exclDemandInd[y])
								continue;
						}
					}
					if(oldDuty->crewPairID != 0 && demandList[i].crewPairID != 0 && oldDuty->crewPairID != demandList[i].crewPairID)
						continue;  //KEEP THIS - IF TWO DIFF CREWS LOCKED TO ONE PLANE DURING WINDOW

					getFlightTime(dutyDest, tripOrig, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
					//check repo flight time against limit
					if(repoFltTm > optParam.maxRepoTm)
						continue;
					//check total block time against limit
					if(oldDuty->blockTm + repoBlkTm > optParam.maxFlightTm)
						continue;
					//Calculate repo end time including turn time. Don't need fboTransitTm for repo leg.
					repoEndTm = getRepoArriveTm(dutyDest, tripOrig, oldDuty->endTm + demandList[oldDuty->demandInd[m]].turnTime, repoElapsedTm);
					//check if repo is feasible (maybe not due to curfews)
					if(repoEndTm == -1)
						continue;
					//check if total elapsed time is less than duty time limit
					//if((repoEndTm - oldDuty->startTm[0])> netMaxDutyTm)  //RLZ CHECK
					if((repoEndTm - oldDuty->crewStartTm)> netMaxDutyTm && oldDuty->crewEndTm >= oldDuty->crewStartTm)  //RLZ CHECK
						continue;

					//RLZ early duty rule
					if (minutesPastMidnight((oldDuty->crewStartTm - optParam.preFlightTm)*60 , demandList[oldDuty->demandInd[0]].outAirportID) <= optParam.cutoffForShortDuty){
						if (repoEndTm - oldDuty->crewStartTm  > netMaxDutyTmEarly)
							continue;				
					}


					//if there is a  fresh crew that can pick up a plane after the final repo and do the next day's trip (per freshCrew determined above)...
					if(freshCrew == 1){
						//check if the repo can be done on time
						if((repoEndTm + optParam.turnTime) > demandList[i].early[j])  //RLZ CHECK always true?
							continue;
					}
					else {
						//then the first crew must do the repo and sleep before doing the next day's trip
						if(!demandList[i].isAppoint){
							if((repoEndTm + optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm) > demandList[i].early[j])
								continue;
						}
						else{//for maintenance or appointment leg, crew can rest until END of maintenance or appointment //RLZ CHECK even beyond
							if((repoEndTm + optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm) > ((int)(demandList[i].reqOut / 60) + demandList[i].elapsedTm[j]))
								continue;
						}
					}
					//allocate memory for each duty and increment dutyTally
					newDuty = dutyAlloc(j);
					initializeDuty(newDuty);
					dutyTally[j][day][m+4].endInd ++;
					//reset dutyAlloc after possible memory reallocation
					oldDuty = &dutyList[j][k]; 
					//copy over existing duty, then update for add final repo
					*newDuty = dutyList[j][k];  //don't use oldDuty without reinitializing because of re-alloc
					newDuty->endTm = repoEndTm;
					newDuty->crewEndTm = repoEndTm;
					if(demandList[i].earlyMPM - repoElapsedTm - optParam.preFlightTm < optParam.cutoffForShortDuty) { 
						//Too early for repo the same day, so we want to do the repo the day before which is the case we are considering now.
						newDuty->actualCost += (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost - SMALL_INCENTIVE;
						if(optParam.withMac == 1)
							newDuty->tempCostForMac += newDuty->earlyLatePen - SMALL_INCENTIVE; //MAC - 09/23/08 ANG
					}
					else{
						newDuty->actualCost += (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost + SMALL_INCENTIVE; 
						if(optParam.withMac == 1)
							newDuty->tempCostForMac += newDuty->earlyLatePen + SMALL_INCENTIVE; //MAC - 09/23/08 ANG
					}

					newDuty->cost = newDuty->earlyLatePen + newDuty->actualCost;  //early or late penalty doesn't change with addition of final repo

					newDuty->blockTm += repoBlkTm;
					newDuty->repoDemandInd = i;
					if(optParam.withMac == 1){
						newDuty->repoFromAptID = dutyDest;//MAC - 09/19/08 ANG
						newDuty->macRepoFltTm = repoFltTm;
						newDuty->macRepoStop = repoStops;
					}

					//if trip we are repositioning to has an aircraft or crewPair tied to it (and exg duty didn't), add aircraft or crewPair to new duty
					if(demandList[i].aircraftID > newDuty->aircraftID)
					{
						newDuty->aircraftID = demandList[i].aircraftID;
						newDuty->acInd = demandList[i].acInd;
					}
					if(demandList[i].crewPairID > newDuty->crewPairID)
						newDuty->crewPairID = demandList[i].crewPairID;
					newDuty->isPureAppoint = 0; //RLZ
				}
			}
		}
	}
	return 0;
}

/************************************************************************************************
*	Function   createRepoOnlyNodes							Date last modified:  02/12/07 SWO	*
*	Purpose:  Create duty nodes which consist of just a positioning move for a trip the next	*
*			day.																				*
************************************************************************************************/
static int createRepoOnlyNodes(int day)
{
	int i, j;
//	int firstCrPrInd;
	Duty *newDuty;
	
	//for each aircraftType, look at adding a repositioning (only)leg to the start of each of the next day's trips
	for(j = 0; j < numAcTypes; j++)
	{
		//initialize dutyTally Matrix
		dutyTally[j][day][8].startInd = dutyTally[j][day][maxTripsPerDuty+3].endInd + 1;
		dutyTally[j][day][8].endInd = dutyTally[j][day][maxTripsPerDuty+3].endInd; // ==startInd - 1
		
		//for each of next day's trips
		for(i = lastTripOfDay[day]+ 1; i<=lastTripOfDay[day+1]; i++)  
		{
			//if blockTm for fleet is INFINITY, trip not feasible for that acType (due to seq or excln) so don't repo
			if(demandList[i].blockTm[j] == INFINITY)
				continue;
			//if earliest trip start is later than optParam.cutoffForFinalRepo, we won't do an evening (final) reposition for it
			if(demandList[i].earlyMPM > optParam.cutoffForFinalRepo)
				continue;
			//if trip has a preceeding trip tied to it, then there is no duty starting with this trip so there should be no repo to it
			if(demandList[i].predDemID > 0)
				continue;
			//if next days trip is not a maintenance/appointment leg or a locked leg
			if(demandList[i].acInd == -1){
				//check next days trip against max flight and duty time (don't repo for it if infeas)
				if(demandList[i].blockTm[j] > optParam.maxFlightTm || demandList[i].elapsedTm[j] > netMaxDutyTm)
					continue;
			}
			//NOTE:  IF WE ARE REPOSITIONING TO AN AIRPORT OUTSIDE THE COUNTRY, we check plane certification when we create the arc
			//from the repo-only node to the duty the next day

			//allocate memory for each duty and increment dutyTally
			newDuty = dutyAlloc(j);
			initializeDuty (newDuty);
			dutyTally[j][day][8].endInd ++;
			
			//Create new position-only "duty Node".
			//NOTE:  no need to initialize any members to zero (this was done by calloc)
			//Assume that next day's trip starts at earliest possible time.
			//Assume a crew must sleep between the repo-only duty and the next-day duty. 
			//RLZ CHECK What about app-only demand?
			newDuty->endTm = demandList[i].early[j] - optParam.preFlightTm - optParam.minRestTm - optParam.postFlightTm - 1;  //1 minute buffer to avoid infeasibilities due to rounding
			//NOTE:  AIRPORT CURFEWS WILL BE CONSIDERED DURING ARC GENERATION (WHEN REPO START AIRPORT IS KNOWN)
			newDuty->startTm[0] = newDuty->endTm;
			newDuty->crewStartTm = newDuty->endTm;
			newDuty->crewEndTm = newDuty->endTm;
			newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
			newDuty->aircraftID = demandList[i].aircraftID;
			//newDuty->crewPairID = demandList[i].crewPairID;  //No need for crewPairID for the repo only. 
			newDuty->acInd = demandList[i].acInd;
			newDuty->repoDemandInd = i; //newDuty->repoFromAptID is not set since we don't have the repo cost to add yet

			newDuty->cost = SMALL_INCENTIVE;
			newDuty->actualCost = SMALL_INCENTIVE;
			if (optParam.withMac)
				newDuty->tempCostForMac = SMALL_INCENTIVE;

			////create another copy for repo to appt in day = 0
			//if (demandList[i].isAppoint){
			//	if ( day == 0 && demandList[i].acInd >= 0){
			//		firstCrPrInd = acList[demandList[i].acInd].firstCrPrInd;
			//	//	if (firstCrPrInd < 0){ //another way to get crewPairInd, if hasflownfirst is not 1
			//	//		firstCrPrInd = acList[demandList[i].acInd].cprInd[0];
			//	//	}

			//		if ( firstCrPrInd >= 0 && crewPairList[firstCrPrInd].activityCode == 0){
			//			newDuty = dutyAlloc(j);
			//			initializeDuty (newDuty);
			//			dutyTally[j][day][0].endInd ++;
			//			newDuty->endTm = max((int)(crewPairList[firstCrPrInd].availDT )/60,(int)(acList[demandList[i].acInd].availDT)/60);						
			//			newDuty->startTm[0] = newDuty->endTm;
			//			newDuty->crewStartTm = newDuty->endTm;
			//			newDuty->crewEndTm = newDuty->endTm;
			//			newDuty->aircraftTypeID=acTypeList[j].aircraftTypeID;
			//			newDuty->aircraftID = demandList[i].aircraftID;
			//			newDuty->crewPairID = demandList[i].aircraftID;
			//			newDuty->acInd = demandList[i].acInd;
			//			newDuty->repoDemandInd = i;
			//			newDuty->actualCost= - 2;  // -2: incentive for choosing this copy of appt
			//			newDuty->cost = newDuty->actualCost;
			//		}
			//	}
			//}//end for output-use copy
		}
	}
	return 0;
}




/************************************************************************************
*	Function   checkTripTimeFeasibility			Date last modified:  8/08/06 BGC	*
*	Purpose:  Determine if duty can be feasibly executed w.r.t. time.	2/26/07 SWO	*
*		Adjust scheduled times of each trip in duty as necessary.					*
*		Ignore curfews for repositioning legs for now.								*
************************************************************************************/

static int
checkTripTimeFeasibility (Duty *exgDuty, int j, Demand *lastTrip, int lastInd, int intTrTm, int *startTm, int m, int *crewStartTm, int *crewEndTm)
{
	/*
	*	m is the number of trips in the existing duty (and index of proposed new trip in duty).
	*	j is the aircraft type index.
	*/

	int i, spread;
	Demand *secondTrip, *thirdTrip;
	Demand *firstTrip = &demandList[exgDuty->demandInd[0]];
	

	/*
	*	First check if last trip can be added to current duty without having to move anything.
	*/
	for (i=0; i<m; i++)
	{
		startTm[i] = exgDuty->startTm[i];
	}	
	startTm[m] = (int) (lastTrip->reqOut/60);



	/*
	if (startTm[m] >= (startTm[m-1] + intTrTm + demandList[exgDuty->demandInd[m-1]].elapsedTm[j]))
	{// There is sufficient slack, now check spread
		if(!firstTrip->isAppoint && !lastTrip->isAppoint){
			spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[0]) - netMaxDutyTm;
			*crewStartTm = startTm[0];
		}
		else if (!firstTrip->isAppoint && lastTrip->isAppoint){
			spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime) - startTm[0]) - netMaxDutyTm;
			*crewStartTm = startTm[0];
		}//?????????????? (m==1?0:exgDuty->intTrTm[0])
		else if(firstTrip->isAppoint && !lastTrip->isAppoint){ //assume repo leg between first and second trips starts as late as possible
			spread = ((startTm[m] + lastTrip->elapsedTm[j]) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm; 
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
		}
		else{ //firstTrip->isAppoint && lastTrip->isAppoint
			spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime)
				- (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime))  - netMaxDutyTm; 
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
		}

		if (spread <= 0)
		{
			*crewEndTm = spread + *crewStartTm + netMaxDutyTm;
			return (0); // trip can be added to the current duty without any adjustments.
		}
	}
	*/
//RLZ 04/11/2008, need to consider the combination of the duty
	
	if (startTm[m] >= (startTm[m-1] + intTrTm + demandList[exgDuty->demandInd[m-1]].elapsedTm[j]))
	{// There is sufficient slack, now check spread
		if(!firstTrip->isAppoint && !lastTrip->isAppoint){
			spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[0]) - netMaxDutyTm;
			*crewStartTm = startTm[0];
		}
		else if (!firstTrip->isAppoint && lastTrip->isAppoint){
			spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime) - startTm[0]) - netMaxDutyTm;
			*crewStartTm = startTm[0];
		}
		else if(firstTrip->isAppoint && !lastTrip->isAppoint){ //assume repo leg between first and second trips starts as late as possible
			switch(m) {
				case 1: 	
					spread = (lastTrip->elapsedTm[j] + intTrTm - firstTrip->turnTime) - netMaxDutyTm; //
					*crewStartTm = (startTm[m] - intTrTm + firstTrip->turnTime);  //intTrTm, firstTrip->turnTime canceled out most of time.
					break;
				case 2: 
					secondTrip = &demandList[exgDuty->demandInd[1]];
					if (secondTrip->isAppoint){
						spread = (lastTrip->elapsedTm[j] + intTrTm - secondTrip->turnTime) - netMaxDutyTm;
						*crewStartTm = (startTm[m] - intTrTm + secondTrip->turnTime);
					}
					else{
						spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[1]) - netMaxDutyTm;
						*crewStartTm = startTm[1];
					}
					break;
				case 3: 
					secondTrip = &demandList[exgDuty->demandInd[1]];
					thirdTrip = &demandList[exgDuty->demandInd[2]];
					if (!secondTrip->isAppoint){
						spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[1]) - netMaxDutyTm;
						*crewStartTm = startTm[1];
					}
					else{
						if (thirdTrip->isAppoint){
							spread = (lastTrip->elapsedTm[j] + intTrTm - thirdTrip->turnTime) - netMaxDutyTm;
							*crewStartTm = (startTm[m] - intTrTm + thirdTrip->turnTime);
						}
						else{
							spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[2]) - netMaxDutyTm;
							*crewStartTm = startTm[2];
						}
					}
					break;
				default: //should never be here
					spread = (lastTrip->elapsedTm[j] + intTrTm - firstTrip->turnTime) - netMaxDutyTm; //
					*crewStartTm = (startTm[m] - intTrTm + firstTrip->turnTime);  //intTrTm, firstTrip->turnTime canceled out most of time.
					fprintf (logFile, "Warning! This part should not be excuted function checkTripTimeFeasiblity. %s Line %d.\n", __FILE__, __LINE__);
                    break;			
			}
		}
		else{ //firstTrip->isAppoint && lastTrip->isAppoint
			switch(m) {
				case 1: 	
					*crewStartTm = (int)(lastTrip->reqIn)/60;
					*crewEndTm = startTm[0];
					return (0);
					//break;
				case 2: 
					secondTrip = &demandList[exgDuty->demandInd[1]];
					if (secondTrip->isAppoint){
						*crewStartTm = (int)(lastTrip->reqIn)/60;
						*crewEndTm = startTm[0];
						return (0);
					}
					else{
						spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime) - startTm[1]) - netMaxDutyTm;
						*crewStartTm = startTm[1];
					}
					break;
				case 3: 
					secondTrip = &demandList[exgDuty->demandInd[1]];
					thirdTrip = &demandList[exgDuty->demandInd[2]];
					if (!secondTrip->isAppoint){
						spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime) - startTm[1]) - netMaxDutyTm;
						*crewStartTm = startTm[1];
					}
					else{
						if (thirdTrip->isAppoint){
							*crewStartTm = (int)(lastTrip->reqIn)/60;
							*crewEndTm = startTm[0];
							return (0);
						}
						else{
							spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime) - startTm[2]) - netMaxDutyTm;
							*crewStartTm = startTm[2];
						}
					}
					break;
				default: //should never be here
					spread = ((startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm - optParam.turnTime)
						- (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime))  - netMaxDutyTm; 
					*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
					fprintf (logFile, "Warning! This part should not be excuted function checkTripTimeFeasiblity. %s Line %d.\n", __FILE__, __LINE__);
                    break;			
			}
		}

		if (spread <= 0)
		{
			*crewEndTm = spread + *crewStartTm + netMaxDutyTm;
			return (0); // trip can be added to the current duty without any adjustments.
		}
	}

	

	// Else lastTrip cannot be added to existing duty without violating feasibility.

	for (i=0; i<m; i++)
	{
		startTm[i] = (int) (demandList[exgDuty->demandInd[i]].reqOut/60);
		early[i] = demandList[exgDuty->demandInd[i]].early[j];
		late[i] = demandList[exgDuty->demandInd[i]].late[j];
	}

	//RLZ 11/24/2008

	if (early[0]> 0){
		startTm[0] = exgDuty->startTm[0];
		early[0] = late[0] = exgDuty->startTm[0];
	}


	startTm[m] = (int) (lastTrip->reqOut/60);
	early[m] = lastTrip->early[j];
	late[m] = lastTrip->late[j];
	
	for (i=0; i<m-1; i++) //inter[i] includes the elapsedTm of trip[i] + intTrTm, which includes elapsed time of any reposition + turns
	{
		inter[i] = demandList[exgDuty->demandInd[i]].elapsedTm[j] + exgDuty->intTrTm[i];
	}
	inter[m-1] = demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + intTrTm;
	/*
	*	Initially all trips are at their reqOut. Now create duty with sufficient slack.
	*/
	for (i=m; i>0; i--)
	{
		startTm[i-1] = max(early[i-1], min(startTm[i-1], startTm[i] - inter[i-1]));
	}
	/*
	*	Start time is the maximum of earliest time and start of next trip - interTripTime.
	*	This still doesn't guarantee slack feasibility. Now pass in reverse direction to ensure feasibility.
	*/
	for (i=0; i<m; i++)
	{
		startTm[i+1] = max(startTm[i+1], startTm[i] + inter[i]);
		if (startTm[i+1] > late[i+1])
			return (-1);
	}
	/*
	*	Duty now has sufficient slack, and all start times are within early and late. Consider four cases
	*	based on how spread is calculated.
	*/
//	CASE I:  neither first nor last trip is an appointment
	if(!firstTrip->isAppoint&& !lastTrip->isAppoint) {
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		/*	If spread is positive, move last trip earlier by this amount (if possible). If not, move last trip to 
		*	its earliest time.
		*/
		startTm[m] = max (early[m], startTm[m]-spread);
		/*
		*	Ensure slack feasibility. Make one backward pass and one forward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=m; i>0; i--)
		{
			startTm[i-1] = max(early[i-1], min(startTm[i-1], startTm[i] - inter[i-1]));
		}
		for (i=0; i<m; i++)
		{
			startTm[i+1] = max(startTm[i+1], startTm[i] + inter[i]);
		}
		/*	
		*  Re-compute spread. 
		*/
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		/*
		*	Now move first trip forward by spread.
		*/
		startTm[0] += spread; 
		if (startTm[0] > late[0])
			return (-2);
		/*
		*	Ensure slack feasibility. Make one forward pass and one backward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=0; i<m; i++)
		{
			startTm[i+1] = min(late[i+1], max(startTm[i+1], startTm[i] + inter[i]));
		}		
		for (i=m; i>0; i--)
		{
			startTm[i-1] = min(startTm[i-1], startTm[i] - inter[i-1]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		else
			return (-3);
	}
//	CASE II:  last trip is an appointment, but first is not
	else if (!firstTrip->isAppoint && lastTrip->isAppoint)
	{
		spread = (startTm[m-1] + inter[m-1] - optParam.turnTime - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;
			return 0; // Slack and spread are feasible.
		}
		/*
		*	If spread is positive, move next-to-last trip earlier by this amount (if possible). If not, move next-to-last trip to 
		*	its earliest time.
		*/
		startTm[m-1] = max (early[m-1], startTm[m-1]-spread);
		/*
		*	Ensure slack feasibility. Make one backward pass and one forward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=m-1; i>0; i--)
		{
			startTm[i-1] = max(early[i-1], min(startTm[i-1], startTm[i] - inter[i-1]));
		}
		for (i=0; i<m; i++)
		{
			startTm[i+1] = max(startTm[i+1], startTm[i] + inter[i]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = (startTm[m-1] + inter[m-1] - optParam.turnTime - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;;
			return 0; // Slack and spread are feasible.
		}
		/*
		*	Now move first trip forward by spread.
		*/
		startTm[0] += spread; 
		if (startTm[0] > late[0])
		{
			return (-4); // Infeasible because startTm[0] HAS to be moved ahead by spread for feasibility.
		}
		/*
		*	Ensure slack feasibility. Make one forward pass and one backward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=0; i<m; i++)
		{
			startTm[i+1] = min (late[i+1], max(startTm[i+1], startTm[i] + inter[i]));
		}		
		for (i=m; i>0; i--)
		{
			startTm[i-1] = min(startTm[i-1], startTm[i] - inter[i-1]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = (startTm[m-1] + inter[m-1] - startTm[0]) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = startTm[0];
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;
			return 0; // Slack and spread are feasible.
		}
		else
			return (-5);
	}
//	CASE III:  first trip is an appointment, but last is not
	else if(firstTrip->isAppoint&& !lastTrip->isAppoint) {
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		/*	If spread is positive, move last trip earlier by this amount (if possible). If not, move last trip to 
		*	its earliest time.
		*/
		startTm[m] = max (early[m], startTm[m]-spread);
		/*
		*	Ensure slack feasibility. Make one backward pass and one forward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=m; i>0; i--)
		{
			startTm[i-1] = max(early[i-1], min(startTm[i-1], startTm[i] - inter[i-1]));
		}
		for (i=0; i<m; i++)
		{
			startTm[i+1] = max(startTm[i+1], startTm[i] + inter[i]);
		}
		/*	
		*  Re-compute spread. 
		*/
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		/*
		*	Now move second trip forward by spread.
		*/
		startTm[1] += spread; 
		if (startTm[1] > late[1])
			return (-6); // Infeasible because startTm[1] HAS to be moved ahead by spread for feasibility.
		/*
		*	Ensure slack feasibility. Make one forward pass and one backward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=1; i<m; i++)
		{
			startTm[i+1] = min(late[i+1], max(startTm[i+1], startTm[i] + inter[i]));
		}		
		for (i=m; i>0; i--)
		{
			startTm[i-1] = min(startTm[i-1], startTm[i] - inter[i-1]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = ((startTm[m] + lastTrip->elapsedTm[j]) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m] + lastTrip->elapsedTm[j];
			return 0; // Slack and spread are feasible.
		}
		else
			return (-7);
	}
//	CASE IV:  first trip and last trip are appointments
	else
	{
		spread = ((startTm[m-1] + inter[m-1] - optParam.turnTime) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;
			return 0; // Slack and spread are feasible.
		}
		/*
		*	If spread is positive, move next-to-last trip earlier by this amount (if possible). If not, move next-to-last trip to 
		*	its earliest time.
		*/
		startTm[m-1] = max (early[m-1], startTm[m-1]-spread);
		/*
		*	Ensure slack feasibility. Make one backward pass and one forward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=m-1; i>0; i--)
		{
			startTm[i-1] = max(early[i-1], min(startTm[i-1], startTm[i] - inter[i-1]));
		}
		for (i=0; i<m; i++)
		{
			startTm[i+1] = max(startTm[i+1], startTm[i] + inter[i]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = ((startTm[m-1] + inter[m-1] - optParam.turnTime) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;
			return 0; // Slack and spread are feasible.
		}
		/*
		*	Now move second trip forward by spread.
		*/
		startTm[1] += spread; 
		if (startTm[1] > late[1])
		{
			return (-8); // Infeasible because startTm[1] HAS to be moved ahead by spread for feasibility.
		}
		/*
		*	Ensure slack feasibility. Make one forward pass and one backward pass to ensure sufficient 
		*	time between trips.
		*/
		for (i=1; i<m; i++)
		{
			startTm[i+1] = min (late[i+1], max(startTm[i+1], startTm[i] + inter[i]));
		}		
		for (i=m; i>0; i--)
		{
			startTm[i-1] = min(startTm[i-1], startTm[i] - inter[i-1]);
		}
		/*
		*	Re-compute spread. 
		*/
		spread = ((startTm[m-1] + inter[m-1] - optParam.turnTime) - (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime)) - netMaxDutyTm;
		if (spread <= 0){
			*crewStartTm = (startTm[1] - exgDuty->intTrTm[0] + firstTrip->turnTime);
			*crewEndTm = startTm[m-1] + inter[m-1]- optParam.turnTime;
			return 0; // Slack and spread are feasible.
		}
		else
			return (-9);
	}
}

/********************************************************************************
*	Function	checkDutyTimeFeasibility	  Date last modified:  8/08/06 BGC	*
*	Purpose:	Checks whether a given duty is time-feasible.	02/28/07 SWO	*
********************************************************************************/

static int checkDutyTimeFeasibility (Duty *duty)
{
	int numTrips=0, i, dutyTm=0, j;
	Demand *lastTrip, *lastButOneTrip;

	while (duty->demandInd[numTrips] >= 0 && numTrips < 4)
		numTrips ++;

	if(numTrips < 2)
		return 0;

	lastTrip = &demandList[duty->demandInd[numTrips-1]];
	lastButOneTrip = &demandList[duty->demandInd[numTrips-2]];

	for (j=0; j<numAcTypes; j++)
	{
		if (acTypeList[j].aircraftTypeID == duty->aircraftTypeID)
			break;
	}

	// Check if start times are within allowable range
	for (i=0; i<numTrips; i++)
	{
		if (((duty->startTm[i] < demandList[duty->demandInd[i]].early[j]) ||
			(duty->startTm[i] > demandList[duty->demandInd[i]].late[j])) && !demandList[duty->demandInd[i]].isAppoint ){
			fprintf (logFile, "duty->startTm[%d]: %d, demandList[%d].early[%d]: %d, demandList[%d].late[%d]: %d.\n",
				i, duty->startTm[i], 
				duty->demandInd[i], j, demandList[duty->demandInd[i]].early[j],
				duty->demandInd[i], j, demandList[duty->demandInd[i]].late[j]);
			//fprintf (logFile, "Num trips: %d, Start Tm: %d, reqout/60: %d, Contract flag: %d\n", 
			//		numTrips,
			//		duty->startTm[i], 
			//		demandList[duty->demandInd[i]].reqOut/60,
			//		demandList[duty->demandInd[i]].contractFlag);
			return (-1); // infeasible -- start tm out of allowable window
		}
	}
	
	// Check sufficient inter trip times, neglecting curfews (curfews for repos are checked in function checkReposAgainstCurfews)
	for (i=1; i<numTrips; i++)
	{
		if (duty->startTm[i] < (duty->startTm[i-1] + demandList[duty->demandInd[i-1]].elapsedTm[j] + duty->intTrTm[i-1]))
		{
			fprintf (logFile, "duty->startTm[%d]: %d,  duty->startTm[%d]: %d, demandList[duty->demandInd[%d]].elapsedTm[%d]: %d, duty->intTrTm[%d]: %d.\n",
				i,
				duty->startTm[i],
				i-1,
				duty->startTm[i-1],
				i-1,
				j,
				demandList[duty->demandInd[i-1]].elapsedTm[j],
				i-1,
				duty->intTrTm[i-1]);
			return (-2); // infeasible -- insufficient time between two trips
		}
	}

	// Check spread. 
	if (duty->crewEndTm - duty->crewStartTm > netMaxDutyTm)
		return (-3);

	return 0;
}


/********************************************************************************
*	Function   dutyAllocInit		          Date last modified:				*
*	Purpose:  																	*
********************************************************************************/

static void
dutyAllocInit(int acTypeCount)
{
	dutyCountsPerAcType = (int *) calloc(acTypeCount, sizeof(int));
	if(! dutyCountsPerAcType) {
		logMsg(logFile,"%s Line %d, Out of Memory in dutyAllocInit().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
}


/********************************************************************************
*	Function   dutyAlloc			          Date last modified:				*
*	Purpose:  																	*
********************************************************************************/
static Duty *
dutyAlloc(int acTypeIdx)
{
	Duty *dPtr;

	if(! *(dutyList + acTypeIdx)) {
		// nothing has been allocated yet
		*(dutyList + acTypeIdx) = (Duty *) calloc(DutyAllocChunk, sizeof(Duty));
		if(!(*(dutyList + acTypeIdx))) {
			logMsg(logFile,"%s Line %d, Out of Memory in dutyAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		dPtr = *(dutyList + acTypeIdx);
		(*(dutyCountsPerAcType + acTypeIdx))++;
		return(dPtr);
	}

	if((!(*(dutyCountsPerAcType + acTypeIdx) % DutyAllocChunk))) {
		// time to realloc
		*(dutyList + acTypeIdx) = (Duty *) realloc(*(dutyList + acTypeIdx),
			((*(dutyCountsPerAcType + acTypeIdx) * sizeof(Duty)) + (DutyAllocChunk * sizeof(Duty))));
		if(! *(dutyList + acTypeIdx)) {
			logMsg(logFile,"%s Line %d, Out of Memory in dutyAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		dPtr = *(dutyList + acTypeIdx);
		dPtr += *(dutyCountsPerAcType + acTypeIdx);
		(*(dutyCountsPerAcType + acTypeIdx))++;
		memset(dPtr,'\0',sizeof(Duty));
		return(dPtr);
	}

	// just return the next pre-allocated Duty
	dPtr = *(dutyList + acTypeIdx);
	dPtr += *(dutyCountsPerAcType + acTypeIdx);
	(*(dutyCountsPerAcType + acTypeIdx))++;
	memset(dPtr,'\0',sizeof(Duty));
	return(dPtr);
}

/****************************************************************************************
*	Function   checkPlaneExclusions					Date last modified:	03/07/07 SWO	*
*	Purpose:  	check that a duty includes NO excluded trips for a plane				*
****************************************************************************************/
int checkPlaneExclusions(Duty *duty, Aircraft *plane, int day)
{
	int x, y; 
	
	//check trips in duty against exclusions for plane for that day
	for(x = (day == 0? 0 : plane->lastExcl[day-1] + 1); x <= plane->lastExcl[day]; x++){
		for(y=0; y<4; y++){
			if(duty->demandInd[y] == -1)
				break; //no more trips in Duty
			if(plane->exclDemandInd[x] == duty->demandInd[y])
				return -1; //infeasible
		}
	}
	//check final repo trip(if any) against exclusions for next day
	if(duty->repoDemandInd > -1){
		for(x = plane->lastExcl[day] + 1; x <= plane->lastExcl[day+1]; x++){
			if(plane->exclDemandInd[x] == duty->repoDemandInd)
				return -1; //infeasible
		}
	}

	return 0; //feasible
}



/************************************************************************************
*	Function   getRepoDepartTm				Date last modified:  12/20/06 SWO		*
*	Purpose:	Given the start and end airports and the latest arrival time,		*
*				calculate the departure time of the repositioning leg				*
*				considering curfews.												*
************************************************************************************/
int getRepoDepartTm(int startAptID, int endAptID, int lateArrTm, int elapsedTm)
{
	int x, arrMPM, depMPM, depTm, newArr, newDep, loopCount;

	depTm = lateArrTm - elapsedTm;
	newArr = 1;   //we must check arrival and departure against curfews at least once
	newDep = 1;
	loopCount = 0;  //track how many times we must check or recheck the arrival time against curfew
	
	while(newArr == 1 && loopCount < 3){
		if(aptCurf[endAptID][0] > 0){
			arrMPM = minutesPastMidnight((depTm + elapsedTm)*60, endAptID);
			x=0;
			while(x < aptCurf[endAptID][0]){
				//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
				if(aptCurf[endAptID][2*x+1] > aptCurf[endAptID][2*x+2]){
					if(arrMPM > aptCurf[endAptID][2*x+1]){
						depTm -= (arrMPM - aptCurf[endAptID][2*x+1]);
						newDep = 1; //departure time must be checked (again)
						break;
					}
					if(arrMPM < aptCurf[endAptID][2*x+2]){
						depTm -= (arrMPM + 1440 - aptCurf[endAptID][2*x+1]);
						newDep = 1;
						break;
					}
				}
				else if(arrMPM > aptCurf[endAptID][2*x+1] && arrMPM < aptCurf[endAptID][2*x+2]){//curfew doesn't cross midnight
					depTm -= (arrMPM - aptCurf[endAptID][2*x+1]);
					newDep = 1;
					break;
				}
				x++;
			}
		}
		newArr = 0; //we have checked current arrival time

		if(newDep == 1 && aptCurf[startAptID][0] > 0){
			depMPM = minutesPastMidnight(depTm*60, startAptID);
			x=0;
			while(x < aptCurf[startAptID][0]){
				//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
				if(aptCurf[startAptID][2*x+1] > aptCurf[startAptID][2*x+2]){
					if(depMPM > aptCurf[startAptID][2*x+1]){
						depTm -= (depMPM - aptCurf[startAptID][2*x+1]);
						newArr = 1; //arrival time must be checked (again)
						break;
					}
					if(depMPM < aptCurf[startAptID][2*x+2]){
						depTm -= (depMPM + 1440 - aptCurf[startAptID][2*x+1]);
						newArr = 1;
						break;
					}
				}
				else if(depMPM > aptCurf[startAptID][2*x+1] && depMPM < aptCurf[startAptID][2*x+2]){ //curfew doesn't cross midnight
					depTm -= (depMPM - aptCurf[startAptID][2*x+1]);
					newArr = 1;
					break;
				}
				x++;
			}
		}
		newDep = 0;  //we have checked current departure time
		loopCount++;
	}
	if(newArr == 1 && loopCount == 3) //if we need to check arrival time (due to adjustments) more than three times, return -1 and assume that reposition is infeasible
		return -1;
	else
		return depTm;
}

/************************************************************************************
*	Function   getRepoArriveTm				Date last modified:  1/02/06 SWO		*
*	Purpose:	Given the start and end airports and the earliest departure time,	*
*				calculate the arrival time of the repositioning leg					*
*				considering curfews.												*
************************************************************************************/
int getRepoArriveTm(int startAptID, int endAptID, int earlyDepTm, int elapsedTm)
{
	int x, arrMPM, depMPM, arrTm, newArr, newDep, loopCount;

	arrTm = earlyDepTm + elapsedTm;
	newArr = 1;   //we must check arrival and departure against curfews at least once
	newDep = 1;
	loopCount = 0;  //track how many times we must check or recheck the arrival time against curfew
	
	while(newDep == 1 && loopCount < 3){
		if(aptCurf[startAptID][0] > 0){
			depMPM = minutesPastMidnight((arrTm - elapsedTm)*60, startAptID);
			x=0;
			while(x < aptCurf[startAptID][0]){
				//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
				if(aptCurf[startAptID][2*x+1] > aptCurf[startAptID][2*x+2]){
					if(depMPM > aptCurf[startAptID][2*x+1]){
						arrTm += (1440 - depMPM + aptCurf[startAptID][2*x+2]);
						newArr = 1; //arrival time must be checked (again)
						break;
					}
					if(depMPM < aptCurf[startAptID][2*x+2]){
						arrTm += (aptCurf[startAptID][2*x+2] - depMPM);
						newArr = 1;
						break;
					}
				}
				else if(depMPM > aptCurf[startAptID][2*x+1] && depMPM < aptCurf[startAptID][2*x+2]){  //curfew doesn't cross midnight
					arrTm += (aptCurf[startAptID][2*x+2] - depMPM);
					newArr = 1;
					break;
				}
				x++;
			}
		}
		newDep = 0; //we have checked current departure time  

		if(newArr == 1 && aptCurf[endAptID][0] > 0){
			arrMPM = minutesPastMidnight(arrTm*60, endAptID);
			x=0;
			while(x < aptCurf[endAptID][0]){
				//if start of curfew (in minutes past Midnight) is greater than end of curfew, start is < midnite and end is after
				if(aptCurf[endAptID][2*x+1] > aptCurf[endAptID][2*x+2]){
					if(arrMPM > aptCurf[endAptID][2*x+1]){
						arrTm += (1440 - arrMPM + aptCurf[endAptID][2*x+2]);
						newDep = 1; //departure time must be checked (again)
						break;
					}
					if(arrMPM < aptCurf[endAptID][2*x+2]){
						arrTm += (aptCurf[endAptID][2*x+2] - arrMPM);
						newDep = 1;
						break;
					}
				}
				else if(arrMPM > aptCurf[endAptID][2*x+1] && arrMPM < aptCurf[endAptID][2*x+2]){  //curfew doesn't cross midnight
					arrTm += (aptCurf[endAptID][2*x+2] - arrMPM);
					newDep = 1;
					break;
				}
				x++;
			}
		}
		newArr = 0;  //we have checked current arrival time
		loopCount++;
	}
	if(newDep == 1 && loopCount == 3) //if we need to check arrival time (due to adjustments) more than three times, return -1 and assume that reposition is infeasible
		return -1;
	else
		return arrTm;
}


	


/************************************************************************************
*	Function   checkReposAgainstCurfews	      Date last modified:  02/26/07 SWO		*
*	Purpose:  After checking the time-feasibility of a multi-trip duty, we assume	*
*		(for now) that the scheduled out times of the demand legs are				*
*		fixed.  We check the feasibility of the repositioning legs within the duty	*
*		against airport curfews, and adjust the repo start times as much as possible*
*		WITHOUT modifying the schedOut for the demand legs.  If the repo leg can't	*
*		be executed without violating a curfew or max duty, the duty is considered	*
*		infeasible.																	*
************************************************************************************/

static int checkReposAgainstCurfews (Duty *exgDuty, int j, Demand *lastTrip, int intTrTm, int *startTm, int m, int *crewStartTm, int *crewEndTm)
{
	/* m is the number of trips in the existing duty prior to potentially adding the new (last) trip.  j is the aircraft type index. */
	/* startTm[0..m], the start times of the demand legs, has been populated previously in the function checkTripTimeFeasibility */

	int x, turnTime, earlyDepTm, lateArrTm, elapsedTm, temp;

	for(x = 0; x < (m-1); x++){
		//if a repo is required between trips AND one or more curfews is involved, check if this is possible given airport curfews
		if(demandList[exgDuty->demandInd[x]].inAirportID != demandList[exgDuty->demandInd[x+1]].outAirportID && 
			(aptCurf[demandList[exgDuty->demandInd[x]].inAirportID][0]>0 || aptCurf[demandList[exgDuty->demandInd[x+1]].outAirportID][0] > 0)){
			
			turnTime = demandList[exgDuty->demandInd[x]].turnTime;
			earlyDepTm = startTm[x] + demandList[exgDuty->demandInd[x]].elapsedTm[j] + turnTime;
			lateArrTm = startTm[x+1] - optParam.turnTime;
			elapsedTm = exgDuty->intTrTm[x] - turnTime - optParam.turnTime;  //turnTime applies before the repo leg;  optParam.turnTime applies after the repo leg
			if((temp = getRepoDepartTm(demandList[exgDuty->demandInd[x]].inAirportID, demandList[exgDuty->demandInd[x+1]].outAirportID, lateArrTm, elapsedTm)) < earlyDepTm)
				return -1;
			if (temp == -1)
				return -1;
			if(x == 0 && demandList[exgDuty->demandInd[0]].isAppoint) //if first trip is a maintenance or airport appt for which crew need not be on duty
				(*crewStartTm) = temp; //update crew start time for duty  (used if there is no repo to duty)
		}
	}
	 //if a repo is required to get to the proposed new (last) trip and a curfew is involved...
	if(demandList[exgDuty->demandInd[m-1]].inAirportID != lastTrip->outAirportID && 
		(aptCurf[demandList[exgDuty->demandInd[m-1]].inAirportID][0]>0 || aptCurf[lastTrip->outAirportID][0] > 0)){ 
		
		turnTime = demandList[exgDuty->demandInd[m-1]].turnTime;
		earlyDepTm = startTm[m-1] + demandList[exgDuty->demandInd[m-1]].elapsedTm[j] + turnTime;
		lateArrTm = startTm[m] - optParam.turnTime;
		elapsedTm = intTrTm - turnTime - optParam.turnTime;  //turnTime applies before the repo leg;  optParam.turnTime applies after the repo leg
		if((temp = getRepoArriveTm(demandList[exgDuty->demandInd[m-1]].inAirportID, lastTrip->outAirportID, earlyDepTm, elapsedTm)) > lateArrTm)
			return -1;
		if (temp == -1)
			return -1;
		if(lastTrip->isAppoint) //if last trip is a maintenance or airport appointment for which crew need not stay on duty
			(*crewEndTm) = temp;  //update crew end time for duty
	}
	//recheck crew duty time (may have been modified above)
	if((*crewEndTm) - (*crewStartTm) > netMaxDutyTm)
		return -1;
	return 0;
}

