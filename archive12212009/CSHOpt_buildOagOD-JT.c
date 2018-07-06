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
#include "CSHOpt_define.h"
#include "CSHOpt_calculateODandOAG.h"
#include "CSHOpt_output.h"

extern FILE *logFile;
extern struct optParameters optParam;
extern Crew *crewList;
extern CrewPair *crewPairList;
extern Aircraft *acList;
extern Demand *demandList;
extern MaintenanceRecord *maintList;
extern int numAcTypes;
extern int numAircraft;
extern int numCrew;
extern int numCrewPairs;
extern int numMaintenanceRecord;
extern int numOptDemand;
extern Leg *legList;
extern int numLegs;
extern PairConstraint *pairConstraintList;
extern int numPairConstraints;
extern int lastTripOfDay[MAX_WINDOW_DURATION];
extern time_t firstEndOfDay;
//extern Airport2 aptList[TOTAL_AIRPORTS_NUM+1];
extern Airport2 *aptList;
extern int numOD;
extern ODEntry *oDTable;
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;

int *legAvailable;
/*  An array of length numLegs that indicates if a swap can occur at the end of that leg.
*	0 or -1 indicate no swap can occur, while 1 and 2 indicate that the swap can occur. */

OrigEntry **o1Tables;
DestEntry **d1Tables;
OrigEntry **o2Tables;
DestEntry **d2Tables;
int numO1Entries[MAX_AC_TYPES];
int numD1Entries[MAX_AC_TYPES];
int numO2Entries[MAX_AC_TYPES];
int numD2Entries[MAX_AC_TYPES];

int numNewOD;
ODEntry *newODTable;
int *oDHashTable;
int *newODHashTable;
int totDeviation = 0;
int numODnoDirects = 0;
int numOneStopQueries = 0;
int numODnoItins = 0;
int totDirects = 0;
int totOneStops = 0;
int totPruned = 0;
int numSameOD = 0;
int initODCount;
int maxOag = 0;
time_t maxArr;	//max(demand.reqOut + demand.lateAdj – optParam.firstPreFlt) across ALL demands 
									//(a pilot wouldn’t travel to a plane unless there was a subsequent demand he could cover)

static void buildO1Tables(void);
static void buildD1Tables(void);
static void buildO2Tables(void);
static void buildD2Tables(void);
static void addOrigEntry(OrigEntry **oTables, int acTypeInd, int aptID, int *numEntries, time_t earlyStart);
static void addDestEntry(DestEntry **dTables, int acTypeInd, int aptID, int *numEntries, time_t lateEnd);
static void mapCommercialOrigins(OrigEntry **oTables, int *numEntries);
static void mapCommercialDestinations(DestEntry **dTables, int *numEntries);
static void buildODTable(void);
static void getOAGLists(void);
static void getOagForNewOD(ODEntry *oDEnt, OagEntry *oagList, int *prune);  
static void updateODTableAndOagLists(void);
double getCommercialFlightCost(int startAptID, int endAptID);
static void initializeODHashTables(void);
static int getODHashIndex(int orig, int dest);
static int findODIndex(int orig, int dest, ODEntry *oDTab, int *oDHash); 
static int findODIndexOrCreateNewIndex(int orig, int dest, ODEntry *oDTab, int indexValue, int *oDHash);
static ODEntry *oDEntryAlloc(int *oDCount, ODEntry **oDTab);
static int identifyAvailableLegs1 (void);
static int identifyAvailableLegs2 (void);


/********************************************************************************
*	Function   buildOagODTable				Date last modified:  8/16/07 SWO	*
*	Purpose:	Build OD table that is read in by the Optimizer	to provide		*
*				commercial travel data for getCrewTravelData functions.			*
********************************************************************************/
void buildOagODTable(void)
{
//temp testing
	//int numAptsPerNumMap[18];
	//int numCommAptsPerNumMap[18];
	//int i;
	//for(i = 0; i<18; i++){
	//	numAptsPerNumMap[i] = 0;
	//	numCommAptsPerNumMap[i] = 0;
	//}
	//fprintf(logFile,"\nNumber of Commercial Airports: %d", j);
	//fflush (logFile);
	//	numAptsPerNumMap[aptList[i].numMaps]++;
	//	k = 0;
	//	for(j = 0; j<aptList[i].numMaps; j++){
	//		if(aptList[aptList[i].aptMapping[j].airportID].commFlag == 1)
	//			k++;
	//	}
	//	numCommAptsPerNumMap[k]++;
	//}
//end temp testing

	//if we will potentially be running crew pairing, run these functions now 
	//since they will be used to determine OD Entries we need for crew pairing
	if(optParam.runType > 0 || optParam.pairingLevel < 3){
		identifyAvailableLegs1 ();
		identifyAvailableLegs2 ();
	}
	buildO1Tables();
	buildD1Tables();
	buildO2Tables();
	buildD2Tables();
	mapCommercialOrigins(o1Tables, numO1Entries);
	mapCommercialOrigins(o2Tables, numO2Entries);
	mapCommercialDestinations(d1Tables, numD1Entries);
	mapCommercialDestinations(d2Tables, numD2Entries);
	buildODTable();
	fprintf(logFile,"\n\nTotal Deviation (OD Hash)= %d.", totDeviation);
	fprintf(logFile,"\n\nNumber of ODEntries: = %d.\n\n", numNewOD);
	fflush (logFile);

	if(optParam.runType == 2)
		getOAGLists();
	else
		updateODTableAndOagLists();
	
	if(optParam.runType > 0 )  
		//RLZ: 01/27/08 Write out these tables for nightly and hourly update.
		//If it is optimizer run (optParam.runType == 0), these tables will be populated at the very end.
		writeODTable();  
	return;
}

/********************************************************************************
*	Function   buildO1Tables				Date last modified:  7/22/07 SWO	*
*	Purpose:	For each fleet, build a table of origin airports and earliest 	*
*				start times based on pilot availability.						*
********************************************************************************/
static void buildO1Tables(void)
{
	int x, y, z, c, cp, crewInd, canTravel, found;
	int *availList;
	int maxEntries[MAX_AC_TYPES];

	//initialize maxEntries & numEntries
	for(x = 0; x < MAX_AC_TYPES; x++){
		maxEntries[x] = 0;
		numO1Entries[x] = 0;
	}

	//allocate memory for availList[], which indicates if pilot might travel commercially when next available
	if((availList = calloc(numCrew, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildO1Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(c=0; c<numCrew; c++)
	{
		if(crewList[c].availDT <= optParam.windowEnd 
			&& crewList[c].tourEndTm + 24*3600*crewList[c].stayLate > optParam.windowStart
			&& crewList[c].lockHome == 0){
				availList[c] = 1;
				maxEntries[crewList[c].acTypeIndex]++;
		}
	}

	for(cp=0; cp<numCrewPairs; cp++)
	{
		if(crewPairList[cp].hasFlownFirst == 0)
			continue; //pilot may travel commercially to get to a plane
		//otherwise, pilots who are already flying a plane are typically unavailable to travel 
		//commercially to a plane unless one of the following exceptions is met
		if(crewPairList[cp].optAircraftID != crewPairList[cp].aircraftID[0])
			//crewPair is locked to a plane that is different than the plane they have been flying
			//OR crewPair has a locked tour on the plane so optAircraftID was not populated
			continue; //pilot may travel commercially to get to a plane
		//crewPair violates a hard pairing constraint, and pilots are considered for repairing
		//Simplify by conservatively assuming all pairConstraints will be considered (some need not be)
		canTravel = 0;
		for(y = 0; y<numPairConstraints; y++){
			if(pairConstraintList[y].priority == 1){
				//if crew1ID is captain and FO doesn't match any of the crew2IDs or categories, constraint is violated
				if(pairConstraintList[y].crew1ID == crewPairList[cp].captainID){
					found = 0;
					for(z= y; z< numPairConstraints; z++){
						if(pairConstraintList[y].crew2ID > 0 && pairConstraintList[y].crew2ID == crewPairList[cp].flightOffID){
							found = 1;
							break;
						}
						if(pairConstraintList[y].crew2ID <= 0 && 
							pairConstraintList[y].categoryID == crewList[crewPairList[cp].crewListInd[1]].categoryID){
							found = 1;
							break;
						}
					}
					if(found == 0){ //hard pairing constraint is violated, and crew is available for repairing
						canTravel = 1;
						break;
					}
				}
				//if crew1ID is FO and captain doesn't match any of the crew2IDs, constraint is violated
				if(pairConstraintList[y].crew1ID == crewPairList[cp].flightOffID){
					found = 0;
					for(z= y; z< numPairConstraints; z++){
						if(pairConstraintList[y].crew2ID > 0 && pairConstraintList[y].crew2ID == crewPairList[cp].captainID){
							found = 1;
							break;
						}
						if(pairConstraintList[y].crew2ID <= 0 && 
							pairConstraintList[y].categoryID == crewList[crewPairList[cp].crewListInd[0]].categoryID){
							found = 1;
							break;
						}
					}
					if(found == 0){ //hard pairing constraint is violated, and crew is available for repairing
						canTravel = 1;
						break;
					}
				}
				
				//if crew2ID is captain and FO doesn't match crew1ID, constraint is violated
				if(pairConstraintList[y].crew2ID == crewPairList[cp].captainID 
					&& pairConstraintList[y].crew1ID != crewPairList[cp].flightOffID){
						canTravel = 1;
						break;
				}
				//if crew2ID is FO and captain doesn't match crew1ID, constraint is violated
				if(pairConstraintList[y].crew2ID == crewPairList[cp].flightOffID 
					&& pairConstraintList[y].crew1ID != crewPairList[cp].captainID){
						canTravel = 1;
						break;
				}
			}
		}
		if(canTravel == 1)
			continue; //pilot may travel commercially to get to a plane
		
		//if we have reached this point, pilots are NOT available to travel commercially to a plane
		for(x = 0; x<=1; x++){
			crewInd = crewPairList[cp].crewListInd[x];
			if(availList[crewInd] == 1){
				availList[crewInd]= 0;
				maxEntries[crewList[crewInd].acTypeIndex]--;
			}
		}
	}
	//check for planes that will be going in for a long maintenance appointment, after which pilots may travel to a new plane
	for(x = 0; x<numOptDemand; x++){
		if(demandList[x].isAppoint && (demandList[x].reqIn - demandList[x].reqOut) > optParam.maintTmForReassign*60){
			maxEntries[acList[demandList[x].acInd].acTypeIndex]++;
		}
	}
	//allocate memory for o1Tables[], which store origin airports and earliest start time by fleet
	if((o1Tables = (OrigEntry **)calloc(numAcTypes, sizeof(OrigEntry *))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildO1Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(x=0; x<numAcTypes; x++){
		if((o1Tables[x] = (OrigEntry *)calloc(maxEntries[x], sizeof(OrigEntry))) == NULL){
			logMsg(logFile,"%s Line %d, Out of Memory in buildO1Tables().\n", __FILE__, __LINE__);
			exit(1);
		}
	}
	//add origin entries for long maintenance legs 
	for(x = 0; x<numOptDemand; x++){
		if(demandList[x].isAppoint && (demandList[x].reqIn - demandList[x].reqOut) > optParam.maintTmForReassign*60)
			addOrigEntry(o1Tables, acList[demandList[x].acInd].acTypeIndex, demandList[x].outAirportID, numO1Entries, (demandList[x].reqOut - 60*demandList[x].earlyAdj));
	}
	//add origin entries for pilots at next available datetime (which may already have been updated for a locked tour)
	for(c=0; c<numCrew; c++){
		if(availList[c] == 1)
			addOrigEntry(o1Tables, crewList[c].acTypeIndex, crewList[c].availAirportID, numO1Entries, crewList[c].availDT);
	}

	free(availList);
	availList = NULL;
	return;
}


/********************************************************************************
*	Function   buildD1Tables				Date last modified:  7/22/07 SWO	*
*	Purpose:	For each fleet, build a table of destination airports and		*
*				latest end times based on pilot availability.					*
********************************************************************************/
static void buildD1Tables(void)
{
	int x, c;
	int *goingHome;
	int maxEntries[MAX_AC_TYPES];

	//initialize maxEntries & numEntries
	for(x = 0; x < MAX_AC_TYPES; x++){
		maxEntries[x] = 0;
		numD1Entries[x] = 0;
	}
	//allocate memory for goingHome[], which indicates if pilot might go home within planning window range
	if((goingHome = calloc(numCrew, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildD1Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(c=0; c<numCrew; c++)
	{
		if(crewList[c].tourEndTm <= optParam.windowEnd){
			goingHome[c] = 1;
			maxEntries[crewList[c].acTypeIndex]++;
		}
	}
	//allocate memory for d1Tables[], which store destination airports and latest end time by fleet
	if((d1Tables = (DestEntry **)calloc(numAcTypes, sizeof(DestEntry *))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildD1Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(x=0; x<numAcTypes; x++){
		if((d1Tables[x] = (DestEntry *)calloc(maxEntries[x], sizeof(DestEntry))) == NULL){
			logMsg(logFile,"%s Line %d, Out of Memory in buildD1Tables().\n", __FILE__, __LINE__);
			exit(1);
		}
	}
	//add destination entries for pilots 
	for(c=0; c<numCrew; c++){
		if(goingHome[c] == 1)
			addDestEntry(d1Tables, crewList[c].acTypeIndex, crewList[c].endLoc, numD1Entries, (time_t)min((crewList[c].tourEndTm + crewList[c].stayLate*86400), optParam.windowEnd + 86400));
	}
	free(goingHome);
	goingHome = NULL;
	return;
}

/********************************************************************************
*	Function   buildO2Tables				Date last modified:  8/17/07 SWO	*
*	Purpose:	For each fleet, build a table of origin airports and			*
*				earliest start times based on demand and other legs				*
*				(potential plane locations.										*
********************************************************************************/
static void buildO2Tables(void)
{
	int x, i, day, acTypeInd;
	time_t minDpt;
	int maxEntries[MAX_AC_TYPES];

	//initialize maxEntries & numEntries
	for(x = 0; x < MAX_AC_TYPES; x++){
		maxEntries[x] = 0;
		numO2Entries[x] = 0;
	}
	for(i=0; i<numOptDemand; i++){
		for(acTypeInd = 0; acTypeInd<numAcTypes; acTypeInd++){
			if(demandList[i].blockTm[acTypeInd] != INFINITY)
				maxEntries[acTypeInd]+= 2;  //one potential entry for demand origin, and one for demand destination
		}
	}
	if(optParam.runType >0 || optParam.pairingLevel < 3){
		for(x = 0; x<numLegs; x++){
			if(legAvailable[x] > 0 && legList[x].demandID == 0)
				maxEntries[acList[legList[x].acInd].acTypeIndex]++; 
		}
	}
	//allocate memory for O2Tables[], which store origin airports and earliest end time by fleet
	if((o2Tables = (OrigEntry **)calloc(numAcTypes, sizeof(OrigEntry *))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildO2Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(x=0; x<numAcTypes; x++){
		if((o2Tables[x] = (OrigEntry *)calloc(maxEntries[x], sizeof(OrigEntry))) == NULL){
			logMsg(logFile,"%s Line %d, Out of Memory in buildO2Tables().\n", __FILE__, __LINE__);
			exit(1);
		}
	}
	//add origin entries for demand origins and destinations
	for(day = 0; day < optParam.planningWindowDuration; day++){
		for(i = (day == 0? 0 : lastTripOfDay[day-1]+ 1); i<=lastTripOfDay[day]; i++){
			for(acTypeInd = 0; acTypeInd<numAcTypes; acTypeInd++){
				
				if(demandList[i].blockTm[acTypeInd] == INFINITY)
					continue;

				//find earliest possible departure time of a pilot from a plane at this airport

				//First, consider demand origin (pilot leaves after repo)
				if(day != 0 && demandList[i].earlyMPM <= optParam.cutoffForFinalRepo) //use start of previous day
					minDpt = (time_t)(max((firstEndOfDay + (day-2)*86400), optParam.windowStart));
				else //use start of that day
					minDpt = (time_t)(max((firstEndOfDay + (day-1)*86400), optParam.windowStart));

				addOrigEntry(o2Tables, acTypeInd, demandList[i].outAirportID, numO2Entries, minDpt);
				
				//Next, consider demand destination

				//find earliest possible departure time of a pilot from a plane at this airport
				minDpt = (time_t)(60*(demandList[i].early[acTypeInd]+demandList[i].elapsedTm[acTypeInd])); 

				addOrigEntry(o2Tables, acTypeInd, demandList[i].inAirportID, numO2Entries, minDpt);
			}
		}
	}
	//we must also add entries for non-demand leg destinations that may be used as pilot origins in crew pairing, although some
	//of these may be duplicates with above (if there is a subsequent demand origin)
	if(optParam.runType > 0 || optParam.pairingLevel < 3){
		for(x = 0; x<numLegs; x++){
			if(legAvailable[x] > 0 && legList[x].demandID == 0)
				addOrigEntry(o2Tables, acList[legList[x].acInd].acTypeIndex, legList[x].inAirportID, numO2Entries, legList[x].adjSchedIn + optParam.finalPostFltTm*60);		
		}
	}
	return;
}


/********************************************************************************
*	Function   buildD2Tables				Date last modified:  8/16/07 SWO	*
*	Purpose:	For each fleet, build a table of destination airports and		*
*				latest end times based on demand legs (potential plane			*
*				locations) and next available locations for planes.				*
********************************************************************************/
static void buildD2Tables(void)
{
	int x, i, acTypeInd;

	int maxEntries[MAX_AC_TYPES];

	//initialize maxEntries & numEntries
	for(x = 0; x < MAX_AC_TYPES; x++){
		maxEntries[x] = 0;
		numD2Entries[x] = 0;
	}

	maxArr = optParam.windowStart;	//max(demand.reqOut + demand.lateAdj – optParam.firstPreFlt) across ALL demands 
									//(a pilot wouldn’t travel to a plane unless there was a subsequent demand he could cover)
	for(i=0; i<numOptDemand; i++){
		maxArr = (time_t)((demandList[i].reqOut + 60*demandList[i].lateAdj)>maxArr? (demandList[i].reqOut + 60*demandList[i].lateAdj): maxArr);
		for(acTypeInd = 0; acTypeInd<numAcTypes; acTypeInd++){
			if(demandList[i].blockTm[acTypeInd] != INFINITY)
				maxEntries[acTypeInd]+= 2;  //one potential entry for demand origin, and one for demand destination
		}
	}
	maxArr -= optParam.firstPreFltTm*60;

	for(x = 0; x<numAircraft; x++)
		maxEntries[acList[x].acTypeIndex] ++;

	if(optParam.runType >0 || optParam.pairingLevel < 3){
		for(x = 0; x<numLegs; x++){
			if(legAvailable[x] > 0 && legList[x].demandID == 0)
				maxEntries[acList[legList[x].acInd].acTypeIndex]++; 
		}
	}

	//allocate memory for d2Tables[], which store destination airports and latest end time by fleet
	if((d2Tables = (DestEntry **)calloc(numAcTypes, sizeof(DestEntry *))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildD2Tables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for(x=0; x<numAcTypes; x++){
		if((d2Tables[x] = (DestEntry *)calloc(maxEntries[x], sizeof(DestEntry))) == NULL){
			logMsg(logFile,"%s Line %d, Out of Memory in buildD2Tables().\n", __FILE__, __LINE__);
			exit(1);
		}
	}

	//add destination entries for demand origins and destinations
	for(i=0; i<numOptDemand; i++){
		for(acTypeInd = 0; acTypeInd<numAcTypes; acTypeInd++){
			
			if(demandList[i].blockTm[acTypeInd] == INFINITY)
				continue;
			
			//First, consider demand origin
			addDestEntry(d2Tables, acTypeInd, demandList[i].outAirportID, numD2Entries, (time_t)(demandList[i].reqOut+60*(demandList[i].lateAdj - optParam.firstPreFltTm)));
			
			//Next, consider demand destination
			addDestEntry(d2Tables, acTypeInd, demandList[i].inAirportID, numD2Entries, maxArr);
		}
	}
	//add destination entries for plane locations
	for(x = 0; x< numAircraft; x++)
		addDestEntry(d2Tables, acList[x].acTypeIndex, acList[x].availAirportID, numD2Entries, maxArr);
	
	//we must also add entries for non-demand leg destinations that may be used as pilot destinations in crew pairing, although some
	//of these may be duplicates with above (if there is a subsequent demand origin)
	if(optParam.runType >0 || optParam.pairingLevel < 3){
		for(x = 0; x<numLegs; x++){
			if(legAvailable[x] > 0 && legList[x].demandID == 0)
				addDestEntry(d2Tables, acList[legList[x].acInd].acTypeIndex, legList[x].inAirportID, numD2Entries, maxArr);		
		}
	}

	return;
}

/********************************************************************************
*	Function   addOrigEntry					Date last modified:  7/18/07 SWO	*
*	Purpose:	add a new entry to the O1 or O2 Tables, or modify the			*
*				existing entry if applicable.									*
********************************************************************************/
static void addOrigEntry(OrigEntry **oTables, int acTypeInd, int aptID, int *numEntries, time_t earlyStart){
	
	int high, low, mid, cond, found, y;

	high = numEntries[acTypeInd]-1;
	//if this is first, or highest, aptID in list, insert if necessary and return.
	if(high == -1 || aptID > oTables[acTypeInd][high].aptID){
		numEntries[acTypeInd]++;
		oTables[acTypeInd][high+1].aptID = aptID;
		oTables[acTypeInd][high+1].earlyStart = earlyStart;
		return;
	}
	if(aptID == oTables[acTypeInd][high].aptID){
		if(earlyStart < oTables[acTypeInd][high].earlyStart)
			oTables[acTypeInd][high].earlyStart = earlyStart;
		return;
	}
	//if this is the lowest aptID in list, insert and return
	if(aptID < oTables[acTypeInd][0].aptID){
		for(y = numEntries[acTypeInd]; y>0; y--)
			oTables[acTypeInd][y] = oTables[acTypeInd][y-1];
		oTables[acTypeInd][0].aptID = aptID;
		oTables[acTypeInd][0].earlyStart = earlyStart;
		numEntries[acTypeInd]++;
		return;
	}
	if(aptID == oTables[acTypeInd][0].aptID){
		if(earlyStart < oTables[acTypeInd][0].earlyStart)
			oTables[acTypeInd][0].earlyStart = earlyStart;
		return;
	}
	low = 0;
	found = 0;
	while(low < high - 1){
		mid = low + (high - low)/2;
		if((cond = aptID - oTables[acTypeInd][mid].aptID) < 0)
			high = mid;
		else if(cond > 0)
			low = mid;
		else{//aptID is stored at [mid]
			found = 1;
			break;
		}
	}
	if(found == 1){
		if(earlyStart < oTables[acTypeInd][mid].earlyStart)
			oTables[acTypeInd][mid].earlyStart = earlyStart;
	}
	else { //insert new origin between current low and high
		for(y = numEntries[acTypeInd]; y>high; y--)
			oTables[acTypeInd][y] = oTables[acTypeInd][y-1];
		oTables[acTypeInd][high].aptID = aptID;
		oTables[acTypeInd][high].earlyStart = earlyStart;
		numEntries[acTypeInd]++;
	}

	return;
}

/********************************************************************************
*	Function   addDestEntry					Date last modified:  7/18/07 SWO	*
*	Purpose:	add a new entry to the D1 or D2 Tables, or modify the			*
*				existing entry if applicable.									*
********************************************************************************/
static void addDestEntry(DestEntry **dTables, int acTypeInd, int aptID, int *numEntries, time_t lateEnd){
	
	int high, low, mid, cond, found, y;

	high = numEntries[acTypeInd]-1;
	//if this is first, or highest, aptID in list, insert and continue.
	if(high == -1 || aptID > dTables[acTypeInd][high].aptID){
		numEntries[acTypeInd]++;
		dTables[acTypeInd][high+1].aptID = aptID;
		dTables[acTypeInd][high+1].lateEnd = lateEnd;
		return;
	}
		if(aptID == dTables[acTypeInd][high].aptID){
		if(lateEnd > dTables[acTypeInd][high].lateEnd)
			dTables[acTypeInd][high].lateEnd = lateEnd;
		return;
	}
	//if this is the lowest aptID in list, insert and return
	if(aptID < dTables[acTypeInd][0].aptID){
		for(y = numEntries[acTypeInd]; y>0; y--)
			dTables[acTypeInd][y] = dTables[acTypeInd][y-1];
		dTables[acTypeInd][0].aptID = aptID;
		dTables[acTypeInd][0].lateEnd = lateEnd;
		numEntries[acTypeInd]++;
		return;
	}
	if(aptID == dTables[acTypeInd][0].aptID){
		if(lateEnd > dTables[acTypeInd][0].lateEnd)
			dTables[acTypeInd][0].lateEnd = lateEnd;
		return;
	}
	low = 0;
	found = 0;
	while(low < high - 1){
		mid = low + (high - low)/2;
		if((cond = aptID - dTables[acTypeInd][mid].aptID) < 0)
			high = mid;
		else if(cond > 0)
			low = mid;
		else{//aptID is stored at [mid]
			found = 1;
			break;
		}
	}
	if(found == 1){
		if(lateEnd > dTables[acTypeInd][mid].lateEnd)
			dTables[acTypeInd][mid].lateEnd = lateEnd;
	}
	else { //insert new origin between current low and high
		for(y = numEntries[acTypeInd]; y>high; y--)
			dTables[acTypeInd][y] = dTables[acTypeInd][y-1];
		dTables[acTypeInd][high].aptID = aptID;
		dTables[acTypeInd][high].lateEnd = lateEnd;
		numEntries[acTypeInd]++;
	}

	return;
}

/********************************************************************************
*	Function   mapCommercialOrigins			Date last modified:  6/27/07 SWO	*
*	Purpose:	Map each pilot origin airport (in the o1 or o2 table			*
*				that is sent to the function) to the commercial airports		*
*				that are nearby.												*
********************************************************************************/
static void mapCommercialOrigins(OrigEntry **oTables, int *numEntries)
{
	int x, j, n, z;
	AptMap *mapPnt;

	for(j = 0; j<numAcTypes; j++){
		for(x = 0; x < numEntries[j]; x++){
			//allocate memory for mappings in the oTable
			if((oTables[j][x].commOrig = (CommOrig *)calloc(aptList[oTables[j][x].aptID].numMaps, sizeof(CommOrig))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in mapCommercialOrigins().\n", __FILE__, __LINE__);
				exit(1);
			}
			z = 0;
			for(n = 0; n<aptList[oTables[j][x].aptID].numMaps; n++){
				mapPnt = &aptList[oTables[j][x].aptID].aptMapping[n];
				if(aptList[mapPnt->airportID].commFlag == 1 && !mapPnt->groundOnly){
					oTables[j][x].commOrig[z].aptID = mapPnt->airportID;
					oTables[j][x].commOrig[z].earlyFlDep = (time_t)(oTables[j][x].earlyStart 
						+ 60*(mapPnt->duration + optParam.preBoardTime));
					z++;
				}
			}
		}
	}
	return;
}

/********************************************************************************
*	Function   mapCommercialDestinations	Date last modified:  6/27/07 SWO	*
*	Purpose:	Map each pilot destination airport (in the d1 or d2 table		*
*				that is sent to the function) to the commercial airports		*
*				that are nearby.												*
********************************************************************************/
static void mapCommercialDestinations(DestEntry **dTables, int *numEntries)
{
	int x, j, n, z;
	AptMap *mapPnt;

	for(j = 0; j<numAcTypes; j++){
		for(x = 0; x < numEntries[j]; x++){
			//allocate memory for mappings in the dTable
			if((dTables[j][x].commDest = (CommDest *)calloc(aptList[dTables[j][x].aptID].numMaps, sizeof(CommDest))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in mapCommercialDestinations().\n", __FILE__, __LINE__);
				exit(1);
			}
			z = 0;
			for(n = 0; n<aptList[dTables[j][x].aptID].numMaps; n++){
				mapPnt = &aptList[dTables[j][x].aptID].aptMapping[n];
				if(aptList[mapPnt->airportID].commFlag == 1 && !mapPnt->groundOnly){
					dTables[j][x].commDest[z].aptID = mapPnt->airportID;
					dTables[j][x].commDest[z].lateFlArr = (time_t)(dTables[j][x].lateEnd 
						- 60*(mapPnt->duration + optParam.postArrivalTime));
					z++;
				}
			}
		}
	}
	return;
}


/********************************************************************************
*	Function   buildODTable					Date last modified:  8/22/07 SWO	*
*	Purpose:	Build a new table of origin and destination commercial airports	*
*				with earliest flight depart times and latest flight arrival		*
*				times.															*
********************************************************************************/
static void buildODTable(void)
{
	int acTypeInd, orig, commO, dest, commD, m ,n, index, c, x, canDrive;
	time_t tempTm;
	double cost;
	ODEntry *newODEnt;
	AptMap *mapPntO, *mapPntD;

	initializeODHashTables();

	numNewOD = 0;
	initODCount = 0;

	//for each fleet....
	for(acTypeInd = 0; acTypeInd < numAcTypes; acTypeInd++){
		//create O1-D2 entries
		for(orig = 0; orig < numO1Entries[acTypeInd]; orig++){
			m = 0;
			while((commO = o1Tables[acTypeInd][orig].commOrig[m].aptID) !=0 && (m < aptList[o1Tables[acTypeInd][orig].aptID].numMaps)){
				for(dest = 0; dest < numD2Entries[acTypeInd]; dest++){
					n = 0;
					cost = 0.0;
					while((commD = d2Tables[acTypeInd][dest].commDest[n].aptID) != 0 
						&& (n < aptList[d2Tables[acTypeInd][dest].aptID].numMaps)){

						if(commO != commD){
							x = 0;
							canDrive = 0;
							while(x < aptList[commO].numMaps){
								if(aptList[commO].aptMapping[x].airportID == commD){
									canDrive = aptList[commO].aptMapping[x].duration;
									break;
								}
								x++;
							}
							if(canDrive == 0 || canDrive > 40){
								if((index = findODIndexOrCreateNewIndex(commO, commD, newODTable, numNewOD, newODHashTable)) == numNewOD){
									//if we have found a new OD entry
									newODEnt = oDEntryAlloc(&numNewOD, &newODTable);
									newODEnt->commOrAptID = commO;
									newODEnt->commDestAptID = commD;
									newODEnt->earlyDpt = o1Tables[acTypeInd][orig].commOrig[m].earlyFlDep;
									newODEnt->lateArr = d2Tables[acTypeInd][dest].commDest[n].lateFlArr;
									//assume that commercial flight cost between commO and any of the airports mapped to dest is close enough
									if(cost == 0.0)
										cost = getCommercialFlightCost(commO, commD);
									newODEnt->cost = cost;
								}
								else{
									newODTable[index].earlyDpt = min(newODTable[index].earlyDpt, o1Tables[acTypeInd][orig].commOrig[m].earlyFlDep);
									newODTable[index].lateArr = max(newODTable[index].lateArr, d2Tables[acTypeInd][dest].commDest[n].lateFlArr);
								}
							}
						}
						n++;
					}  //end while((commD = 
				}  //end for(dest = 0
				m++;
			}  //end while((commO = 
		}  //end for(orig = 
		
		//create 02-D1 entries
		for(orig = 0; orig < numO2Entries[acTypeInd]; orig++){
			m = 0;
			while((commO = o2Tables[acTypeInd][orig].commOrig[m].aptID) !=0 && (m < aptList[o2Tables[acTypeInd][orig].aptID].numMaps)){
				for(dest = 0; dest < numD1Entries[acTypeInd]; dest++){
					n = 0;
					cost = 0.0;
					while((commD = d1Tables[acTypeInd][dest].commDest[n].aptID) != 0
						&& (n < aptList[d1Tables[acTypeInd][dest].aptID].numMaps)){
						
						if(commO != commD){
							x = 0;
							canDrive = 0;
							while(x < aptList[commO].numMaps){
								if(aptList[commO].aptMapping[x].airportID == commD){
									canDrive = aptList[commO].aptMapping[x].duration;
									break;
								}
								x++;
							}
							if(canDrive == 0 || canDrive > 40){
								if((index = findODIndexOrCreateNewIndex(commO, commD, newODTable, numNewOD, newODHashTable)) == numNewOD){
									//if we have found a new OD entry
									newODEnt = oDEntryAlloc(&numNewOD, &newODTable);
									newODEnt->commOrAptID = commO;
									newODEnt->commDestAptID = commD;
									newODEnt->earlyDpt = o2Tables[acTypeInd][orig].commOrig[m].earlyFlDep;
									newODEnt->lateArr = d1Tables[acTypeInd][dest].commDest[n].lateFlArr;
									//assume that commercial flight cost between commO and any of the airports mapped to dest is close enough
									if(cost == 0.0)
										cost = getCommercialFlightCost(commO, commD);
									newODEnt->cost = cost;
								}
								else{
									newODTable[index].earlyDpt = min(newODTable[index].earlyDpt, o2Tables[acTypeInd][orig].commOrig[m].earlyFlDep);
									newODTable[index].lateArr = max(newODTable[index].lateArr, d1Tables[acTypeInd][dest].commDest[n].lateFlArr);
								}
							}
						}
						n++;
					}
				}  //end for(dest = 0
				m++;
			}  //end while((commO = 
		}  //end for(orig = 

	}  //end for(acTypeInd=

	//create entries for pilots located away from home who might travel home within window
	for(c=0; c<numCrew; c++){
		if(crewList[c].tourEndTm <= optParam.windowEnd && crewList[c].availAirportID != crewList[c].endLoc){
			//assume commercial ticket cost estimation using distance between pilot available and end locations is good enough
			cost = getCommercialFlightCost(crewList[c].availAirportID, crewList[c].endLoc);
			for(m = 0; m<aptList[crewList[c].availAirportID].numMaps; m++){
				mapPntO = &aptList[crewList[c].availAirportID].aptMapping[m];
				if(aptList[mapPntO->airportID].commFlag == 1 && !mapPntO->groundOnly){
					commO = mapPntO->airportID;
					for(n = 0; n<aptList[crewList[c].endLoc].numMaps; n++){
						mapPntD = &aptList[crewList[c].endLoc].aptMapping[n];
						if(aptList[mapPntD->airportID].commFlag == 1 && !mapPntD->groundOnly){
							commD = mapPntD->airportID;
							if(commO != commD){
								x = 0;
								canDrive = 0;
								while(x < aptList[commO].numMaps){
									if(aptList[commO].aptMapping[x].airportID == commD){
										canDrive = aptList[commO].aptMapping[x].duration;
										break;
									}
									x++;
								}
								if(canDrive == 0 || canDrive > 40){
									if((index = findODIndexOrCreateNewIndex(commO, commD, newODTable, numNewOD, newODHashTable)) == numNewOD){
										//if we have found a new OD entry
										newODEnt = oDEntryAlloc(&numNewOD, &newODTable);
										newODEnt->commOrAptID = commO;
										newODEnt->commDestAptID = commD;
										newODEnt->earlyDpt = crewList[c].availDT +
											60*(aptList[crewList[c].availAirportID].aptMapping[m].duration + optParam.preBoardTime);
										newODEnt->lateArr = (time_t)min((crewList[c].tourEndTm + crewList[c].stayLate*86400), optParam.windowEnd + 86400)
											- 60*(aptList[crewList[c].endLoc].aptMapping[n].duration + optParam.postArrivalTime);
										newODEnt->cost = cost;
									}
									else{
										tempTm = crewList[c].availDT + 60*(aptList[crewList[c].availAirportID].aptMapping[m].duration + optParam.preBoardTime);
										newODTable[index].earlyDpt = min(newODTable[index].earlyDpt, tempTm);
										tempTm = (time_t)min((crewList[c].tourEndTm + crewList[c].stayLate*86400), optParam.windowEnd + 86400)
											- 60*(aptList[crewList[c].endLoc].aptMapping[n].duration + optParam.postArrivalTime);
										newODTable[index].lateArr = max(newODTable[index].lateArr, tempTm);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	//if this is an Oag pre-process run, oDTable should now point 
	//to newODTable, and oDHashTable to newODHashTable 
	if(optParam.runType == 2){
		oDTable = newODTable;
		oDHashTable = newODHashTable;  
		numOD = numNewOD;
	}

	//free and null memory allocated for O1, O2, D1, and D2 tables
	for(n = 0; n<numAcTypes; n++){
		free(o1Tables[n]);
		o1Tables[n] = NULL;
		free(o2Tables[n]);
		o2Tables[n] = NULL;
		free(d1Tables[n]);
		d1Tables[n] = NULL;
		free(d2Tables[n]);
		d2Tables[n] = NULL;
	}
	free(o1Tables);
	o1Tables = NULL;
	free(o2Tables);
	o2Tables = NULL;
	free(d1Tables);
	d1Tables = NULL;
	free(d2Tables);
	d2Tables = NULL;

	return;
}


/********************************************************************************************************
*	Function	getOAGLists											Date last modified:  08/01/07 SWO	*
*	Purpose:	Populate the OAG lists (commercial itineraries) in the new OD Table						*																		*
********************************************************************************************************/
static void getOAGLists(void)
{
	int i;
	OagEntry oagList[MAX_OAG_PER_OD];
	int prune[MAX_OAG_PER_OD];

	//initialize oagList and prune array
	for(i = 0; i < MAX_OAG_PER_OD; i++){
		oagList[i].arrTm = 0;
		oagList[i].dptTm = 0;
		oagList[i].connAptID = 0;
		prune[i] = 0;
	}

	logMsg(logFile,"** Start getOagForNewOD\n");
	for(i = 0; i<numNewOD; i++)
		getOagForNewOD(&newODTable[i], oagList, prune); 

	fprintf(logFile,"\nNumber of OD Entries with no directs: %d.", numODnoDirects);
	fprintf(logFile,"\nNumber of One-Stop Queries (few or no directs): %d.", numOneStopQueries);
	fprintf(logFile,"\nNumber of OD Entries with no itineraries: %d.", numODnoItins);
	fprintf(logFile,"\nMax number of oag per OD before pruning: %d.", maxOag);
	fprintf(logFile,"\nTotal Direct Itineraries: %d.", totDirects);
	fprintf(logFile,"\nTotal One-Stop Itineraries: %d.", totOneStops);
	fprintf(logFile,"\nTotal Pruned Itineraries: %d.\n\n", totPruned);
	logMsg(logFile,"** End getOagForNewOD\n\n");
		
	return;
}

/********************************************************************************************************
*	Function	getOagForNewOD										Date last modified:  08/10/07 SWO	*																	*
********************************************************************************************************/
static void getOagForNewOD(ODEntry *oDEnt, OagEntry *oagList, int *prune) 

{
	time_t bestArr, currArr;
	int n, m;
	int numOag = 0;
	int numPrune = 0;


	//query oag for direct and one-stop and populate oagList.  set numOAG to number of itineraries for this OD	
	getNewODOAG(oDEnt, oagList, &numOag); // CSHOpt_calculateODandOAG.c:getNewODOAG() doesn't have numPrune parameter

	if(numOag == 0)
		numODnoItins++;
	if(numOag > MAX_OAG_PER_OD) {
		logMsg(logFile,"%s Line %d, Exceeded MAX_OAG_PER_OD in getOagForNewOD().\n", __FILE__, __LINE__);
		numOag = MAX_OAG_PER_OD;
		//exit(1);
	}
	if(numOag > maxOag)
		maxOag = numOag;
	 
    //prune out "bad" itineraries and update numOag
	bestArr = oagList[numOag-1].arrTm;
	for(n = numOag - 2; n >= 0; n--){
		//if the current entry has the same or later arrival than an entry we have already looked at
		//(the latter must have the same or later departure due to sort), then we can prune the current entry
		if((currArr = oagList[n].arrTm) >= bestArr){
			prune[n] = 1;
			numPrune++;
		}
		else
			bestArr = currArr;
	}
	
	//allocate memory for oagList in the oDTable
	if((oDEnt->oagList = (OagEntry *)calloc((numOag - numPrune), sizeof(OagEntry))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in getOagForNewOD().\n", __FILE__, __LINE__);
		exit(1);
	}
	totPruned += numPrune;
	//copy oagList to oDTable
	oDEnt->numOag = (numOag - numPrune);
	m = 0;
	for(n = 0; n<numOag; n++){
		if(!prune[n]){
			oDEnt->oagList[m] = oagList[n];
			//Jintao's fix
			if (oagList[n].arrTm <0)
              oagList[n].arrTm=0;
			if (oagList[n].dptTm <0)
              oagList[n].dptTm=0;
		    //Jintao's fix					
			m++;
		}
	}
//SONote:  we probably don't need to clean out oagList if we are careful with numOag, 
//but we do need to clean out prune
	//cleanout oagList
	for(n = 0; n < numOag; n++){
		//oagList[n].arrTm = 0;
		//oagList[n].dptTm = 0;
		//oagList[n].connAptID = 0;
		prune[n] = 0;
	}
	return;
}


/********************************************************************************************************
*	Function	updateODTableAndOagLists							Date last modified:  08/10/07 SWO	*
*	Purpose:	Compare the newODTable to the (old)oDTable.  Expand the(old)ODTable as required,		*
*				getting additional OAG lists and entries as needed										*
********************************************************************************************************/
static void updateODTableAndOagLists(void)
{
	int i, hashIndex, exgInd, n, m, numOag, numPrune;
	ODEntry *newODEnt, *oDEnt, *exgOD;
	OagEntry oagList[MAX_OAG_PER_OD];
	int prune[MAX_OAG_PER_OD];
	time_t bestArr, currArr;

	logMsg(logFile,"** Start getOagForNewOD\n");

	//we have a hash table (newODHashTable) for the newODTable that we have just created, but we need
	//to populate the hash table for the(old)oDTable that was read in with the input data
	for(i = 0; i<numOD; i++){
		hashIndex = getODHashIndex(oDTable[i].commOrAptID, oDTable[i].commDestAptID);
		while ((oDHashTable[hashIndex] != -1)){
			hashIndex++;
			if (hashIndex == OD_HASH_SIZE)
				hashIndex = 0;
		}
		oDHashTable[hashIndex] = i;
	}

	//initialize oagList and prune array
	for(n = 0; n < MAX_OAG_PER_OD; n++){
		oagList[n].arrTm = 0;
		oagList[n].dptTm = 0;
		oagList[n].connAptID = 0;
		prune[n] = 0;
	}
	initODCount = numOD;

	//compare newODTable to oDTable, and update oDTable (and oDHashTable) as required
	for(i = 0; i<numNewOD; i++){
		oDEnt = &newODTable[i];
		
		//if this ODEntry entry has been found in the existing table...
	 if((exgInd = findODIndex(oDEnt->commOrAptID, oDEnt->commDestAptID, oDTable, oDHashTable))!= -1){
           exgOD = &oDTable[exgInd];
			
			//if this newODEntry approx. matches (or encompasses a smaller window than) that in the existing oDTable, do nothing
		   if(difftime(exgOD->earlyDpt, oDEnt->earlyDpt) <= 60 && difftime(oDEnt->lateArr, exgOD->lateArr) <= 60){
				numSameOD++;
				continue;
		   }

			//if this newODEntry starts earlier, or ends later, than that in the existing oDTable, 
			//we must update the existing table. Retrieve additional oag records, sort, prune, and copy.

			 //if there are no entries in the exg oagList
			if(exgOD->numOag == 0){
				exgOD->earlyDpt = min(exgOD->earlyDpt, oDEnt->earlyDpt);
				exgOD->lateArr = max(exgOD->lateArr, oDEnt->lateArr);
				getOagForNewOD(exgOD, oagList, prune); 
				continue;
			}       

			//if this newODEntry starts earlier than that in the exg ODTable....
			if(difftime(exgOD->earlyDpt, oDEnt->earlyDpt) > 60)
			{
				numOag = 0;
				numPrune = 0;
				
				//query OAG2 for direct itineraries such that (oDEnt->earlyDpt <= departure time < exgOD->earlyDpt)
				//and populate oagList.  set numOAG to number of itineraries retrieved	
				updatesODOAG_earlydpt(oDEnt, exgOD, oagList, &numOag);

				if(numOag > MAX_OAG_PER_OD) {
					logMsg(logFile,"%s Line %d, Exceeded MAX_OAG_PER_OD in updateODTableAndOagLists().\n", __FILE__, __LINE__);
					numOag = MAX_OAG_PER_OD;
					//exit(1);
				}
				if(numOag > maxOag)
					maxOag = numOag;
				
				exgOD->earlyDpt = oDEnt->earlyDpt;
				
				if(numOag > 0){
					//prune out "bad" itineraries and update numOag
					bestArr = exgOD->oagList[0].arrTm;
					for(n = numOag-1; n >= 0; n--){
						//if the current entry has the same or later arrival than an entry we have already looked at
						//(the latter must have the same or later departure due to sort), then we can prune the current entry
						if((currArr = oagList[n].arrTm) >= bestArr){
							prune[n] = 1;
							numPrune++;
						}
						else
							bestArr = currArr;
					}	
					if((numOag - numPrune) > 0){
						//re-allocate memory for oagList in the oDTable
						if((exgOD->oagList = (OagEntry *)realloc((exgOD->oagList),(exgOD->numOag)*sizeof(OagEntry) + (numOag - numPrune)* sizeof(OagEntry))) == NULL) {
							logMsg(logFile,"%s Line %d, Out of Memory in updateODTableAndOagLists().\n", __FILE__, __LINE__);
							exit(1);
						}
						//shift oagList in oDTable
						for(n = exgOD->numOag - 1; n >= 0; n--)
							exgOD->oagList[n+numOag-numPrune] = exgOD->oagList[n];
						//copy oagList to start of oDTable
						exgOD->numOag += (numOag - numPrune);
						m = 0;
						for(n = 0; n<numOag; n++){
							if(!prune[n]){
								exgOD->oagList[m] = oagList[n];
								//Jintao's fix
								if (oagList[n].arrTm <0)
                                    oagList[n].arrTm=0;
								if (oagList[n].dptTm <0)
                                    oagList[n].dptTm=0;
								//Jintao's fix
								m++;
							}
						}
					}
					//SONote:  we probably don't need to clean out oagList if we are careful with numOag, 
					//but we do need to clean out prune
						//cleanout oagList
					for(n = 0; n < numOag; n++)
					{
						//oagList[n].arrTm = 0;
						//oagList[n].dptTm = 0;
						//oagList[n].connAptID = 0;
						prune[n] = 0;
					}
				}
			}
			//if this newODEntry ends later than that in the exg ODTable....
			if(difftime(oDEnt->lateArr, exgOD->lateArr) > 60)
			{
				numOag = 0;
				numPrune = 0;
				
				//query OAG2 for direct itineraries such that (oDEnt->lateArr > arrival time > exgOD->lateArr)
				//and populate oagList.  set numOAG to number of itineraries retrieved			
        		updatesODOAG_latearr(oDEnt, exgOD, oagList, &numOag);

				if(numOag > MAX_OAG_PER_OD) {
					logMsg(logFile,"%s Line %d, Exceeded MAX_OAG_PER_OD in updateODTableAndOagLists().\n", __FILE__, __LINE__);
					numOag = MAX_OAG_PER_OD;
					//exit(1);
				}
				if(numOag > maxOag)
					maxOag = numOag;

				exgOD->lateArr = oDEnt->lateArr;

				if(numOag > 0){
					//prune out "bad" itineraries and update numOag
					bestArr = oagList[numOag-1].arrTm;
					for(n = numOag-2; n >= 0; n--){
						//if the current entry has the same or later arrival than an entry we have already looked at
						//(the latter must have the same or later departure due to sort), then we can prune the current entry
						if((currArr = oagList[n].arrTm) >= bestArr){
							prune[n] = 1;
							numPrune++;
						}
						else
							bestArr = currArr;
					}
					n =0;
					//if a new entry departs earlier than an existing entry (which must arrive earlier), 
					//then we can prune the new entry
					while(oagList[n].dptTm <= exgOD->oagList[exgOD->numOag - 1].dptTm && n < numOag){
						if(prune[n] == 0){
							prune[n] = 1;
							numPrune++;
						}
						n++;
					}
					if((numOag-numPrune) > 0){

						//re-allocate memory for oagList in the oDTable if necessary
						if((exgOD->oagList = (OagEntry *)realloc((exgOD->oagList),(exgOD->numOag + numOag - numPrune)* sizeof(OagEntry))) == NULL) {
							logMsg(logFile,"%s Line %d, Out of Memory in updateODTableAndOagLists().\n", __FILE__, __LINE__);
							exit(1);
						}
						//copy oagList to end of oDTable
						m = exgOD->numOag;
						exgOD->numOag += (numOag - numPrune);
						for(n = 0; n<numOag; n++){
							if(!prune[n]){
								exgOD->oagList[m] = oagList[n];
								//Jintao's fix
								if (oagList[n].arrTm <0)
                                    oagList[n].arrTm=0;
								if (oagList[n].dptTm <0)
                                    oagList[n].dptTm=0;
								//Jintao's fix
									m++;
							}
						}
					}
					//SONote:  we probably don't need to clean out oagList if we are careful with numOag, 
					//but we do need to clean out prune
						//cleanout oagList
						//for(n = 0; n < numOag; n++){ MAX_OAG_PER_OD
					for(n = 0; n < numOag; n++){
							//oagList[n].arrTm = 0;
							//oagList[n].dptTm = 0;
							//oagList[n].connAptID = 0;
							prune[n] = 0;
					}
				}
			}	
		}
		//else if this ODEntry does not exist in the existing oDTable, we must add it
		
     else { //exgInd == -1
			newODEnt = oDEntryAlloc(&numOD, &oDTable);
			newODEnt->commOrAptID = oDEnt->commOrAptID;
			newODEnt->commDestAptID = oDEnt->commDestAptID;
			newODEnt->earlyDpt = oDEnt->earlyDpt;
			newODEnt->lateArr = oDEnt->lateArr;
			newODEnt->cost = oDEnt->cost;
//RLZ 02/27/2009 Hash OAG BUG
            hashIndex = getODHashIndex(oDEnt->commOrAptID, oDEnt->commDestAptID);
			while ((oDHashTable[hashIndex] != -1)){
				hashIndex++;
			if (hashIndex == OD_HASH_SIZE)
				hashIndex = 0;
			}
			oDHashTable[hashIndex] = numOD-1;

			//getOAGLists
			getOagForNewOD(newODEnt, oagList, prune);
		}
	}
	fprintf(logFile, "\nNumber of unchanged OD Entries:  %d.", numSameOD);
	fprintf(logFile,"\nNumber of new /expanded OD Entries with no directs: %d.", numODnoDirects);
	fprintf(logFile,"\nNumber of One-Stop Queries (few or no directs): %d.", numOneStopQueries);
	fprintf(logFile,"\nNumber of new / expanded OD Entries with no itineraries: %d.", numODnoItins);
	fprintf(logFile,"\nMax number of  new oag per OD before pruning: %d.", maxOag);
	fprintf(logFile,"\nTotal Direct Itineraries Retrieved: %d.", totDirects);
	fprintf(logFile,"\nTotal One-Stop Itineraries Retrieved: %d.", totOneStops);
	fprintf(logFile,"\nTotal Pruned Itineraries: %d.\n\n", totPruned);
	logMsg(logFile,"** End updateODTableAndOagLists\n\n");

	//free newODTable and newODHashTable
	free(newODTable); //is this okay?
	newODTable = NULL;
	free(newODHashTable);
	newODHashTable = NULL;

	return;
}



/********************************************************************************************************
*	Function	getCommercialFlightCost								Date last modified:  05/05/06 SWO	*
*	Purpose:	for a given pair of airports, estimate cost of a commercial flight						*																		*
********************************************************************************************************/
double getCommercialFlightCost(int startAptID, int endAptID)
{
	double cost, temp, dist;
	AirportLatLon *start_all, *end_all;
	char writetodbstring1[200];

	cost = 0.0;

	if((start_all = getAirportLatLonInfoByAptID(startAptID)) == NULL) 
	{
		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, startAptID);
        sprintf(writetodbstring1, "%s Line %d, airportID %d not found\n", __FILE__,__LINE__, startAptID);
        if(errorNumber==0)
	      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getCommercialFlightCost().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
		      }
		  }
	    else
		  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getCommercialFlightCost().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
	          }
		  }
		initializeWarningInfo(&errorinfoList[errorNumber]);
        errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
        strcpy(errorinfoList[errorNumber].group_name,"group_airport");
		errorinfoList[errorNumber].airportid=startAptID;
		sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
		errorinfoList[errorNumber].format_number=21;
        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
        errorNumber++;
		writeWarningData(myconn); exit(1);
	}
	if((end_all = getAirportLatLonInfoByAptID(endAptID)) == NULL)
	{
		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, endAptID);
		sprintf(writetodbstring1, "%s Line %d, airportID %d not found\n", __FILE__,__LINE__, endAptID);
        if(errorNumber==0)
	      {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getCommercialFlightCost().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
		      }
		  }
	    else
		  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		      {logMsg(logFile,"%s Line %d, Out of Memory in getCommercialFlightCost().\n", __FILE__,__LINE__);
		       writeWarningData(myconn); exit(1);
	          }
		  }
		initializeWarningInfo(&errorinfoList[errorNumber]);
        errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
        strcpy(errorinfoList[errorNumber].group_name,"group_airport");
		errorinfoList[errorNumber].airportid=endAptID;
		sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
		sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
		errorinfoList[errorNumber].format_number=21;
        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
        errorNumber++;
		writeWarningData(myconn); exit(1);
	}
	//calculate distance between origin and destination in miles   (3959 is earth's radius in miles)
	temp = (double) cos(start_all->lat * M_PI / 180.0) * cos(end_all->lat * M_PI / 180.0) * cos((start_all->lon - end_all->lon) * M_PI / 180.0) + sin(start_all->lat * M_PI / 180.0) * sin(end_all->lat * M_PI / 180.0);
	dist = (3959 * atan(sqrt(1 - temp * temp) / temp)); 
	//calculate approximate commercial flight cost given the distance
	cost = optParam.ticketCostFixed + dist*optParam.ticketCostVar;
	return cost;
}


/************************
*  HASH TABLE FUNCTIONS	*
************************/

static void initializeODHashTables(void)
{
	int i;

	//allocate memory for newODHashTable[]
	if((newODHashTable = calloc(OD_HASH_SIZE, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in initializeODHashTables().\n", __FILE__, __LINE__);
		exit(1);
	}
	for (i = 0 ; i < OD_HASH_SIZE; i++)
		newODHashTable[i] = -1;
	if(optParam.runType < 2){
		//allocate memory for oDHashTable[]
		if((oDHashTable = calloc(OD_HASH_SIZE, sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in initializeODHashTables().\n", __FILE__, __LINE__);
			exit(1);
		}
		for (i = 0; i < OD_HASH_SIZE; i++)
			oDHashTable[i] = -1;
	}
}

static int getODHashIndex(int orig, int dest)
{
	int index;
	index = (orig*1000 + dest)%149993;
	return index;
}



/************************************************************************************
*	Function   findODIndex						Date last modified:  7/07/07 SWO	*
*	Purpose:  This function finds the index of the entry in the (old) or newODTable	*
************************************************************************************/
static int findODIndex(int orig, int dest, ODEntry *oDTab, int *oDHash) 
{
	int hashIndex;
	hashIndex = getODHashIndex(orig, dest);
	while ((oDHash[hashIndex] != -1)){
		if (orig == oDTab[oDHash[hashIndex]].commOrAptID && dest == oDTab[oDHash[hashIndex]].commDestAptID)
			return oDHash[hashIndex];
		else
			hashIndex++;
		if (hashIndex == OD_HASH_SIZE)
			hashIndex = 0;
	}
	return -1;
}

/************************************************************************************
*	Function   findODIndexOrCreateNewIndex		Date last modified:  7/07/07 SWO	*
*	Purpose:  This function finds the index of the entry in the (old) or newODTable	*
*			if one exists, otherwise it returns the next OD index and inputs the 	*
*			value in the OD Hash Table.												*
*			(A combination of the three preceding functions)						*
************************************************************************************/
static int findODIndexOrCreateNewIndex(int orig, int dest, ODEntry *oDTab, int indexValue,int *oDHash)
{
	int hashIndex, deviation, originalHash;
//	static int maxDeviation = 0; 
	
	hashIndex = getODHashIndex(orig, dest);
	originalHash = hashIndex;
	deviation = 0;
	while ((oDHash[hashIndex] != -1)){
		if (orig == oDTab[oDHash[hashIndex]].commOrAptID && dest == oDTab[oDHash[hashIndex]].commDestAptID)
			return oDHash[hashIndex];
		else
			hashIndex++;
			deviation++;
		if (hashIndex == OD_HASH_SIZE)
			hashIndex = 0;
//		if (maxDeviation < deviation){
//			maxDeviation = deviation;
//			if (maxDeviation%10 == 0)
//				fprintf(logFile, "\nMaxDeviation (OD) now %d -- orig %d, dest %d(%d,%d).",maxDeviation, orig, dest ,originalHash,hashIndex);
//		}
	}
	//if we have not found the entry, then we input the new indexValue in the hash table and return the new indexValue
	oDHash[hashIndex] = indexValue; 
//testing
	totDeviation += deviation;
	if(deviation > 10)
		fprintf(logFile,"\nDeviation (OD)= %d -- orig %d, dest %d, hashIndex %d, oDTab index %d.", deviation, orig, dest ,hashIndex, indexValue);
//end testing

	return indexValue;
}


/********************************************************************************
*	Function   oDEntryAlloc					Date last modified:	08/09/07 SWO	*
*	Purpose:  	dynamically allocate memory for the newODTable or (old) oDTable	*
********************************************************************************/
static ODEntry *
oDEntryAlloc(int *oDCount, ODEntry **oDTab)
{
	ODEntry *oDPtr;

	if(!(*oDTab)) {
		// nothing has been allocated yet
		(*oDTab) = (ODEntry *) calloc(ODEntryAllocChunk, sizeof(ODEntry));
		if(!(*oDTab)) {
			logMsg(logFile,"%s Line %d, Out of Memory in ODEntryAlloc().\n", __FILE__, __LINE__);
			exit(1);
		}
		oDPtr = (*oDTab);
		(*oDCount)++;
		return(oDPtr);
	}
	if((!((*(oDCount) - initODCount) % ODEntryAllocChunk))) {
		// time to realloc
		(*oDTab) = (ODEntry *) realloc((*oDTab),(*oDCount * sizeof(ODEntry)) + (ODEntryAllocChunk * sizeof(ODEntry)));
		if(!(*oDTab)) {
			logMsg(logFile,"%s Line %d, Out of Memory in ODEntryAlloc().\n", __FILE__, __LINE__);
			exit(1);
		}
	}
	// return the next pre-allocated ODEntry
	oDPtr = (*oDTab);
	oDPtr += (*oDCount);
	(*oDCount)++;
	memset(oDPtr,'\0',sizeof(ODEntry));
	return(oDPtr);
}


/************************
*  PAIR CREWS FUNCTIONS	*
************************/

/************************************************************************************************
*	Function	identifyAvailableLegs1						Date last modified:  8/01/06 BGC	*
*	Purpose:	Finds all legs (excluding maintenance) at the end of which a crew swap could	*
*				potentially occur.																*
************************************************************************************************/

static int
identifyAvailableLegs1 (void)
{
	int i, j, k;

	if ((legAvailable = (int *) calloc (numLegs, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in identifyAvailableLegs1().\n", __FILE__,__LINE__);
		exit(1);
	}

	for (i=0; i<numLegs; i++)
	{
		legAvailable[i] = 0;
		if (legList[i].planeLocked)
		{
			legAvailable[i] = 1; 
			/*
			*	If a leg is locked, its destination is a potential candidate for a crew swap
			*	since the location of the plane is precisely known.
			*/
		}
		else 
		{
			for (k=0; k<numCrewPairs; k++)
			{
				if ((crewPairList[k].optAircraftID > 0) && (legList[i].aircraftID == crewPairList[k].optAircraftID)
					&& (legList[i].crewPairID == crewPairList[k].crewPairID))
				{
					legAvailable[i] = 2;
					/*
					*	If a crew is flying the plane at the start of the planning window, then legs along its entire
					*	tour are crew swap candidates. This will be adjusted later to prohibit stealing planes.
					*/
				}
			}
		}
	}

	/* 
	*	At the end of the above bit of code, we have identified all legs after which crew swaps could potentially
	*	occur:
	*	1.	At the destination of a locked leg.
	*	2.	At the destination of a leg currently scheduled to be flown by a crew that is locked to a plane. 
	*		Even though	this last case doesn't happen with certainty since the optimizer could change leg 
	*		assignments, it is very likely to happen since the crew-plane assignment doesn't change and there is 
	*		a penalty for re-assigning legs. So, the pilot pairer will assume that legs scheduled to be flown by
	*		a crew that is currently flying a plane will NOT change and evaluate crews that swap out the existing
	*		crew.
	*/



	/*
	*	A leg may be available for (an optimized) crew swap only if there 
	*	is no leg in the future that is locked to the same plane AND any crew.
	*/
	for (i=numLegs-1; i>0; i--)
	{
		if ((legList[i].crewLocked) && (legList[i].planeLocked))
		{
			for (j=i-1; j>=0; j--)
			{
				if (legList[i].aircraftID == legList[j].aircraftID)
				{
					legAvailable[j] = -1;
				}
			}
		}
	}

	return 0;

}


/************************************************************************************************
*	Function	identifyAvailableLegs2						Date last modified:  05/04/07 BGC	*
*	Purpose:	Removes legs from available list if they are before the last regular day		*
*				of the crew's tour.																*
************************************************************************************************/

static int
identifyAvailableLegs2 (void)
{
	int i, cpind;

	for (i=0; i<numLegs; i++)
	{
		cpind = legList[i].crewPairInd;
		if ((optParam.prohibitStealingPlanes) && (cpind >= 0))
		{// If the crew pair exists and prohibit stealing planes
			if((firstEndOfDay + crewPairList[cpind].endRegDay * 86400 -  legList[i].adjSchedIn) > 86400)
			{// If the crew has more than a day left in its regular tour, leg is not available.
				legAvailable[i] = -1;
			}
		}
	}
	return 0;
}

