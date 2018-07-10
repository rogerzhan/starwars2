#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>//fei Jan 2011
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
#include "CSHOpt_define.h"
#include "CSHOpt_arcs.h"
#include "CSHOpt_callOag.h"
#include "CSHOpt_tours.h"
#include "memory.h"
#include "CSHOpt_output.h"

#define bounds(idx,count) ((idx >= 0 && idx < count) ? idx : stoppit(__FILE__,__LINE__,idx, count))

extern FILE *logFile;
extern int month, lastTripOfDay[MAX_WINDOW_DURATION];
extern struct optParameters optParam;
extern Demand *demandList;
extern AircraftType *acTypeList;
extern Aircraft *acList;
extern int netMaxDutyTm;
extern int netMaxDutyTmEarly;
extern int numAcTypes;
extern int numAircraft;
extern Duty **dutyList;
extern Crew *crewList;
extern CrewPair *crewPairList;
extern Leg *legList;
extern int numLegs;
extern int numDemand;
extern int numOptDemand;
extern int numCrew;
extern int numOptCrewPairs;
extern int numAircraft;
extern int numOAGCallsEarly;
extern int numOAGCallsLate;
extern int maxTripsPerDuty;
extern time_t firstEndOfDay;
extern time_t firstMidnight;
//extern int lastTripOfDay[MAX_WINDOW_DURATION]; //fei FA
extern struct listMarker dutyTally[MAX_AC_TYPES][MAX_WINDOW_DURATION][9];
extern int potCrewStarts[MAX_AC_TYPES][MAX_WINDOW_DURATION];
extern int potCrewEnds[MAX_AC_TYPES][MAX_WINDOW_DURATION];
extern int **aptCurf; //stores airport curfews (first (row) index is airport, first column is number of curfews, remaining cols are start and end of curfews
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;
extern int withOag;
extern int multiCrewAC;
extern OrigDemInfo *origDemInfos; //fei Jan 2011 - original demand list
extern int numOrigDem; //fei Jan 2011

AcGroup *acGroupList;
int numAcGroups;

int dutyArcListTally[MAX_AC_TYPES][MAX_WINDOW_DURATION - 1][MAX_PLANES_PER_FLEET + 1]; 
/*The value of dutyArcListTally[acTypeInd][day][0] stores the number of arc lists that must be generated out of each typical duty node on that day for that aircraftType
//The value of dutyArcListTally[acTypeInd][day][x] stores the acList index or (-)acGroup index of the plane(s) that will use arcList [x] out of those duty nodes
//arcList[0] is reserved for the fleet.  */

int countsPerArcList[MAX_PLANES_PER_FLEET + 1];
int numSetsSpecConnConstr;
int separateNWByFleet[MAX_AC_TYPES][MAX_PLANES_PER_FLEET];
int numSepNWByFleet[MAX_AC_TYPES];

int **pickupTripList; //one list for each fleet plus one for each set of special connection constraints
	//first index is for fleet and plane or plane group;  second index is demandList index. 
	//integer value stored in array = 1 for can pickup at start of trip (repo node), = 2 for can pickup at end of trip, 
	//= 3 for can pickup at both (3 = 1 + 2), and = 0 if can't pick up there.
int **puTripListInfo;  // array with the acList Index or -acGroupIndex (-1 for a fleet, in which case array index = acTypeList index), 
				//number of "pickup at start" trips and number of "pickup at end" trips for each pickupTripList 
				//Note that puSDual and puEDual for trips are stored in an array according to these same fleet/plane indices
int numPlaneArcs;
int numPickupArcs;
int numDutyArcCopies;
int numDutyArcs;
int numCrewPlaneArcs;
int numCrewPickupArcs;
int numArcsToFirstDuties;
int numAvailPlanes[MAX_AC_TYPES];
int availACInd[MAX_AC_TYPES][MAX_PLANES_PER_FLEET];
//int numOAGCalls;
int maxArcAlloc;
int numUnFlag[MAX_AC_TYPES];

time_t maxArr;

//FUNCTION DECLARATIONS
static int countDutyArcLists(void);
static int findAircraftGroup(int acInd);
static int createCrewArcsToPlanes(void);
static int getCrewArcTimeAndCost(int cp, int pickUpAptID, time_t acAvailTm, int puStartdemandInd, CrewArc *newCrewArc);
static int createPlaneArcs(void);
static void checkReachableFromPrevIncl(int acInd, Demand *prevIncl, int endAptID, int j, Duty *endDuty, int unreachableIndex);
static int checkCanReachFutureIncl(int acInd, Demand *nextIncl, int j, Duty *endDuty, int unreachableIndex);
//static void checkReachableFromPrevInclFA(int acInd, OrigDemInfo *prevIncl, int endAptID, int j, Duty *endDuty, int unreachableIndex); //fei Jan 2011
static void checkReachableFromPrevInclFA(int acInd, int demInd, int endAptID, int j, Duty *endDuty, int unreachableIndex); //fei Jan 2011
static int checkCanReachFutureInclFA(int acInd, int demInd, int j, Duty *endDuty, int unreachableIndex);//fei Jan 2011
static int checkPlaneInclusions(Duty *duty, Aircraft *plane, int day, int unreachableIndex);

static int checkPlaneInclusionsFA(Duty *duty, Aircraft *plane, int day, int unreachableIndex);

static NetworkArc *arcAlloc(NetworkArc **arcList, int *arcCount);
static NetworkArc *arcArrayAlloc(NetworkArc **arcList, int arcListInd);
static int findPlanesWithConnxnConstraints(void);
static int createPickupTripLists(void);
static int createCrewArcsToPickups(void);
static int createPickupArcs(void);
static int createArcsToFirstDuties(void);
static int calculateArcsToFirstDuties(int cp, int j, CrewArc *crewArc, NetworkArc *pickupArc, int k, int puStartdemandInd, NetworkArc *newArc, int p); //RLZ added p, 09/18/2008
static int calculateArcsToFirstDuties2(int cp, int j, CrewArc *crewArc, NetworkArc *pickupArc, int k, int puStartdemandInd, NetworkArc *newArc);
static int calculateGetHomeCosts(void); 
static int createDutyNetwork(void);
static int createDutyToDutyArcs(int acTypeListInd, int day1, int day2, Duty *startDuty, int startAptID, int startFboID);
static int ifCrewFirstDayStart5to8AM(int givenDay, int crewPairInd, time_t nextOutTime, int nextAptID);
static int ifCrewStartBefore5AM(int crewPairInd, time_t nextOutTime, int nextAptID);

int
stoppit(char *file, int line, int idx, int max)
{
 fprintf(logFile,"%s Line %d: array subscript=%d max=%d\n", file, line, idx, max);
 exit(0);
 return(-1);
}


/********************************************************************************
*	Function   createArcs			      Date last modified:  06/07/07 SWO		*
*	Purpose:	create arcs between crewPair, Plane, and Duty Nodes				*
********************************************************************************/
int createArcs(void)
{
//	numOAGCalls = 0;
	numOAGCallsEarly = 0;
	numOAGCallsLate = 0;
	maxArcAlloc = 0;
	
	//logMemoryStatus();
	logMsg(logFile, "countDutyArcLists \n");
	countDutyArcLists();
	logMsg(logFile, "createCrewArcsToPlanes \n");
	createCrewArcsToPlanes();
	fprintf(logFile,"\n%d  crew-plane arcs created.\n", numCrewPlaneArcs);
	//logMemoryStatus();
	fflush (logFile);
	createPlaneArcs();
	fprintf(logFile,"\n%d plane arcs created.\n", numPlaneArcs); 
	//logMemoryStatus(); 
	fflush (logFile);
	findPlanesWithConnxnConstraints();
	createPickupTripLists();
	createCrewArcsToPickups();
	fprintf(logFile,"\n%d crew-pickup arcs created.\n", numCrewPickupArcs);
	fflush (logFile);
	//logMemoryStatus();
	createPickupArcs();
	fprintf(logFile,"\n%d pickup arcs created.\n", numPickupArcs);
	//logMemoryStatus(); 
	fflush (logFile);
	calculateGetHomeCosts();
	fprintf(logFile,"\nDone with calculateGetHomeCosts.\n");
	fflush (logFile);
	createArcsToFirstDuties();
	fprintf(logFile,"\n%d crew arcs to first duties created.\n", numArcsToFirstDuties);
	//logMemoryStatus(); 
	fflush (logFile);
	createDutyNetwork();
	fprintf(logFile,"\n%d duty arcs, and %d duty arc copies created.\n", numDutyArcs, numDutyArcCopies);
	fprintf(logFile,"\n%d + %d calls made to OAG function during arc generation.\n", numOAGCallsEarly, numOAGCallsLate);
	//logMemoryStatus(); 
	fflush (logFile);
	//debug only
	//myDebugFunction();
	

	return 0;
}

/********************************************************************************************************
*	Function	countDutyArcLists					Date last modified:  03/19/06 SWO					*
*	Purpose:	For each day and acType, identify planes which have inclusions (appointments or locked	*
*				legs) or exclusions on subsequent days, check if plane can be grouped with other planes	*
*				(same exclusions and no inclusions), and store info in dutyArcListTally.				*
*				By doing so, we can determine the number of lists of plane arcs that					*
*				must be generated from each duty node in addition to the arcs for the (general) fleet.	*
*				Store the index of the arcList for each day with the plane.	Also allocate memory for 	*
*				*unreachableFlag for duties, and assign unreachable index for planes.					*										*
********************************************************************************************************/
/*	Note that we require that ALL locked and appoint legs be included in each duty node that is reachable from a previous day duty node or plane node.
	dutyArcListTally[acTypeInd][day][0] stores the number of arc lists that must be generated out of each typical duty node on that day for that aircraftType
	dutyArcListTally[acTypeInd][day][x] stores the the acList index or (-)acGroupList index of the plane that will use arcList [x] out of those duty nodes
	arcList[0] is reserved for the fleet.  EXCEPTION:  If a duty node is tied to a plane, arcList[0] is for the plane and there are no other arcLists out of the node.*/
static int countDutyArcLists(void)
{
	int p, j, day, day1, maxDay, k, grpInd;

	numAcGroups = 0;

	//allocate memory for acGroupList
	if((acGroupList = calloc((2 + numAircraft/2), sizeof(AcGroup))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in countDutyArcLists().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(j= 0; j< MAX_AC_TYPES; j++) //initialize count of planes with inclusions and/or exclusions
		numUnFlag[j] = 0;
	//initialize dutyArcListTally
	for(j = 0; j < numAcTypes; j++){
		for(day = 0; day < (optParam.planningWindowDuration-1); day++){
			dutyArcListTally[j][day][0] = 1; //initialize count to 1 (one arc list for fleet)
			for(p = 1; p < MAX_PLANES_PER_FLEET; p++)
				dutyArcListTally[j][day][p] = -1; //initialize plane index to -1
		}
	}
	logMsg(logFile, "countDutyArcLists, numAircraft = %d \n", numAircraft);
	//for each plane...
	for(p = 0; p < numAircraft; p++)
	{	//initialize dutyNodeArcIndex for plane
		//logMsg(logFile, "countDutyArcLists, aircraft p = %d \n", p);
		for(day = 0; day < optParam.planningWindowDuration; day++)
			acList[p].dutyNodeArcIndex[day] = 0;  //initialize to 0, which is index for fleet
		
		//if this plane has exclusions or inclusions
		//if(acList[p].lastExcl[optParam.planningWindowDuration-1]> -1 || acList[p].lastIncl[optParam.planningWindowDuration-1] > -1)
		if (acList[p].lastExcl[optParam.planningWindowDuration-1]> -1 || 
		((!optParam.withFlexOS && acList[p].lastIncl[optParam.planningWindowDuration-1] > -1) || ( optParam.withFlexOS && acList[p].numIncl))) //fei Jan 2011
		{
			j = acList[p].acTypeIndex;
			acList[p].unreachableInd = numUnFlag[j];
			numUnFlag[j]++;
			grpInd = -1;
			acList[p].acGroupInd = -1;

			//check if we have already looked at another plane from the same fleet with the same exclusions 
			//that could be grouped with this plane
			//if(acList[p].lastIncl[optParam.planningWindowDuration - 1] == -1){
			if((!optParam.withFlexOS && acList[p].lastIncl[optParam.planningWindowDuration - 1] == -1) ||
			( optParam.withFlexOS && !acList[p].numIncl)) 
			{  //fei Jan 2011
				//	if (acList[p].multiCrew)
				//		continue;
				
				if((grpInd = findAircraftGroup(p)) > 0 ){
					acList[p].acGroupInd= grpInd;
					acGroupList[grpInd].acInd[acGroupList[grpInd].numAircraft] = p;
					acGroupList[grpInd].numAircraft++;
					for(day = 0; day < optParam.planningWindowDuration; day++)
						acList[p].dutyNodeArcIndex[day] = acList[acGroupList[grpInd].acInd[0]].dutyNodeArcIndex[day];
				}
			}
			if(grpInd == -1){
				for(day = optParam.planningWindowDuration - 1; day >= 0; day--)
				{
					maxDay = -1;
					if(acList[p].lastExcl[day]>(day == 0 ? -1 : acList[p].lastExcl[day-1])) 
						maxDay = day-1;  //we need plane-specific arcs out of duties each day up to, but not including, this day
					//else if(acList[p].lastIncl[day]>(day == 0 ? -1 : acList[p].lastIncl[day-1])) 
					else if((!optParam.withFlexOS && acList[p].lastIncl[day]>(day == 0 ? -1 : acList[p].lastIncl[day-1])) ||
					//( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->seIndByDay[1][day] > acList[p].inclInfoP->seIndByDay[0][day])) 
					( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->isIncl[day] )) //fei FA
						maxDay = day-1;  //we need plane-specific arcs out of duties each day up to, but not including, this day

					if(maxDay > -1)
					{
						for(day1 = 0; day1<=maxDay; day1++)
						{	//store plane index in tally, increase count, and store arc list index with plane 
							dutyArcListTally[j][day1][dutyArcListTally[j][day1][0]] = p;
							acList[p].dutyNodeArcIndex[day1] = dutyArcListTally[j][day1][0];
							dutyArcListTally[j][day1][0]++;
						}
						break; //once we find an exclusion or inclusion, no need to check earlier days so we break from "for(day" loop
						//Note: we may still need to check for flexible OS - FlexOS - 02/01/11 ANG
					}
				}
			} //end if(acGroupInd == 0)
		}  // end if(acList[p].lastExcl[optParam.p
		else{ //aircraft is part of general fleet(no exclusions or inclusions)
			acList[p].unreachableInd = -1;
			acList[p].acGroupInd = -1;
		}

	}//end p loop (plane loop)

	//logMsg(logFile, "countDutyArcLists, Done with all aircraft p loop \n");

	//allocate memory for unreachable flag for duties
	for(day = 0; day < optParam.planningWindowDuration; day++){
		for(j = 0; j< numAcTypes; j++){
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){
				if((dutyList[j][k].unreachableFlag = calloc((size_t)numUnFlag[j], sizeof(int))) == NULL) {
					logMsg(logFile,"%s Line %d, Out of Memory in countDutyArcLists().\n", __FILE__, __LINE__);
					writeWarningData(myconn); exit(1);
				}
			}
		}
	}

	//logMsg(logFile, "countDutyArcLists, Done allocate memory for unreachable flag for duties \n");

	//allocate memory for arcTallyByDay for parent (general fleet) arc list out of duties
	for(day = 0; day < (optParam.planningWindowDuration-1); day++){  //no arcs out of duties on last day
		for(j = 0; j< numAcTypes; j++){
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){		
				if((dutyList[j][k].arcTallyByDay = calloc((size_t)(optParam.planningWindowDuration), sizeof(int))) == NULL) {
					logMsg(logFile,"%s Line %d, Out of Memory in countDutyArcLists().\n", __FILE__, __LINE__);
					writeWarningData(myconn); exit(1);
				}
			}
		}
	}	

	//logMsg(logFile, "countDutyArcLists, Done allocate memory for arcTallyByDay for parent (general fleet) arc list out of duties \n");

	return 0;
}

/********************************************************************************************************
*	Function	findAircraftGroup								Date last modified:  03/12/07 SWO		*
*	Purpose:	Determine if we have already examined a plane with the same exclusions as the current	*
*				plane (and no inclusions).  If so, create a new aircraft group, or add the current		*
*				plane to the existing group.															*
********************************************************************************************************/
static int findAircraftGroup(int acInd)
{
	int p, x, y, z, day; 

	if (acList[acInd].multiCrew) //RLZ: seperate the multicrew aircraft
		return -1;

	if (checkIfXlsPlus(acList[acInd].aircraftID) || checkIfCj4(acList[acInd].aircraftID)) //Separate XLS+ - 06/09/11 ANG and CJ4 - 06/13/11 ANG
		return -1;

	//START - Separate M-aircraft - MAC - 08/19/08 ANG
	if(optParam.withMac == 1){
		if (acList[acInd].isMac && acList[acInd].hasOwnerTrip == 1)
			return -1;
	}
	//END - MAC - 08/19/08 ANG

	//Separate OCF Aircraft - 10/12/11 ANG
	if(optParam.withFlexOS){
		if (acList[acInd].reqOCF == 1)
			return -1;
	}

	//START - Separate CPAC-aircraft - CPAC - 06/17/09 ANG
	//if(optParam.withMac == 1){
		if (acList[acInd].applyCPACbonus && acList[acInd].cpIndCPACbonus > -1)
			return -1;
	//}
	//END - CPAC - 06/17/09 ANG

	for(p = 0; p < acInd; p++){

		//MAC - 08/19/08 ANG
		if(optParam.withMac == 1){
			if (acList[p].isMac != acList[acInd].isMac)
				continue; //if one of the a/c is Mac, can't be grouped
		}

		if(acList[p].acTypeIndex != acList[acInd].acTypeIndex)
			continue;  //planes are in different fleets and can't be in a group
		if(checkIfXlsPlus(acList[acInd].aircraftID) || checkIfCj4(acList[acInd].aircraftID))//separate XLS+ aircraft - XLS+ - 06/09/11 ANG and CJ4 - 06/13/11 ANG
			continue;
		//if(acList[p].lastIncl[optParam.planningWindowDuration-1] > -1)
		if((!optParam.withFlexOS && acList[p].lastIncl[optParam.planningWindowDuration-1] > -1) ||
		   ( optParam.withFlexOS && acList[p].numIncl)) //fei Jan 2011
			continue;  //plane has inclusions and can't be in a group
		if(acList[p].lastExcl[optParam.planningWindowDuration-1] != acList[acInd].lastExcl[optParam.planningWindowDuration-1])
			continue;  //planes have a different number of exclusions and can't be in a group
		//check that each exclusion for the current plane is an exclusion for the previously reviewed plane
		for(x = 0; x<=acList[acInd].lastExcl[optParam.planningWindowDuration-1]; x++){
			for(y = 0; y<= acList[p].lastExcl[optParam.planningWindowDuration-1];y++){
				if(acList[acInd].exclDemandInd[x] == acList[p].exclDemandInd[y])
					break; //we have found this exclusion in the other plane's list, so stop searching
			}
			if(y > acList[p].lastExcl[optParam.planningWindowDuration-1])
				break;//we did NOT find this exclusion in the other planes list; no need to look at other exclusions
		}
		if(x>acList[acInd].lastExcl[optParam.planningWindowDuration-1]){
			//we have found a plane which has all the same exclusions as the current plane
			if(acList[p].acGroupInd > 1)//if plane we found is already part of a group
				return acList[p].acGroupInd;
			else{  //Plane we found is NOT already part of a group, so create a group.
				//Allocate memory for list of acList indices for the new group.
				//Note that acGroupList[0] and acGroupList[1] are NULL.  We don't use these indices
				//because in subsequent parts of the code, we use acInd or -acGroupInd to designate
				//a single plane or group of planes. -1 is reserved to indicate the fleet.
				if((acGroupList[numAcGroups+2].acInd = calloc(MAX_PLANES_PER_FLEET, sizeof(int))) == NULL) {
					logMsg(logFile,"%s Line %d, Out of Memory in findAircraftGroup().\n", __FILE__, __LINE__);
					writeWarningData(myconn); exit(1);
				}
				for(z = 0; z < MAX_PLANES_PER_FLEET; z++)
					acGroupList[numAcGroups+2].acInd[z] = -1;
				acGroupList[numAcGroups+2].acInd[0] = p;
				acGroupList[numAcGroups+2].numAircraft = 1;
				acList[p].acGroupInd = numAcGroups+2;
				//update dutyArcListTally for each day, replacing acList index with (-)acGroup index
				for(day = 0; day<optParam.planningWindowDuration; day++){
					if(acList[p].dutyNodeArcIndex[day] > 0)
						dutyArcListTally[acList[p].acTypeIndex][day][acList[p].dutyNodeArcIndex[day]] = -(numAcGroups+2);
				}
				numAcGroups++;
				return (numAcGroups+1);//+1 because we already incremented numAcGroups
			}
		}
	}
	return -1; // we did not find a plane with the same exclusions, so current plane will NOT be added to a group
}



/********************************************************************************************************
*	Function	createCrewArcsToPlanes							Date last modified:  07/06/07 SWO		*
*	Purpose:	Create arcs from crewPairs to planes.  Arcs include	the cost to get to that plane,		*
*				 and the earliest possible start time for a leg on that	plane.	We do this before		*
*				 plane arc generation so that we can consider the crew info when applicable				*
********************************************************************************************************/
/*If a plane is already assigned to a crew (crew is locked to plane), the crew can pick up the plane when it is next available.  For the case where a 
	crew picks up a plane not yet assigned to them.....
If optParam.prohibitStealingPlanes = TRUE...  
  For plane pickups, first verify that (the specific) plane is not locked to another crew.  Otherwise, check the (currently assigned) crew's endRegDay 
		to determine if it can be picked up by the new crew.
If optParam.prohibitStealingPlanes = FALSE...
  For plane pickups, a crew can use a plane when it is next available unless the plane is locked to another crew. */
static int createCrewArcsToPlanes(void)
{
	int j, p, i, availDay, cp, pMax, acInd, avail, found;
	int repoFltTm = 0, repoBlkTm = 0, repoElapsedTm = 0, repoStops = 0;
	char *result;
	CrewArc tempArc;
	CrewArc *newCrewArc;
	
	tempArc.acInd = -1;
	tempArc.demandInd = -1;
	tempArc.numArcs = 0;
	tempArc.arcList = NULL;

	numCrewPlaneArcs = 0;
	
	maxArr = optParam.windowStart;	//max(demand.reqOut + demand.lateAdj � optParam.firstPreFlt) across ALL demands 
									//(a pilot wouldn�t travel to a plane unless there was a subsequent demand he could cover)

	//logMsg(logFile, "createCrewArcsToPlanes, numOptDemand = %d \n", numOptDemand);

	//find maxArr, which will be used in getCrewArcTimeAndCost function
	for(i=0; i<numOptDemand; i++)
		maxArr = (time_t)((demandList[i].reqOut + 60*demandList[i].lateAdj)>maxArr? (demandList[i].reqOut + 60*demandList[i].lateAdj): maxArr);
	maxArr -= optParam.firstPreFltTm*60;


	//initialize number and indices of available planes per fleet  (planes that can be picked up when next available 
	//by a crew other than the crew locked to the plane at the start)
	for(j = 0; j < MAX_AC_TYPES; j++){
		numAvailPlanes[j] = 0;
		for(p = 0; p < MAX_PLANES_PER_FLEET; p++)
			availACInd[j][p] = -1;
	}

	//logMsg(logFile, "createCrewArcsToPlanes, numAircraft = %d \n", numAircraft);

	//update first crews locked (assigned) to planes if necessary, and find number and indices of available planes per fleet
	for(p = 0; p < numAircraft; p++){		

		//logMsg(logFile, "  createCrewArcsToPlanes, processing aircraft p = %d \n", p);

		avail = 1; //avail = 1 indicates plane is available (will verify and update as required)	
		
		//note that acList[p].firstCrPrID  and firstCrPrInd currently store the crewPair that is flying plane at start of window 
		// UNLESS that crewPair's tour on the plane was locked OR crew is not avail until after window end
		//(and thus not considered in optimization)OR that crewPair is locked to another plane

		//find availDay, the day of planning window that plane is next available
		for(availDay = 0; availDay < optParam.planningWindowDuration; availDay++){
			if(acList[p].availDT < (firstEndOfDay + availDay*24*3600)) 
				break;
		}
		acList[p].availDay = availDay;
		//if a crew is locked to a leg on the plane on the plane's first available day, this crew overrides any crew assigned prior to optimization run
		//for(i = 0; i < acList[p].inclInfoP->seIndByDay[1][availDay]; i++){ 
		//for(i = 0; i < ((optParam.withFlexOS && acList[j].numIncl) ? acList[p].inclInfoP->seIndByDay[1][availDay] : acList[p].lastIncl[availDay]); i++){ //base//Double check this - 03/17/11 ANG

		//logMsg(logFile, "    createCrewArcsToPlanes, availDay for aircraft p %d = %d\n", p, availDay);

		if( !optParam.withFlexOS )
		{
			for(i = 0; i <= acList[p].lastIncl[availDay] ; i++){ 
				if(acList[p].inclCrewID[i] > 0){
					avail = 0; //plane can not be picked up by another crew when first available
					acList[p].firstCrPrID = acList[p].inclCrewID[i];
					found = 0;
					for(cp = 0; cp<numOptCrewPairs; cp++){
						if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
							acList[p].firstCrPrInd = cp;
							found = 1;
							break;
						}
					}
					//logMsg(logFile, "    createCrewArcsToPlanes, aircraft p %d cannot be assigned to other crewpair, only to crewpairID %d\n", p, acList[p].firstCrPrID);
					//exit with error message if index was not found
					if(found == 0){
						logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
						writeWarningData(myconn); exit(1);
					}
					break;
				}
			}

			if(avail == 0)
				continue;  //plane can not be picked up by another crew when first available
			//otherwise, if a crew (call it crew X) is locked to a leg on the plane on a later day (not its first avail), plane cannot be picked up by another 
			//crew when first available AND if the plane is not already locked to a crew, we will lock the plane to crew X from the start 
			for(i = 0; i <= acList[p].lastIncl[optParam.planningWindowDuration - 1]; i++){
				if(acList[p].inclCrewID[i] > 0){
					avail = 0; //plane can not be picked up by a (random) crew when first available
					if(acList[p].firstCrPrInd == -1){
						acList[p].firstCrPrID = acList[p].inclCrewID[i];
						found = 0;
						for(cp = 0; cp<numOptCrewPairs; cp++){
							if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
								acList[p].firstCrPrInd = cp;
								found = 1;
								break;
							}
						}
						//logMsg(logFile, "    createCrewArcsToPlanes, aircraft p %d cannot be assigned to other crewpair, only to crewpairID %d\n", p, acList[p].firstCrPrID);
						//exit with error message if index was not found
						if(found == 0){
							logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
							writeWarningData(myconn); exit(1);
						}
					}
					break;
				}
			}
			
		} else if ( acList[p].numIncl )//fei FA
		{
			for(i = 0; i < acList[p].inclInfoP->seIndByDay[1][availDay]; i++)//note seIndByDay//fei FA
			{ 
				if( acList[p].inclInfoP->inclCrewID[i] > 0 && !origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl ) 
				{
					avail = 0; //plane can not be picked up by another crew when first available
					acList[p].firstCrPrID = acList[p].inclInfoP->inclCrewID[i] ;
					found = 0;
					for(cp = 0; cp<numOptCrewPairs; cp++){
						if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
							acList[p].firstCrPrInd = cp;
							found = 1;
							break;
						}
					}
					//exit with error message if index was not found
					if(found == 0){
						logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
						writeWarningData(myconn); exit(1);
					}
					break;
				}
			}

			if(avail == 0)
				continue;  //plane can not be picked up by another crew when first available
			//otherwise, if a crew (call it crew X) is locked to a leg on the plane on a later day (not its first avail), plane cannot be picked up by another 
			//crew when first available AND if the plane is not already locked to a crew, we will lock the plane to crew X from the start 
			for(i = 0; i < acList[p].numIncl ; i++)
			{
				if( acList[p].inclInfoP->inclCrewID[i] > 0 && ! origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl  )
				{
					avail = 0; //plane can not be picked up by a (random) crew when first available
					if(acList[p].firstCrPrInd == -1)
					{
						acList[p].firstCrPrID = acList[p].inclInfoP->inclCrewID[i] ;
						found = 0;
						for(cp = 0; cp<numOptCrewPairs; cp++){
							if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
								acList[p].firstCrPrInd = cp;
								found = 1;
								break;
							}
						}
						//exit with error message if index was not found
						if(found == 0){
							logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
							writeWarningData(myconn); exit(1);
						}
					}
					break;
				}
			}//end for
		}//end else

		/*
		for(i = 0; i < ((optParam.withFlexOS && acList[p].numIncl) ? acList[p].inclInfoP->seIndByDay[1][availDay] : (acList[p].lastIncl[availDay] + 1)); i++){ 
			//if(acList[p].inclCrewID[i] > 0){
			if((!optParam.withFlexOS && acList[p].inclCrewID[i] > 0) ||
			   ( optParam.withFlexOS && acList[p].inclInfoP->inclCrewID[i] > 0 && !origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl)) {
				avail = 0; //plane can not be picked up by another crew when first available
				//acList[p].firstCrPrID = ((optParam.withFlexOS && acList[j].inclInfoP) ? acList[p].inclInfoP->inclCrewID[i] : acList[p].inclCrewID[i]);
				acList[p].firstCrPrID = ( optParam.withFlexOS ? acList[p].inclInfoP->inclCrewID[i] : acList[p].inclCrewID[i]);
				found = 0;
				for(cp = 0; cp<numOptCrewPairs; cp++){
					if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
						acList[p].firstCrPrInd = cp;
						found = 1;
						break;
					}
				}
				//exit with error message if index was not found
				if(found == 0){
					logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
					writeWarningData(myconn); exit(1);
				}
				break;
			}
		}

		if(avail == 0)
			continue;  //plane can not be picked up by another crew when first available
		//otherwise, if a crew (call it crew X) is locked to a leg on the plane on a later day (not its first avail), plane cannot be picked up by another 
		//crew when first available AND if the plane is not already locked to a crew, we will lock the plane to crew X from the start 
		//for(i = 0; i <= acList[p].lastIncl[optParam.planningWindowDuration - 1]; i++){
		//for(i = 0; i < (optParam.withFlexOS ? acList[p].numIncl : acList[p].lastIncl[optParam.planningWindowDuration - 1]) ; i++){ //base
		for(i = 0; i < (optParam.withFlexOS ? acList[p].numIncl : (acList[p].lastIncl[optParam.planningWindowDuration - 1] + 1)) ; i++){
			//if(acList[p].inclCrewID[i] > 0){
			if((!optParam.withFlexOS && acList[p].inclCrewID[i] > 0) ||
			   ( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->inclCrewID[i] > 0 && ! origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl )){

				//\\_ASSERTE( i > acList[p].lastIncl[availDay] );//fei Jan 2011

				avail = 0; //plane can not be picked up by a (random) crew when first available
				if(acList[p].firstCrPrInd == -1){
					//acList[p].firstCrPrID = acList[p].inclCrewID[i];
					acList[p].firstCrPrID = (optParam.withFlexOS ? acList[p].inclInfoP->inclCrewID[i] : acList[p].inclCrewID[i]);
					found = 0;
					for(cp = 0; cp<numOptCrewPairs; cp++){
						if(crewPairList[cp].crewPairID == acList[p].firstCrPrID){
							acList[p].firstCrPrInd = cp;
							found = 1;
							break;
						}
					}
					//exit with error message if index was not found
					if(found == 0){
						logMsg(logFile,"%s Line %d, index for firstCrPrID = %d on acList[%d] not found.\n", __FILE__,__LINE__, acList[p].firstCrPrID, p);
						writeWarningData(myconn); exit(1);
					}
				}
				break;
			}
		}
		*/

		if(avail == 0) //plane can not be picked up by another crew when first available
			continue; 
		//otherwise, if plane is already locked to a crewPair and we can't steal their plane until the end of their tour....
		if(acList[p].firstCrPrInd > -1 && optParam.prohibitStealingPlanes == 1){
			//check if currently assigned crew can end tour on availDay
			if(crewPairList[acList[p].firstCrPrInd].endRegDay > availDay)
				continue;  //plane can't be "stolen" from currently assigned crew yet and is not available to be picked up by others when first available
		}
		//if we have gotten to this point, add plane to list of available planes for fleet
		availACInd[acList[p].acTypeIndex][numAvailPlanes[acList[p].acTypeIndex]] = p;
		numAvailPlanes[acList[p].acTypeIndex]++;
	}

	//logMsg(logFile, "createCrewArcsToPlanes, numOptCrewPairs = %d \n", numOptCrewPairs);

	//for each crew considered in optimization, find arcs to locked plane or available planes as applicable
	for(cp = 0; cp < numOptCrewPairs; cp++){

		//logMsg(logFile, "  createCrewArcsToPlanes, processing cp = %d \n", cp);

		//if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd)
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;
		
		//if crewPair is locked to plane AND (no other crew is locked to the plane before them OR we can take the plane at this point), 
		//we generate plane arcs for crew with this one plane
		if(crewPairList[cp].optAircraftID > 0)
		{	

			if(acList[crewPairList[cp].acInd].firstCrPrInd != cp){ //crewPair is NOT the first one assigned to the plane 

				//if the first crew assigned to plane has an inclusion on the plane, then new crew can't pick up plane when next available
				if( optParam.withFlexOS && acList[crewPairList[cp].acInd].numIncl > 0)
				{
					//_ASSERTE( acList[crewPairList[cp].acInd].numIncl > 0 );
					for(i = 0; i < acList[crewPairList[cp].acInd].numIncl; i++)
					{
						//if(acList[crewPairList[cp].acInd].inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID)
						if( acList[crewPairList[cp].acInd].inclInfoP->inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID )
						{
							_ASSERTE( !origDemInfos[acList[crewPairList[cp].acInd].inclInfoP->origDemIndices[i]].skipIncl); //fei Jan 2011
							
							i = 999;
						}
					}
				} else
				{
					for(i = 0; i <= acList[crewPairList[cp].acInd].lastIncl[optParam.planningWindowDuration - 1] ; i++){//base
						//if(acList[crewPairList[cp].acInd].inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID)
						if( acList[crewPairList[cp].acInd].inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID){
							i = 999;
						}
					}//end for
				}
				
				/*
				//for(i = 0; i <= acList[crewPairList[cp].acInd].lastIncl[optParam.planningWindowDuration - 1]; i++){
				//for(i = 0; i < (optParam.withFlexOS ? acList[crewPairList[cp].acInd].numIncl : acList[crewPairList[cp].acInd].lastIncl[optParam.planningWindowDuration - 1]) ; i++){//base
				for(i = 0; i < (optParam.withFlexOS ? acList[crewPairList[cp].acInd].numIncl : ( acList[crewPairList[cp].acInd].lastIncl[optParam.planningWindowDuration - 1] + 1) ) ; i++){//base
					//if(acList[crewPairList[cp].acInd].inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID)
					if((!optParam.withFlexOS && acList[crewPairList[cp].acInd].inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID) ||
					   //( optParam.withFlexOS && acList[crewPairList[cp].acInd].numIncl && acList[crewPairList[cp].acInd].inclInfoP->inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID)){//base
					   ( optParam.withFlexOS && acList[crewPairList[cp].acInd].inclInfoP->inclCrewID[i] == acList[crewPairList[cp].acInd].firstCrPrID)){
						//_ASSERTE( ! acList[crewPairList[cp].acInd].inclInfoP->inclDemandSkip[i] ); //fei Jan 2011
						//\\_ASSERTE( ! origDemInfos[acList[crewPairList[cp].acInd].inclInfoP->origDemIndices[i]].skipIncl ); //fei Jan 2011
						i = 999;
					}
				}
				*/
				if(i==999)
					continue;
				//if the plane cannot be taken yet from the first crew assigned to the plane, then the new crew can't pick up plane when next avail
				if(crewPairList[acList[crewPairList[cp].acInd].firstCrPrInd].endRegDay > availDay && optParam.prohibitStealingPlanes == 1)
					continue;
			}
			//allocate memory for only one arc in the crewPlaneList
			if((crewPairList[cp].crewPlaneList = calloc(1, sizeof(CrewArc))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPlanes().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			pMax = 1;
		}
		else //we  generate arcs to all available planes
		{	//allocate memory for arcs in crewPlaneList
			j = crewPairList[cp].acTypeIndex;
			if((crewPairList[cp].crewPlaneList = calloc(numAvailPlanes[j], sizeof(CrewArc))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPlanes().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			pMax = numAvailPlanes[j];
		}
		//create arcs
		crewPairList[cp].numPlaneArcs = 0;
		for(p = 0; p < pMax; p++)
		{
			if(crewPairList[cp].optAircraftID > 0)
				acInd = crewPairList[cp].acInd;
			else
				acInd = availACInd[j][p];

			/*
			if (crewPairList[cp].optAircraftID > 0 && crewPairList[cp].optAircraftID != acList[acInd].aircraftID){
				//if non-XLS+ crew paired with XLS+ crew (or vice versa), don't create arc - XLS+ - 06/10/11 ANG
				if(acTypeList[crewPairList[cp].acTypeIndex].aircraftTypeID == 11)
					if(!(checkIfXlsPlus(acList[acInd].aircraftID) && crewList[crewPairList[cp].crewListInd[0]].isDup == 1 && crewList[crewPairList[cp].crewListInd[1]].isDup == 1))
						continue;
				//if non-CJ4 crew paired with XLS+ crew (or vice versa), don't create arc - CJ4 - 06/13/11 ANG
				//else if(acTypeList[crewPairList[cp].acTypeIndex].aircraftTypeID == 54)
				//	if(!(checkIfCj4(acList[acInd].aircraftID) && crewList[crewPairList[cp].crewListInd[0]].isDup == 1 && crewList[crewPairList[cp].crewListInd[1]].isDup == 1))
				//		continue;
			}
			*/

			if(acTypeList[crewPairList[cp].acTypeIndex].aircraftTypeID == 11)//if excel
			{
				if( checkIfXlsPlus(acList[acInd].aircraftID) )//if excel plus
				{
					if( (crewList[crewPairList[cp].crewListInd[0]].isDup == 0 || crewList[crewPairList[cp].crewListInd[1]].isDup == 0) //wrong copy of the crew
					&& crewPairList[cp].optAircraftID != acList[acInd].aircraftID ) //and excel plus ac is not the opt ac
						continue ; //not generating arc
				} else if( crewList[crewPairList[cp].crewListInd[0]].isDup == 1 || crewList[crewPairList[cp].crewListInd[1]].isDup == 1 ) //not excel plus
					continue ; //not generating arc
			}

			result = (char *)malloc(5);
			strncpy(result, acList[acInd].registration + 1, 3);
			//Two fusion pilots cannot be assigned to a King Air requiring proline pilots (Added by Ali-- 6/25/2018)
			if (acList[acInd].aircraftTypeID == 5 && atoi(result) <= 856 && crewList[crewPairList[cp].crewListInd[0]].qualification == 2 && crewList[crewPairList[cp].crewListInd[1]].qualification == 2)
				continue;
			//Two proline pilots cannot be assigned to a King Air requiring fusion pilots (Added by Ali-- 6/25/2018) 
			if (acList[acInd].aircraftTypeID == 5 && atoi(result) > 856 && crewList[crewPairList[cp].crewListInd[0]].qualification == 1 && crewList[crewPairList[cp].crewListInd[1]].qualification == 1)
				continue;

			//if plane is not available until after crewPair's end day, don't create arc
			if(acList[acInd].availDay > crewPairList[cp].endDay)
				continue;
			//if plane is not available until after window end, don't create arc
			if(acList[acInd].availDT > optParam.windowEnd)
				continue;
			if(!getCrewArcTimeAndCost(cp, acList[acInd].availAirportID, acList[acInd].availDT, -1, &tempArc)){
				newCrewArc = &crewPairList[cp].crewPlaneList[crewPairList[cp].numPlaneArcs];
				*newCrewArc = tempArc;
				newCrewArc->acInd = acInd;
				newCrewArc->demandInd = -1;
				crewPairList[cp].numPlaneArcs++;
			}
		} //end for(p=0... loop
		//logMsg(logFile, "    createCrewArcsToPlanes, cp %d has %d numPlaneArcs, ", cp, crewPairList[cp].numPlaneArcs);
		numCrewPlaneArcs += crewPairList[cp].numPlaneArcs;
		//logMsg(logFile, " total numCrewPlaneArcs now = %d \n", numCrewPlaneArcs);
	} // end for(cp=0... loop
	return 0;
}




/************************************************************************************
*	Function   createPlaneArcs			      Date last modified:  03/15/07 SWO		*
*	Purpose:	Create arcs from each plane to all duty nodes that can be directly	*
*				reached by a plane in order to expedite generation of crew arcs.	*
*				Also flag nodes that can't ever be visited by a						*
*				plane with dutyList[][].unreachableFlag[] so we don't generate		*
*				plane-specific arc lists unnecessarily.	NOTE:  unreachableFlag  	*
*				is based on getting there on time if plane flies directly, on		*
*				inclusions, and exclusions only (duty and flightTm constraint can	*
*				be circumvented with multiple crews).								*
************************************************************************************/
static int createPlaneArcs(void)
{
	int p, p2, j, day, k, z, endAptID, endFboID, maxRepoArrTm, x;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int createArc, elapsedTm, repoStartTm, arcCount, fixedCPInd, incentive = 0;
	NetworkArc *newArc;
	Duty *endDuty;
	int addTime = 0; //OCF - 10/18/11 ANG

	numPlaneArcs = 0;

	for(p=0; p<numAircraft; p++){	//initialize arcTallyByDay
		for(day = 0; day < optParam.planningWindowDuration; day++)	
			acList[p].arcTallyByDay[day] = -1;
		//if we don't know where plane is, skip it
		if(acList[p].availAirportID == 0)
			continue;
		//if plane is not available within planning window, skip it
		if(acList[p].availDT > optParam.windowEnd)
			continue;

		j = acList[p].acTypeIndex;
		z = acList[p].unreachableInd;

		//determine if the crewPair picking up the plane when next available is fixed because either
		//(a) the crew pair is locked to a leg on the plane or 
		//(b) there is a crewPair assigned to the plane and we are not allowing somebody to take the plane from
		//them yet (optParam.prohibitStealingPlanes = 1)
		//If a plane is NOT on the list of available planes, then we know the crew pair is fixed per the above criteria
		fixedCPInd = acList[p].firstCrPrInd; //first, assume crewPair is fixed, but then check
		if(fixedCPInd > -1){
			for(p2 = 0; p2<numAvailPlanes[j]; p2++){
				if(availACInd[j][p2] == p){
					fixedCPInd = -1;
					break;
				}
			}
		}
		//initialize arcCount (one arcList per plane)
		arcCount = 0;

		for(day = 0; day < optParam.planningWindowDuration; day++)
		{	//re-initialize arcTallyByDay for all but first day
			if(day != 0)
				acList[p].arcTallyByDay[day] = acList[p].arcTallyByDay[day-1];
			//for each duty for that fleet and day...
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){
				
				//if plane is in a acGroup and we have already flagged node as unreachable (for all planes
				//in the group), there is no need to continue
				if(dutyList[j][k].unreachableFlag[z]==2)
					continue;
				
				createArc = 1;
				maxRepoArrTm = -1;
				incentive = 0;

				endDuty = &dutyList[j][k];
				
				//If duty ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
				//and we will not create an arc to that duty.  There is no need to set unreachable flag as there will be NO arcs to that duty.
				if(endDuty->lastDemInd > -1 && demandList[endDuty->lastDemInd].succDemID > 0)
					continue;

				if(endDuty->demandInd[0] != -1)
					endAptID = demandList[endDuty->demandInd[0]].outAirportID;
				else 
					endAptID = demandList[endDuty->repoDemandInd].outAirportID;
				//if the crew picking up the plane when next available is fixed...
				if(fixedCPInd > -1){
					//don't create arcs directly to duties after the crewPair's end day (though not unreachable)
					if(crewPairList[fixedCPInd].endDay < day)
						createArc = 0;

					//RLZ emergent patch 07/04/08
					if(crewPairList[fixedCPInd].startDay == PAST_WINDOW){ //No crewPlaneList
						//if(z>-1) //set unreachableFlag to 1 (if plane has exclusions/inclusions and thus unreachable flag)
						//	endDuty->unreachableFlag[z]=1;
						continue;  //continue without creating arc
					}
				

					if(!crewPairList[fixedCPInd].crewPlaneList){ //should never be here
                       // if(z>-1) //set unreachableFlag to 1 (if plane has exclusions/inclusions and thus unreachable flag)
						//	endDuty->unreachableFlag[z]=1;
						continue;  //continue without creating arc
					}

					//check that duty start is not earlier than plane can be picked up  (IGNORING REPO TIME FOR NOW; will check later) //RLZ CHECK
					if((endDuty->crewStartTm < crewPairList[fixedCPInd].crewPlaneList[0].earlyPickupTm[2]) || (endDuty->startTm[0]< (int)acList[p].availDT/60)){ //there is only one plane in the crewPlane list for fixed crews
						if(z>-1) //set unreachableFlag to 1 (if plane has exclusions/inclusions and thus unreachable flag)
							endDuty->unreachableFlag[z]=1;
						continue;  //continue without creating arc
					}

					//if crewPair is available this day (not earlier) and available time is fixed, then check that duty hours will not be exceeded
					if(crewPairList[fixedCPInd].startDay == day){
						//if availAptID is populated for crewPair, then they are already together and we need only check duty hours for pair
						if(crewPairList[fixedCPInd].availAptID > 0){
							if(crewPairList[fixedCPInd].activityCode == 0){ //if available time is fixed
								if(endDuty->demandInd[0] != -1){ //if this is NOT a repo-only node

									/*  RLZ CHECK
									if((endDuty->crewEndTm + optParam.postFlightTm - (int)crewPairList[fixedCPInd].availDT/60 + crewPairList[fixedCPInd].dutyTime) > optParam.maxDutyTm){
										if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
											endDuty->unreachableFlag[z]=1;
										continue;  //continue without creating arc
									}
									*/
									if (endDuty->crewEndTm >= endDuty->crewStartTm){
										if((endDuty->crewEndTm + optParam.postFlightTm - (int)crewPairList[fixedCPInd].availDT/60 + crewPairList[fixedCPInd].dutyTime) > optParam.maxDutyTm){
											if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
												endDuty->unreachableFlag[z]=1;
											continue;  //continue without creating arc
										}
										//START - FATIGUE - 02/05/10 ANG
										//if this is first day of tour for any member of crewPair, mark as unreachable if short duty rule is violated
										//Check if duty start between 5-8AM local time - 02/25/10 ANG
										if( ifCrewFirstDayStart5to8AM(day, fixedCPInd, demandList[endDuty->demandInd[0]].reqOut, demandList[endDuty->demandInd[0]].outAirportID)){
											if((endDuty->crewEndTm + optParam.postFlightTm + optParam.shortDutyHrDif - (int)crewPairList[fixedCPInd].availDT/60 + crewPairList[fixedCPInd].dutyTime) > optParam.maxDutyTm){
												if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
													endDuty->unreachableFlag[z]=1;
												continue;  //continue without creating arc
											}
										}
										//END - FATIGUE - 02/05/10 ANG
									}
								}
								else{//this is a repo-only node, and can start as soon as plane can be picked up.  
									//determine max repo time so that duty hours aren't exceeded for use below
									maxRepoArrTm = (int)crewPairList[fixedCPInd].availDT/60+ optParam.maxDutyTm 
										- optParam.postFlightTm - crewPairList[fixedCPInd].dutyTime;

									//START - FATIGUE - 02/05/10 ANG
									//if this is first day of tour for any member of crewPair, mark as unreachable
									if( ifCrewFirstDayStart5to8AM(day, fixedCPInd, crewPairList[fixedCPInd].availDT, crewPairList[fixedCPInd].availAptID) ){
										maxRepoArrTm -= optParam.shortDutyHrDif;
									}
									//END - FATIGUE - 02/05/10 ANG
								}
							}
						}
						//else crew members must be checked separately  
						//NOTE:  THIS ELSE LOOP WILL BE EXECUTED ONLY VERY RARELY; DON'T BOTHER WITH REPO-ONLY NODE LOGIC FROM ABOVE
						else {//we know from previous checks that crew is available no later than this day, so just check if avail after start of this day, 
							//which equals end of day - 24 hours
							if(crewList[crewPairList[fixedCPInd].crewListInd[0]].availDT > (firstEndOfDay + (day-1)*24*3600) && 
								crewList[crewPairList[fixedCPInd].crewListInd[0]].activityCode == 0){
								if((endDuty->crewEndTm + optParam.postFlightTm - (int)crewList[crewPairList[fixedCPInd].crewListInd[0]].availDT + 
									crewList[crewPairList[fixedCPInd].crewListInd[0]].dutyTime) > optParam.maxDutyTm){
									if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
										endDuty->unreachableFlag[z]=1;
									continue;  //continue without creating arc
								}

								//START - FATIGUE - 02/05/10 ANG
								if( ifCrewFirstDayStart5to8AM(day, fixedCPInd, crewList[crewPairList[fixedCPInd].crewListInd[0]].availDT, crewList[crewPairList[fixedCPInd].crewListInd[0]].availAirportID)){
									if((endDuty->crewEndTm + optParam.postFlightTm + optParam.shortDutyHrDif - (int)crewList[crewPairList[fixedCPInd].crewListInd[0]].availDT + 
										crewList[crewPairList[fixedCPInd].crewListInd[0]].dutyTime) > optParam.maxDutyTm){
										if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
											endDuty->unreachableFlag[z]=1;
										continue;  //continue without creating arc
									}
								}
								//END - FATIGUE - 02/05/10 ANG
							}
							if(crewList[crewPairList[fixedCPInd].crewListInd[1]].availDT > (firstEndOfDay + (day-1)*24*3600) && 
								crewList[crewPairList[fixedCPInd].crewListInd[1]].activityCode == 0){
								if((endDuty->crewEndTm + optParam.postFlightTm - (int)crewList[crewPairList[fixedCPInd].crewListInd[1]].availDT + 
									crewList[crewPairList[fixedCPInd].crewListInd[1]].dutyTime) > optParam.maxDutyTm){
									if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
										endDuty->unreachableFlag[z]=1;
									continue;  //continue without creating arc
								}

								//START - FATIGUE - 02/05/10 ANG
								//if this is first day of tour for any member of crewPair, mark as unreachable
								if( ifCrewFirstDayStart5to8AM(day, fixedCPInd, crewList[crewPairList[fixedCPInd].crewListInd[1]].availDT, crewList[crewPairList[fixedCPInd].crewListInd[1]].availAirportID)){
									if((endDuty->crewEndTm + optParam.postFlightTm + optParam.shortDutyHrDif - (int)crewList[crewPairList[fixedCPInd].crewListInd[1]].availDT + 
										crewList[crewPairList[fixedCPInd].crewListInd[1]].dutyTime) > optParam.maxDutyTm){
										if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
											endDuty->unreachableFlag[z]=1;
										continue;  //continue without creating arc
									}
								}
								//END - FATIGUE - 02/05/10 ANG
							}
						}	
					}
				} //end if(fixedCPInd > -1)

				//else just check that duty start is not earlier than plane is available
				else if(endDuty->startTm[0]< (int)acList[p].availDT/60)
				{
					if(z>-1) //set unreachableFlag to 1 (if plane has inclusions or exclusions and thus unreachable flag)
						endDuty->unreachableFlag[z]=1;
					continue;  //continue without creating arc
				}
				//if plane has inclusions (required trips) for that day....
				if(!optParam.withFlexOS && acList[p].lastIncl[day]>(day == 0 ? -1 : acList[p].lastIncl[day-1])) 
				{	//the duty node must include them and thus must be tied to that plane
					//Note: may no longer be the case for flex os - FlexOS - 02/01/11 ANG

					if(endDuty->acInd != p){ 
						if(z>-1) //set unreachableFlag to 1
							endDuty->unreachableFlag[z]=1;
						continue; //continue without creating arc
					}
					//check inclusions
					if(checkPlaneInclusions(&dutyList[j][k], &acList[p], day, z))
					//returns true if infeasible, AND flags node as unreachable in the function
						continue; //continue without creating arc
				}
				//if plane has inclusions (required trips) for that day....
				//else if( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->seIndByDay[1][day] > acList[p].inclInfoP->seIndByDay[0][day])	//fei Jan 2011
				else if( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->isIncl[day] )	//fei FA
				{	
					_ASSERTE( z >= 0 ) ; //fei Jan 2011 
					//the duty node must include them and thus must be tied to that plane 
					//possible that endDuty->acInd == -1 and we want to make some maint/app optional
					//so use two cases: endDuty->acInd == -1 or >= 0
					//if(endDuty->acInd != p){ 
					if(endDuty->acInd >= 0 && endDuty->acInd != p){   //fei Jan 2011 
						if(z>-1) //set unreachableFlag to 1
							endDuty->unreachableFlag[z]=1;
						continue; //continue without creating arc
					}

					//fei Jan 2011
					//if( ! optParam.noInclusionTest && acList[p].inclInfoP->seHardIndByDay[1][day] > acList[p].inclInfoP->seHardIndByDay[0][day] ) //there is a hard inclusion
					if( acList[p].inclInfoP->seHardIndByDay[1][day] > acList[p].inclInfoP->seHardIndByDay[0][day] ) //there is a hard inclusion
					{
						if( endDuty->acInd != p)
						{
							_ASSERTE( endDuty->acInd == -1 );
							if(z>-1) //set unreachableFlag to 1
								endDuty->unreachableFlag[z]=1;
							continue; //continue without creating arc
						}

						//check inclusions
						if(checkPlaneInclusionsFA(&dutyList[j][k], &acList[p], day, z))
						//returns true if infeasible, AND flags node as unreachable in the function
							continue; //continue without creating arc
					}
					//no inclusion test //fei Jan 2011
				}
				//else duty must not be tied to a different plane
				else if(endDuty->aircraftID != 0 && endDuty->aircraftID != acList[p].aircraftID) 
				{
					if(z>-1){ //set unreachableFlag to 1 or 2 if plane has inclusions or exclusions
						if(acList[p].acGroupInd > 1){
							for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
								endDuty->unreachableFlag[acList[acGroupList[acList[p].acGroupInd].acInd[x]].unreachableInd] = 2;
						}
						else
							endDuty->unreachableFlag[z] = 1;
					}
					continue; //continue without creating arc
				}//if a plane has exclusions that day OR the next (might repo to an exclusion) 
				
				if(acList[p].lastExcl[(day == (optParam.planningWindowDuration - 1)? day: day+1)]>(day == 0 ? -1 : acList[p].lastExcl[day-1]))
				{
					if(checkPlaneExclusions(&dutyList[j][k], &acList[p], day)) {	//returns true if infeasible
						if(z>-1){ //set unreachableFlag to 1 or 2 if plane has inclusions or exclusions
							if(acList[p].acGroupInd > 1){
								for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
									endDuty->unreachableFlag[acList[acGroupList[acList[p].acGroupInd].acInd[x]].unreachableInd] = 2;
							}
							else
								endDuty->unreachableFlag[z] = 1;
						}
						continue;
					}
				}		
				//if no reposition to duty
				if(acList[p].availAirportID == endAptID)
				{
					//if this is a repo only node and there is (currently) no reposition required, don't create arc (but don't flag 
					//node as unreachable, and we must finish other checks before moving to next node)
					if(endDuty->demandInd[0] == -1)
						createArc = 0;
					//check if we must transfer to another fbo
					if(endDuty->demandInd[0] != -1)
						endFboID = demandList[endDuty->demandInd[0]].outFboID;
					else 
						endFboID = demandList[endDuty->repoDemandInd].outFboID;
					
					if(acList[p].availFboID > 0 && endFboID > 0 && acList[p].availFboID != endFboID)
					{
						elapsedTm = optParam.fboTransitTm;
						//check if it is possible to get plane to duty on time.  NOTE THAT UPDATED acList.availDT INCLUDES TURNTIME (BEFORE REPO).
						if(endDuty->startTm[0]< (int)acList[p].availDT/60 + elapsedTm)
						{
							if(z>-1) //set unreachableFlag to 1 if plane has inclusions or exclusions
								endDuty->unreachableFlag[z]=1;						
							continue;
						}
					//assume fboTransit is not part of blockTm (may be towed, may be inconsequential) per BILL HALL 4/12/06
					}
					else
						elapsedTm = 0; //no need to check duty time

					//check if there are inclusions for plane that must occur on previous days (in which case we can't create arc directly from plane to this node)
					//Note: need to modify for flex OS - FlexOS - 02/01/11 ANG
					if( !optParam.withFlexOS )
					{
						if(day > 0){
							if(acList[p].lastIncl[day-1] > -1){
								//check if this node can be reached from previous inclusions first (if not, flag as unreachable)
								checkReachableFromPrevIncl(p, &demandList[acList[p].inclDemandInd[acList[p].lastIncl[day-1]]], endAptID, j, endDuty, z);
								continue;
							}
						}
						//check if there are inclusions for plane on a following day.  if can't reach future inclusions
						//from this node, flag this node as unreachable for the plane and continue without creating arc
						if(day < (optParam.planningWindowDuration - 1)){
							if(acList[p].lastIncl[day] < acList[p].lastIncl[optParam.planningWindowDuration - 1]){
								if(checkCanReachFutureIncl(p, &demandList[acList[p].inclDemandInd[acList[p].lastIncl[day]+1]], j, endDuty, z)) //returns -1 (true) if can't reach;  returns 0 (false) if can reach
									continue;
							}
						}
					} else if( acList[p].numIncl)
					{ 
						//check if there are inclusions for plane that must occur on previous days (in which case we can't create arc directly from plane to this node)
						if(day > 0) 
						{
							//if( (p2 = acList[p].inclInfoP->seHardIndByDay[0][day] ) > 0)
							if( (p2 = acList[p].inclInfoP->seHardIndByDay[1][day-1] ) > 0)//fei FA
							{ //fei Jan 2011
								_ASSERTE( acList[p].inclInfoP->prevHardIncl[p2] >= 0 );
								//checkReachableFromPrevInclFA(p, &origDemInfos[acList[p].inclInfoP->origDemIndices[p2 - 1]], endAptID, j, endDuty, z);
								checkReachableFromPrevInclFA(p, acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->prevHardIncl[p2]], endAptID, j, endDuty, z);
								continue;
							}
						}
						//check if there are inclusions for plane on a following day.  if can't reach future inclusions
						//from this node, flag this node as unreachable for the plane and continue without creating arc
						//if(day < (optParam.planningWindowDuration - 1))
						if( acList[p].inclInfoP->seHardIndByDay[1][day] < acList[p].inclInfoP->seHardIndByDay[1][optParam.planningWindowDuration - 1] )
						{
							//index of the next hard inclusion in origDemIndices
							p2 = acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->seHardIndByDay[1][day]] ;//next possible hard inclusion after current day, original index
							if( origDemInfos[p2].skipIncl )//non hard
								p2 = acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->nextHardIncl[acList[p].inclInfoP->seHardIndByDay[1][day]]] ;//get the next one
							
							_ASSERTE( p2 >= 0 && !origDemInfos[p2].skipIncl ) ;

							if(( endDuty->repoDemandInd < 0 || p2 != demandList[endDuty->repoDemandInd].origDemInd )//not repo to the next hard inclusion, otherwise has been checked
							&& checkCanReachFutureInclFA(p, p2, j, endDuty, z) )
								continue;//fei Jan 2011
						}
					}//end

					//Add OCF check here and add to elapsedTm if necessary - 10/18/11 ANG
					if (optParam.withFlexOS == 1 && acList[p].reqOCF == 1){
						//if no repo is needed
						if (endAptID = demandList[endDuty->demandInd[0]].outAirportID)
							continue;//
						//else if repo only duty
						//else if (endAptID = demandList[endDuty->repoDemandInd].outAirportID) or if(endDuty->demandInd[0] == -1)
						//	createArc = 0;//follow earlier message - may be revised:
					}

					if(createArc == 0)
						continue;

					//If we have gotten through all the checks, arc is feasible.
					//allocate memory for arc
					newArc = arcAlloc(&acList[p].arcList, &arcCount);
					//create arc
					newArc->destDutyInd = k;
					//assume fboTransit is not part of blockTm (may be towed, may be inconsequential) per BILL HALL 4/12/06
					newArc->blockTm = endDuty->blockTm;
					newArc->cost = endDuty->cost; //includes cost of destination node

					if(optParam.withMac == 1){
						newArc->tempCostForMac = endDuty->tempCostForMac; //MAC - 09/23/08 ANG
						newArc->repoFromAptID = 0; //MAC - 09/23/08 ANG - Note: in this case, no repo needed since a/c avail at the start loc of duty
						newArc->macRepoFltTm = 0; 
						newArc->macRepoStop = 0; 
					}

					//newArc->startTm = endDuty->startTm[0] - elapsedTm;  //RLZ CHECK 04142008
					newArc->startTm = endDuty->crewStartTm  - elapsedTm;  //RLZ CHECK 04142008

					//increment arcTallyByDay 
					acList[p].arcTallyByDay[day]++;
					numPlaneArcs++;
				}
				else //we need repo to duty (or else this is a repo-only node)(NOTE: we consider repositioning the day BEFORE the duty by looking at arcs to repo only nodes, or nodes with final evening repo) 
				{
					getFlightTime(acList[p].availAirportID, endAptID, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
					if(maxRepoArrTm > -1){ //maxRepoArrTm > -1 if set above based on duty hours of fixed crew that will repo ASAP
						//RLZ: Put Max to get the accurate available time	 ,(int)(acList[p].availDT / 60))			
						repoStartTm = getRepoArriveTm(acList[p].availAirportID, endAptID, crewPairList[fixedCPInd].crewPlaneList[0].earlyPickupTm[2] , repoElapsedTm) - repoElapsedTm;

						//if(repoStartTm == -1 || (repoStartTm + repoElapsedTm) > maxRepoArrTm || maxRepoArrTm < endDuty->crewEndTm ){ //RLZ
						if(repoStartTm == -1 || (repoStartTm + repoElapsedTm) > maxRepoArrTm || maxRepoArrTm < endDuty->crewEndTm ){ //Break condition into 2 - OCF - 10/19/11 ANG
							if(z>-1) //set unreachableFlag to 1 if plane has inclusions or exclusions
								endDuty->unreachableFlag[z]=1;
							continue;  //continue without creating arc
						}

						//Check OCF timing - 10/18/11 ANG
						if(optParam.withOcf == 1){
							if(optParam.withFlexOS && acList[p].reqOCF == 1){
								addTime = 0;
								if(!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, (endDuty->demandInd[0]!=-1 ? demandList[endDuty->demandInd[0]].reqOut : demandList[endDuty->repoDemandInd].reqOut), endAptID, j, repoStartTm, &addTime))
									continue;
								else if (addTime > 0)
									repoStartTm = repoStartTm - addTime; //note: addTm includes preOCF and postOCF times
							}
						}

						//if(repoStartTm == -1 || (repoStartTm + repoElapsedTm) > maxRepoArrTm || maxRepoArrTm < endDuty->crewEndTm ){ //RLZ
						if((repoStartTm + repoElapsedTm) > maxRepoArrTm || maxRepoArrTm < endDuty->crewEndTm ){ //Break condition into 2 - OCF - 10/19/11 ANG
							if(z>-1) //set unreachableFlag to 1 if plane has inclusions or exclusions
								endDuty->unreachableFlag[z]=1;
							continue;  //continue without creating arc
						}
						//if(demandList[endDuty->repoDemandInd].earlyMPM - repoElapsedTm - optParam.preFlightTm < optParam.cutoffForShortDuty ) //Too early for repo the same day
						//if(demandList[endDuty->repoDemandInd].earlyMPM - repoElapsedTm - addTime - optParam.preFlightTm < optParam.cutoffForShortDuty ) //Too early for repo the same day
						if(demandList[endDuty->repoDemandInd].earlyMPM - repoElapsedTm - addTime - acTypeList[j].preFlightTm < optParam.cutoffForShortDuty ) //07/17/2017 ANG
							incentive = SMALL_INCENTIVE;
						else
							incentive = -SMALL_INCENTIVE;


					}
					//RLZ: 06/23/2008, there might be a boundary case, where aircraft is available for other crew pickup, but the original crew pair
					//can do it ASAP. 
					else{ //else repo is done as late as possible						
						repoStartTm = getRepoDepartTm(acList[p].availAirportID, endAptID, endDuty->startTm[0] - (endDuty->demandInd[0] != -1 ? optParam.turnTime:0), repoElapsedTm);
						//RLZ:06/23/08 no need for turn time if it is a repo only duty

						//Check OCF timing - 10/18/11 ANG
						if(optParam.withOcf == 1){
							if(optParam.withFlexOS && acList[p].reqOCF == 1){
								addTime = 0;
								if(!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, endDuty->startTm[0]*60, endAptID, j, repoStartTm, &addTime))
									continue;
								else if (addTime > 0)
									repoStartTm = repoStartTm - addTime; //note: addTm includes preOCF and postOCF times
							}
						}

						if(repoStartTm == -1){
							if(z>-1) //set unreachableFlag to 1 if plane has inclusions or exclusions
								endDuty->unreachableFlag[z]=1;	
							continue;
						}

					}
					//check if it is possible to get plane to duty on time.  Note that updated acList.availDT includes turntime before repo.
					if( repoStartTm < (int)(acList[p].availDT / 60)) // RLZ turn time?
					{
						if(z>-1) //set unreachableFlag to 1 if plane has inclusions or exclusions
							endDuty->unreachableFlag[z]=1;	
						continue;
					}
					//check if there are inclusions for plane that must occur on previous days (in which case we can't create arc directly from plane to this node)
					//Note: need to modify for flex OS - FlexOS - 02/01/11 ANG
					if( ! optParam.withFlexOS )
					{
						if(day > 0){
							if(acList[p].lastIncl[day-1] > -1){
								checkReachableFromPrevIncl(p, &demandList[acList[p].inclDemandInd[acList[p].lastIncl[day-1]]], endAptID, j, endDuty, z);
								continue;
							}
						}
						//check if there are inclusions for plane on a following day.  if can't reach future inclusions
						//from this node, flag this node as unreachable for the plane and continue without creating arc
						if(day < (optParam.planningWindowDuration - 1)){
							if(acList[p].lastIncl[day] < acList[p].lastIncl[optParam.planningWindowDuration - 1]){
								//if we are not repositioning to the next inclusion (in which case we know it is reachable)
								if(acList[p].inclDemandInd[acList[p].lastIncl[day]+1] != endDuty->repoDemandInd)
									if(checkCanReachFutureIncl(p, &demandList[acList[p].inclDemandInd[acList[p].lastIncl[day]+1]], j, endDuty, z)) //returns -1 (true) if can't reach;  returns 0 (false) if can reach
										continue;
							}
						}
					} else if (acList[p].numIncl)
					{
						//check if there are inclusions for plane that must occur on previous days (in which case we can't create arc directly from plane to this node)
						if(day > 0)
						{
							//if( (p2 = acList[p].inclInfoP->seHardIndByDay[0][day] ) > 0)
							if( (p2 = acList[p].inclInfoP->seHardIndByDay[1][day-1] ) > 0)//fei FA
							{ //fei Jan 2011
								_ASSERTE( acList[p].inclInfoP->prevHardIncl[p2] >= 0 ); 
								checkReachableFromPrevInclFA(p, acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->prevHardIncl[p2]], endAptID, j, endDuty, z);
								//checkReachableFromPrevInclFA(p, &origDemInfos[acList[p].inclInfoP->origDemIndices[p2 - 1]], endAptID, j, endDuty, z);
								continue;
							}
						}

						//check if there are inclusions for plane on a following day.  if can't reach future inclusions
						//from this node, flag this node as unreachable for the plane and continue without creating arc
						if( acList[p].inclInfoP->seHardIndByDay[1][day] < acList[p].inclInfoP->seHardIndByDay[1][optParam.planningWindowDuration - 1] )
						{
							//index of the next hard inclusion in origDemIndices
							p2 = acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->seHardIndByDay[1][day]] ;//next possible hard inclusion after current day, original index

							if( origDemInfos[p2].skipIncl )//non hard
								p2 = acList[p].inclInfoP->origDemIndices[acList[p].inclInfoP->nextHardIncl[acList[p].inclInfoP->seHardIndByDay[1][day]]] ;//get the next one
							
							_ASSERTE( p2 >= 0 && !origDemInfos[p2].skipIncl ) ;

							if(( endDuty->repoDemandInd < 0 || p2 != demandList[endDuty->repoDemandInd].origDemInd )//not repo to the next hard inclusion, otherwise has been checked
							&& checkCanReachFutureInclFA(p, p2, j, endDuty, z) )
								continue;//fei Jan 2011
						}
					}//end

					//check repo flight time against limit
					if(repoFltTm > optParam.maxRepoTm)
						continue;  //but don't flag as unreachable
					//check block time and duty time assuming morning repo (or repo only).  if exceeded, don't create arc (but finish checks for unreachable)
					if((repoBlkTm + endDuty->blockTm) > optParam.maxFlightTm)
						continue;  //but don't flag as unreachable
					//if last leg of duty is appointment and there is no final repo, crew need not stay on duty for the last leg
					//if((endDuty->crewEndTm - repoStartTm) > netMaxDutyTm)
					if((endDuty->crewEndTm - repoStartTm) > ((endDuty->twoDutyFlag == 1) ? netMaxDutyTm+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTm)) //2DutyDay - 05/21/10 ANG
						continue;  //but don't flag as unreachable

					//RLZ early duty rule
					//if (minutesPastMidnight((repoStartTm - optParam.preFlightTm)*60 , acList[p].availAirportID) <= optParam.cutoffForShortDuty){
					if (minutesPastMidnight((repoStartTm - acTypeList[j].preFlightTm)*60 , acList[p].availAirportID) <= optParam.cutoffForShortDuty){ //07/17/2017 ANG
						//if (endDuty->crewEndTm - repoStartTm > netMaxDutyTmEarly)
						if (endDuty->crewEndTm - repoStartTm > ((endDuty->twoDutyFlag == 1) ? netMaxDutyTmEarly+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTmEarly)) //2DutyDay - 05/21/10 ANG
							continue;				
					}					
					
					if(createArc == 0)
						continue;
					//If we have gotten through all the checks, arc is feasible.
					//allocate memory for arc
					newArc = arcAlloc(&acList[p].arcList, &arcCount);
					//create arc
					newArc->destDutyInd = k;
					newArc->blockTm = repoBlkTm + endDuty->blockTm;
					newArc->cost = (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost+ endDuty->cost - incentive; //includes cost of destination node

					//START - MAC - 09/23/08 ANG
					if(optParam.withMac == 1){
						newArc->tempCostForMac = endDuty->tempCostForMac - incentive; 
						newArc->repoFromAptID = acList[p].availAirportID;
						newArc->macRepoFltTm = repoFltTm; 
						newArc->macRepoStop = repoStops; 
					}
					//END - MAC

					newArc->startTm = repoStartTm;
					//increment arcTallyByDay 
					acList[p].arcTallyByDay[day]++;
					numPlaneArcs++;
				}
			} //end k loop
		} //end day loop
	} //end p loop
	return 0;
}




/************************************************************************************************************
*	Function	findPlanesWithConnxnConstraints						Date last modified:  05/13/07 SWO		*
*	Purpose:	Determine which planes can be picked up by a second crew prior to inclusions / exclusions	*
*				These planes require separate connection constraints, and separate crewPickup arcs.			*
*				Also determine which planes require separate networks during tour generation.				*
************************************************************************************************************/
/*Add an indicator BY DAY to planes: specConnxnConstr[MAX_WINDOW_DURATION + 1] indicates if plane can be picked up 
before any inclusions / exclusions, so special connection constraints are required in the formulation. 
Value of 1 indicates special constraint for pickup at START of trips, value of 2 indicates special constraint 
for pickup at END of trips, and value of 3 indicates special constraint for BOTH.
specConnxnConstr[MAX_WINDOW_DURATION] = 1 if there are special constraints for the plane on ANY day, = 2 if
plane is locked to two different crews in which case it must be picked up by the 2nd crew prior to the second crew's inclusions.*/

static int findPlanesWithConnxnConstraints(void)
{
	int cp, p, day, day1, day2, day3, day4, i, i2, j, n, x;
	int availCrews[MAX_AC_TYPES];
	int numSpecConnMac = 0;
	
	numSetsSpecConnConstr = 0;

	for(j = 0; j<MAX_AC_TYPES; j++){
		numSepNWByFleet[j] = 0;
		for(p = 0; p<MAX_PLANES_PER_FLEET; p++)
			separateNWByFleet[j][p] = -1;
	}

	//determine which fleets have available crewPairs (crews not yet flying the plane that they are locked to)
	//these are crews that could pickup a plane after a trip
	for(i = 0; i<MAX_AC_TYPES; i++)
		availCrews[i] = 0;
	for(cp = 0; cp < numOptCrewPairs; cp++){
//		if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd)
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		if(crewPairList[cp].hasFlownFirst != 1 || crewPairList[cp].optAircraftID != crewPairList[cp].aircraftID[0])
			availCrews[crewPairList[cp].acTypeIndex] = 1;
	}
	//determine if, and when, a plane needs special connection constraints
	for(p = 0; p < numAircraft; p++){
		//SEE NOTE BELOW
//		if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]> 0)
//			continue;  //plane is part of an acGroup for which we have already determined that special connxn constraints are required
		//if plane is not available until after the planning window, no special connection constraints are required for the plane
		if(acList[p].availDay > (optParam.planningWindowDuration - 1))
			continue;

		if(acList[p].multiCrew){ 
			day = max(crewPairList[acList[p].firstCrPrInd].endDay,acList[p].availDay);
			acList[p].specConnxnConstr[day+1]+= 1;//for pickup at start of trips
			acList[p].specConnxnConstr[day]+= 2;//for pickup at end of trips
			acList[p].specConnxnConstr[MAX_WINDOW_DURATION] = 1; //2;

			//Note: comment out codes below, acGroupInd for multiCrew = -1
			//if(acList[p].acGroupInd > 1){//plane is part of a group
			//	separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= -acList[p].acGroupInd;
			//	numSepNWByFleet[acList[p].acTypeIndex]++;
			//	acList[p].sepNW = 1;
			//	for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
			//		acList[acGroupList[acList[p].acGroupInd].acInd[x]].sepNW = 1;
			//}
			//else{//plane is NOT part of a group			
				separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= p;
				numSepNWByFleet[acList[p].acTypeIndex]++;
				numSetsSpecConnConstr++;
				acList[p].sepNW = 1;
			//}

			continue;
		}

		//START - MAC - 10/23/08 ANG
		if(optParam.withMac == 1){
			//if(acList[p].isMac == 1 && acList[p].sepNW != 1){ //for Mac, we separate them
			//if((acList[p].isMac == 1 || acList[p].applyCPACbonus == 1) && acList[p].sepNW != 1){ //for Mac or CPAC, we separate them - CPAC - 06/17/09 ANG
			if((acList[p].isMac == 1 || acList[p].applyCPACbonus == 1 || checkIfXlsPlus(acList[p].aircraftID) || checkIfCj4(acList[p].aircraftID)) && acList[p].sepNW != 1){ //in addtion, we also separate XLS+ - 06/09/11 ANG and CJ4 - 06/13/11 ANG
				day = acList[p].availDay;
				acList[p].specConnxnConstr[day+1]+= 1;//for pickup at start of trips
				acList[p].specConnxnConstr[day]+= 2;//for pickup at end of trips
				acList[p].specConnxnConstr[MAX_WINDOW_DURATION] = 1; //2;

				if(acList[p].acGroupInd > 1){//plane is part of a group
					separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= -acList[p].acGroupInd;
					numSepNWByFleet[acList[p].acTypeIndex]++;
					numSetsSpecConnConstr++;
					acList[p].sepNW = 1;
					for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
						acList[acGroupList[acList[p].acGroupInd].acInd[x]].sepNW = 1;
				}
				else{//plane is NOT part of a group			
					separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= p;
					numSepNWByFleet[acList[p].acTypeIndex]++;
					numSetsSpecConnConstr++;
					acList[p].sepNW = 1;
				}
				continue;
			}
		}
		//END - MAC - 10/23/08 ANG

		//if plane has no inclusions after its first day, //and no exclusions on or after its first day, (== if there is any exclusion)
		//no special connection constraints are required for the plane
		//(can't be picked up by a second crew prior to inclusions / exclusions)
		if(acList[p].lastExcl[optParam.planningWindowDuration - 1] == (acList[p].availDay > 0? acList[p].lastExcl[acList[p].availDay - 1] : -1) )
		{
			if( !optParam.withFlexOS )
			{
				if( acList[p].lastIncl[optParam.planningWindowDuration - 1] == acList[p].lastIncl[acList[p].availDay] )
					continue;
			} else if( acList[p].numIncl )//fei FA
			{
				for( i = acList[p].availDay + 1; i < optParam.planningWindowDuration; i ++ )
					if( acList[p].inclInfoP->isIncl[i] )
						break;

				if( i >= optParam.planningWindowDuration )
					continue;
			}
		}//end


		//&& ((!optParam.withFlexOS && acList[p].lastIncl[optParam.planningWindowDuration - 1] == acList[p].lastIncl[acList[p].availDay]) ||
		//( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->seIndByDay[1][optParam.planningWindowDuration - 1] == acList[p].inclInfoP->seIndByDay[1][acList[p].availDay])))
		//	continue;


		
		//if there is no available crew in the fleet (a crew that is not yet locked to a plane),
		//there are no special connection constraints for plane(can't be picked up)
		if(availCrews[acList[p].acTypeIndex] == 0)
			continue;
	
		//if plane has inclusions with more than one crew, or is assigned to a different crew prior to first inclusion, 
		//plane must be picked up between inclusions for first and second crews (regardless of end days)
		day2 = -1;
		//if(acList[p].firstCrPrID > 0 && acList[p].lastIncl[optParam.planningWindowDuration-1] > -1){//firstCrPrID is always populated for planes with inclusions (???? CHECK), and is equal to the crew  for any inclusions on the first day // fei Jan 2011
		//if(acList[p].firstCrPrID > 0 && (optParam.withFlexOS ? acList[p].numIncl : acList[p].lastIncl[optParam.planningWindowDuration-1]) > -1){//base
		if(acList[p].firstCrPrID > 0 && (optParam.withFlexOS ? acList[p].numIncl : (acList[p].lastIncl[optParam.planningWindowDuration-1] + 1)) > 0 ) {//fei FA
			day1 = acList[p].availDay;
			
			if( !optParam.withFlexOS ){
				for(day =acList[p].availDay; day<(optParam.planningWindowDuration - 1); day++){
					for(i = (acList[p].lastIncl[day]+ 1); i<=acList[p].lastIncl[day+1]; i++){
						if(acList[p].inclCrewID[i] == acList[p].firstCrPrID)
							day1 = day+1;
						else if(acList[p].inclCrewID[i] > 0){ // && acList[p].inclCrewID[i] != acList[p].firstCrPrID){
							day2 = day+1;
							break;
						}
					}
					if(day2 > -1){ //plane must be picked up by inclCrewID[i] between day1 and day2
						if(crewPairList[acList[p].firstCrPrInd].endDay + 1 < day2) //if the first crew must end tour earlier
							//than the day before day2
							day2 = crewPairList[acList[p].firstCrPrInd].endDay + 1;
						break; 
					}
				}
			} else // if (acList[p].numIncl)
			//look for the next inluded crew pair //fei Jan 2011
			//interval [ day1, day2] needs special connections
			//change to the next hard included crew pair or the last included crew pair //fei Jan 2011
			//day2 become later
			{
				_ASSERTE(acList[p].numIncl) ;
				for(day =acList[p].availDay; day<(optParam.planningWindowDuration - 1); day++)
				{
					for(i = acList[p].inclInfoP->seIndByDay[0][day+1]; i<acList[p].inclInfoP->seIndByDay[1][day+1]; i++)
					{ //fei Jan 2011
						if(acList[p].inclInfoP->inclCrewID[i] == acList[p].firstCrPrID) //fei Jan 2011
						{
							day1 = day+1;
							_ASSERTE( !origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl );
						} else if(acList[p].inclInfoP->inclCrewID[i] > 0 && ! origDemInfos[acList[p].inclInfoP->origDemIndices[i]].skipIncl )
						{ // && acList[p].inclInfoP->inclCrewID[i] != acList[p].firstCrPrID){
							day2 = day+1;
							break;
						}
					}
					if(day2 > -1)
					{ //plane must be picked up by inclCrewID[i] between day1 and day2
						if(crewPairList[acList[p].firstCrPrInd].endDay + 1 < day2) //if the first crew must end tour earlier
							//than the day before day2
							day2 = crewPairList[acList[p].firstCrPrInd].endDay + 1;
						break; 
					}
				}//end for
			}//end else

			for(day = day1; day<day2; day++){
				acList[p].specConnxnConstr[day+1]+= 1;//for pickup at start of trips
				acList[p].specConnxnConstr[day]+= 2;//for pickup at end of trips
			}
			//check if there is a third crew locked to plane
			//change to if there is a third hard included crew //fei Jan 2011
			//assume at most 3 crews?  //fei Jan 2011
			if(day2 > -1){
				day3 = day2;
				day4 = -1;

				if( ! optParam.withFlexOS )
				{
					for(day =day3; day<(optParam.planningWindowDuration-1); day++){
						for(i2 = (acList[p].lastIncl[day]+ 1); i2<=acList[p].lastIncl[day+1]; i2++){
							if(acList[p].inclCrewID[i2] == acList[p].inclCrewID[i])
								day3 = day+1;
							else if(acList[p].inclCrewID[i2] > 0){ // && acList[p].inclCrewID[i2] != acList.inclCrewID[i]){
								day4 = day+1;
								break;
							}
						}
						if(day4 > -1)//plane must be picked up by inclCrewID[i] (or inclInfoP->inclCrewID[i]) between day3 and day4
							break;  
					}
				} else// if ( acList[p].numIncl )//fei FA
				{
					_ASSERTE( acList[p].numIncl ) ;
					for(day =day3; day<(optParam.planningWindowDuration-1); day++)
					{
						for(i2 = acList[p].inclInfoP->seIndByDay[0][day+1]; i2<acList[p].inclInfoP->seIndByDay[1][day+1]; i2 ++)
						{
							if(acList[p].inclInfoP->inclCrewID[i2] == acList[p].inclInfoP->inclCrewID[i])
								day3 = day+1;
							else if(acList[p].inclInfoP->inclCrewID[i2] > 0 && ! origDemInfos[acList[p].inclInfoP->origDemIndices[i2]].skipIncl )
							{ // && acList[p].inclInfoP->inclCrewID[i2] != acList.inclInfoP->inclCrewID[i]){
								day4 = day+1;
								break;
							}
						}
						if(day4 > -1)//plane must be picked up by inclCrewID[i] (or inclInfoP->inclCrewID[i]) between day3 and day4
							break;  
					}
				}//end else

				for(day = day3; day<day4; day++)
				{
					acList[p].specConnxnConstr[day+1]+= 1;//for pickup at start of trips
					acList[p].specConnxnConstr[day]+= 2;//for pickup at end of trips
				}
			}
			//(We assume that there are no more than three crews for a plane within the planning window.  We further assume
			//that if there are three different crews on a plane within the window, the spread between day3 and day4 is small and we won't
			//bother checking if inclCrewID (inclInfoP->inclCrewID[i]) ends tour realier than the day before day2)
		}
		if(day2 > -1 ){//plane was assigned / locked to more than one crew and
			acList[p].specConnxnConstr[MAX_WINDOW_DURATION] = 2;
			//we have taken care of special connection constraints above, so do nothing additional
			continue;
		}
		else{//plane is not assigned to multiple crews, but may need special connection constraints
			//for pickup at start of trips, we need special constraints up to last day for both exclusions and inclusions
			//for pickup at end of trips, we need special constraints <= last day of exclusions, but < last day for inclusions.  
			//find last day of inclusions and last day of exclusions for plane
			day1 = -1;
			for(day = (optParam.planningWindowDuration - 1); day >= 0; day--){
				if ((!optParam.withFlexOS && (acList[p].lastIncl[day] > (day>0? acList[p].lastIncl[day-1] : -1))) ||
				    //( optParam.withFlexOS && acList[p].numIncl && (acList[p].inclInfoP->seIndByDay[1][day] > acList[p].inclInfoP->seIndByDay[0][day]))){ //fei Jan 2011
					( optParam.withFlexOS && acList[p].numIncl && acList[p].inclInfoP->isIncl[day] )){ //fei FA
					day1 = day; //last day of inclusions
					break;
				}
			}
			day2 = -1;
			for(day = (optParam.planningWindowDuration - 1); day >= 0; day--){
				if(acList[p].lastExcl[day] > (day>0? acList[p].lastExcl[day-1] : -1)){
					day2 = day; //last day of exclusions
					break;
				}
			}
	
			if(optParam.prohibitStealingPlanes == 1){
				if(acList[p].firstCrPrInd > -1){//The earliest day we must consider special constraints at trip end
					//is endRegDay for crewPair.  The earliest day we must consider special constraints
					//at trip starts is endRegDay+1 for crewPair.  After this day, look at potential end days for fleet (we
					//don't know who will pick it up next).
					if(crewPairList[acList[p].firstCrPrInd].endRegDay != PAST_WINDOW){
						day3 = crewPairList[acList[p].firstCrPrInd].endRegDay;
						if(day3< acList[p].availDay)
							day3 = acList[p].availDay;
						for(day = day3; day < optParam.planningWindowDuration; day++){
							//for pickups at start of trips
							if((day+1 <= day1 || day+1 <= day2) && potCrewEnds[acList[p].acTypeIndex][day]==1)
								acList[p].specConnxnConstr[day+1]+=1;
							//for pickups at end of trips
							if((day < day1 || day <= day2)&& potCrewEnds[acList[p].acTypeIndex][day] == 1)
								acList[p].specConnxnConstr[day]+=2;
						}
					}
				}
				else{//the only days we must consider special constraints at trip ends are potential end days for fleet.  
					//The only days we must consider special constraints at trip starts are (potential end day + 1) for fleet.
					for(day = acList[p].availDay; day < optParam.planningWindowDuration; day++){//can't pick up earlier than availDay
						//for pickups at start of trips
						if((day+1 <= day1  || day+1 <= day2) && potCrewEnds[acList[p].acTypeIndex][day]==1)
							acList[p].specConnxnConstr[day+1] += 1;
						//for pickups at end of trips
						if((day < day1 || day <= day2) && potCrewEnds[acList[p].acTypeIndex][day] == 1)
							acList[p].specConnxnConstr[day]+= 2;
					}
				}
			} //end if(optParam.prohibitStealingPlanes == 1..
			else{//(optParam.prohibitStealingPlanes == 0) 
				//we need special connection constraints starting from last day plane is locked to first crew (if any)
				//or from plane available day until end of inclusions/exclusions

				//find last day after avail day that plane is locked to first crew (if any)
				day3 = acList[p].availDay;
				if(acList[p].firstCrPrID > 0){//this is always populated for planes with inclusions, and is equal to the crew ID for any inclusions on the first day
					
					for(day =acList[p].availDay; day<(optParam.planningWindowDuration-1); day++){
						if (optParam.withFlexOS && acList[p].numIncl)
						{
							//_ASSERTE( acList[p].numIncl ) ;
							for(i = acList[p].inclInfoP->seIndByDay[0][day+1]; i<acList[p].inclInfoP->seIndByDay[1][day+1]; i++){ //fei Jan 2011
								if(acList[p].inclInfoP->inclCrewID[i] == acList[p].firstCrPrID)
									day3 = day+1;
							}
						}
						else {
							for(i = (acList[p].lastIncl[day]+ 1); i<=acList[p].lastIncl[day+1]; i++){
								if(acList[p].inclCrewID[i] == acList[p].firstCrPrID)
									day3 = day+1;
							}
						}
					}
				}
				for(day = day3; day < optParam.planningWindowDuration; day++){
					//for pickups at start of trips
					if(day+1 <= day1  || day+1 <= day2)
						acList[p].specConnxnConstr[day+1] += 1;
					//for pickups at end of trips
					if(day < day1 || day <= day2)
						acList[p].specConnxnConstr[day]+= 2;
				}
			} //end else(//optParam.prohibitStealingPlanes == 0)

			for(day = 0; day<optParam.planningWindowDuration; day++){
				if(acList[p].specConnxnConstr[day] > 0){
					acList[p].specConnxnConstr[MAX_WINDOW_DURATION] = 1;  
					break; //RLZ 04/13/08
				}
			}
			//WE CANNOT DO THIS, BECAUSE PLANES IN A GROUP MAY HAVE DIFFERENT AVAIL DAYS AND DIFF VALUES FOR specConnexnConstr[day]
			////if plane is part of a group, update special connection constraint info for group
			//if(acList[p].acGroupInd > 0){
			//	for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++){
			//		for(day = 0; day<=MAX_WINDOW_DURATION; day++)
			//			acList[acGroupList[acList[p].acGroupInd].acInd[x]].specConnxnConstr[day] = acList[p].specConnxnConstr[day];
			//	}
			//}
		} //end else{//plane is not assigned to multiple crews, but may need special connection constraints
	}//end for(p =
	
	//store indices of planes with special connections constraints for each fleet
	//if a plane is part of a group (acGroupList), then we need special connection constraints for acGroup and
	//we will store (-)index of group instead rather than plane index (acList index)
	for(p = 0; p<numAircraft; p++){
		if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]>0){
			if(acList[p].sepNW == 1)
				continue; //this plane is part of a group for which we already updated/stored spec connxn info
			else if(acList[p].acGroupInd > 1){ //if plane is part of a group for which we have not previously determined spec connxn requirement
				separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= -acList[p].acGroupInd;
				numSepNWByFleet[acList[p].acTypeIndex]++;
				numSetsSpecConnConstr++;
				for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
					acList[acGroupList[acList[p].acGroupInd].acInd[x]].sepNW = 1;
			}
			else{ //plane is NOT part of a group
				separateNWByFleet[acList[p].acTypeIndex][numSepNWByFleet[acList[p].acTypeIndex]]= p;
				numSepNWByFleet[acList[p].acTypeIndex]++;
				numSetsSpecConnConstr++;
				acList[p].sepNW = 1;		
			}
		}
	}

	/*Determine additional planes (besides those that require special connection constraints) that require separate networks during tour generation.
	If a plane can be picked up when next avail by a crew that is not currently locked to the plane 
	AND it has inclusions after, or exclusions on or after, its first day, then it (and any aircraft group it is in)
	requires a separate network during tour generation.*/
	if( !optParam.withFlexOS )
	{
		for(j = 0; j<numAcTypes; j++){
			p = 0;
			while(availACInd[j][p]> -1){
				n = availACInd[j][p];
				if ((acList[n].lastExcl[optParam.planningWindowDuration - 1] > (acList[n].availDay > 0? acList[n].lastExcl[acList[n].availDay - 1] : -1) 
					|| acList[n].lastIncl[optParam.planningWindowDuration - 1] >  acList[availACInd[j][p]].lastIncl[acList[availACInd[j][p]].availDay] )
					&& acList[n].sepNW == 0){
						//add plane to list
						if(acList[n].acGroupInd > 1){//plane is part of a group
							separateNWByFleet[acList[n].acTypeIndex][numSepNWByFleet[acList[n].acTypeIndex]]= -acList[n].acGroupInd;
							numSepNWByFleet[acList[n].acTypeIndex]++;
							acList[n].sepNW = 1;
							for(x = 0; x < acGroupList[acList[n].acGroupInd].numAircraft; x++)
								acList[acGroupList[acList[n].acGroupInd].acInd[x]].sepNW = 1;
						}
						else{//plane is NOT part of a group			
							separateNWByFleet[acList[n].acTypeIndex][numSepNWByFleet[acList[n].acTypeIndex]]= n;
							numSepNWByFleet[acList[n].acTypeIndex]++;
							acList[n].sepNW = 1;
						}
					}
					p++;
				}
		}
	} else //fei FA
	{
		for(j = 0; j<numAcTypes; j++){
			p = 0;
			while(availACInd[j][p]> -1){
				n = availACInd[j][p];

				i = optParam.planningWindowDuration ;//initialize infeasible, no incl/excl after/on acList[n].availDay
				if (acList[n].lastExcl[optParam.planningWindowDuration - 1] > (acList[n].availDay > 0? acList[n].lastExcl[acList[n].availDay - 1] : -1) )
					i = 0;//excl, set feasible
				else if (acList[n].numIncl )
				{
					for( i = acList[n].availDay + 1; i < optParam.planningWindowDuration; i ++ )
						if( acList[n].inclInfoP->isIncl[i] )
							break;//incl, set feasible
				}

				//if(((acList[n].lastExcl[optParam.planningWindowDuration - 1] > (acList[n].availDay > 0? acList[n].lastExcl[acList[n].availDay - 1] : -1) 
					//|| acList[n].lastIncl[optParam.planningWindowDuration - 1] >  acList[availACInd[j][p]].lastIncl[acList[availACInd[j][p]].availDay])
				//	|| ( optParam.withFlexOS && acList[n].numIncl && acList[n].inclInfoP->seIndByDay[1][optParam.planningWindowDuration - 1] >  acList[n].inclInfoP->seIndByDay[1][acList[n].availDay])))
				//	&& acList[n].sepNW == 0){
				if( i < optParam.planningWindowDuration && acList[n].sepNW == 0)//there is incl/excl after/on acList[n].availDay, and no sepNW 
				{
					//add plane to list
					if(acList[n].acGroupInd > 1){//plane is part of a group
						separateNWByFleet[acList[n].acTypeIndex][numSepNWByFleet[acList[n].acTypeIndex]]= -acList[n].acGroupInd;
						numSepNWByFleet[acList[n].acTypeIndex]++;
						acList[n].sepNW = 1;
						for(x = 0; x < acGroupList[acList[n].acGroupInd].numAircraft; x++)
							acList[acGroupList[acList[n].acGroupInd].acInd[x]].sepNW = 1;
					}
					else{//plane is NOT part of a group			
						separateNWByFleet[acList[n].acTypeIndex][numSepNWByFleet[acList[n].acTypeIndex]]= n;
						numSepNWByFleet[acList[n].acTypeIndex]++;
						acList[n].sepNW = 1;
					}
				}
				p++;
			}//end while
		}
	}//end else

	return 0;
}

/********************************************************************************************************
*	Function	createPickupTripLists							Date last modified:  04/12/07 SWO		*
*	Purpose:	For each fleet and each plane with special connection constraints, create lists of		*
*				trips before and after which planes can be picked up.									*
********************************************************************************************************/
static int createPickupTripLists(void)
{
	int j, i, p, x, x1, x2, crID1, day, d, canPickup, k;
	int canPUStart, canPUEnd;
	int numLists, index; 
	int *intPtr;
	Aircraft *plane;

	//allocate memory for pickupTripLists
	numLists = numAcTypes + numSetsSpecConnConstr;
	pickupTripList = (int **) calloc(numLists, sizeof(int *));
	puTripListInfo = (int **) calloc(numLists, sizeof(int *));
	for(j = 0; j< numLists; j++){  
		if ((intPtr = (int *)calloc(numOptDemand,sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d: Out of Memory in createPickupTripLists().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		pickupTripList[j] = intPtr;

		if((intPtr = (int *)calloc(3,sizeof(int))) == NULL) {
			logMsg(logFile,"%s Line %d: Out of Memory in createPickupTripLists().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		puTripListInfo[j] = intPtr;
	}
	//for each fleet, create lists of trips before /after which crews could potentially pick up planes... 
	for(j=0; j<numAcTypes; j++)
	{
		puTripListInfo[j][0] = -1; //for all fleets, value = -1
		//determine trips after which we can pickup a plane from another crew that left it there at the end
		for(day = 0; day < optParam.planningWindowDuration; day++){
			canPickup = 0;
			for(d = day; d < optParam.planningWindowDuration; d++){
				if(potCrewStarts[j][d]==1){ 
					canPickup = 1; //a crew can start on (or after) this day
					break;
				}
			}
			if(canPickup == 0)
				continue; //no crew can pick up after that trip
			if(potCrewEnds[j][day]==0) 
				continue; //a crew can't end on that day
			for(i = (day==0? 0:(lastTripOfDay[day-1]+1)); i <= lastTripOfDay[day]; i++){
				if(demandList[i].blockTm[j]==INFINITY)//if trip not feasible for fleet, continue
					continue;
				//If trip has a succeeding (following) trip tied to it, then the following trip must be included in the same duty as the first trip,
				//and we will not pickup a plane at the end of the first trip 
				if(demandList[i].succDemID > 0)
					continue;
				if(demandList[i].acInd > -1){//if trip is tied to a plane (it is an inclusion for the plane) 
					//if this is not the last inclusion for the plane that day, continue
					//Note: modify this for flex OS - FlexOS - 02/01/11 ANG
					plane = &acList[demandList[i].acInd];
					

					if ( !optParam.withFlexOS )
					{
						if(plane->inclDemandInd[plane->lastIncl[day]] != i)
							continue;
					} else// if ( plane->numIncl ) //fei FA
					{
						_ASSERTE( plane->numIncl ) ;
						//last hard inclusion on day > demand i
						//plane->inclInfoP->prevHardIncl is initialized to be -1
						if( plane->inclInfoP->prevHardIncl[plane->inclInfoP->seHardIndByDay[1][day]] > origDemInfos[demandList[i].origDemInd].inclusionInd ) //fei Jan 2011
							continue;
					}

					//if there is a special connection constraint for the plane, we generate separate pickup arc list below so don't add trip here
					if(plane->specConnxnConstr[MAX_WINDOW_DURATION]> 0)
						continue;
				}
				pickupTripList[j][i]+= 2;
				puTripListInfo[j][2]++;
			}
		}
		//create a list of trips before which we can pickup a plane from another crew that left it there at the start
		for(day = 1; day < optParam.planningWindowDuration; day++){
			if(potCrewStarts[j][day]==0) 
				continue; //a crew can't start on that day
			if(potCrewEnds[j][day-1]==0) 
				continue; //a crew can't end on the previous day //WOULD NEED TO CHANGE THIS TO ALLOW FINAL REPOSITION FOR SAME DAY TRIP
			for(i = (lastTripOfDay[day-1]+1); i <= lastTripOfDay[day]; i++){
				if(demandList[i].blockTm[j]==INFINITY)//if trip not feasible for fleet, continue
					continue;
				if(demandList[i].acInd > -1)//if trip is tied to a plane (it is an inclusion for the plane), then if it is possible to pick up the plane 
					//at start of this trip, we have special connxn constraints for the plane that day, and we generate separate pickup arc list below so 
					//we don't add trip here
						continue;
				//if earliest trip start is later than optParam.cutoffForFinalRepo, we won't do an evening (final) reposition for it
				if(demandList[i].earlyMPM > optParam.cutoffForFinalRepo)
					continue;
				//If trip has a preceeding trip tied to it, we will not pickup a plane at the start of this trip 
				if(demandList[i].predDemID > 0)
					continue;
				pickupTripList[j][i]+= 1;
				puTripListInfo[j][1]++;
			}
		}
	} //end for each fleet j loop

	//for each plane with special connection constraints, identify trips after/before which we can pickup the plane (from another crew that left it there)
	//note that for many cases, we don't NEED to pick up the plane on a day requiring special connection constraints;  we can pick it up later
	index = numAcTypes - 1;
	for(p = 0; p< numAircraft; p++){
		if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION] == 0 && acList[p].puTripListIndex < numAcTypes) // RLZ adds the &&
			acList[p].puTripListIndex = acList[p].acTypeIndex; //populate puTripListIndex on planes WITHOUT special connxn constraints
		else{ //if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]> 0)
			//if we have not already populated the index for this plane (as part of a group)
			if(acList[p].puTripListIndex < numAcTypes){
				if(acList[p].acGroupInd > 1 && acList[p].multiCrew == 0){//plane is part of a group
					index++;
					puTripListInfo[index][0] = -acList[p].acGroupInd;
					for(x = 0; x < acGroupList[acList[p].acGroupInd].numAircraft; x++)
						acList[acGroupList[acList[p].acGroupInd].acInd[x]].puTripListIndex = index;
				}
				else{//plane is not part of a group													
					index++;
					puTripListInfo[index][0] = p;
					acList[p].puTripListIndex = index;
				}
			}
			else { //already get the puTripListIndex and related info from other memeber in the group. RLZ 11/03/2008
				continue;
			}
			for(day = 0; day<optParam.planningWindowDuration; day++){
				j = acList[p].acTypeIndex;
				canPUStart = 0;
				canPUEnd = 0;
				//determine if we can pick up plane from end of duties on this day

				if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION] == 2){//plane is locked to > 1 crew so
					//we MUST pick up plane on a day with special connection constraints

					//determine if we can pick up plane from end of trips on this day
					if(acList[p].specConnxnConstr[day] == 2 || acList[p].specConnxnConstr[day] == 3)
						canPUEnd = 1;
					//determine if we can pick up plane from start of trips on the NEXT day (since we will repo to those trips on this day)
					if(day < optParam.planningWindowDuration - 1){//if this is not the last day in the window
						if(acList[p].specConnxnConstr[day+1] == 1 || acList[p].specConnxnConstr[day+1] == 3)
							canPUStart = 1;
					}
				}
				else{ //acList.specConnxnConstr[MAX_WINDOW_DURATION] == 1, so we need not pickup on a day with spec connxn constr; can pick up later
					
					//for end of trips, the day must be later than or equal to the first day of special connxn constr for this plane
					for(d = 0; d<=day; d++){
						if(acList[p].specConnxnConstr[d]== 2 || acList[p].specConnxnConstr[d] == 3){
							canPUEnd = 1;
							break;
						}
					}
					//for start of trips, the NEXT day must be later than or equal to the first day of special connxn constr
					for(d = 0; d<=day+1; d++){
						if(acList[p].specConnxnConstr[d]== 1 || acList[p].specConnxnConstr[d] == 3){
							canPUStart = 1;
							break;
						}
					}			
					//check that crew can dropoff plane that day 
					if(canPUEnd == 1 || canPUStart == 1){
						//if no crew can end that day
						if(potCrewEnds[j][day]== 0){
							canPUEnd = 0;
							canPUStart = 0;
						}
						//if a crew is locked to plane, check that crew can end that early
						else if(acList[p].firstCrPrInd > -1 && optParam.prohibitStealingPlanes == 1){
							if(day < crewPairList[acList[p].firstCrPrInd].endRegDay){
									canPUEnd = 0;
									canPUStart = 0;
							}
						}
					} //end if(canPUEnd
				} //end else{ //acList.specConnxnConstr[MAX_WINDOW_DURATION] == 1

				if(canPUEnd == 1 || canPUStart == 1){
					//Note: we should rewrite this piece of code to run from demandList instead of dutyList - 10/30/2008 ANG
					for(k = dutyTally[j][day][0].startInd; k<= dutyTally[j][day][8].endInd; k++){
						//check if reachable for plane
						//if(dutyList[j][k].unreachableFlag[acList[p].unreachableInd] >= 1)//changed from == 1 on 10/26/2008 - ANG
						if(acList[p].unreachableInd >= 0 && dutyList[j][k].unreachableFlag[acList[p].unreachableInd] >= 1)//10/30/2008 - ANG
							continue;
						if(dutyList[j][k].aircraftID > 0 && dutyList[j][k].aircraftID != acList[p].aircraftID)//Dont want to create pickupTripList for dutyList tied to certain aircraft - 11/13/08 ANG
							continue;
						if(dutyList[j][k].repoDemandInd > -1 && canPUStart == 1){ //final repo - can pickup at start of trip		
							i = dutyList[j][k].repoDemandInd;
							if(pickupTripList[acList[p].puTripListIndex][i] == 0 || pickupTripList[acList[p].puTripListIndex][i] == 2){ //thus far we have no indication that we can pickup at start
								pickupTripList[acList[p].puTripListIndex][i] += 1;  //can pick up plane at start of this trip - set value to 1 or 3, as applicable
								puTripListInfo[acList[p].puTripListIndex][1]++;
							}
						}
						else if(dutyList[j][k].repoDemandInd == -1 && canPUEnd == 1){ 
							//no final repo - can pickup at end of last trip IF there is no successor trip tied to this trip
							i = dutyList[j][k].lastDemInd;
							if(demandList[i].succDemID > 0)
								continue;
							if(pickupTripList[acList[p].puTripListIndex][i] < 2){ //thus far we have no indication that we can pickup at end of trip
								pickupTripList[acList[p].puTripListIndex][i] += 2; //can pick up plane at end of this trip - set value to 2 or 3, as applicable
								puTripListInfo[acList[p].puTripListIndex][2]++;
							}
						}
					}//end for(k = ...
				} //end if(canPU..
			}   //end for(day..
		} 	//end else{ //if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]> 0)	
	}  //end for(p..

	//planes can be picked up at the end of appointment legs even if crews left planes there much earlier.  make sure that appt legs
	//are included as potential pickup end locations
	for(i = 0; i<numOptDemand; i++){
		if(demandList[i].isAppoint > 0){
			p = demandList[i].acInd;
			j = acList[p].acTypeIndex;
			
			//if plane does NOT have special connection constraints (no exclusions, and inclusions on first available day of plane only)
			if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]== 0){
				//trip is potential pickup end location for fleet; puTripList[j][i] should be 2 or 3
				if(pickupTripList[j][i]==0 || pickupTripList[j][i]==1){
					pickupTripList[j][i]+=2;
					puTripListInfo[j][2]++;
				}
			}
			else{ // if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]> 0)
				//trip is potential pickup end location for plane and pickupTripList[acList[p].puTripListIndex][i] should be 2 or 3
				///UNLESS a crew must fly plane both before AND after this inclusion
				if(pickupTripList[acList[p].puTripListIndex][i]==0 || pickupTripList[acList[p].puTripListIndex][i]==1){

					if ( !optParam.withFlexOS )
					{
						x1 = 0;
						while(acList[p].inclDemandInd[x1] > -1){
							if(acList[p].inclDemandInd[x1] == i) //fei Jan 2011
								break;
							x1++;
						} //x1 is inclDemandInd for this appoint / maint leg
						x2 = x1;
						crID1 =0;
						while(x2>=0){
							if(acList[p].inclCrewID[x2] > 0){
								crID1 = acList[p].inclCrewID[x2];
								break;
							}
							x2--;
						}
						if(crID1 == 0 && acList[p].firstCrPrID > 0)
							crID1 = acList[p].firstCrPrID;
						if(crID1 > 0){//if a crew (crID1) must fly plane before / at this inclusion
							x2 = x1+1;
							while(acList[p].inclDemandInd[x2]> -1){
								if(acList[p].inclCrewID[x2] == crID1)
									break;
								x2++;								
							}
							//if we found the same crew ID before and after the end of the inclusion,
							//this is not a potential pickup location so continue to next trip
							if(acList[p].inclDemandInd[x2]> -1)
								continue;
						}
					} else// if ( acList[p].numIncl)
					{
						_ASSERTE( acList[p].numIncl && origDemInfos[demandList[i].origDemInd].inclusionInd >= 0 ) ; //fei Jan 2011

						/*
						x1 = 0;
						while(acList[p].inclInfoP->origDemIndices[x1] > -1){
							//if(acList[p].inclDemandInd[x1] == i)
							if(acList[p].inclInfoP->origDemIndices[x1] == demandList[i].origDemInd ) //fei Jan 2011
								break;
							x1++;
						} //x1 is inclDemandInd for this appoint / maint leg

						x2 = x1;
						crID1 =0;
						while(x2>=0){
							//if(acList[p].inclInfoP->inclCrewID[x2] > 0){
							//if( !acList[p].inclInfoP->inclDemandSkip[x2] && acList[p].inclCrewID[x2] > 0){  //fei Jan 2011
							if( !origDemInfos[acList[p].inclInfoP->origDemIndices[x2]].skipIncl && acList[p].inclInfoP->inclCrewID[x2] > 0){  //fei Jan 2011
								crID1 = acList[p].inclInfoP->inclCrewID[x2];
								break;
							}
							x2--;
						}
						if(crID1 == 0 && acList[p].firstCrPrID > 0)
							crID1 = acList[p].firstCrPrID;

						if(crID1 > 0){//if a crew (crID1) must fly plane before / at this inclusion
							x2 = x1+1;
							while(acList[p].inclInfoP->origDemIndices[x2]> -1){
								//if(acList[p].inclCrewID[x2] == crID1)
								//if(  !acList[p].inclInfoP->inclDemandSkip[x2] && acList[p].inclCrewID[x2] == crID1) //fei Jan 2011
								if( ! origDemInfos[acList[p].inclInfoP->origDemIndices[x2]].skipIncl && acList[p].inclInfoP->inclCrewID[x2] == crID1 )  //fei Jan 2011
									break;
								x2++;
							}
							//if we found the same crew ID before and after the end of the inclusion,
							//this is not a potential pickup location so continue to next trip
							if(acList[p].inclInfoP->origDemIndices[x2]> -1)
								continue;
						}
						*/

						//x1 is inclDemandInd for this appoint / maint leg
						crID1 =0;

						//fei FA
						for( x2 =  origDemInfos[demandList[i].origDemInd].inclusionInd; x2 >= 0; x2 -- )
							if( !origDemInfos[acList[p].inclInfoP->origDemIndices[x2]].skipIncl && acList[p].inclInfoP->inclCrewID[x2] > 0)
							{
								crID1 = acList[p].inclInfoP->inclCrewID[x2];
								break;
							}

						if(crID1 == 0 && acList[p].firstCrPrID > 0)
							crID1 = acList[p].firstCrPrID;

						if(crID1 > 0)//if a crew (crID1) must fly plane before / at this inclusion
						{
							for( x2 =  origDemInfos[demandList[i].origDemInd].inclusionInd+1; x2 < acList[p].numIncl; x2 ++ )
								if( !origDemInfos[acList[p].inclInfoP->origDemIndices[x2]].skipIncl && acList[p].inclInfoP->inclCrewID[x2] == crID1 )  //fei Jan 2011
									break;

							//if we found the same crew ID before and after the end of the inclusion,
							//this is not a potential pickup location so continue to next trip
							if(x2 < acList[p].numIncl)
								continue;
						}//end if(crID1 > 0)

					}//end else
					
					pickupTripList[acList[p].puTripListIndex][i]+=2;
					puTripListInfo[acList[p].puTripListIndex][2]++;
				} //end if (pickupTripList[acList[p].puTripListIndex][i]==0 || ...
			} //end else{ // if(acList[p].specConnxnConstr[MAX_WINDOW_DURATION]> 0)
		} //end if(demandList[i].isAppoint > 0){
	}  //end for(i = 0; i<numOptDemand; i++){
	return 0;
}





/*********************************************************************************************************
*	Function	createCrewArcsToPickups							Date last modified:  5/13/07 SWO		 *
*	Purpose:	For each crew that is NOT locked to a plane FROM THE START, create arcs to pickup nodes. *
*				The arcs include the cost to get to the plane, and the earliest possible start time		 *
*				 for a leg on that plane.																 *
*********************************************************************************************************/
static int createCrewArcsToPickups(void)
{
	int j, i, iMin, iMax, maxPUEDay, lastDay, cp;
	int maxPUSArcs, maxPUEArcs, crewPUSInd, crewPUEInd, isFeasible;
	int numLists, index; 
	CrewArc *newCrewArc;
	CrewArc tempArc;

	tempArc.acInd = -1;
	tempArc.demandInd = -1;
	tempArc.arcList = NULL;
	tempArc.numArcs = 0;
	
	numCrewPickupArcs = 0;
	
	numLists = numAcTypes + numSetsSpecConnConstr;

	//for each crewPair considered in optimization that is not locked to a plane when the plane is next available (they are not the first crew for the plane)
	for(cp = 0; cp < numOptCrewPairs; cp++){
		j = crewPairList[cp].acTypeIndex;
		if(crewPairList[cp].optAircraftID > 0){
			if(acList[crewPairList[cp].acInd].firstCrPrInd == cp)
				continue;
		}
//		if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd)
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;
		//if a crewPair is locked to a leg on a plane that they MUST pick up from another crew (we know from above
		//"if" statement that they are not locked to it at start), then we only need generate pickup arcs for this one plane.
		//and we have already considered this crew when generating special connxn constraints and pickupTripLists.
		//This plane is not part of a group, because it has an inclusion.
		if(crewPairList[cp].optAircraftID > 0){
			//search through puTripListInfo (the plane-specific indices) to find corresponding plane to get numArcs
			for(index = numAcTypes; index < numLists; index++){
				if(puTripListInfo[index][0] == crewPairList[cp].acInd)
					break;
			}
			//exit with error message if index was not found
			if(index == numLists){
				logMsg(logFile,"%s Line %d, puTripListInfo index for acList[%d] not found.\n", __FILE__,__LINE__,crewPairList[cp].acInd);
				writeWarningData(myconn); 
				//continue;
				exit(1);
			}
			//allocate memory for pointers to crew pickup arcs.  conservatively allocate maxPUSArcs and maxPUEArcs
			maxPUSArcs = puTripListInfo[index][1];
			if((crewPairList[cp].crewPUSList = (CrewArc **)calloc(maxPUSArcs, sizeof(CrewArc *))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			maxPUEArcs = puTripListInfo[index][2];
			if((crewPairList[cp].crewPUEList = (CrewArc **)calloc(maxPUEArcs, sizeof(CrewArc *))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			//initialize arc counters for crewPUSList and crewPUEList
			crewPUSInd = 0;
			crewPUEInd = 0;
			//create crewArcs
			for(i = 0; i<numOptDemand; i++){
				if(maxPUSArcs == 0 && maxPUEArcs == 0)
					break;
				//create pickup arc to start of trip if feasible
				if(pickupTripList[index][i] == 1 || pickupTripList[index][i]==3){
					if(!getCrewArcTimeAndCost(cp, demandList[i].outAirportID, demandList[i].early[j]*60, i, &tempArc)){
						if((crewPairList[cp].crewPUSList[crewPUSInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
							logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
						}
						newCrewArc = crewPairList[cp].crewPUSList[crewPUSInd];
						*newCrewArc = tempArc;
						newCrewArc->acInd = crewPairList[cp].acInd;
						newCrewArc->demandInd = i;
						crewPUSInd++;
					}
				}
				//create pickup arc to end of trip if feasible
				if(pickupTripList[index][i] == 2 || pickupTripList[index][i]==3){
					if(!getCrewArcTimeAndCost(cp, demandList[i].inAirportID, 60*(demandList[i].late[j] + demandList[i].elapsedTm[j]), -1, &tempArc)){
						if((crewPairList[cp].crewPUEList[crewPUEInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
							logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
						}
						newCrewArc = crewPairList[cp].crewPUEList[crewPUEInd];
						*newCrewArc = tempArc;
						newCrewArc->acInd = crewPairList[cp].acInd;
						newCrewArc->demandInd = i;
						crewPUEInd++;
					}
				}
			}
		} //end if(crewPairList[cp].optAircraftID > 0)

		//else, generate pickup arcs for (1) fleet and (2) all special-connection planes in the fleet that aren't locked to (other) crews as indicated by 
		//acList[p].specConnxnConstr[MAX_WINDOW_DURATION] = 2.   
		else{
			j = crewPairList[cp].acTypeIndex;
			lastDay= (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));

			maxPUSArcs = puTripListInfo[j][1];  //pickup start arcs for fleet (all days of window)
			maxPUEArcs = puTripListInfo[j][2];  //pickup end arcs for fleet (all days of window)
			for(index = numAcTypes; index < numLists; index++){
				if(puTripListInfo[index][0] >=0){ //list is associated with a single plane
					if(acList[puTripListInfo[index][0]].acTypeIndex == j && acList[puTripListInfo[index][0]].specConnxnConstr[MAX_WINDOW_DURATION] != 2){
						maxPUSArcs += puTripListInfo[index][1]; //pickup start arcs for plane (all days of window)
						maxPUEArcs += puTripListInfo[index][2]; //pickup end arcs for plane (all days of window)
					}
				}
				else{ //list is associated with a group of planes
					if(acList[acGroupList[-puTripListInfo[index][0]].acInd[0]].acTypeIndex == j){
						maxPUSArcs += puTripListInfo[index][1]; //pickup start arcs for plane (all days of window)
						maxPUEArcs += puTripListInfo[index][2]; //pickup end arcs for plane (all days of window)

					}
				}
			}
			//allocate memory for pointers to crewArcs - conservatively allocate maxPUSArcs and maxPUEArcs
			
			if((crewPairList[cp].crewPUSList = (CrewArc **)calloc(maxPUSArcs, sizeof(CrewArc *))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			if((crewPairList[cp].crewPUEList = (CrewArc **)calloc(maxPUEArcs, sizeof(CrewArc *))) == NULL) {
				logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			//initialize arc counters for crewPUSList and crewPUEList
			crewPUSInd = 0;
			crewPUEInd = 0;

			//Create crewPickupStart arcs. Crews can pick up planes left at the start of a trip from their start day until their end day.
			if(crewPairList[cp].startDay == 0) //we can't pick up a plane left at the start of a trip on the first day of the planning window;  
				iMin = lastTripOfDay[0]+1;  //rather, we will just pickup the plane when next available in this case			
			else
				iMin = lastTripOfDay[crewPairList[cp].startDay - 1]+1;
			for(i = iMin; i<=lastTripOfDay[lastDay]; i++){
				if(maxPUSArcs == 0)
					break;
				
				isFeasible = 0; //arc feasibility is yet unknown

				//create arc for fleet if feasible
				if(pickupTripList[j][i] == 1 || pickupTripList[j][i] == 3){
					if(!getCrewArcTimeAndCost(cp, demandList[i].outAirportID, demandList[i].early[j]*60, i, &tempArc)){
						isFeasible = 1; //trip is on pickupTripList for fleet AND arc is feasible
						if((crewPairList[cp].crewPUSList[crewPUSInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
							logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
						}
						newCrewArc = crewPairList[cp].crewPUSList[crewPUSInd];
						*newCrewArc = tempArc;
						newCrewArc->acInd =-1;
						newCrewArc->demandInd = i;
						crewPUSInd++;
					}
					else
						isFeasible = -1; //arc is not feasible for fleet or any other plane
				}
				//create arcs for planes with special connection constraints (from the crew's fleet) 
				//if feasible for plane, or for at least one plane in group
				if(isFeasible >= 0){
					for(index = numAcTypes; index < numLists; index++){
						if(puTripListInfo[index][0] >=0){ //list is associated with a single plane
							if(acList[puTripListInfo[index][0]].acTypeIndex != j || acList[puTripListInfo[index][0]].specConnxnConstr[MAX_WINDOW_DURATION] == 2)
								continue;
						}
						else if(acList[acGroupList[-puTripListInfo[index][0]].acInd[0]].acTypeIndex != j)
							continue;

						if(pickupTripList[index][i] == 1 || pickupTripList[index][i] == 3){ //trip is on pickup List for plane
							if(isFeasible == 0){ //still need to check feasibility of arc
								if(!getCrewArcTimeAndCost(cp, demandList[i].outAirportID, demandList[i].early[j]*60, i, &tempArc))
									isFeasible = 1; //arc is feasible, 
								else{
									isFeasible = -1; //arc is not feasible
									break;
								}
							}
							if(isFeasible == 1){
								if((crewPairList[cp].crewPUSList[crewPUSInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
									logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
									writeWarningData(myconn); exit(1);
								}
								newCrewArc = crewPairList[cp].crewPUSList[crewPUSInd];
								*newCrewArc = tempArc;
								newCrewArc->acInd = puTripListInfo[index][0]; //if negative, this is an acGroup, not a single plane index
								newCrewArc->demandInd = i;
								crewPUSInd++;
							}
						}
					}
				}
			} //end i loop for pickup Start

			//Create crewPickupEnd arcs.  Crews can pickup planes left at the end of a trip up until the day prior to their end day
			//(assume that crew doesn't do a same-day duty following another crew on their last day)
			maxPUEDay= (crewPairList[cp].endDay < optParam.planningWindowDuration? (crewPairList[cp].endDay - 1) : (optParam.planningWindowDuration - 1));
			if(maxPUEDay > -1)
				iMax = lastTripOfDay[maxPUEDay];
			else
				iMax = -1;
			for(i = 0; i<=iMax; i++){
				if(maxPUEArcs == 0)
					break;

				isFeasible = 0; //arc feasibility is yet unknown

				//create arc for fleet if feasible
				if(pickupTripList[j][i] == 2 || pickupTripList[j][i]==3){
					if(!getCrewArcTimeAndCost(cp, demandList[i].inAirportID, 60*(demandList[i].late[j] + demandList[i].elapsedTm[j]), -1, &tempArc)){
						isFeasible = 1;
						if((crewPairList[cp].crewPUEList[crewPUEInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
							logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
						}
						newCrewArc = crewPairList[cp].crewPUEList[crewPUEInd];
						*newCrewArc = tempArc;
						newCrewArc->acInd = -1;
						newCrewArc->demandInd = i;
						crewPUEInd++;
					}
					else
						isFeasible = -1; //arc is not feasible for fleet or any other plane
				}
				//create arcs for planes with special connection constraints (from the crew's fleet)
				//if feasible for plane, or for at least one plane in group
				if(isFeasible >=0){
					for(index = numAcTypes; index < numLists; index++){
						if(puTripListInfo[index][0] >=0){ //list is associated with a single plane
							if(acList[puTripListInfo[index][0]].acTypeIndex != j || acList[puTripListInfo[index][0]].specConnxnConstr[MAX_WINDOW_DURATION] == 2)
								continue;
						}
						else if(acList[acGroupList[-puTripListInfo[index][0]].acInd[0]].acTypeIndex != j)
							continue;
						if(pickupTripList[index][i] == 2 || pickupTripList[index][i] == 3){ //trip is on pickup List for plane
							if(isFeasible == 0){//still need to check feasibility of arc
								if(!getCrewArcTimeAndCost(cp, demandList[i].inAirportID, 60*(demandList[i].late[j] + demandList[i].elapsedTm[j]), -1, &tempArc))
									isFeasible = 1; //arc is feasible
								else{
									isFeasible = -1; //arc is not feasible
									break;
								}
							}
							if(isFeasible == 1){				
								if((crewPairList[cp].crewPUEList[crewPUEInd] = (CrewArc *)calloc(1, sizeof(CrewArc))) == NULL){
									logMsg(logFile,"%s Line %d, Out of Memory in createCrewArcsToPickups().\n", __FILE__, __LINE__);
									writeWarningData(myconn); exit(1);
								}
								newCrewArc = crewPairList[cp].crewPUEList[crewPUEInd];
								*newCrewArc = tempArc;
								newCrewArc->acInd = puTripListInfo[index][0]; //if negative, this is an acGroup, not a single plane index
								newCrewArc->demandInd = i;
								crewPUEInd++;
							}
						}
					}
				}
			} //end i loop for pickup End

		} //end else (crewPair is not locked to a plane that it mustPickup from another crew)loop

		crewPairList[cp].numPUStartArcs = crewPUSInd;
		crewPairList[cp].numPUEndArcs = crewPUEInd;
		numCrewPickupArcs += (crewPUSInd + crewPUEInd);
	} //end cp loop
	return 0;
}
/************************************************************************************
*	Function   createPickupArcs			      Date last modified:  4/12/07 SWO		*
*	Purpose:	Create arcs from each trip at which planes can be picked up			*
*				to all duty nodes that can be directly								*
*				reached by a plane picked up at that trip (start and/or end)		*
*				in order to expedite generation of crew arcs from pickups to first	*
*				duties.	 Create lists from pickup at start and from pickup at end	* 
*				for each fleet.  Specific planes (for planes with special connxn	*
*				constraints) will be considered in the crew arc generation but not	*
*				here.																*
************************************************************************************/

static int createPickupArcs(void)
{
	int j, day1, day2, i, isFeasible, index, k, z, startAptID, endAptID, repoStartTm;
	Duty *duty;
	Demand *trip;
	NetworkArc *newArc;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int ind; //fei Jan 2011

	numPickupArcs = 0;

	//For pickup at end of trips. Include last day of planning window, as we can pick up a plane and do a same-day duty.  
	//Include last trip of day, as duty might be repo (for next day) only.
	for(day1 = 0; day1 < optParam.planningWindowDuration; day1++){
		for(i = (day1 == 0? 0: lastTripOfDay[day1-1]+1); i<=lastTripOfDay[day1]; i++){
			//allocate memory for puEnd arc list per fleet out of trip. will allocate memory for arc structures as needed below.
			demandList[i].puEArcList = (NetworkArc **) calloc(numAcTypes, sizeof(NetworkArc *));
			if(!demandList[i].puEArcList) {
				logMsg(logFile,"%s Line %d, Out of Memory in createPickupArcs().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			trip = &demandList[i];
			for(j= 0; j<numAcTypes; j++){
				//determine if pickup at end of trip is feasible for either the fleet or 
				//for a plane with special connxn constraints from that fleet
				isFeasible = 0;
				if(pickupTripList[j][i] == 2 || pickupTripList[j][i]==3)
					isFeasible = 1;
				else{
					for(index = numAcTypes; index < (numAcTypes + numSetsSpecConnConstr); index++){
						if(puTripListInfo[index][0] >=0){ //list is associated with a single plane
							if(acList[puTripListInfo[index][0]].acTypeIndex == j){
								if(pickupTripList[index][i] == 2 || pickupTripList[index][i] == 3){
									isFeasible = 1;
									break;
								}
							}
						}
						else{ //list is associated with a group of planes
							if(acList[acGroupList[-puTripListInfo[index][0]].acInd[0]].acTypeIndex == j){
								if(pickupTripList[index][i] == 2 || pickupTripList[index][i] == 3){
									isFeasible = 1;
									break;
								}
							}
						}
					}
				}
				//if can't pickup up at end of trip for this fleet, continue to next fleet
				if(isFeasible == 0)
					continue;
				
				//trip->numPUEArcs[MAX_AC_TYPES] will be zero after calloc;  no need to initialize
				//initialize counts per arcList for this trip / fleet
				countsPerArcList[j] = 0;

				for(day2 = day1; day2 < optParam.planningWindowDuration; day2++){
					for(k=dutyTally[j][day2][0].startInd; k<=dutyTally[j][day2][8].endInd; k++){
						duty = &dutyList[j][k];
						//make sure duty starts later than trip ends
						if((trip->late[j] + trip->elapsedTm[j] + optParam.turnTime) > duty->startTm[0]) //fei FA: note: checked
							continue;
						//If duty ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
						//and we will not create an arc to the duty. 
						if(duty->lastDemInd > -1 && demandList[duty->lastDemInd].succDemID > 0)
							continue;
						//if trip is tied to a plane...
						if(trip->acInd > -1){
							if(duty->aircraftID != 0 && duty->aircraftID != demandList[i].aircraftID) //duty must not be tied to a different plane						
								continue;
							z = acList[trip->acInd].unreachableInd;
							//duty must be reachable for that plane (includes check of inclusions/exclusions)
							if(z>-1 && duty->unreachableFlag[z] == 1)
								continue;
			
							if ( !optParam.withFlexOS )
							{
								//if duty is not the same day or the day after the trip, make sure we didn't skip a day with inclusions
								//Note: we may skip some inclusions for flex OS - FlexOS - 02/01/11 ANG
								if(day2 - day1 > 1)
									if(acList[trip->acInd].lastIncl[day2-1] > acList[trip->acInd].lastIncl[day1])
										continue;
							} else// if ( acList[trip->acInd].numIncl)
							{
								_ASSERTE( acList[trip->acInd].numIncl) ;
								//fei Jan 2011: note: check whether the demand is in duty
								for( ind = 0; ind < maxTripsPerDuty; ind ++ )
									if( trip->origDemInd == demandList[duty->demandInd[ind]].origDemInd )
										break;

								if( ind < maxTripsPerDuty )
									continue;

								if( duty->firstInclInd >= 0 && duty->firstInclInd <= origDemInfos[trip->origDemInd].inclusionInd )
									continue;
								//end fei Jan 2011
				
								//if duty is not the same day or the day after the trip, make sure we didn't skip a day with inclusions
								if(day2 - day1 > 1)
									//if( acList[trip->acInd].inclInfoP->seHardIndByDay[0][day2] > acList[trip->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei Jan 2011
									if( acList[trip->acInd].inclInfoP->seHardIndByDay[1][day2-1] > acList[trip->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei FA
										continue;
							}//end else
						}
						//else if trip is NOT tied to a plane and duty is, make sure both trip and duty are reachable for that plane, 
						//and that we don't skip any inclusions for plane
						else if(duty->acInd > -1){
							z = acList[duty->acInd].unreachableInd;
							if(z>-1 && duty->unreachableFlag[z] == 1)
								continue;
							if(pickupTripList[acList[duty->acInd].puTripListIndex][i] == 0 || pickupTripList[acList[duty->acInd].puTripListIndex][i] == 1)
								continue; //can't pick up at end of trip for that plane	

							if ( !optParam.withFlexOS )
							{
								if(day2 - day1 > 1)
									if(acList[duty->acInd].lastIncl[day2-1] > acList[duty->acInd].lastIncl[day1])//make sure we didn't skip any inclusions for plane
										//Note: we may skip some inclusions for flex OS - FlexOS - 02/01/11 ANG
										continue;
							} else// if (acList[duty->acInd].numIncl)
							{
								_ASSERTE( acList[duty->acInd].numIncl ) ;
								if(day2 - day1 > 1)
									//if( acList[duty->acInd].inclInfoP->seHardIndByDay[0][day2] > acList[duty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei Jan 2011
									if( acList[duty->acInd].inclInfoP->seHardIndByDay[1][day2-1] > acList[duty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei FA
										continue;
							}//end else
						}
						startAptID = trip->inAirportID;
						if(duty->demandInd[0] != -1)
							endAptID = demandList[duty->demandInd[0]].outAirportID;
						else 
							endAptID = demandList[duty->repoDemandInd].outAirportID;
						//if no reposition to duty
						if(startAptID == endAptID){	//if this is a repo only node and there is no reposition required, don't create arc
							if(duty->demandInd[0] == -1)
								continue;
							//no need to check duty time here, as we have already checked duty time for the duty node, and we aren't adding any significant duty time
							//We assume fboTransit is not part of blockTm (plane may be towed, may be inconsequential) or duty time if at start/end of day per BILL HALL 4/12/06
							
							//If we have gotten through all the checks, the arc is feasible.
							//allocate memory for arc
							newArc = arcArrayAlloc(trip->puEArcList, j);
							//create arc
							newArc->destDutyInd = k;
							newArc->blockTm = duty->blockTm;
							newArc->cost = duty->cost; //includes cost of destination node

							if(optParam.withMac == 1){
								newArc->tempCostForMac = duty->tempCostForMac; //MAC - 09/23/08 ANG
								newArc->repoFromAptID = 0; //MAC - 09/23/08 ANG - Note: no repo required to this repo only node
								newArc->macRepoFltTm = 0; 
								newArc->macRepoStop = 0;
							}

							//assume fboTransit is not part of blockTm (may be towed, may be inconsequential) per BILL HALL 4/12/06
							//newArc->startTm = duty->startTm[0];  //RLZ CHECK
							newArc->startTm = duty->crewStartTm; 
							//increment arc count
							trip->numPUEArcs[j]++;
						} //end no repo loop
						else //we need to repo (or repo only) to duty
						{
							getFlightTime(startAptID, endAptID, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
							//check repo flight time against limit
							if(repoFltTm > optParam.maxRepoTm)
								continue;
							repoStartTm = getRepoDepartTm(startAptID, endAptID, duty->startTm[0] - optParam.turnTime, repoElapsedTm);
							//check if repo is feasible considering curfews
							if(repoStartTm == -1)
								continue;
							//check that we can get to duty on time
							if((trip->late[j] + trip->elapsedTm[j] + optParam.turnTime) > repoStartTm)//fei FA: note: checked
								continue;
							//check block time and duty time
							if((repoBlkTm + duty->blockTm) > optParam.maxFlightTm) 
								continue;
							//if last leg of duty is appointment and there is no final repo, crew need not stay on duty for the last leg
							//if((duty->crewEndTm - repoStartTm) > netMaxDutyTm)
							if((duty->crewEndTm - repoStartTm) > ((duty->twoDutyFlag == 1) ? netMaxDutyTm+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTm)) //2DutyDay - 05/21/10 ANG
								continue;

							//RLZ early duty rule
							//if (minutesPastMidnight((repoStartTm - optParam.preFlightTm)*60 , startAptID) <= optParam.cutoffForShortDuty){
							if (minutesPastMidnight((repoStartTm - acTypeList[j].preFlightTm)*60 , startAptID) <= optParam.cutoffForShortDuty){ //07/17/2017 ANG
								//if (duty->crewEndTm - repoStartTm> netMaxDutyTmEarly)
								if (duty->crewEndTm - repoStartTm> ((duty->twoDutyFlag == 1) ? netMaxDutyTmEarly+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTmEarly)) //2DutyDay - 05/21/10 ANG
									continue;				
							}
							//If we have gotten through all the checks, arc is feasible.
							//allocate memory for arc
							newArc = arcArrayAlloc(trip->puEArcList, j);
							//create arc
							newArc->destDutyInd = k;
							newArc->blockTm = repoBlkTm + duty->blockTm;
							newArc->cost = (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost+ duty->cost; //includes cost of destination node

							//START - MAC - 08/19/08 ANG
							if(optParam.withMac == 1){
								newArc->tempCostForMac = duty->tempCostForMac; 
								newArc->repoFromAptID = startAptID;
								newArc->macRepoFltTm = repoFltTm; 
								newArc->macRepoStop = repoStops; 
							}
							//END - MAC

							newArc->startTm = repoStartTm;
							//increment arc count
							trip->numPUEArcs[j]++;
						} //end else //we need to repo loop
					} //end for(k=
				} //end for(day2...
				numPickupArcs += trip->numPUEArcs[j];
			} //end for(j = 
		} //end for(i = loop FOR PICKUP AT END
	} //end for(day1 = loop FOR PICKUP AT END

	//For pickup at start of trip. Can't pick up a plane left at start of a day 0 trip - this is just next avail location for plane.
	for(day1 = 1; day1 < optParam.planningWindowDuration; day1++){
		for(i = (lastTripOfDay[day1-1]+1); i<=lastTripOfDay[day1]; i++){
			//allocate memory for puStart arc list per fleet out of trip. will allocate memory for arc structures as needed below.
			demandList[i].puSArcList = (NetworkArc **) calloc(numAcTypes, sizeof(NetworkArc *));
			if(!demandList[i].puSArcList) {
				logMsg(logFile,"%s Line %d, Out of Memory in createPickupArcs().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
			trip = &demandList[i];
			for(j= 0; j<numAcTypes; j++){
				//determine if pickup at start of trip is feasible for either the fleet or 
				//for a plane with special connxn constraints from that fleet
				isFeasible = 0;
				if(pickupTripList[j][i] == 1 || pickupTripList[j][i]==3)
					isFeasible = 1;
				else{
					for(index = numAcTypes; index < (numAcTypes + numSetsSpecConnConstr); index++){
						if(puTripListInfo[index][0] >=0){ //list is associated with a single plane
							if(acList[puTripListInfo[index][0]].acTypeIndex == j){
								if(pickupTripList[index][i] == 1 || pickupTripList[index][i] == 3){
									isFeasible = 1;
									break;
								}
							}
						}
						else{ //list is associated with a group of planes
							if(acList[acGroupList[-puTripListInfo[index][0]].acInd[0]].acTypeIndex == j){
								if(pickupTripList[index][i] == 1 || pickupTripList[index][i] == 3){
									isFeasible = 1;
									break;
								}
							}
						}
					}
				}
				//if can't pickup up at start of trip for this fleet, continue to next fleet
				if(isFeasible == 0)
					continue;

				//trip->numPUSArcs[MAX_AC_TYPES] will be zero after calloc;  no need to initialize
				//initialize counts per arcList for this trip / fleet
				countsPerArcList[j] = 0;

				//for each duty on that day, check if duty starts with the trip that we repositioned to
				for(k=dutyTally[j][day1][0].startInd; k<=dutyTally[j][day1][8].endInd; k++){
					duty = &dutyList[j][k];
					//duty must start with the trip that we positioned to
					if(i != duty->demandInd[0])
						continue;
					//If duty ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
					//and we will not create an arc to the duty. 
					if(duty->lastDemInd > -1 && demandList[duty->lastDemInd].succDemID > 0)
						continue;
					//we must make the following checks before creating an arc because it is only in arc creation that we check if a duty node includes all
					//inclusions for a plane, and if it is reachable for a plane
					//if trip is tied to a plane...  
					if(trip->acInd > -1){
						//CHECK NOT REQUIRED.  DUTY MUST START WITH THIS TRIP AND THUS MUST BE TIED TO THE SAME PLANE
						//if(duty->aircraftID != 0 && duty->aircraftID != demandList[i].aircraftID) //duty must not be tied to a different plane
						//		continue;
						z = acList[trip->acInd].unreachableInd;
						//duty must be reachable for that plane (includes check of inclusions/exclusions)
						if(z>-1 && duty->unreachableFlag[z] == 1)
							continue;
					}
					//else if trip is NOT tied to a plane and duty is, make sure both trip and duty are reachable for that plane
					else if(duty->acInd > -1){
						z = acList[duty->acInd].unreachableInd;
						if(z>-1 && duty->unreachableFlag[z] == 1)
							continue;
						if(pickupTripList[acList[duty->acInd].puTripListIndex][i] == 0 || pickupTripList[acList[duty->acInd].puTripListIndex][i] == 1)
							continue; //can't pick up at end of trip for that plane	
					}
					//If we have gotten through all the checks, the arc is feasible.
					//allocate memory for arc
					newArc = arcArrayAlloc(trip->puSArcList, j);
					//create arc
					newArc->destDutyInd = k;
					newArc->blockTm = duty->blockTm;
					newArc->cost = duty->cost; //includes cost of destination node

					if(optParam.withMac == 1){
						newArc->tempCostForMac = duty->tempCostForMac; //MAC - 09/23/08 ANG
						newArc->repoFromAptID = 0; //MAC - 09/23/08 ANG - Note: no addl repo since duty starts with the trip that we repositioned to
						newArc->macRepoFltTm = 0; 
						newArc->macRepoStop = 0; 
					}

					//assume fboTransit is not part of blockTm (may be towed, may be inconsequential) per BILL HALL 4/12/06
					//newArc->startTm = duty->startTm[0]; //RLZ CHECK
					newArc->startTm = duty->crewStartTm;
					//increment arc count
					trip->numPUSArcs[j]++;
				} //end for(k= loop
				numPickupArcs += trip->numPUSArcs[j];
			}  //end for(j= loop
		} //end for(i = loop FOR PICKUP AT START
	} //end for(day1 = loop FOR PICKUP AT START

	return 0;
}


/********************************************************************************************************
*	Function	createArcsToFirstDuties								Date last modified:  05/13/07 SWO	*
*	Purpose:	For each crew, create arcs from planes and pickup nodes (start/end of trips)			*
*				to the first duty that the crewPair will cover.	Free plane & pickup arcs when done		*												
********************************************************************************************************/
static int createArcsToFirstDuties(void)
{
	int cp, x, y, p, a, aMin, lastDay, i, j, k, aircraftID, canReach, flag;
	NetworkArc tempArc;
	NetworkArc *newArc;
	CrewArc *crewArc;
	extern int maxTripsPerDuty;

	//START - Prevent wrong crewPairs perform fake airport assignment - 04/22/08 ANG
	CrewEndTourRecord *xPtr; 
	int ty, flag2; 
	extern CrewEndTourRecord *crewEndTourList;
	extern int crewEndTourCount;

	xPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));
	if(! xPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory while creating crewEndTourRecord pointer in createArcsToFirstDuties().\n", __FILE__,__LINE__);
		exit(1);
	}
	//END - Prevent wrong crewPairs perform fake airport assignment - 04/22/08 ANG

	numArcsToFirstDuties = 0;

	logMsg(logFile, "numOptCrewPairs = %d\n", numOptCrewPairs);
	for(cp = 0; cp<numOptCrewPairs; cp++){
		//if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd)
		//logMsg(logFile, "processing cp = %d\n", cp);
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		//logMsg(logFile, "startDay for cp %d is within the window\n", cp);
		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;
		//logMsg(logFile, "availDT for both crews <= pairEndTm\n");
		j = crewPairList[cp].acTypeIndex;
		lastDay= (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));
		//logMsg(logFile, "  lastDay for cp %d = %d\n", cp, lastDay);

		//logMsg(logFile, "  crewPairList[%d].numPlaneArcs = %d\n", cp, crewPairList[cp].numPlaneArcs);
		//create arcs from planes to first duties
		for(x = 0; x<crewPairList[cp].numPlaneArcs; x++){
			//initialize arc count
			crewArc = &crewPairList[cp].crewPlaneList[x];
			crewArc->numArcs = 0;
			p = crewArc->acInd;

			//consider first duties between start and end days of crewPair (or last day of window)
			aMin = (crewPairList[cp].startDay == 0?  0 : acList[p].arcTallyByDay[crewPairList[cp].startDay - 1]+1);

			//logMsg(logFile, "    processing cp #%d numPlaneArcs #%d going to acInd %d, aMin = %d, acList[%d].arcTallyByDay[%d] = %d\n", cp, x, p, aMin, p, lastDay, acList[p].arcTallyByDay[lastDay]);

			for(a = aMin; a <= acList[p].arcTallyByDay[lastDay]; a++){
				k = acList[p].arcList[a].destDutyInd;
				//logMsg(logFile, "      processing a=%d k=%d\n", a, k);

				//START - Prevent wrong crewPairs perform fake airport assignment - 04/22/08 ANG
				flag2 = 1; //indicator whether we have to add arc to first duty
				if(optParam.autoFlyHome == 1)
				{
					for (i = 0; i < maxTripsPerDuty; i++)
					{ //for every demand included in the dutyList[j][k]
						//if it is a fake airport assignment
						if (demandList[dutyList[j][k].demandInd[i]].isAppoint==4)
						{
							//search for corresponding crew in crewEndTourList using the assignedDemandID field
							for(ty = 0, xPtr = crewEndTourList;  ty < crewEndTourCount; ++ty, ++xPtr)
							{
								/*
								if (demandList[dutyList[j][k].demandInd[i]].demandID == xPtr->assignedDemandID){
									//if demandID found, check if crewpair match
										if(!(crewPairList[cp].captainID == xPtr->crewID1 ||
											crewPairList[cp].flightOffID == xPtr->crewID2 ||
											crewPairList[cp].captainID == xPtr->crewID2 ||
											crewPairList[cp].flightOffID == xPtr->crewID1))
												flag2 = 0;
										break;
									}//end if
								*/
								
								//fei FA
								for(y=0; y < xPtr->numAD; y ++)
								{
									if (demandList[dutyList[j][k].demandInd[i]].demandID == xPtr->assignedDemandID[y])
									{
										//if demandID found, check if crewpair match
										if(!(crewPairList[cp].captainID == xPtr->crewID1 ||
											crewPairList[cp].flightOffID == xPtr->crewID2 ||
											crewPairList[cp].captainID == xPtr->crewID2 ||
											crewPairList[cp].flightOffID == xPtr->crewID1))
										{
												flag2 = 0;
												goto createArcsToFirstDutiesStop; //not feasible
										}
										break; //need to go to the next demand
									}
								}//end for(y=0; y < xPtr->numAD; y ++)

								if( y < xPtr->numAD )//demand is found, go to the next demand
									break;

							}//end for(ty = 0,
						}//end if (demandList[dutyList[j][k].demandInd[i]].isAppoint==4)
						
						//if (flag2 == 0)
						//	break;
					}//end for (i = 0; i < maxTripsPerDuty; i++)
				}//end if(optParam.autoFlyHome == 1)
				
createArcsToFirstDutiesStop:
				if (flag2 == 0){
					//logMsg(logFile, "it should never come here");
					continue;
				}
				//END - Prevent wrong crewPairs perform fake airport assignment - 04/22/08 ANG

				//START - If this is from an infeasible existing solution, try to create arc anyway - 05/19/08 ANG
				if (optParam.inclInfeasExgSol == 1){
					//logMsg(logFile, "      inclInfeasExgSol for a=%d j=%d k=%d\n", a, j, k);
					if(calculateArcsToFirstDuties2(cp, j, crewArc, &acList[p].arcList[a], k, -1, &tempArc) == 0){
						//logMsg(logFile, "      calculateArcsToFirstDuties2 returns 0 for a=%d j=%d k=%d\n", a, j, k);
						newArc = arcAlloc(&crewArc->arcList, &crewArc->numArcs);	
						*newArc = tempArc;

						//START - MAC - 09/23/08 ANG
						if(optParam.withMac == 1){
							newArc->tempCostForMac += getChangePenalty(&dutyList[j][k], acList[p].aircraftID, crewPairList[cp].crewPairID);
						}
						//END - MAC

						newArc->cost += getChangePenalty(&dutyList[j][k], acList[p].aircraftID, crewPairList[cp].crewPairID);

						numArcsToFirstDuties++;
						//fprintf(logFile, "Added arc for crewPairID %d, from aircraftID %d to dutyList[%d][%d] with demandIDs %d, %d, %d, %d.\n", crewPairList[cp].crewPairID, acList[crewArc->acInd].aircraftID, j, k, demandList[dutyList[j][k].demandInd[0]].demandID, ((dutyList[j][k].demandInd[1] > 0) ? demandList[dutyList[j][k].demandInd[1]].demandID : 0), ((dutyList[j][k].demandInd[2] > 0) ? demandList[dutyList[j][k].demandInd[2]].demandID : 0), ((dutyList[j][k].demandInd[3] > 0) ? demandList[dutyList[j][k].demandInd[3]].demandID : 0));
					}
				}
				//END - 05/19/08 ANG

				//if plane can't be picked up by the plane arc start time, continue
				//if(crewArc->earlyPickupTm[2] > acList[p].arcList[a].startTm)  // RLZ CHECK
				if(crewArc->earlyPickupTm[2] > acList[p].arcList[a].startTm && dutyList[j][k].crewStartTm <= dutyList[j][k].crewEndTm ) // RLZ CHECK
					continue;
				if(crewArc->earlyPickupTm[2] > dutyList[j][k].startTm[0] && dutyList[j][k].crewStartTm > dutyList[j][k].crewEndTm ) // RLZ CHECK
					continue;


				//check get-home feasibility for crew and duty if we are considering sending crew home in this window
				if(crewPairList[cp].endRegDay <= optParam.planningWindowDuration - 1){ 
					if(crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY -1))
						continue;
				}
				//check time feasibility and calculate cost of arc
				//logMsg(logFile, "      before calling calculateArcsToFirstDuties for a=%d j=%d k=%d\n", a, j, k);
				if(calculateArcsToFirstDuties(cp, j, crewArc, &acList[p].arcList[a], k, -1, &tempArc, p)){
					//logMsg(logFile,"      called calculateArcsToFirstDuties, infeasible arc\n");
					continue; //if arc is infeasible, continue
				}
				////If we have gotten through all the checks, arc is feasible.
				////allocate memory for arc
				newArc = arcAlloc(&crewArc->arcList, &crewArc->numArcs);	
				*newArc = tempArc;
				//since we have crew and plane info, add changePenalty cost to arc
				newArc->cost += getChangePenalty(&dutyList[j][k], acList[p].aircraftID, crewPairList[cp].crewPairID);

				//START - MAC - 08/19/08 ANG
				if(optParam.withMac == 1){
					newArc->tempCostForMac += getChangePenalty(&dutyList[j][k], acList[p].aircraftID, crewPairList[cp].crewPairID);
				}
				//END - MAC

				numArcsToFirstDuties++;
				//logMsg(logFile, "      feasible arc found, numArcsToFirstDuties = %d\n", numArcsToFirstDuties);
			}

			//logMsg(logFile, "    done processing cp #%d numPlaneArcs #%d, numArcsToFirstDuties = %d\n", cp, x, numArcsToFirstDuties);

		}

		//logMsg(logFile, "  crewPairList[%d].numPUEndArcs = %d\n", cp, crewPairList[cp].numPUEndArcs);

		//create arcs from pickup at end of trips to first duties
		for(x = 0; x<crewPairList[cp].numPUEndArcs; x++){
			//initialize arc count
			crewArc = crewPairList[cp].crewPUEList[x];
			crewArc->numArcs = 0;
			i = crewArc->demandInd;
			//consider first duties that you can reach from end of trip
			for(a = 0; a < demandList[i].numPUEArcs[j]; a++){
				k = demandList[i].puEArcList[j][a].destDutyInd;
				//consider only those first duties between start and end days of crewPair
				if(k < dutyTally[j][crewPairList[cp].startDay][0].startInd || k > dutyTally[j][lastDay][8].endInd)
					continue;

				//if plane can't be picked up by the pickup arc start time, continue
				//if(crewArc->earlyPickupTm[2] > demandList[i].puEArcList[j][a].startTm)
				if(crewArc->earlyPickupTm[2] > demandList[i].puEArcList[j][a].startTm  && dutyList[j][k].crewStartTm <= dutyList[j][k].crewEndTm ) // RLZ CHECK
					continue;
				if(crewArc->earlyPickupTm[2] > dutyList[j][k].startTm[0] && dutyList[j][k].crewStartTm > dutyList[j][k].crewEndTm ) // RLZ CHECK
					continue;


				//if crewArc is plane-specific...
				if(crewArc->acInd > -1){
					int m, n; //fei Jan 2011

					//check reachability of duty node
					if(acList[crewArc->acInd].unreachableInd > -1 && dutyList[j][k].unreachableFlag[acList[crewArc->acInd].unreachableInd] == 1)
						continue;
					if(dutyList[j][k].aircraftID > 0 && acList[crewArc->acInd].aircraftID != dutyList[j][k].aircraftID)//If dutyList tied with other aircraft, don't want to create arcs - 11/13/08 ANG
						continue;

					//fix the skipping hard inclusion case //fei Jan 2011
					if( demandList[i].acInd < 0 && dutyList[j][k].acInd < 0 ) //both demand and duty are fleet type
					{
						//get dutyList[j][k]'s day: m
						for(m = 0; m < optParam.planningWindowDuration; m ++)
							if ( k  >= dutyTally[j][m][0].startInd && k  <= dutyTally[j][m][8].endInd)
								break;
						_ASSERTE( m < optParam.planningWindowDuration );

						//if there are days between demand and duty, and  there are hard inclusions
						if( ! optParam.withFlexOS )
						{
							//get demand i's day: n
							for(n = 0; n < optParam.planningWindowDuration; n ++)
								if ( i <= lastTripOfDay[n] )
									break;
							_ASSERTE( n < optParam.planningWindowDuration && n <= m );

							if ( n + 1 < m && acList[crewArc->acInd].lastIncl[m-1] > acList[crewArc->acInd].lastIncl[n] ) //03/16/11 ANG
								continue;
						} else if ( acList[crewArc->acInd].numIncl )
						{
							n = origDemInfos[demandList[i].origDemInd].day;
							//if ( n + 1 < m && acList[crewArc->acInd].inclInfoP->seHardIndByDay[0][m] > acList[crewArc->acInd].inclInfoP->seHardIndByDay[1][n] ) //fei Jan 2011
							if ( n + 1 < m && acList[crewArc->acInd].inclInfoP->seHardIndByDay[1][m-1] > acList[crewArc->acInd].inclInfoP->seHardIndByDay[1][n] ) //fei FA
								continue;
						} //end
					}
					//end fix

					aircraftID = acList[crewArc->acInd].aircraftID;
				}
				//if crewArc is specific to a group of planes
				else if(crewArc->acInd < -1){ //index is negative index of acGroupList, for which indices 0 and 1 weren't used
					//check if duty node is reachable by at least one of the planes in the group
					canReach = 0;
					for(y = 0; y < acGroupList[-crewArc->acInd].numAircraft; y++){
						if(acList[acGroupList[-crewArc->acInd].acInd[y]].unreachableInd > -1 && (flag = dutyList[j][k].unreachableFlag[acList[acGroupList[-crewArc->acInd].acInd[y]].unreachableInd]) == 2)
							break; //duty is unreachable by all planes in group
						if(flag == 0){
							canReach = 1;
							break;
						}
					}
					if(canReach == 0)
						continue;
					aircraftID = 0;
				}
				else{ //if crewArc is not plane-specific (it is for fleet), check to make sure duty node is not tied 
					//to a plane (in which case a plane-specific crewArc is required), and set aircraftID = 0
					if(dutyList[j][k].aircraftID > 0)
						continue;
					aircraftID = 0;
				}
				//check get-home feasibility for crew and duty if we are considering sending crew home in this window
				if(crewPairList[cp].endRegDay <= optParam.planningWindowDuration - 1){ 
					if(crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY -1))
						continue;
				}
				//check time feasibility and calculate cost of arc
				if(calculateArcsToFirstDuties(cp, j, crewArc, &demandList[i].puEArcList[j][a], k, -1, &tempArc, -1))
					continue; //if arc is infeasible, continue
				//If we have gotten through all the checks, arc is feasible.
				//allocate memory for arc
				newArc = arcAlloc(&crewArc->arcList, &crewArc->numArcs);
				*newArc = tempArc;
				//since we have crew and plane info, add changePenalty cost to arc
				newArc->cost += getChangePenalty(&dutyList[j][k], aircraftID, crewPairList[cp].crewPairID); //Note: aircraftID maybe 0 = no a/c associated

				//START - MAC - 08/19/08 ANG
				if(optParam.withMac == 1){
					newArc->tempCostForMac += getChangePenalty(&dutyList[j][k], aircraftID, crewPairList[cp].crewPairID);
				}
				//END - MAC

				numArcsToFirstDuties++;
			}
		}

		//logMsg(logFile, "  crewPairList[%d].numPUStartArcs = %d\n", cp, crewPairList[cp].numPUStartArcs);

		//create arcs from pickup at start of trips to first duties
		for(x = 0; x<crewPairList[cp].numPUStartArcs; x++){
			//initialize arc count
			crewArc = crewPairList[cp].crewPUSList[x];
			crewArc->numArcs = 0;
			i = crewArc->demandInd;
			//consider first duties that you can reach from start of trip (they all start with trip)
			for(a = 0; a < demandList[i].numPUSArcs[j]; a++){
				k = demandList[i].puSArcList[j][a].destDutyInd;
				//no need to check that duty is between start and end days of crew;  this was considered
				//when generating crew arcs to pickups at trip starts

				//if crewArc is plane-specific, check reachability of duty node, and get aircraftID
				if(crewArc->acInd > -1){
					if(acList[crewArc->acInd].unreachableInd > -1 && dutyList[j][k].unreachableFlag[acList[crewArc->acInd].unreachableInd] == 1)
						continue;
					if(dutyList[j][k].aircraftID > 0 && acList[crewArc->acInd].aircraftID != dutyList[j][k].aircraftID)//If dutyList tied with other aircraft, don't want to create arcs - 11/13/08 ANG
						continue;
					aircraftID = acList[crewArc->acInd].aircraftID;
				}
				//if crewArc is specific to a group of planes
				else if(crewArc->acInd < -1){ //index is negative index of acGroupList, for which indices 0 and 1 weren't used
					//check if duty node is reachable by at least one of the planes in the group
					canReach = 0;
					for(y = 0; y < acGroupList[-crewArc->acInd].numAircraft; y++){
						if(acList[acGroupList[-crewArc->acInd].acInd[y]].unreachableInd > -1 && (flag = dutyList[j][k].unreachableFlag[acList[acGroupList[-crewArc->acInd].acInd[y]].unreachableInd]) == 2)
							break; //duty is unreachable by all planes in group
						if(flag == 1){
							canReach = 1;
							break;
						}
					}
					if(canReach == 0)
						continue;
					aircraftID = 0;
				}
				else{ //if crewArc is not plane-specific (it is for fleet), check to make sure duty node is not tied 
					//to a plane (in which case a plane-specific crewArc is required), and set aircraftID = 0
					if(dutyList[j][k].aircraftID > 0)
						continue;
					aircraftID = 0;
				}
				//if trip start time is flexible, we still need to check if crew can pickup plane
				//by the pickup arc start time (we created pickup arc if possible for even the 
				//latest trip start time)

				//if(crewArc->earlyPickupTm[2] > demandList[i].puSArcList[j][a].startTm)
				if(crewArc->earlyPickupTm[2] > demandList[i].puSArcList[j][a].startTm && dutyList[j][k].crewStartTm <= dutyList[j][k].crewEndTm ) //RLZ CHECK
					continue;
				if(crewArc->earlyPickupTm[2] > dutyList[j][k].startTm[0] && dutyList[j][k].crewStartTm > dutyList[j][k].crewEndTm ) // RLZ CHECK
					continue;



				//check get-home feasibility for crew and duty if we are considering sending crew home in this window
				if(crewPairList[cp].endRegDay <= optParam.planningWindowDuration - 1){ 
					if(crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY -1))
						continue;
				}
				//check time feasibility and calculate cost of arc
				if(calculateArcsToFirstDuties(cp, j, crewArc, &demandList[i].puSArcList[j][a], k, i, &tempArc, -1))
					continue; //if arc is infeasible, continue
				//If we have gotten through all the checks, arc is feasible.
				//allocate memory for arc
				newArc = arcAlloc(&crewArc->arcList, &crewArc->numArcs);
				*newArc = tempArc;
				//since we have crew and plane info, add changePenalty cost to arc
				newArc->cost += getChangePenalty(&dutyList[j][k], aircraftID, crewPairList[cp].crewPairID); //Note: aircraftID maybe 0 = no a/c associated

				//START - MAC - 08/19/08 ANG
				if(optParam.withMac == 1){
					newArc->tempCostForMac += getChangePenalty(&dutyList[j][k], aircraftID, crewPairList[cp].crewPairID);
				}
				//END - MAC

				numArcsToFirstDuties++;
			}
		}

				//RLZ EXPERIMENT:
		//if (crewPairList[cp].captainID == 23399 && crewPairList[cp].flightOffID == 83225){
		//	//	crewArc = &crewPairList[cp].crewPlaneList[x];			
		//		newArc = arcAlloc(&crewArc->arcList, &crewArc->numArcs);
		//		newArc->blockTm = 99;
		//		newArc->cost = 3051;
		//		newArc->destDutyInd = 4;
		//		newArc->startTm = (int)crewPairList[cp].availDT/60;
		//		numArcsToFirstDuties++;
		//}

	} //end for(cp..

	//free memory for plane arcs and pickup arcs
	for(p= 0; p<numAircraft; p++){
		if(acList[p].arcTallyByDay[optParam.planningWindowDuration-1]> -1)
			free(acList[p].arcList);
	}
	for(i = 0; i<numOptDemand; i++){
		for(j = 0; j<numAcTypes; j++){
			if(demandList[i].numPUEArcs[j] > 0){
				free(demandList[i].puEArcList[j]);
			}
			if(demandList[i].numPUSArcs[j] > 0){
				free(demandList[i].puSArcList[j]);
			}
		}
		free(demandList[i].puEArcList);
		free(demandList[i].puSArcList);
	}
	return 0;
}


/************************************************************************************************************
*	Function	calculateArcsToFirstDuties					Date last modified:  6/04/07 SWO				*
*	Purpose:	calculate the time feasibility and cost (including overtime) of the arcs from planes and	*
*				and pickup locations (at start and end of trips) to first duties.							*
*					cp is crewPair index, j = crewPairList[cp].acTypeIndex, crewArc is arc from crew to		*
*					plane or trip where plane is picked up, k is dutyList index for fleet j,				*
*					puStartdemandInd > -1 if crewArc involves picking up at start of a trip,				*
*					newArc is the arc that we create														*
************************************************************************************************************/
static int calculateArcsToFirstDuties(int cp, int j, CrewArc *crewArc, NetworkArc *pickupArc, int k, int puStartdemandInd, NetworkArc *newArc, int p)
{
	double overtimeCost;
	int halfDaysOT, c, crewInd, i;
	time_t departTm, dutyStartTm, arrivalTm; 
	int endAptID;
	double minusOne;
	int arcFeasible[2];
	int dutyEndTime, pickupTime;
	int repoASAPStartTm;
	int repoEndTm;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int day, day1, day2; //FATIGUE - 02/05/10 ANG
	int addTime = 0; //OCF - 10/18/11 ANG

	//Added 10/07/11 ANG
	CrewArc tempArc;
	tempArc.acInd = -1;
	tempArc.demandInd = -1;
	tempArc.numArcs = 0;
	tempArc.arcList = NULL;

	minusOne = -1.0;
	arcFeasible[0] = 0;
	arcFeasible[1] = 0;
	overtimeCost = 0;
	repoASAPStartTm = -1;

	//logMsg(logFile,"entering calculateArcsToFirstDuties for cp=%d, j=%d, k=%d, crewArc->acInd=%d\n", cp, j, k, crewArc->acInd);

	//if crew is plane specific and the duty is plane specific but the planes are different, 
	//then continue without creating arc - 11/13/08 ANG 
	if (crewArc->acInd > -1 && dutyList[j][k].acInd > 0 && crewArc->acInd != dutyList[j][k].acInd) {
		fprintf(logFile, "For crewPairID %d, crewArc->acInd = %d mismatch with dutyList[%d][%d].acInd = dutyList[j][k].acInd \n", crewPairList[cp].crewPairID, crewArc->acInd, j, k, dutyList[j][k].acInd);
	//	return -1;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #1\n");

	//find location where crew must pick up the plane
	if(crewArc->demandInd == -1)//if picking up plane when next available
		endAptID = acList[crewArc->acInd].availAirportID;
	else if(puStartdemandInd > -1)//if picking up plane at start of trip
		endAptID = demandList[puStartdemandInd].outAirportID;
	else //else we are picking up plane at end of trip
		endAptID = demandList[crewArc->demandInd].inAirportID; 

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #2\n");

	//calculate pickupTime for crew.  If no repo to duty, crewStartTm considers whether first leg is an appointment not requiring crew
	pickupTime = pickupArc->startTm;
	i = dutyList[j][k].demandInd[0]; 

	////RLZ 01082008 Temp fix
	//AD2017 if (i < -1 || i > 100000){ 
	if (i <= -1 || i > 100000){ 
		//logMsg(logFile,"Warning: location mismatched.  %s Line %d .\n", __FILE__, __LINE__);
		return -1;	
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #3\n");

	//logMsg(logFile, "  withVac = %d\n", optParam.withVac);

	//START - Do not put no-charter demands on VAC - 11/17/11 ANG
	if(optParam.withVac){
		//logMsg(logFile, "  withVac = %d\n", optParam.withVac);
		if(crewPairList[cp].captainID >= 1000000){
			//go through duty List
			for(c = 0; c < 4; c++){
				if(dutyList[j][k].demandInd[c] >= 0 && demandList[dutyList[j][k].demandInd[c]].isAppoint == 0){
					if(demandList[dutyList[j][k].demandInd[c]].contractID < 0)
						return -1;
					if(demandList[dutyList[j][k].demandInd[c]].noCharterFlag == 1)
						return -1;
					if(demandList[dutyList[j][k].demandInd[c]].ownerID == 87359)
						return -1;
					if(!optParam.charterMacDmd && demandList[dutyList[j][k].demandInd[c]].isMacDemand == 1)
						return -1;
					if(demandList[dutyList[j][k].demandInd[c]].outAirportID == demandList[dutyList[j][k].demandInd[c]].inAirportID)
						return -1;
				}
			}
		}
	}
	//END - VAC - 11/17/11 ANG*/

	//logMsg(logFile,"  endAptID = %d, i = %d\n", endAptID, i);
	//logMsg(logFile,"  demandList[%d].demandID = %d\n", i, demandList[i].demandID);
	//logMsg(logFile,"  crewStartTm = dutyList[%d][%d].crewStartTm = %d\n", j, k, dutyList[j][k].crewStartTm);

	if(endAptID == demandList[i].outAirportID)
		pickupTime = dutyList[j][k].crewStartTm;

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #4\n");

	//START - FATIGUE - 02/05/10 ANG
	day1 = crewList[crewPairList[cp].crewListInd[0]].day;
	day2 = crewList[crewPairList[cp].crewListInd[1]].day;
	day = -1;
	if( pickupTime*60 < firstEndOfDay + 86400*0 &&
		pickupTime*60 >= firstEndOfDay + 86400*(0-1))
		day = 0;
	else if( pickupTime*60 < firstEndOfDay + 86400*1 &&
		pickupTime*60 >= firstEndOfDay + 86400*(1-1))
		day = 1;
	else if( pickupTime*60 < firstEndOfDay + 86400*2 &&
		pickupTime*60 >= firstEndOfDay + 86400*(2-1))
		day = 2;
	//END - FATIGUE - 02/05/10 ANG

	//if (dutyList[j][k].crewStartTm > dutyList[j][k].crewEndTm) //RLZ 05/02/2008 pure appt
	//	pickupTime = dutyList[j][k].crewStartTm;

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #5\n");

	//calculate dutyEndTime for crew including post flight time (and considering if last demand leg is appointment and no final repo)
	dutyEndTime = dutyList[j][k].crewEndTm;  //RLZ CHECK 04152008

	//if (dutyList[j][k].crewEndTm < dutyList[j][k].crewStartTm) // RLZ a pure appoint node
	//	dutyEndTime = max(crewPairList[cp].availDT/60,(dutyList[j][k].crewEndTm - optParam.maxDutyTm)); 

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #6\n");

	if(dutyList[j][k].startTm[0] > ((int)firstEndOfDay/60 + (crewPairList[cp].endRegDay - 1)*24*60)) //Assuming first duty in first day? RLZ
		dutyEndTime += optParam.finalPostFltTm;
	else {
		dutyEndTime += optParam.postFlightTm;

		//START - FATIGUE - 02/05/10 ANG
		//if this is first day of tour for any member of crewPair, add shortDutyHrDif (note that we add postFlightTm above already
		if ((day1 >= 0 && k >= dutyTally[j][day1][0].startInd && k <= dutyTally[j][day1][8].endInd && ifCrewFirstDayStart5to8AM(day1, cp, (time_t)(60*pickupTime), endAptID)) ||
			(day2 >= 0 && k >= dutyTally[j][day2][0].startInd && k <= dutyTally[j][day2][8].endInd && ifCrewFirstDayStart5to8AM(day2, cp, (time_t)(60*pickupTime), endAptID)) ){
			dutyEndTime += optParam.shortDutyHrDif;

			//if 1st day 5-8AM 12hr rule is not satisfied, don't continue, return -1 instead
			//if(dutyEndTime - crewPairList[cp].availDT/60 + max (crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime) > optParam.maxDutyTm ){
			if(dutyEndTime - pickupTime + max(optParam.firstPreFltTm, max(crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime)) > optParam.maxDutyTm ){
				return -1;
			}
		}	
		//END - FATIGUE - 02/05/10 ANG
	}
	
	//AD20171019 - this check needs to be done for all scenarios
	if(dutyEndTime - pickupTime + max(acTypeList[j].preFlightTm, max(crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime)) > optParam.maxDutyTm ){
		return -1;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #7\n");

	//START - Check OCF whenever possible - 10/12/11 ANG
	if(optParam.withOcf == 1){
		if(optParam.withFlexOS  && i>=0){
			if(crewArc->demandInd == -1){//if picking up plane when next available
				if(acList[crewArc->acInd].reqOCF == 1){
					addTime = 0;
					if (!checkOcfTiming(acList[crewArc->acInd].availDT, acList[crewArc->acInd].availAirportID, demandList[i].reqOut, demandList[i].outAirportID, j, pickupTime, &addTime))
						return -1;
					else if (addTime > 0)
						pickupTime = pickupTime - addTime; //note: addTm includes preOCF and postOCF times
				}
			}
			else if(puStartdemandInd > -1){//if picking up plane at start of trip
				;//we cannot do anything here yet since aircraft info is not available
			}
			else {//else we are picking up plane at end of trip
				if(demandList[crewArc->demandInd].reqOCF == 1){
					addTime = 0;
					if (!checkOcfTiming(demandList[crewArc->demandInd].reqIn, demandList[crewArc->demandInd].inAirportID, demandList[i].reqOut, demandList[i].outAirportID, j, pickupTime, &addTime))
						return -1;
					else if (addTime > 0)
						pickupTime = pickupTime - addTime; //note: addTm includes preOCF and postOCF times
				}
			}
		}
	}
	//END - Check OCF whenever possible - 10/12/11 ANG


	//if crew can start later AND has no duty hours (other than preFlight) at EPU (this means they are coming from rest without notification or home and
	//don't need to travel to plane), then we can just delay duty start until
	//pickupArc start time, and arc is feasible
	if(crewArc->canStrtLtrAtEPU[2] == 1 && crewArc->dutyTmAtEPU[2]<=optParam.firstPreFltTm){ 
		arcFeasible[0] = 1;
		arcFeasible[1] = 1;

		if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
			//if 10hr rule is not satisfied, don't continue, return -1 instead
			if((crewArc->dutyTmAtEPU[2] + dutyEndTime - crewArc->earlyPickupTm[2]) > optParam.shortDutyTm){
				arcFeasible[0] = 0;
				arcFeasible[1] = 0;
				return -1;
			}
		}
	}
	//if duty and flying hours are okay based on early pickup (EPU), then arc is feasible
	else if((crewArc->dutyTmAtEPU[2]+dutyEndTime-crewArc->earlyPickupTm[2])<= optParam.maxDutyTm &&
		(crewArc->blockTmAtEPU[2]+ pickupArc->blockTm)<= optParam.maxFlightTm && dutyList[j][k].demandInd[0] > -1){ 
		//RLZ 050708 repo-only node still need dutyTime
		arcFeasible[0] = 1;
		arcFeasible[1] = 1;

		//Note: 10/06/11 ANG
		//This does not include the check whether:
		//		crewArc->earlyPickupTm[2] (at location) <= needed time (with repo or not) to perform duty k 
		//Below code is added:
		if(dutyList[j][k].demandInd[0] > -1 && crewArc->demandInd == -1){
		//	//if(!getCrewArcTimeAndCost(cp, demandList[dutyList[j][k].demandInd[0]].outAirportID, demandList[dutyList[j][k].demandInd[0]].early[j]*60, dutyList[j][k].demandInd[0], &tempArc)){
			if(getCrewArcTimeAndCost(cp, demandList[dutyList[j][k].demandInd[0]].outAirportID, demandList[dutyList[j][k].demandInd[0]].early[j]*60, dutyList[j][k].demandInd[0], &tempArc)){
				arcFeasible[0] = 0;
				arcFeasible[1] = 0;
				return -1;
			}
		}

		//START - Check short duty rule - 10/14/09 ANG
		//if (minutesPastMidnight(crewArc->earlyPickupTm[2]*60, endAptID) <= optParam.cutoffForShortDuty && 
		//if (minutesPastMidnight(crewArc->earlyPickupTm[2]*60-crewArc->dutyTmAtEPU[2]*60, endAptID) <= optParam.cutoffForShortDuty && 
		//	(crewArc->dutyTmAtEPU[2] + dutyEndTime - crewArc->earlyPickupTm[2]) > optParam.shortDutyTm){ - Changed to below - 11/11/10 ANG
		if (ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){ 
			if ((crewArc->dutyTmAtEPU[2] + dutyEndTime - crewArc->earlyPickupTm[2]) > optParam.shortDutyTm){
				arcFeasible[0] = 0;
				arcFeasible[1] = 0;
				return -1;
			}
		}
		//END - Check short duty rule - 10/14/09 ANG
	}
	//else duty or flying hours are NOT okay based on EPU. Check if crew can sleep prior to 
	//picking up plane on time for duty.

		//RLZ CHECK CHECK
		//RLZ: earlyPickupTm[2] has preFlightTm in some cases (blockTm=0), and already has turn time for sure.
		//The following inequality is not 100% right. but since preflightTm=turnTm. it should be fine. 
		//It did not lose any possible arcs. 06/23/08
	else if( (crewArc->earlyPickupTm[2]+ optParam.minRestTm ) <= pickupTime){  //RLZ 06/20 add postflightTm
		//no need to subtract preflight time before rest and add preflight time after, since this will cancel out
		arcFeasible[0] = 1; 
		arcFeasible[1] = 1;

		//if ( crewArc->blockTmAtEPU[2] > 0 && ((int)(crewPairList[cp].availDT/60) + optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm ) > pickupTime){
		if ( crewArc->blockTmAtEPU[2] > 0 && ((int)(crewPairList[cp].availDT/60) + optParam.minRestTm + optParam.postFlightTm + acTypeList[j].preFlightTm ) > pickupTime){ //07/17/2017 ANG
			arcFeasible[0] = 0; 
			arcFeasible[1] = 0;
		}

		if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
			//if 10hr rule is not satisfied, don't continue, return -1 instead
			//if(dutyEndTime - pickupTime + ((day1 == day || day2 == day) ? optParam.firstPreFltTm : optParam.preFlightTm) > optParam.shortDutyTm){
			if(dutyEndTime - pickupTime + ((day1 == day || day2 == day) ? optParam.firstPreFltTm : acTypeList[j].preFlightTm) > optParam.shortDutyTm){ //07/17/2017 ANG
				arcFeasible[0] = 0;
				arcFeasible[1] = 0;
				return -1;
			}
		}
	}
	//else if this is a repo-only node, check feasibility with reposition starting ASAP (NOT at pickup arc start time) 
	//We can assume that crew must reposition plane, or plane/pickup arc wouldn't have been created
	else if (dutyList[j][k].demandInd[0] == -1){
		if(aptCurf[endAptID][0] == 0 && aptCurf[demandList[dutyList[j][k].repoDemandInd].outAirportID][0] == 0){// if no curfews are involved...
			/*Replaced below by RLZ 050708
			if((crewArc->dutyTmAtEPU[2]+dutyEndTime-pickupArc->startTm)< optParam.maxDutyTm &&(crewArc->blockTmAtEPU[2]+ pickupArc->blockTm)< optParam.maxFlightTm){
			//note that (dutyList[j][k].endTm-pickupArc->startTm) == repo elapsed time
				repoASAPStartTm = crewArc->earlyPickupTm[2];
				arcFeasible[0] = 1;
				arcFeasible[1] = 1;
			}
			*/

			if((crewArc->dutyTmAtEPU[2]+dutyEndTime-crewArc->earlyPickupTm[2] )<= optParam.maxDutyTm &&
			(crewArc->blockTmAtEPU[2]+ pickupArc->blockTm)<= optParam.maxFlightTm ){ 
				if (crewArc->earlyPickupTm[2]+ pickupArc->blockTm <= dutyList[j][k].crewEndTm){
					//RLZ REPO ASAP only if aircraft is avaiable. 09/18/2008 need more thoughts
					if ( p!= -1){
						repoASAPStartTm = max(crewArc->earlyPickupTm[2], (int)acList[p].availDT/60);
					}
					else {
						repoASAPStartTm = crewArc->earlyPickupTm[2];
					}
					arcFeasible[0] = 1;
					arcFeasible[1] = 1;

					if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
						//if 10hr rule is not satisfied, don't continue, return -1 instead
						if(crewArc->dutyTmAtEPU[2]+dutyEndTime-crewArc->earlyPickupTm[2] > optParam.shortDutyTm){
							arcFeasible[0] = 0;
							arcFeasible[1] = 0;
							return -1;
						}
					}
				}
			}
		}
		else{ //curfews are involved
			getFlightTime(endAptID, demandList[dutyList[j][k].repoDemandInd].outAirportID, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);		
			if((crewArc->blockTmAtEPU[2]+ pickupArc->blockTm)<= optParam.maxFlightTm &&
				(repoEndTm = getRepoArriveTm(endAptID, demandList[dutyList[j][k].repoDemandInd].outAirportID, crewArc->earlyPickupTm[2], repoElapsedTm)) != -1 && 
				(crewArc->dutyTmAtEPU[2]+ repoEndTm + optParam.finalPostFltTm - crewArc->earlyPickupTm[2])<= optParam.maxDutyTm){
					if (repoEndTm <= dutyList[j][k].crewEndTm){ //RLZ 050708 added condition.
						repoASAPStartTm = repoEndTm - repoElapsedTm;
						arcFeasible[0] = 1;
						arcFeasible[1] = 1;

						if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
							//if 10hr rule is not satisfied, don't continue, return -1 instead
							if(crewArc->dutyTmAtEPU[2]+ repoEndTm + optParam.finalPostFltTm - crewArc->earlyPickupTm[2] > optParam.shortDutyTm){
								arcFeasible[0] = 0;
								arcFeasible[1] = 0;
								return -1;
							}
						}
					}
			}
		}
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #8\n");

	if(crewArc->canStrtLtrAtEPU[2] ==0 && arcFeasible[0] == 0)//if crew can not start duty later, then one or both pilots has already started duty 
		//before or at their avail time, AND has not rested since (they were at, or traveled to, the plane location on their 
		//available day). If neither of the three previous options is feasible, we assume that they can't pick up plane and do this duty.
		return -1;	

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #9\n");

	//if overtime calcs are irrelevant for both pilots AND arc is feasible from above, 
	//then we can just create the arc and return
	if(crewList[crewPairList[cp].crewListInd[0]].overtimeMatters == 0 && 
		crewList[crewPairList[cp].crewListInd[1]].overtimeMatters == 0 && arcFeasible[0] ==1){ //arcFeasible[1] must also == 1 due to above code

		newArc->blockTm = pickupArc->blockTm;
		newArc->destDutyInd = k;
		newArc->startTm =  (repoASAPStartTm > -1? repoASAPStartTm: pickupTime);
		newArc->cost = pickupArc->cost;
		if(optParam.withMac == 1){
			newArc->tempCostForMac = pickupArc->tempCostForMac; //MAC - 09/23/08 ANG
			newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
			newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
			newArc->macRepoStop = pickupArc->macRepoStop; 
		}
		return 0;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #10\n");

	//If the crew need not travel to the plane...
	if(crewList[crewPairList[cp].crewListInd[0]].availAirportID == endAptID && 
		crewList[crewPairList[cp].crewListInd[1]].availAirportID == endAptID){
		//The following code will never be hit.  If the crew need not travel to plane AND canStartLater[2]==1, then we set arcFeasible == 1 above.
		//If canStartLater[2] == 0 and arcFeasible == 0, then we returned with -1 above.
			//if(arcFeasible[0] == 0)//If the arc is not feasible based on above checks, then arc is not feasible. 
			//	return -1;

		//[The following code will rarely be hit.  If the crew need not travel to plane AND canStartLater[2]==1, then we set arcFeasible == 1 above
		//without setting repoASAPStartTm > -1.  If canStartLater[2] == 1, then overtime is irrelevant for at least one pilot, and the arc is feasible and we have often
		//already created the arc.]
			//calc OT if necessary for each pilot based on pickupTime, create arc, and return
			if(repoASAPStartTm > -1)
				pickupTime = repoASAPStartTm;
			//calc OT if necessary for each pilot based on pickupTime, create arc, and return
			for(c = 0; c<2; c++){
				crewInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewInd].overtimeMatters == 1 && 60*pickupTime < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*pickupTime)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			newArc->blockTm = pickupArc->blockTm;
			newArc->destDutyInd = k;
			newArc->startTm =  pickupTime;
			newArc->cost = pickupArc->cost + overtimeCost;
			if(optParam.withMac == 1){
				newArc->tempCostForMac = pickupArc->tempCostForMac + overtimeCost; //MAC - 09/23/08 ANG
				newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
				newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
				newArc->macRepoStop = pickupArc->macRepoStop; 
			}
			return 0;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #11\n");

	/*Otherwise, overtime is a consideration AND/OR the arc is not feasible based on early pickup.
	We need to look at latest commercial flight for one or both pilots. 
	We may need to look at pilots individually.  */

		/*NOTE:  We know that crew can start later than their available time. (Why?
		Because if they can't start later, then overtime is not a consideration.  And, if the arc
		were infeasible above, we would have returned with -1.  If the arc were feasible above, we would have
		returned with 0.) Therefore, crew is resting or at home prior to their commercial flight or avail time (if no travel reqd).*/

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #12\n");

	//if pilots are together...  
	if(crewPairList[cp].availAptID > 0){//this field is only populated if crewPair is already together with the same hours at start of window)
		
		/*if crew is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > preFlight 
		AND canStrtLtr == 1 AND the duty doesn't start later than trip's earliest start, THEN we have already used the 
		latest commercial flight when generating the crew arc*/
		if(puStartdemandInd > -1){
			if(crewPairList[cp].availAptID != endAptID && crewArc->dutyTmAtEPU[2]>optParam.firstPreFltTm 
				&& crewArc->canStrtLtrAtEPU[2] == 1 && dutyList[j][k].startTm[0] == demandList[puStartdemandInd].early[j]){

				//if duty hours are no good, then return -1
				if((crewArc->dutyTmAtEPU[2]+dutyEndTime-crewArc->earlyPickupTm[2])> optParam.maxDutyTm){
					return -1;
				}
				else{//else calc overtime for each pilot if necessary and create arc.  early pickup time is the same for both,
					//but tour start time might not be
					for(c = 0; c<2; c++){
						crewInd = crewPairList[cp].crewListInd[c];
						if(crewList[crewInd].overtimeMatters == 1 && 60*crewArc->earlyPickupTm[2] < crewList[crewInd].tourStartTm){
							halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*crewArc->earlyPickupTm[2])/(12*3600);//integer division truncates
							overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
						}
					}
					newArc->blockTm = pickupArc->blockTm;
					newArc->destDutyInd = k;
					newArc->startTm =  pickupTime;
					newArc->cost = pickupArc->cost + overtimeCost;
					if(optParam.withMac == 1){
						newArc->tempCostForMac = pickupArc->tempCostForMac + overtimeCost; //MAC - 09/23/08 ANG
						newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
						newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
						newArc->macRepoStop = pickupArc->macRepoStop; 
					}
					return 0;
				}
			}
		}

		//can pilots travel to plane same day as duty (coming from rest or home/tour start)?
		if(!getCrewTravelDataLate(crewPairList[cp].availDT, 60*(pickupTime - optParam.firstPreFltTm), crewPairList[cp].availAptID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)
			&&(dutyEndTime - (int)dutyStartTm/60) < optParam.maxDutyTm){
			//then calculate overtime if necessary for each pilot based on this flight
			for(c = 0; c<2; c++){
				crewInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			arcFeasible[0] = 1;
			arcFeasible[1] = 1;

			if(ifCrewStartBefore5AM(cp, dutyStartTm, crewPairList[cp].availAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
				//if 10hr rule is not satisfied, don't continue, return -1 instead
				if((dutyEndTime - (int)dutyStartTm/60) > optParam.shortDutyTm){
					arcFeasible[0] = 0;
					arcFeasible[1] = 0;
					return -1;
				}
			}
		}
		else{//else look at traveling to the plane as late as possible the day before the duty
			if(!getCrewTravelDataLate(crewPairList[cp].availDT, 60*(pickupTime - optParam.firstPreFltTm - optParam.minRestTm), crewPairList[cp].availAptID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)
			    &&(dutyEndTime - (int)dutyStartTm/60) < optParam.maxDutyTm){ //Second condition added on 10/15/10 ANG

				//calculate overtime if necessary for each pilot based on this flight
				for(c = 0; c<2; c++){
					crewInd = crewPairList[cp].crewListInd[c];
					if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
						halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
						overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
					}
				}
				arcFeasible[0] = 1;
				arcFeasible[1] = 1;

				if(ifCrewStartBefore5AM(cp, dutyStartTm, crewPairList[cp].availAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if(dutyEndTime - (int)dutyStartTm/60 > optParam.shortDutyTm){
						arcFeasible[0] = 0;
						arcFeasible[1] = 0;
						return -1;
					}
				}
			}
		}
		if(arcFeasible[0] ==1){ //and arcFeasible[1] == 1 due to code above
			newArc->blockTm = pickupArc->blockTm;
			newArc->destDutyInd = k;
			newArc->startTm =  pickupTime;
			newArc->cost = pickupArc->cost + overtimeCost;
			if(optParam.withMac == 1){
				newArc->tempCostForMac = pickupArc->tempCostForMac + overtimeCost; //MAC - 09/23/08 ANG
				newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
				newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
				newArc->macRepoStop = pickupArc->macRepoStop; 
			}
			return 0;
		}
		else
			return -1;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #13\n");

	//Else, we must consider each pilot separately
	for(c = 0; c<2; c++){
		if(repoASAPStartTm > -1)
			arcFeasible[c] = 0; //arc is feasible above, but we must look at late start because of overtime
								//and we can't have one pilot start repo-only duty ASAP while the other starts at a later time, so assume we start later for both
								//finally, if overtime matters and arc was feasible if started repo ASAP, then arc should be feasible with a later start as well??
		crewInd = crewPairList[cp].crewListInd[c];
		/*if pilot is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > preFlight 
		AND canStrtLtr == 1 AND the duty doesn't start later than trip's earliest start, THEN we have already used the 
		latest commercial flight for pilot when generating the crew arc*/
		if(puStartdemandInd > -1){
			if(crewList[crewInd].availAirportID != endAptID && crewArc->dutyTmAtEPU[c]>optParam.firstPreFltTm 
				&& crewArc->canStrtLtrAtEPU[c] == 1 && dutyList[j][k].startTm[0] == demandList[puStartdemandInd].early[j])
			{//if duty hours are no good, then return -1
				if((crewArc->dutyTmAtEPU[c]+dutyEndTime-crewArc->earlyPickupTm[c]) > optParam.maxDutyTm)
					return -1;
				else{//else calc overtime if necessary and continue on to next pilot
					if(crewList[crewInd].overtimeMatters == 1 && 60*crewArc->earlyPickupTm[c] < crewList[crewInd].tourStartTm){
						halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*crewArc->earlyPickupTm[c])/(12*3600);//integer division truncates
						overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
					}
					arcFeasible[c] = 1;

					if(ifCrewStartBefore5AM(cp, (time_t)(crewArc->earlyPickupTm[c]-crewArc->dutyTmAtEPU[c]), crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
						//if 10hr rule is not satisfied, don't continue, return -1 instead
						if((crewArc->dutyTmAtEPU[c]+dutyEndTime-crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
							arcFeasible[c] = 0;
							return -1;
						}
					}

					continue;
				}
			}
		}
		//if arc is feasible (from above) and overtime is not a consideration for this pilot, continue
		if(arcFeasible[c] == 1 && crewList[crewInd].overtimeMatters == 0)
			continue;
		//if arc is not feasible for pilot pair based on EPU from above, it may still be feasible for this pilot
		if(arcFeasible[c] == 0){ //(see all comments above for pilot pair)
			if(crewArc->canStrtLtrAtEPU[c] == 1 && crewArc->dutyTmAtEPU[c]<=optParam.firstPreFltTm){
				arcFeasible[c] = 1;

				if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if((crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			else if((crewArc->dutyTmAtEPU[c]+dutyEndTime-crewArc->earlyPickupTm[c])< optParam.maxDutyTm &&
				(crewArc->blockTmAtEPU[c]+ pickupArc->blockTm)< optParam.maxFlightTm){
				arcFeasible[c] = 1;

				/*if (minutesPastMidnight(crewArc->earlyPickupTm[c]*60-crewArc->dutyTmAtEPU[c]*60, endAptID) <= optParam.cutoffForShortDuty && //Check short duty rule - 10/15/10 ANG
					(crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
					arcFeasible[c] = 0;
					return -1;
				} changed to code below - 11/11/10 ANG */
				if (ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){ 
					if ((crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			else if((crewArc->earlyPickupTm[c]+ optParam.minRestTm) < pickupTime){
				arcFeasible[c] = 1; 

				if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					//if(dutyEndTime - pickupTime + optParam.preFlightTm > optParam.shortDutyTm){ //note(!): preFlightTm vs firstPreFltTm
					if(dutyEndTime - pickupTime + acTypeList[j].preFlightTm > optParam.shortDutyTm){ // 07/17/2017 ANG
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			else if(crewArc->canStrtLtrAtEPU[c] ==0 && arcFeasible[c] == 0) 
				return -1; //we won't ever hit this - would have returned -1 above
		}

		//if overtime calcs are irrelevant for this pilot AND duty is feasible assuming early pickup, 
		//then we can just continue on to next pilot
		if(crewList[crewInd].overtimeMatters == 0 && arcFeasible[c] ==1)
			continue;
		//if pilot need not travel... 
		if(crewList[crewInd].availAirportID == endAptID){
			//calc OT if necessary based on pickup start and continue
			if(crewList[crewInd].overtimeMatters == 1 && 60*pickupTime < crewList[crewInd].tourStartTm){
				halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*pickupTime)/(12*3600);//integer division truncates
				overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
			}
			continue;
		}
		//Otherwise, either overtime is a consideration AND/OR the arc is not feasible based on early pickup for this pilot.
		//NOTE:  We know that pilot can start later than his available time(see above).  Therefore, pilot must be 
		//resting or at home prior to his avail time. We need to look at latest commercial flight for pilot.

		//can pilot travel to plane same day as duty (coming from rest or home/tour start)?
		if(!getCrewTravelDataLate(crewList[crewInd].availDT, 60*(pickupTime - optParam.firstPreFltTm), 
			crewList[crewInd].availAirportID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)
			&&(dutyEndTime - (int)dutyStartTm/60) < optParam.maxDutyTm){
			//then calculate overtime if necessary based on this flight
			if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			arcFeasible[c] = 1;

			if(ifCrewStartBefore5AM(cp, dutyStartTm, crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
				//if 10hr rule is not satisfied, don't continue, return -1 instead
				if((dutyEndTime - (int)dutyStartTm/60) > optParam.shortDutyTm){
					arcFeasible[c] = 0;
					return -1;
				}
			}
		}
		else{//else look at traveling to the plane as late as possible the day before the duty
			if(!getCrewTravelDataLate(crewList[crewInd].availDT, 60*(pickupTime - optParam.firstPreFltTm - optParam.minRestTm), 
				crewList[crewInd].availAirportID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)){
				//calculate overtime if necessary based on this flight

				if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
				arcFeasible[c] = 1;

				if(ifCrewStartBefore5AM(cp, dutyStartTm, crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if(dutyEndTime - (int)dutyStartTm/60 > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
		}
		if(arcFeasible[c] == 0)
			return -1;
	}

	//logMsg(logFile,"  calculateArcsToFirstDuties check point #14\n");

	//if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
	//	//if 10hr rule is not satisfied, don't continue, return -1 instead
	//	if(dutyEndTime - crewPairList[cp].availDT/60 + max (crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime) > optParam.shortDutyTm){
	//		arcFeasible[0] = 0;
	//		arcFeasible[1] = 0;
	//		return -1;
	//	}
	//}

	//if we have gotten this far, arc is feasible for both pilots, so create arc and return
	newArc->blockTm = pickupArc->blockTm;
	newArc->destDutyInd = k;
	newArc->startTm =  pickupTime;
	newArc->cost = pickupArc->cost + overtimeCost;
	if(optParam.withMac == 1){
		newArc->tempCostForMac = pickupArc->tempCostForMac + overtimeCost; //MAC - 09/23/08 ANG
		newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
		newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
		newArc->macRepoStop = pickupArc->macRepoStop; 
	}
	return 0;
}


/********************************************************************************************************
*	Function	calculateGetHomeCosts								Date last modified:  08/23/07 SWO 	*
*	Purpose:	For each crewPair for which the last day of the tour is potentially within the planning *
*				window, we create an array (corresponding to the duty nodes)							*
*				of the get-home cost including overtime(infinite if infeasible) MINUS the cost of		*
*				sending them straight home from their current location.									*																		*
********************************************************************************************************/
static int calculateGetHomeCosts(void)
{
	int cp, c, j, day, lastDay, k, startAptID, halfDaysOT, crewListInd;
	time_t crewGetHomeTm[2];
	time_t getHomeTime;
	time_t departTm;
	time_t dutyStartTm;
	time_t earlyDpt;
	double getHmCst;
	double straightHmCst;

	logMsg(logFile, "numOptCrewPairs = %d\n", numOptCrewPairs);

	for(cp = 0; cp < numOptCrewPairs; cp++){
		//logMsg(logFile, "first loop : processing cp = %d\n", cp);
		//initialize crewPairList[cp].nodeStartIndex
		j = crewPairList[cp].acTypeIndex;
		
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		
		crewPairList[cp].nodeStartIndex = dutyTally[j][crewPairList[cp].startDay][0].startInd;

		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;
		//if we aren't considering sending crew home in this window 
		if(crewPairList[cp].endRegDay == PAST_WINDOW)
			continue;
		//calculate cost of sending crewPair straight home without any duties (if endRegDay is within window)
		straightHmCst = getStraightHomeCost(cp, -1, 0);
		if(straightHmCst > (INFINITY - 1))
			straightHmCst = 5000.00;
		lastDay= (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));

		//fprintf( logFile, " --> straight home cost: %f \n", straightHmCst );

		//allocate memory for array of get-home costs for this crew
		if((crewPairList[cp].getHomeCost = calloc((dutyTally[j][lastDay][8].endInd - dutyTally[j][crewPairList[cp].startDay][0].startInd + 1), sizeof(double))) == NULL) {
			logMsg(logFile,"%s Line %d, Out of Memory in calculateGetHomeCosts().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		//for each day of the crewPair's tour
		for(day = crewPairList[cp].startDay; day <= lastDay; day++){
			//  if this day is greater than or equal to the last day of a crew member's regular (non-overtime) tour, we
			//  calculate earliest get-home time, check get-home feasibility, and determine get-home cost including overtime for each duty node
			//  if day is  before last day of regular tour, we assume feasible and just calculate get-home cost
            if(crewPairList[cp].endRegDay > day) //RLZ: 04/19/2011 Send crew home only if they are doing duty on the last day.
                continue;		
			
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){
				//determine start airport of get-home = end airport of duty
				if(dutyList[j][k].repoDemandInd > -1)
					startAptID = demandList[dutyList[j][k].repoDemandInd].outAirportID;
				else
					startAptID = demandList[dutyList[j][k].lastDemInd].inAirportID;
				//calculate earliest departure time of a trip home
			
				//earlyDpt = (dutyList[j][k].crewEndTm + optParam.finalPostFltTm)*60;  //RLZ: CHECK. 041408, replaced below

				if (dutyList[j][k].crewEndTm < dutyList[j][k].crewStartTm)  // single appoint node
					if (day > 0)
						earlyDpt = (dutyList[j][k].crewEndTm - optParam.maxDutyTm)*60;
					else
						earlyDpt = crewPairList[cp].availDT;  //or dutyEndTime = max(crewPairList[cp].availDT,(dutyList[j][k].crewEndTm - optParam.maxDutyTm)*60);
				    
				else
					earlyDpt = (dutyList[j][k].crewEndTm + optParam.finalPostFltTm)*60;


				//fei FA //must initialize
				memset(crewGetHomeTm, 0, sizeof(crewGetHomeTm));
				//fei FA //must initialize

				//check get-home for crew members
				for(c = 0; c<2; c++){
					crewListInd = crewPairList[cp].crewListInd[c];
					if(crewList[crewListInd].endRegDay == PAST_WINDOW)
						continue;
                    if(crewList[crewListInd].endRegDay > day) //RLZ: 04/19/2011 Send crew home only if they are doing duty on the last day.
                        continue;
					departTm = 0;
					dutyStartTm = 0;
					getHomeTime = 0;
					getHmCst = 0.0;
					
					if(getCrewTravelDataEarly(earlyDpt, (time_t)min(crewList[crewListInd].tourEndTm + 
					crewList[crewListInd].stayLate*24*3600 + optParam.maxCrewExtension*60, optParam.windowEnd + 86400), startAptID, 
					crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &getHomeTime, &getHmCst, withOag)){

						//if we can't find a flight home on time, set cost to infinity and break
						crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] = INFINITY;
						break;
					}
					
					/* //fei FA debug
					if( crewPairList[cp].crewPairID == 5 )
					{
						fprintf( logFile, " --> day %d, duty %d, earlyDpt: %d, arrival: %d, startAptID: %d, endAptId: %d, depart time: %d, dutyStartTm: %d, getHomeTime: %d, getHomeCost: %f, oag: %d \n"
						, day, k, (int)(earlyDpt), (int)min(crewList[crewListInd].tourEndTm + 
						(int)(crewList[crewListInd].stayLate)*24*3600 + optParam.maxCrewExtension*60, optParam.windowEnd + 86400), startAptID, 
						crewList[crewListInd].endLoc, (int)(departTm), (int)(dutyStartTm), (int)(getHomeTime), getHmCst , withOag) ;

						fprintf( logFile, " --> crew: %d, tour end time: %d, stay late: %d,  extenstion: %d, end time: %d, end time: %d, index: %d, cost: %f \n"
						,c, (int)(crewList[crewListInd].tourEndTm),  (int)(crewList[crewListInd].stayLate), optParam.maxCrewExtension
						, (int)crewList[crewListInd].tourEndTm, optParam.windowEnd + 86400, crewPairList[cp].nodeStartIndex
						, crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] );
					}
					*/

					crewGetHomeTm[c] = getHomeTime;
					crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] += getHmCst;

					//if( crewPairList[cp].crewPairID == 5 )
					//	fprintf( logFile, " --> cost: %f \n\n", crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] );
					
				}
				//if one or both crew members can't get home, move to next node
				if(crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY-1))
					continue;
				//otherwise, add any overtime cost to get-home cost 
				for(c = 0; c<2; c++){
					//if crew member gets home on overtime, determine the number of overtime days  
					if(crewGetHomeTm[c] > crewList[crewPairList[cp].crewListInd[c]].tourEndTm){

						//if( crewPairList[cp].crewPairID == 5 )
						//	fprintf( logFile, " --> crew: %d, time: %d, crew end time: %d, half days: %d, cost: %f \n\n", c, (int)(crewGetHomeTm[c]), (int)(crewList[crewPairList[cp].crewListInd[c]].tourEndTm)
						//	, halfDaysOT, crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] );

						halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewPairList[cp].crewListInd[c]].tourEndTm)/(12*3600);//integer division truncates
						//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
						crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] += 
							optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);

						//if( crewPairList[cp].crewPairID == 5 )
						//	fprintf( logFile, " --> crew: %d, time: %d, crew end time: %d, half days: %d, cost: %f \n\n", c, (int)(crewGetHomeTm[c]), (int)(crewList[crewPairList[cp].crewListInd[c]].tourEndTm)
						//	, halfDaysOT, crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] );

					}
				}
				//subtract cost of sending crew straight home (no duty) if endRegDay is within window
				crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] -= straightHmCst;
			}  //end for(k loop
		} //end for(day loop
	} //end for(cp loop

	//logMsg(logFile, "entering second loop in calculateGetHomeCosts()\n");

		//flag crew who needs commercial travel, Jintao
	for(cp = 0; cp < numOptCrewPairs; cp++){
		//logMsg(logFile, "second loop : processing cp = %d\n", cp);
	
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;

		//logMsg(logFile, " start day <> 99\n");

		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;

		//logMsg(logFile, " availDT for both crewPairList[%d] <= pairEndTm \n", cp);

		//if we aren't considering sending crew home in this window 
		if(crewPairList[cp].endRegDay == PAST_WINDOW)
			continue;

		//logMsg(logFile, " end day <> 99\n");

		//flag whether we should send crew home
		for(c = 0; c<2; c++){
					crewListInd = crewPairList[cp].crewListInd[c];
					//logMsg(logFile, "  considering c = %d crewListInd = %d\n", c, crewListInd);
					if(crewList[crewListInd].endRegDay == PAST_WINDOW)
						continue;
					//logMsg(logFile, "  c = %d endRegDay <> 99\n", c);
					if(crewList[crewListInd].endRegDay > day) //RLZ: 04/19/2011 Send crew home only if they are doing duty on the last day.
						continue;
					//logMsg(logFile, "  c = %d endRegDay <= day = %d\n", c, day);
					crewList[crewListInd].needgettinghome = 1;
					//logMsg(logFile, "  c = %d (crewId = %d) needs to go home\n", c, crewList[crewListInd].crewID);
		}
	}

	/*
	//fei FA test //debug
	for(cp = 0; cp < numOptCrewPairs; cp++){
		//initialize crewPairList[cp].nodeStartIndex
		j = crewPairList[cp].acTypeIndex;
		
		if(crewPairList[cp].startDay == PAST_WINDOW)
			continue;
		
		crewPairList[cp].nodeStartIndex = dutyTally[j][crewPairList[cp].startDay][0].startInd;

		if(crewList[crewPairList[cp].crewListInd[0]].availDT > crewPairList[cp].pairEndTm || crewList[crewPairList[cp].crewListInd[1]].availDT > crewPairList[cp].pairEndTm)
			continue;
		//if we aren't considering sending crew home in this window 
		if(crewPairList[cp].endRegDay == PAST_WINDOW)
			continue;

		if(crewPairList[cp].crewPairID != 5 )
			continue;

		//calculate cost of sending crewPair straight home without any duties (if endRegDay is within window)
		straightHmCst = getStraightHomeCost(cp, -1, 0);
		if(straightHmCst > (INFINITY - 1))
			straightHmCst = 5000.00;
		lastDay= (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));

		fprintf( logFile, " --> straight home cost: %f \n", straightHmCst );

		//for each day of the crewPair's tour
		for(day = crewPairList[cp].startDay; day <= lastDay; day++)
		{
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++)
			{
				fprintf( logFile, " --> day %d, duty %d, home cost: %f \n", day, k, crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] ) ;
			}
		}
	}//end fei FA test
	*/
	
	return 0; 
}


/********************************************************************************************************
*	Function	getStraightHomeCost									Date last modified:  08/23/07 SWO	*
*	Purpose:	Calculate the cost of sending a crewPair straight home from a given location and time.	*
*				If startLoc = -1, then starting location and time are equal to next available location	*
*				and time for each crew member.															*
********************************************************************************************************/
double getStraightHomeCost(int cp, int startLoc, time_t startTime)
{
	int c, crewListInd,halfDaysOT, origStartLoc;
	int sendHomeNextAvail;
	time_t crewGetHomeTm[2];
	time_t getHomeTime;
	time_t departTm;
	time_t dutyStartTm;
	double getHmCst;
	double straightHmCst;
	
	straightHmCst = 0.0;
	sendHomeNextAvail = 0;
	origStartLoc = startLoc;

	for(c = 0; c<2; c++){
		departTm = 0;
		dutyStartTm = 0;
		getHomeTime = 0;
		getHmCst = 0.0;
		crewListInd = crewPairList[cp].crewListInd[c];
		//if we should send this pilot home within the window
		if(crewList[crewListInd].endRegDay != PAST_WINDOW){
			//if origStartLoc == -1, we are interested in sending pilots directly home when next available - no (additional) tour
			if(origStartLoc == -1){
				startLoc = crewList[crewListInd].availAirportID;
				startTime = crewList[crewListInd].availDT;
				sendHomeNextAvail = 1;
			}

			if(getCrewTravelDataEarly(startTime, (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), (optParam.windowEnd + 86400)), 
				startLoc, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &getHomeTime, &getHmCst, withOag)){
				//if we can't find a flight home, set cost to infinity and break out of for(c = 0... loop
				straightHmCst = INFINITY;  
				break;
			}
			//if we AREN'T sending pilots home when next available, and pilot can't get home on time, set cost to infinity and break out of for(c.. loop
			if(!sendHomeNextAvail && getHomeTime > (crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*24*3600)){
				straightHmCst = INFINITY;
				break;
			}
			//note that travel home is not counted as part of duty hours, so no need to check if duty hours are exceeded
			crewGetHomeTm[c] = getHomeTime;
			straightHmCst += getHmCst;
			//if crew member gets home on overtime, determine the number of overtime days  
			if(crewGetHomeTm[c] > crewList[crewListInd].tourEndTm){
				halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewListInd].tourEndTm)/(12*3600);//integer division truncates
				//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
				straightHmCst+= 
					optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);
			}
		}
	}
	return straightHmCst;
}


/********************************************************************************
*	Function   createDutyNetwork		      Date last modified:  02/12/07 SWO	*
*	Purpose:	create arcs between duty nodes									*
********************************************************************************/
static int createDutyNetwork(void)
{
	int day1, day2, j, k1, x, d;
	Duty *startDuty;
	int startAptID;
	int startFboID;

	numDutyArcs = 0;
	numDutyArcCopies = 0;

	//for each aircraft type..
	for(j=0; j<numAcTypes; j++)
	{
		//for each day of the planning horizon except the last...
		for(day1 = 0; day1 < (optParam.planningWindowDuration-1); day1++)
		{
			logMsg(logFile,"For j = %d, day1 = %d, k1 ranges from %d to %d\n", j, day1, dutyTally[j][day1][0].startInd, dutyTally[j][day1][8].endInd);//06/18/2017 ANG
			//for each duty node for that fleet and day, create all feasible arcs to duty nodes on later days
			for(k1=dutyTally[j][day1][0].startInd; k1<=dutyTally[j][day1][8].endInd; k1++)
			{
				startDuty = &dutyList[j][k1];
				//If startDuty ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
				//and we will not create any arcs out of the duty. 
				if(startDuty->lastDemInd > -1 && demandList[startDuty->lastDemInd].succDemID > 0)
					continue;
				
				//if startDuty is not tied to a plane...
				if(startDuty->aircraftID == 0)   //	if(startDuty->acInd == -1)
				{
					//allocate memory for an arcList array for a typical duty node.  will allocate memory for arc structures as needed below.
					startDuty->arcList = (NetworkArc **) calloc(dutyArcListTally[j][day1][0], sizeof(NetworkArc *));
					if(! startDuty->arcList) {
						logMsg(logFile,"%s Line %d, Out of Memory in createDutyNetwork().\n", __FILE__, __LINE__);
						writeWarningData(myconn); exit(1);
					}
				}
				else //if startDuty is tied to a plane...
				{	
					//exit with error message if acIndex is not found
					if(startDuty->acInd == -1){
						logMsg(logFile,"%s Line %d, acIndex not found for dutyList[%d][%d].\n", __FILE__,__LINE__, j, k1);
						writeWarningData(myconn); exit(1);
					}
					//check to see if the dutyNode is reachable by (feasible for) the plane (if not, don't generate any arcs)
					if(acList[startDuty->acInd].unreachableInd > -1 && startDuty->unreachableFlag[acList[startDuty->acInd].unreachableInd]==1)
						continue;		
					//allocate memory for an arcList for a duty node that is tied to a plane  
					//will allocate memory for arc structures as needed below
					startDuty->arcList = (NetworkArc **) calloc(1, sizeof(NetworkArc *));
					if(! startDuty->arcList) {
						logMsg(logFile,"%s Line %d, Out of Memory in createDutyNetwork().\n", __FILE__, __LINE__);
						writeWarningData(myconn); exit(1);
					}
				}
				//initialize arcTallyByDay. Note that arcTallyByDay[0] will always equal -1 
				//as there can be no arcs from a dutyNode to a dutyNode on the first day
				for(d = 0; d < optParam.planningWindowDuration; d++)
					startDuty->arcTallyByDay[d] = -1;
				//initialize countsPerArcList
				for(x = 0; x < dutyArcListTally[j][day1][0]; x++)
					countsPerArcList[x]=0;
				
				//determine end airport and fbo of duty
				if(startDuty->repoDemandInd > -1){
					startAptID = demandList[startDuty->repoDemandInd].outAirportID;
					startFboID = demandList[startDuty->repoDemandInd].outFboID;
				}
				else{
					startAptID = demandList[startDuty->lastDemInd].inAirportID;
					startFboID = demandList[startDuty->lastDemInd].inFboID;
				}
				//generate arcs from duty nodes to duty nodes on later days.
				if(k1 <= dutyTally[j][day1][maxTripsPerDuty - 1].endInd)  //if k1 is NOT a "final repo" or "repo only" node, generate arcs to all later days in window
				{
					for(day2 = day1 + 1; day2 < optParam.planningWindowDuration; day2++)
						createDutyToDutyArcs(j, day1, day2, startDuty, startAptID, startFboID);	
				}
				else //k1 is a "final repo" or "repo only" node, and we only generate arcs to (some of) next day's nodes
					createDutyToDutyArcs(j, day1, (day1 + 1), startDuty, startAptID, startFboID);
				
				//allocate memory to store countsPerArcList with the duty node
				startDuty->countsPerArcList = (int *) calloc(dutyArcListTally[j][day1][0], sizeof(int));
				if(! startDuty->countsPerArcList) {
					logMsg(logFile,"%s Line %d, Out of Memory in createDutyNetwork().\n", __FILE__, __LINE__);
					writeWarningData(myconn); exit(1);
				}
				//store countsPerArcList with duty node
				for(x = 0; x < dutyArcListTally[j][day1][0]; x++){
					startDuty->countsPerArcList[x] = countsPerArcList[x];
					//logMsg(logFile, "countsPerArcList[%d] = %d\n", x, countsPerArcList[x]);
				}
			} //end for(k1..) loop		
		}  //end for(day1..) loop
	}  //end for(j...) loop
	return 0;
}  //end createDutyNetwork





/********************************************************************************
*	Function   createDutyToDutyArcs			  Date last modified:  03/12/07 SWO	*
*	Purpose:	create arcs between duty nodes and the later day's duty nodes	*
*				First create general arcs for fleet, then create plane-specific	*
*				arc lists if necessary.											*
********************************************************************************/
static int createDutyToDutyArcs(int acTypeListInd, int day1, int day2, Duty *startDuty, int startAptID, int startFboID)
{
	int k2, j, overnightTm, endAptID, repoStartTm, arcListInd, arcInd, acInd, z, p;
	NetworkArc *newArc, *parentArc;
	Duty *endDuty;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int canReach, y, flag;
	int m, n;
	int addTime = 0; //OCF - 10/18/11 ANG

	j= acTypeListInd;
	if(day2 == day1 + 1)
		//overnightTm = optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm;
		//overnightTm = (startDuty->twoDutyFlag == 1) ? optParam.postFlightTm + optParam.minSecondRestTm + optParam.preFlightTm : optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm; //2DutyDay - 05/23/10 ANG
		overnightTm = (startDuty->twoDutyFlag == 1) ? optParam.postFlightTm + optParam.minSecondRestTm + optParam.preFlightTm : optParam.postFlightTm + optParam.minRestTm + acTypeList[j].preFlightTm; //07/17/2017 ANG
	//re-initialize arcTallyByDay if we may have already created arcs out of this duty node
	if(day2 > (day1+1))
		startDuty->arcTallyByDay[day2] = startDuty->arcTallyByDay[day2-1];
	//for each of the day2 dutyNodes for this fleet
	for(k2=dutyTally[j][day2][0].startInd; k2<=dutyTally[j][day2][8].endInd; k2++)
	{
		endDuty = &dutyList[j][k2];

		//if start node is a "final repo" or "repo only" node, endDuty must start with the trip that we positioned to
		if(startDuty->repoDemandInd > -1)
		{	if(startDuty->repoDemandInd != endDuty->demandInd[0])
				continue;
		}
		//If end duty ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
		//and we will not create an arc to the duty. 
		if(endDuty->lastDemInd > -1 && demandList[endDuty->lastDemInd].succDemID > 0)
			continue;

		//fei FA //check whether two duties contain the same demand
		//assume that this is only possible for maint/app
		 if( optParam.withFlexOS )
		 {
			 for(m=0; m < maxTripsPerDuty; m ++)
				 if( startDuty->demandInd[m] >= 0 )
				 {
					 for(n=0; n < maxTripsPerDuty; n ++)
						 if( endDuty->demandInd[n] >= 0 && demandList[startDuty->demandInd[m]].origDemInd ==  demandList[endDuty->demandInd[n]].origDemInd)
							 break;

					 if( n < maxTripsPerDuty )//found
						 break;
				 }
			 if( m < maxTripsPerDuty )//found
				 continue;
		 }//end

		//initialize arc list index (= 0, parent arc list, UNLESS start duty is not tied to a plane and end duty IS, in which case we update arc list index below)
		arcListInd = 0;

		//if startDuty is tied to a plane...
		if(startDuty->acInd > -1)
		{
			if(endDuty->aircraftID != 0 && endDuty->aircraftID != startDuty->aircraftID) //duty must not be tied to a different plane
				continue;
			z = acList[startDuty->acInd].unreachableInd;
			//end duty must be reachable for that plane (includes check of inclusions/exclusions)
			if(z>-1 && endDuty->unreachableFlag[z] == 1)
				continue;

			if ( ! optParam.withFlexOS )
			{
				//if end duty is not the day after the start duty, make sure we didn't skip a day with inclusions
				//Note: we may skip some for flex OS - FlexOS - 02/01/11 ANG
				if(day2 - day1 > 1)
					if(acList[startDuty->acInd].lastIncl[day2-1] > acList[startDuty->acInd].lastIncl[day1])
						continue;
			} else // if ( acList[startDuty->acInd].numIncl)
			{
				_ASSERTE( acList[startDuty->acInd].numIncl ) ;
				//if end duty is not the day after the start duty, make sure we didn't skip a day with inclusions
				if(day2 - day1 > 1)
					//if( acList[startDuty->acInd].inclInfoP->seHardIndByDay[0][day2] > acList[startDuty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei Jan 2011
					if( acList[startDuty->acInd].inclInfoP->seHardIndByDay[1][day2-1] > acList[startDuty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei FA
						continue;

				 if( endDuty->firstInclInd >= 0 && endDuty->firstInclInd <= startDuty->lastInclInd )//order of inclusions
					continue;
			}//end else
		}
		//else if startDuty is NOT tied to a plane and endDuty is, make sure both start duty and end duty are reachable for that plane, 
		//and that we don't skip any inclusions for plane
		else if(endDuty->acInd > -1){
			z = acList[endDuty->acInd].unreachableInd;
			if(z>-1 && startDuty->unreachableFlag[z] == 1)
				continue;
			if(z>-1 && endDuty->unreachableFlag[z] == 1)
				continue;
			if ( optParam.withFlexOS ) //&& acList[endDuty->acInd].numIncl)
			{
				_ASSERTE( acList[endDuty->acInd].numIncl ) ;
				//if( acList[endDuty->acInd].inclInfoP->seHardIndByDay[0][day2] > acList[endDuty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei Jan 2011
				if( acList[endDuty->acInd].inclInfoP->seHardIndByDay[1][day2-1] > acList[endDuty->acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei FA
					continue;
			} else 
			{
				if(acList[endDuty->acInd].lastIncl[day2-1] > acList[endDuty->acInd].lastIncl[day1])
					//Note: we may skip some for flex OS - FlexOS - 02/01/11 ANG
					continue;
			}

			//update arc list index to index for plane (not parent arc for fleet)
			arcListInd = acList[endDuty->acInd].dutyNodeArcIndex[day1];
		}

		if(endDuty->demandInd[0] != -1)
			endAptID = demandList[endDuty->demandInd[0]].outAirportID;
		else 
			endAptID = demandList[endDuty->repoDemandInd].outAirportID;

		//if no reposition to duty
		if(startAptID == endAptID)
		{	//if this is a repo only node and there is no reposition required, don't create arc
			if(endDuty->demandInd[0] == -1)
				continue;
			//if generating arcs for NEXT day, check if it is possible to get to duty on time,
			//and that crew can get to duty on time after resting (assume okay for subsequent days)
			if(day2 == day1 + 1){
				if(endDuty->crewStartTm < startDuty->crewEndTm + overnightTm)		//RLZ CHECK
				//if ((endDuty->crewStartTm < startDuty->crewEndTm + overnightTm) && startDuty->crewEndTm >= startDuty->crewStartTm)
					continue;

			//	if ((endDuty->crewStartTm < startDuty->startTm[0]  + overnightTm) && startDuty->crewEndTm < startDuty->crewStartTm){
                    //continue;  //RLZ
			//		startDuty->crewEndTm = endDuty->crewStartTm -  overnightTm;
			//	}

			}//fei FA
			
			//fei FA: move out of }
			if(startDuty->repoDemandInd == -1)
			{
				if(endDuty->startTm[0]< startDuty->endTm + demandList[startDuty->lastDemInd].turnTime)
					continue;
			} else if(endDuty->startTm[0]< startDuty->endTm + optParam.turnTime)
				continue;
			
			//}//fei FA

			//check OCF requirements - only for aircraft specific duties - 10/11/11 ANG
			if (optParam.withFlexOS && startDuty->lastDemInd > -1 && startDuty->repoDemandInd == -1){
				//only if the last demand needs an OCF then do the check
				if(demandList[startDuty->lastDemInd].reqOCF == 1){
					continue; //cannot do this when no reposition to duty - this code is hit when if (startAptID == endAptID)
				}
			}

			//no need to check duty time here, as we have already checked duty time for the endDuty node, and we aren't adding any significant duty time
			//We assume fboTransit is not part of blockTm (plane may be towed, may be inconsequential) or duty time if at start/end of day per BILL HALL 4/12/06

			//If we have gotten through all the checks, the arc is feasible.
			//allocate memory for arc for parent OR plane-specific arc list per arc list index
//DEBUG
			//exit with error message if we are exceeding arcListInd
			if(arcListInd > (startDuty->aircraftID == 0? (dutyArcListTally[j][day1][0]-1) : 0)){
				logMsg(logFile,"%s Line %d, arcListInd out of bounds for j = %d, day1 = %d.\n", __FILE__,__LINE__, j, day1);
				writeWarningData(myconn); exit(1);
			}
//END DEBUG

			if(rand()%100 < (100 - optParam.percentCreateDutyArc) ) //add randomness to limit number of created arcs - 06/20/2017 ANG
				continue;

			newArc = arcArrayAlloc(startDuty->arcList, arcListInd);
			//create arc
			newArc->destDutyInd = k2;
			newArc->blockTm = endDuty->blockTm;
			newArc->cost = endDuty->cost; //includes cost of destination node
			if(optParam.withMac == 1){
				newArc->tempCostForMac = endDuty->tempCostForMac; //MAC - 09/23/08 ANG
				newArc->repoFromAptID = 0; //MAC - 09/23/08 ANG - Note: no repo needed in this duty-to-duty arc
				newArc->macRepoFltTm = 0; 
				newArc->macRepoStop = 0;
			}

			//assume fboTransit is not part of blockTm (may be towed, may be inconsequential) per BILL HALL 4/12/06
			newArc->startTm = endDuty->startTm[0];  //RLZ: CHECK crewStartTm? or dose not matter now. 
			//increment arcTallyByDay and total arc count
			if(arcListInd == 0)
				startDuty->arcTallyByDay[day2]++; //tally for parent arc list
			numDutyArcs++;
		} //end no repo loop

		else //we need to repo (or repo only) to duty
		{
			getFlightTime(startAptID, endAptID, acTypeList[j].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
			//check repo flight time against limit
			if (repoFltTm > optParam.maxRepoTm)
				continue;
			repoStartTm = getRepoDepartTm(startAptID, endAptID, endDuty->startTm[0] - optParam.turnTime, repoElapsedTm);

			//check if repo is feasible considering curfews
			if(repoStartTm == -1)
				continue;
			//if generating arcs to duties on the NEXT day, check if it is possible to get to duty on time after resting (assume okay for subsequent days)
			if(day2 == (day1 + 1)){
				//if(repoStartTm < startDuty->endTm + overnightTm)	//RLZ CHECK	
				if(repoStartTm < startDuty->crewEndTm + overnightTm)
			//	if ((repoStartTm < startDuty->crewEndTm + overnightTm) && startDuty->crewEndTm >= startDuty->crewStartTm)
					continue;

			//	if(repoStartTm < startDuty->endTm)	//RLZ 06/23/2008 make sure duty node not overlap	
			//		continue;


				//if ((repoStartTm < startDuty->startTm[0] + overnightTm) && startDuty->crewEndTm < startDuty->crewStartTm){
                //    continue;  //RLZ
					//startDuty->crewEndTm = repoStartTm -  overnightTm;
				//}
			}

			if( optParam.withFlexOS )
			{
				_ASSERTE( startDuty->lastDemInd >= 0 ) ;
				if(repoStartTm < startDuty->endTm + demandList[startDuty->lastDemInd].turnTime )//fei FA
					continue;
			}//end

			//check block time and duty time
			if((repoBlkTm + endDuty->blockTm) > optParam.maxFlightTm) 
				continue;
			//if this is a repo-only node, endDuty->endTm may not apply due to curfews
			if(endDuty->demandInd[0] == -1){
				//if(repoElapsedTm > netMaxDutyTm)
				if(repoElapsedTm > ((endDuty->twoDutyFlag == 1) ? netMaxDutyTm+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTm))//2DutyDay - 05/21/10 ANG
					continue;
			}
			//else if last leg of duty is appointment and there is no final repo, crew need not stay on duty for the last leg //RLZ CHECK
			//else if((endDuty->crewEndTm - repoStartTm) > netMaxDutyTm)
			else if((endDuty->crewEndTm - repoStartTm) > ((endDuty->twoDutyFlag == 1) ? netMaxDutyTm+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTm))//2DutyDay - 05/21/10 ANG
				continue;
			else{
				//RLZ early duty rule
				//if (minutesPastMidnight((repoStartTm - optParam.preFlightTm)*60 , startAptID) <= optParam.cutoffForShortDuty){
				if (minutesPastMidnight((repoStartTm - acTypeList[j].preFlightTm)*60 , startAptID) <= optParam.cutoffForShortDuty){ //07/17/2017 ANG
					//if (endDuty->crewEndTm - repoStartTm > netMaxDutyTmEarly)
					if (endDuty->crewEndTm - repoStartTm > ((endDuty->twoDutyFlag == 1) ? netMaxDutyTmEarly+optParam.minRestTm+optParam.maxSecondDutyTm : netMaxDutyTmEarly))//2DutyDay - 05/21/10 ANG
						continue;				
				}
			}

			//check OCF requirements - only for aircraft specific duties - 10/11/11 ANG
			if(optParam.withOcf == 1){
				if (optParam.withFlexOS && startDuty->lastDemInd > -1 && startDuty->repoDemandInd == -1){
					//only if the last demand needs an OCF then do the check
					if(demandList[startDuty->lastDemInd].reqOCF == 1){
						addTime = 0;
						if (!checkOcfTiming(repoStartTm - (optParam.turnTime + optParam.preOCFtime + optParam.postOCFtime), startAptID, endDuty->startTm[0], endAptID, j, repoStartTm, &addTime))
							continue; //cannot do this when no reposition to duty (startAptID == endAptID)
						else if (addTime > 0)
							repoStartTm = repoStartTm - addTime; //note: addTm includes preOCF and postOCF times
					}
				}
			}

			p = (startDuty->acInd > -1? startDuty->acInd : endDuty->acInd);
//DEBUG
			//exit with error message if we are exceeding arcListInd
			if(arcListInd > (startDuty->acInd == -1? (dutyArcListTally[j][day1][0]-1) : 0)){
				logMsg(logFile,"%s Line %d, arcListInd out of bounds for j = %d, day1 = %d.\n", __FILE__,__LINE__, j, day1);
				writeWarningData(myconn); exit(1);
			}
//DEBUG

			if(rand()%100 < (100 - optParam.percentCreateDutyArc) ) //add randomness to limit number of created arcs - 06/20/2017 ANG
				continue;

			//If we have gotten through all the checks, arc is feasible.
			//allocate memory for arc for parent OR plane-specific arc list per arc list index
			newArc = arcArrayAlloc(startDuty->arcList, arcListInd);
			//create arc
			newArc->destDutyInd = k2;
			newArc->blockTm = repoBlkTm + endDuty->blockTm;
			newArc->cost = (repoFltTm*acTypeList[j].operatingCost)/60 + (repoStops+1)*acTypeList[j].taxiCost+ endDuty->cost; //includes cost of destination node

			//START - MAC - 09/23/08 ANG
			if(optParam.withMac == 1){
				newArc->tempCostForMac = endDuty->tempCostForMac; 
				newArc->repoFromAptID = startAptID;
				newArc->macRepoFltTm = repoFltTm; 
				newArc->macRepoStop = repoStops; 
			}
			//END - MAC

			newArc->startTm = repoStartTm;
			//increment arcTallyByDay and total arc count
			if(arcListInd == 0)
				startDuty->arcTallyByDay[day2]++; //tally for parent arc list
			numDutyArcs++;
		}
	} //end k2 loop
														
	//create plane-specific arc lists (copies of parent arcs) IF START DUTY IS NOT TIED TO A PLANE
	//For end duties tied to a plane, a plane-specific arc was created above and there won't be a parent arc
	if(startDuty->acInd == -1)
	{
		//for each (potential) additional arcList for the startDuty 
		//(this depends on number of planes with same- or later-day exclusions, or later-day inclusions, for that fleet)
		for(arcListInd = 1; arcListInd < dutyArcListTally[j][day1][0]; arcListInd++){
			//get acInd for plane that corresponds to this arcList
			acInd = dutyArcListTally[j][day1][arcListInd];
			//if arcList corresponds to a single plane...
			if(acInd > -1){
				//check if startDuty node is reachable by that plane
				if(acList[acInd].unreachableInd > -1 && startDuty->unreachableFlag[acList[acInd].unreachableInd] == 1)
					continue;
				//also, we dont want to create arc if startDuty is associated with different aircraft - 11/13/08 ANG

				//if(startDuty->aircraftID > 0 && startDuty->aircraftID != acList[acInd].aircraftID)//redundant, fei Jan 2011
				//	continue;

				//make sure we aren't skipping any inclusions between day1 and day2
				if ( !optParam.withFlexOS )
				{
					if(day2 - day1 > 1)
						if(acList[acInd].lastIncl[day2-1] > acList[acInd].lastIncl[day1])
							//Note: we may skip some for flex OS - FlexOS - 02/01/11 ANG
							continue;
				} else if( acList[acInd].numIncl )
				{
					if(day2 - day1 > 1)
						//if( acList[acInd].inclInfoP->seHardIndByDay[0][day2] > acList[acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei Jan 2011
						if( acList[acInd].inclInfoP->seHardIndByDay[1][day2-1] > acList[acInd].inclInfoP->seHardIndByDay[1][day1] ) //fei FA
							continue; //fei Jan 2011
				}//end else
			}
			//else arcList corresponds to a group of planes
			else{ //index is negative index of acGroupList, for which indices 0 and 1 weren't used
				canReach = 0;
				//check if startDuty is reachable by at least one of the planes in the group
				for(y = 0; y < acGroupList[-acInd].numAircraft; y++){
					//if(acList[acGroupList[-acInd].acInd[y]].unreachableInd > -1 && (flag = startDuty->unreachableFlag[acList[acGroupList[-acInd].acInd[y]].unreachableInd]) == 2)
					//	break;
					//if(flag == 0){
					//	canReach = 1;
					//	break;
					//}
					if(acList[acGroupList[-acInd].acInd[y]].unreachableInd > -1 ) //fei Jan 2011 //flag needs to be initialized correctly
					{
						if( (flag = startDuty->unreachableFlag[acList[acGroupList[-acInd].acInd[y]].unreachableInd] ) == 2 )
							break;
						else if ( flag == 0 )
						{
							canReach = 1;
							break;
						}
					}//end if 
				}
				if(canReach == 0)
					continue;
			}
			//if we have gotten this far, we can generate arcList from the startDuty node

			//for each general fleet arc (parent arc)
			for(arcInd = (startDuty->arcTallyByDay[day2 - 1]+1); arcInd <= startDuty->arcTallyByDay[day2]; arcInd++)
			{
				parentArc = &startDuty->arcList[0][arcInd];
				//if endDuty node is not reachable by plane (or one of the group of planes), don't create arc
				endDuty = &dutyList[j][parentArc->destDutyInd];
				//if arcList corresponds to a single plane...
				if(acInd > -1){
					//check if endDuty node is reachable by that plane
					if(acList[acInd].unreachableInd > -1 && endDuty->unreachableFlag[acList[acInd].unreachableInd] == 1)
						continue;
					//also, we dont want to create arc if endDuty is associated with other aircraft - 11/13/08 ANG
					if(endDuty->aircraftID > 0 && endDuty->aircraftID != acList[acInd].aircraftID)
						continue;
				}
				//else arcList corresponds to a group of planes
				else{ //index is negative index of acGroupList, for which indices 0 and 1 weren't used
					canReach = 0;
					//check if endDuty is reachable by at least one of the planes in the group
					for(y = 0; y < acGroupList[-acInd].numAircraft; y++){
						//if(acList[acGroupList[-acInd].acInd[y]].unreachableInd > -1 && (flag = endDuty->unreachableFlag[acList[acGroupList[-acInd].acInd[y]].unreachableInd]) == 2)
						//	break;  //endDuty is unreachable by all planes in group
						//if(flag == 0){
						//	canReach = 1;
						//	break;
						//}
						if(acList[acGroupList[-acInd].acInd[y]].unreachableInd > -1 ) //fei Jan 2011 //fix the flag initialization problem
						{
							if( (flag = endDuty->unreachableFlag[acList[acGroupList[-acInd].acInd[y]].unreachableInd] ) == 2 )
								break;
							else if ( flag == 0 )
							{
								canReach = 1;
								break;
							}
						}//end if 
					}
					if(canReach == 0)
						continue;
				}

				//OCF check condition - 10/12/11 ANG
				if(optParam.withOcf == 1){
					if(optParam.withFlexOS && startDuty->lastDemInd >= 0 && endDuty->demandInd[0] >= 0){
						if(demandList[startDuty->lastDemInd].reqOCF == 1){
							if (!checkOcfTiming(demandList[startDuty->lastDemInd].reqIn, demandList[startDuty->lastDemInd].inAirportID, demandList[endDuty->demandInd[0]].reqOut, demandList[endDuty->demandInd[0]].outAirportID, j, -1, &addTime))
								continue;
						}
					}
				}

				if(rand()%100 < (100 - optParam.percentCreateDutyArc) ) //add randomness to limit number of created arcs - 06/20/2017 ANG  -1 is orignal model
					continue;

				//if we have gotten this far, allocate memory for, and create, arc
				newArc = arcArrayAlloc(startDuty->arcList, arcListInd);
				*newArc = *parentArc;
				numDutyArcCopies++;	
			}	
		}
	}
	return 0;
}  //end createDutyToDutyArcs




/****************************************************************************************
*	Function   checkPlaneInclusions					Date last modified:	04/20/06 SWO	*
*	Purpose:  	check that a duty node with inclusions includes ALL inclusions			*
*				(required trips, ie. appointments, locked legs) for plane that day	*
****************************************************************************************/
static int checkPlaneInclusions(Duty *duty, Aircraft *plane, int day, int unreachableIndex)
{
	int x, y;

	//we need only check that the last inclusion (required trip) is part of node, 
	//because we check inclusions in order during node creation
	x = plane->inclDemandInd[plane->lastIncl[day]];

	//START - 04/18/08 ANG
	if (optParam.autoFlyHome == 1 && demandList[x].isAppoint == 4){
	//if (optParam.autoFlyHome == 1){
		return 0;
	}
	//END- 04/18/08 ANG

	//for(y=0; y<4; y++){
	for(y=0; y<maxTripsPerDuty; y++){
		if(duty->demandInd[y] == x)
			return 0; //feasible
	}
	
	duty->unreachableFlag[unreachableIndex] = 1;	//this node is tied to a plane, so entire node is unreachable
	return -1; //infeasible
}

static int checkPlaneInclusionsFA(Duty *duty, Aircraft *plane, int day, int unreachableIndex)
{
	int x, y;

	//fei Jan 2011
	_ASSERTE( unreachableIndex >= 0 && plane->numIncl && plane->inclInfoP->seHardIndByDay[1][day] > plane->inclInfoP->seHardIndByDay[0][day] ) ;
	
	x = plane->inclInfoP->origDemIndices[plane->inclInfoP->prevHardIncl[plane->inclInfoP->seHardIndByDay[1][day]]];//fei Jan 2011//last hard inclusion of current day
	
	_ASSERTE( !origDemInfos[x].skipIncl && x >= plane->inclInfoP->seHardIndByDay[0][day] );//hard inclusion on current day

	//START - 04/18/08 ANG
	if (optParam.autoFlyHome == 1 && origDemInfos[x].isAppoint == 4){
		return 0;
	}
	//END- 04/18/08 ANG

	for(y=0; y<4; y++){
		if(demandList[duty->demandInd[y]].origDemInd == x)
			return 0; //feasible
	}

	duty->unreachableFlag[unreachableIndex] = 1;	//this node is tied to a plane, so entire node is unreachable
	return -1; //infeasible
}

/************************************************************************************************************************
*	Function	getCrewArcTimeAndCost							Date last modified:  8/17/07 SWO	
*	Purpose:	For arcs to planes and pickups at ends of trips, we calculate early pickup time (earliest
*				time crew could get to plane location) and cost for crew arcs.							
*					For arcs to pickups at start of trips, we do as above.  Also, we check if crew could get to 
*				plane before the latest trip start.  Finally,  if a pilot has to travel to plane and has duty hours at early
*				pickup > 0 and they could start later, we know that they must travel that day but could
*				possibly travel later in the day, so check LATEST commercial flight.  Then, when generating
*				arcs from pickup at trip start to a duty, we need no additional time calcs for this pilot
*				UNLESS the duty starts later than than the earliest trip start (flex time).
* 
****************************************************************************************************************************/

static int getCrewArcTimeAndCost(int cp, int pickUpAptID, time_t acAvailTm, int puStartDemandInd, CrewArc *newCrewArc)
{
	int y, yMax, preFltTm, j, tmpPickupTm;
	int crListInd[2];
	time_t lateArr, earlyDpt;
	time_t arrivalTm = 0;
	time_t dutyStartTm = 0;
	time_t departTm = 0;
	double minusOne = -1.0;
	double cost = 0;
	int day, day1, day2, postFlightTm, hasEarlyStart; //FATIGUE - 02/05/10 ANG
	//NOTE: Do we need to implement fatigue in this function? 
	//      Maybe NOT? since this function is not called in createArcsToFirstDuties()? - FATIGUE - 02/26/10 ANG
	
	j = crewPairList[cp].acTypeIndex;

	//if crewPair is already together and has same hours
	if(crewPairList[cp].availAptID > 0)//this field is only populated if crewPair is already together and has same hours at start of window
		yMax = 0; //need only look at one crew member (say, the pilot)
	else //else crewPair is not already together or members have different hours logged, so consider crew members separately
		yMax = 1;
	//get latest arrival time of crew to plane
	if(puStartDemandInd > -1)
	    lateArr = (time_t)(60*(demandList[puStartDemandInd].late[j]-optParam.firstPreFltTm));
	else
		lateArr = maxArr;

	//get ticket cost and early pickup time and hours for each crew member
	crListInd[0] = crewPairList[cp].crewListInd[0];
	crListInd[1] = crewPairList[cp].crewListInd[1];
	newCrewArc->cost = 0.0;

	//START - FATIGUE - 02/05/10 ANG
	//get day of puStartDemandInd
	hasEarlyStart = 0;
	postFlightTm = optParam.postFlightTm;
	day = -1;
	day1 = crewList[crListInd[0]].day;
	day2 = crewList[crListInd[1]].day;
	if(puStartDemandInd > -1){
		if( demandList[puStartDemandInd].reqOut < firstEndOfDay + 86400*0 &&
			demandList[puStartDemandInd].reqOut >= firstEndOfDay + 86400*(0-1))
			day = 0;
		else if( demandList[puStartDemandInd].reqOut < firstEndOfDay + 86400*1 &&
			demandList[puStartDemandInd].reqOut >= firstEndOfDay + 86400*(1-1))
			day = 1;
		else if( demandList[puStartDemandInd].reqOut < firstEndOfDay + 86400*2 &&
			demandList[puStartDemandInd].reqOut >= firstEndOfDay + 86400*(2-1))
			day = 2;

		//populate hasEarlyStart and postFlightTm
		if( ifCrewFirstDayStart5to8AM(day, cp, demandList[puStartDemandInd].reqOut, demandList[puStartDemandInd].outAirportID) ){
			hasEarlyStart = 1;
			postFlightTm += optParam.shortDutyHrDif;
		}
	}
	//END - FATIGUE - 02/05/10 ANG

	//Revise logic for availDT - 10/27/09 ANG
	if(yMax == 0){
		y = 0;
		//if picking up at start of trip, check if pilot is available before latest trip start
		if(puStartDemandInd > -1){
			if(crewPairList[cp].availDT > (60*demandList[puStartDemandInd].late[j])) 
				return -1;  //can't get to trip on time
		}

		if(crewPairList[cp].availAptID != pickUpAptID){//if pilot must travel to plane
			//conservatively add postFlightTm to earlyDpt for pilots who have flown
			//do not add if changing AC. RLZ  This may not be able to completely address the postFlightTm waste issue.
			//still on the conservative side.

			//earlyDpt = crewList[crListInd[y]].availDT + 60*(crewList[crListInd[y]].blockTm > 0? optParam.postFlightTm : 0);
			//earlyDpt = crewPairList[cp].availDT + (1-crewList[crListInd[0]].inTourTransfer)*60*(crewPairList[cp].blockTm > 0? optParam.postFlightTm : 0); FATIGUE - 02/05/10 ANG
			earlyDpt = crewPairList[cp].availDT + (1-crewList[crListInd[0]].inTourTransfer)*60*(crewPairList[cp].blockTm > 0? postFlightTm : 0);

			if(getCrewTravelDataEarly(earlyDpt, lateArr, crewPairList[cp].availAptID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
				return -1; //if no flight to the plane, return -1 (don't create arc)
			//if((int)(arrivalTm - dutyStartTm)/60 > optParam.maxDutyTm)//if takes longer than allowable duty just to travel to plane, don't create arc 
			if((int)(arrivalTm - dutyStartTm)/60 > optParam.maxDutyTm - ((hasEarlyStart==1) ? optParam.shortDutyHrDif : 0))//Replace above code - FATIGUE - 02/05/10 ANG
				return -1;
			//check if could rest before earliest possible commercial flight, which is perhaps not leaving til next morning
			//conservatively add postFlightTm for pilots who have flown
			//do not add if changing AC. RLZ  
			//if((int)((departTm - crewPairList[cp].availDT)/60 - ((crewPairList[cp].blockTm*(1-crewList[crListInd[0]].inTourTransfer)) > 0? optParam.postFlightTm : 0)) > optParam.minRestTm){ FATIGUE - 02/05/10 ANG
			if((int)((departTm - crewPairList[cp].availDT)/60 - ((crewPairList[cp].blockTm*(1-crewList[crListInd[0]].inTourTransfer)) > 0? postFlightTm : 0)) > optParam.minRestTm){
				newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60); //will add preFlightTm below
				newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
				newCrewArc->blockTmAtEPU[y] = 0;
				newCrewArc->canStrtLtrAtEPU[y] = 1;
			}
			//else check if can travel on earliest flight without exceeding duty
			//else if((int)arrivalTm/60 - (crewPairList[cp].activityCode>0? (int)dutyStartTm/60 : ((int)crewPairList[cp].availDT/60 -crewPairList[cp].dutyTime)) < optParam.maxDutyTm){ FATIGUE - 02/05/10 ANG
			else if((int)arrivalTm/60 - (crewPairList[cp].activityCode>0? (int)dutyStartTm/60 : ((int)crewPairList[cp].availDT/60 -crewPairList[cp].dutyTime)) < optParam.maxDutyTm - ((hasEarlyStart==1) ? optParam.shortDutyHrDif : 0)){
				newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60); //will add preFlightTm below
				newCrewArc->dutyTmAtEPU[y] = (int)arrivalTm/60 - (crewPairList[cp].activityCode? (int)dutyStartTm/60 : ((int)crewPairList[cp].availDT/60 -crewPairList[cp].dutyTime)); //tempDutyTm;
				newCrewArc->blockTmAtEPU[y] = crewPairList[cp].blockTm;
				newCrewArc->canStrtLtrAtEPU[y] = (crewPairList[cp].activityCode? 1 : 0); //if activity code is 1 or 2 (coming from rest but not yet notifed, or starting tour) then can start later
			}
			else { //else can't travel without exceeding duty, so must rest first before commercial flight
				earlyDpt += 60*optParam.minRestTm;
				if(getCrewTravelDataEarly(earlyDpt, lateArr, crewPairList[cp].availAptID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag))
					return -1; //if no flight to the plane, return -1 (don't create arc)
				newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60);//will add preFlightTm below
				newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
				newCrewArc->blockTmAtEPU[y] = 0;
				newCrewArc->canStrtLtrAtEPU[y] = 1;
			}
			newCrewArc->cost += cost;
		} // end if pilot must travel to plane

		else{ //pilot need not travel to plane
			//arc cost = 0 (no commercial ticket)
			//Get earliest time at which crew could depart on a leg
			newCrewArc->earlyPickupTm[y] = (int)(crewPairList[cp].availDT/60);
			newCrewArc->dutyTmAtEPU[y] = crewPairList[cp].dutyTime;
			newCrewArc->blockTmAtEPU[y] = crewPairList[cp].blockTm;
			newCrewArc->canStrtLtrAtEPU[y] =(crewPairList[cp].activityCode? 1 : 0);
			//RLZ: double check. deal with turn time for availDT
			tmpPickupTm = newCrewArc->earlyPickupTm[y];
			if (newCrewArc->canStrtLtrAtEPU[y]==0 && newCrewArc->blockTmAtEPU[y]>0 && crewPairList[cp].blockTm>0 && crewPairList[cp].acInd >-1 ){
				newCrewArc->earlyPickupTm[y] = max(tmpPickupTm, (int)(acList[crewPairList[cp].acInd].availDT/60));
				newCrewArc->dutyTmAtEPU[y] += (newCrewArc->earlyPickupTm[y] - tmpPickupTm);
			}
		}
		//Calculate preFlight time here
		if(crewPairList[cp].hasFlownFirst == 1 && crewPairList[cp].optAircraftID == crewPairList[cp].aircraftID[0]){ 
			if(newCrewArc->blockTmAtEPU[0] > 0)
				preFltTm = 0;
			else
				//preFltTm = optParam.preFlightTm;
				preFltTm = acTypeList[j].preFlightTm; //07/17/2017 ANG
		}
		else
			preFltTm = optParam.firstPreFltTm;

		if(preFltTm > 0 && newCrewArc->dutyTmAtEPU[0] > 0) //Added 10/25/10 ANG
			preFltTm = max (0,  preFltTm - newCrewArc->dutyTmAtEPU[0]);

		//RLZ 04/30/2008 Save preFlightTm is crew is idle. This step, I think, is done by adjusting availDT.

		//NOW CONSIDER AVAILABLE TIME OF PLANE
		//earlyPickupTm is not delayed until acAvailTm here, because we still need to add preFlight time, and we consider
		//acAvailTm in pickup arcs and plane arcs

		//RLZ earlyPickupTm need to be delayed, preFlight time is not added for all cases. 05/16/08

		//if pilot has duty hours > 0 and can rest BEFORE plane is available, put them to rest
		//conservatively add postFlightTm for pilots who have flown
		//if(newCrewArc->dutyTmAtEPU[y] > 0 && (newCrewArc->earlyPickupTm[y]+ (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0) + optParam.minRestTm + preFltTm) < (int)acAvailTm/60){ FATIGUE - 02/05/10 ANG
		if(newCrewArc->dutyTmAtEPU[y] > 0 && (newCrewArc->earlyPickupTm[y]+ (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0) + optParam.minRestTm + preFltTm) < (int)acAvailTm/60){
			//newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0) + optParam.minRestTm; //will add preFltTm later
			newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0) + optParam.minRestTm; //FATIGUE - 02/05/10 ANG
			newCrewArc->dutyTmAtEPU[y] = 0;
			newCrewArc->blockTmAtEPU[y] = 0;
			newCrewArc->canStrtLtrAtEPU[y] = 1;
		}
		//if pilot will be out of duty before this particular plane could be flown for a 30 minute flight, and can NOT start duty later, put to rest
		//note:  we know pilot can't finish rest before plane is available from above
		//else if(newCrewArc->canStrtLtrAtEPU[y] == 0 && ((newCrewArc->earlyPickupTm[y] + preFltTm <(int)acAvailTm/60? ((int)acAvailTm/60 - newCrewArc->earlyPickupTm[y]) : preFltTm)+ 30 + optParam.postFlightTm + newCrewArc->dutyTmAtEPU[y]) > optParam.maxDutyTm){ FATIGUE - 02/05/10 ANG
		else if(newCrewArc->canStrtLtrAtEPU[y] == 0 && ((newCrewArc->earlyPickupTm[y] + preFltTm <(int)acAvailTm/60? ((int)acAvailTm/60 - newCrewArc->earlyPickupTm[y]) : preFltTm)+ 30 + postFlightTm + newCrewArc->dutyTmAtEPU[y]) > optParam.maxDutyTm){
			//newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0)+ optParam.minRestTm;
			newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0)+ optParam.minRestTm; //FATIGUE - 02/05/10 ANG
			newCrewArc->dutyTmAtEPU[y] = 0;
			newCrewArc->blockTmAtEPU[y] = 0;
			newCrewArc->canStrtLtrAtEPU[y] = 1;
		}

		//if picking up at start of trip, check if pilot can get to plane before latest trip start
		if(puStartDemandInd > -1){
			if(newCrewArc->earlyPickupTm[y]+ preFltTm > demandList[puStartDemandInd].late[j])
				return -1;  //can't get to trip on time
		}

		//if pilot is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > 0 AND 
		//canStrtLtr == 1 AND earlyPickup is before earliest trip start, THEN we know that they 
		//must travel that day but could possibly travel later in the day, so check LATEST commercial flight.
		if(puStartDemandInd > -1){
			if(crewPairList[cp].availAptID != pickUpAptID && 
				newCrewArc->dutyTmAtEPU[y]>0 && newCrewArc->canStrtLtrAtEPU[y] == 1 && 
				(newCrewArc->earlyPickupTm[y] + preFltTm)< demandList[puStartDemandInd].early[j]){
				//we know that at least one itinerary is available from above, so no need to check for return of -1
				getCrewTravelDataLate(earlyDpt, 60*(demandList[puStartDemandInd].early[j] - preFltTm),
					crewPairList[cp].availAptID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag);
				newCrewArc->earlyPickupTm[y] = (int)arrivalTm/60;
				newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
			}
		}
	}//end if yMax==0
	else{
		for(y = 0; y <= yMax; y++){
			//if picking up at start of trip, check if pilot is available before latest trip start
			if(puStartDemandInd > -1){
				if(crewList[crListInd[y]].availDT > (60*demandList[puStartDemandInd].late[j])) 
					return -1;  //can't get to trip on time
			}

			if(crewList[crListInd[y]].availAirportID != pickUpAptID){//if pilot must travel to plane
				//conservatively add postFlightTm to earlyDpt for pilots who have flown
				//do not add if changing AC. RLZ  This may not be able to completely address the postFlightTm waste issue.
				//still on the conservative side.

				//earlyDpt = crewList[crListInd[y]].availDT + 60*(crewList[crListInd[y]].blockTm > 0? optParam.postFlightTm : 0);
				//earlyDpt = crewList[crListInd[y]].availDT + (1-crewList[crListInd[y]].inTourTransfer)*60*(crewList[crListInd[y]].blockTm > 0? optParam.postFlightTm : 0); FATIGUE - 02/05/10 ANG
				earlyDpt = crewList[crListInd[y]].availDT + (1-crewList[crListInd[y]].inTourTransfer)*60*(crewList[crListInd[y]].blockTm > 0? postFlightTm : 0);

				if(getCrewTravelDataEarly(earlyDpt, lateArr, crewList[crListInd[y]].availAirportID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					return -1; //if no flight to the plane, return -1 (don't create arc)

				//if((int)(arrivalTm - dutyStartTm)/60 > optParam.maxDutyTm)//if takes longer than allowable duty just to travel to plane, don't create arc 
				if((int)(arrivalTm - dutyStartTm)/60 > optParam.maxDutyTm - (hasEarlyStart==1 ? optParam.shortDutyHrDif : 0))//replace the above - FATIGUE - 02/05/10 ANG
					return -1;
				//check if could rest before earliest possible commercial flight, which is perhaps not leaving til next morning
				//conservatively add postFlightTm for pilots who have flown
				//do not add if changing AC. RLZ  
				//if((int)((departTm - crewList[crListInd[y]].availDT)/60 - ((crewList[crListInd[y]].blockTm*(1-crewList[crListInd[y]].inTourTransfer)) > 0? optParam.postFlightTm : 0)) > optParam.minRestTm){ FATIGUE - 02/05/10 ANG
				if((int)((departTm - crewList[crListInd[y]].availDT)/60 - ((crewList[crListInd[y]].blockTm*(1-crewList[crListInd[y]].inTourTransfer)) > 0? postFlightTm : 0)) > optParam.minRestTm){
					newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60); //will add preFlightTm below
					newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
					newCrewArc->blockTmAtEPU[y] = 0;
					newCrewArc->canStrtLtrAtEPU[y] = 1;
				}
				//else check if can travel on earliest flight without exceeding duty
				//else if((int)arrivalTm/60 - (crewList[crListInd[y]].activityCode>0? (int)dutyStartTm/60 : ((int)crewList[crListInd[y]].availDT/60 -crewList[crListInd[y]].dutyTime)) < optParam.maxDutyTm){ FATIGUE - 02/05/10 ANG
				else if((int)arrivalTm/60 - (crewList[crListInd[y]].activityCode>0? (int)dutyStartTm/60 : ((int)crewList[crListInd[y]].availDT/60 -crewList[crListInd[y]].dutyTime)) < optParam.maxDutyTm - ((hasEarlyStart==1) ? optParam.shortDutyHrDif : 0)){
					newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60); //will add preFlightTm below
					newCrewArc->dutyTmAtEPU[y] = (int)arrivalTm/60 - (crewList[crListInd[y]].activityCode? (int)dutyStartTm/60 : ((int)crewList[crListInd[y]].availDT/60 -crewList[crListInd[y]].dutyTime)); //tempDutyTm;
					newCrewArc->blockTmAtEPU[y] = crewList[crListInd[y]].blockTm;
					newCrewArc->canStrtLtrAtEPU[y] = (crewList[crListInd[y]].activityCode? 1 : 0); //if activity code is 1 or 2 (coming from rest but not yet notifed, or starting tour) then can start later
				}
				else { //else can't travel without exceeding duty, so must rest first before commercial flight
					earlyDpt += 60*optParam.minRestTm;
					if(getCrewTravelDataEarly(earlyDpt, lateArr, crewList[crListInd[y]].availAirportID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag))
						return -1; //if no flight to the plane, return -1 (don't create arc)
					newCrewArc->earlyPickupTm[y] = (int)(arrivalTm/60);//will add preFlightTm below
					newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
					newCrewArc->blockTmAtEPU[y] = 0;
					newCrewArc->canStrtLtrAtEPU[y] = 1;
				}
				newCrewArc->cost += cost;
			} // end if pilot must travel to plane

			else{ //pilot need not travel to plane
				//arc cost = 0 (no commercial ticket)
				//Get earliest time at which crew could depart on a leg
				newCrewArc->earlyPickupTm[y] = (int)(crewList[crListInd[y]].availDT/60);
				newCrewArc->dutyTmAtEPU[y] = crewList[crListInd[y]].dutyTime;
				newCrewArc->blockTmAtEPU[y] = crewList[crListInd[y]].blockTm;
				newCrewArc->canStrtLtrAtEPU[y] =(crewList[crListInd[y]].activityCode? 1 : 0);
				//RLZ: double check. deal with turn time for availDT
				tmpPickupTm = newCrewArc->earlyPickupTm[y];
				if (newCrewArc->canStrtLtrAtEPU[y]==0 && newCrewArc->blockTmAtEPU[y]>0 && crewPairList[cp].blockTm>0 && crewPairList[cp].acInd >-1 ){
					newCrewArc->earlyPickupTm[y] = max(tmpPickupTm, (int)(acList[crewPairList[cp].acInd].availDT/60));
					newCrewArc->dutyTmAtEPU[y] += (newCrewArc->earlyPickupTm[y] - tmpPickupTm);
				}
			}
			//Calculate preFlight time here
			if(crewPairList[cp].hasFlownFirst == 1 && crewPairList[cp].optAircraftID == crewPairList[cp].aircraftID[0]){ 
				if(newCrewArc->blockTmAtEPU[0] > 0)
					preFltTm = 0;
				else
					//preFltTm = optParam.preFlightTm;
					preFltTm = acTypeList[j].preFlightTm; //07/17/2017 ANG
			}
			else
				preFltTm = optParam.firstPreFltTm;

			if(preFltTm > 0 && newCrewArc->dutyTmAtEPU[0] > 0) //Added 10/25/10 ANG
				preFltTm = max (0,  preFltTm - newCrewArc->dutyTmAtEPU[0]);

			//RLZ 04/30/2008 Save preFlightTm is crew is idle. This step, I think, is done by adjusting availDT.

			//NOW CONSIDER AVAILABLE TIME OF PLANE
			//earlyPickupTm is not delayed until acAvailTm here, because we still need to add preFlight time, and we consider
			//acAvailTm in pickup arcs and plane arcs

			//RLZ earlyPickupTm need to be delayed, preFlight time is not added for all cases. 05/16/08

			//if pilot has duty hours > 0 and can rest BEFORE plane is available, put them to rest
			//conservatively add postFlightTm for pilots who have flown
			//if(newCrewArc->dutyTmAtEPU[y] > 0 && (newCrewArc->earlyPickupTm[y]+ (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0) + optParam.minRestTm + preFltTm) < (int)acAvailTm/60){ FATIGUE - 02/05/10 ANG
			if(newCrewArc->dutyTmAtEPU[y] > 0 && (newCrewArc->earlyPickupTm[y]+ (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0) + optParam.minRestTm + preFltTm) < (int)acAvailTm/60){
				//newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0) + optParam.minRestTm; //will add preFltTm later
				newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0) + optParam.minRestTm; //FATIGUE - 02/05/10 ANG
				newCrewArc->dutyTmAtEPU[y] = 0;
				newCrewArc->blockTmAtEPU[y] = 0;
				newCrewArc->canStrtLtrAtEPU[y] = 1;
			}
			//if pilot will be out of duty before this particular plane could be flown for a 30 minute flight, and can NOT start duty later, put to rest
			//note:  we know pilot can't finish rest before plane is available from above
			//else if(newCrewArc->canStrtLtrAtEPU[y] == 0 && ((newCrewArc->earlyPickupTm[y] + preFltTm <(int)acAvailTm/60? ((int)acAvailTm/60 - newCrewArc->earlyPickupTm[y]) : preFltTm)+ 30 + optParam.postFlightTm + newCrewArc->dutyTmAtEPU[y]) > optParam.maxDutyTm){ FATIGUE - 02/05/10 ANG
			else if(newCrewArc->canStrtLtrAtEPU[y] == 0 && ((newCrewArc->earlyPickupTm[y] + preFltTm <(int)acAvailTm/60? ((int)acAvailTm/60 - newCrewArc->earlyPickupTm[y]) : preFltTm)+ 30 + postFlightTm + newCrewArc->dutyTmAtEPU[y]) > optParam.maxDutyTm){
				//newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? optParam.postFlightTm : 0)+ optParam.minRestTm;
				newCrewArc->earlyPickupTm[y] += (newCrewArc->blockTmAtEPU[y] > 0? postFlightTm : 0)+ optParam.minRestTm; //FATIGUE - 02/05/10 ANG
				newCrewArc->dutyTmAtEPU[y] = 0;
				newCrewArc->blockTmAtEPU[y] = 0;
				newCrewArc->canStrtLtrAtEPU[y] = 1;
			}

			//if picking up at start of trip, check if pilot can get to plane before latest trip start
			if(puStartDemandInd > -1){
				if(newCrewArc->earlyPickupTm[y]+ preFltTm > demandList[puStartDemandInd].late[j])
					return -1;  //can't get to trip on time
			}

			//if pilot is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > 0 AND 
			//canStrtLtr == 1 AND earlyPickup is before earliest trip start, THEN we know that they 
			//must travel that day but could possibly travel later in the day, so check LATEST commercial flight.
			if(puStartDemandInd > -1){
				if(crewList[crListInd[y]].availAirportID != pickUpAptID && 
					newCrewArc->dutyTmAtEPU[y]>0 && newCrewArc->canStrtLtrAtEPU[y] == 1 && 
					(newCrewArc->earlyPickupTm[y] + preFltTm)< demandList[puStartDemandInd].early[j]){
					//we know that at least one itinerary is available from above, so no need to check for return of -1
					getCrewTravelDataLate(earlyDpt, 60*(demandList[puStartDemandInd].early[j] - preFltTm),
						crewList[crListInd[y]].availAirportID, pickUpAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag);
					newCrewArc->earlyPickupTm[y] = (int)arrivalTm/60;
					newCrewArc->dutyTmAtEPU[y] = (int)(arrivalTm - dutyStartTm)/60;
				}
			}
		} //end for(y..
	}//end else - yMax = 1

	//Adjust preFltTm here, as we may have put crew to rest above
	if(crewPairList[cp].hasFlownFirst == 1 && crewPairList[cp].optAircraftID == crewPairList[cp].aircraftID[0]){ 
		if(newCrewArc->blockTmAtEPU[0] > 0)
			preFltTm = 0;
		else
			//preFltTm = optParam.preFlightTm;
			preFltTm = acTypeList[j].preFlightTm; //07/17/2017 ANG
	}
	else
		preFltTm = optParam.firstPreFltTm;

	if(preFltTm > 0 && newCrewArc->dutyTmAtEPU[0] > 0) //Added 10/25/10 ANG
		preFltTm = max (0,  preFltTm - newCrewArc->dutyTmAtEPU[0]);

	//if pilots are together, double the cost for the captain from above and populate newCrewArc fields to match captain, 
	//adding in preflight time as required
	if(crewPairList[cp].availAptID > 0){//this field is only populated if crewPair is already together at start of window
	//if(crewPairList[cp].availAptID > 0 && crewList[crewPairList[cp].crewListInd[0]].day == crewList[crewPairList[cp].crewListInd[1]].day){//replaced the above code - FATIGUE - 02/05/10 ANG
		newCrewArc->cost *=2;
		newCrewArc->earlyPickupTm[0]+= preFltTm;	
		newCrewArc->dutyTmAtEPU[0] += preFltTm;
		for(y = 1; y<=2; y++){
			newCrewArc->blockTmAtEPU[y] = newCrewArc->blockTmAtEPU[0];
			newCrewArc->earlyPickupTm[y] = newCrewArc->earlyPickupTm[0];
			//newCrewArc->dutyTmAtEPU[y] = newCrewArc->dutyTmAtEPU[0];  //RLZ 06/23 Duty time can not different
			if ( y == 1)
				newCrewArc->dutyTmAtEPU[1] = crewList[crewPairList[cp].crewListInd[1]].dutyTime;
			else
				newCrewArc->dutyTmAtEPU[2] = max(newCrewArc->dutyTmAtEPU[0], newCrewArc->dutyTmAtEPU[1]);
			
			newCrewArc->canStrtLtrAtEPU[y] = newCrewArc->canStrtLtrAtEPU[0];
		}
	}//end if pilots are together..
	else {//Pilots aren't together or have different hours
		//if captain has duty hours > 0 and can rest BEFORE FO can pick up plane, put captain to rest.  
		//Add preFlight time. Set values for pair to worst case (FO)
		//if(newCrewArc->dutyTmAtEPU[0] > 0 && (newCrewArc->earlyPickupTm[0]+ (newCrewArc->blockTmAtEPU[0] > 0? optParam.postFlightTm : 0) + optParam.minRestTm) < newCrewArc->earlyPickupTm[1]){ FATIGUE - 02/05/10 ANG
		if(newCrewArc->dutyTmAtEPU[0] > 0 && (newCrewArc->earlyPickupTm[0]+ (newCrewArc->blockTmAtEPU[0] > 0? postFlightTm : 0) + optParam.minRestTm) < newCrewArc->earlyPickupTm[1]){
			newCrewArc->earlyPickupTm[1] += preFltTm;
			newCrewArc->dutyTmAtEPU[1] += preFltTm;
			newCrewArc->earlyPickupTm[0] = newCrewArc->earlyPickupTm[1];
			newCrewArc->dutyTmAtEPU[0] = preFltTm;
			newCrewArc->blockTmAtEPU[0] = 0;
			newCrewArc->canStrtLtrAtEPU[0] = 1;
			newCrewArc->blockTmAtEPU[2] = newCrewArc->blockTmAtEPU[1];
			newCrewArc->earlyPickupTm[2] = newCrewArc->earlyPickupTm[1];
			newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[1];
			newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[1];
		}
		//else if FO has duty hours > 0 and can rest BEFORE captain can pick up plane, put FO to rest.  
		//Add preFlight time.  Set values for pair to worst case (captain)
		//else if(newCrewArc->dutyTmAtEPU[1] > 0 && (newCrewArc->earlyPickupTm[1]+ (newCrewArc->blockTmAtEPU[1] > 0? optParam.postFlightTm : 0) + optParam.minRestTm) < newCrewArc->earlyPickupTm[0]){ FATIGUE - 02/05/10 ANG
		else if(newCrewArc->dutyTmAtEPU[1] > 0 && (newCrewArc->earlyPickupTm[1]+ (newCrewArc->blockTmAtEPU[1] > 0? postFlightTm : 0) + optParam.minRestTm) < newCrewArc->earlyPickupTm[0]){
			newCrewArc->earlyPickupTm[0] += preFltTm;
			newCrewArc->dutyTmAtEPU[0] += preFltTm;
			newCrewArc->earlyPickupTm[1] = newCrewArc->earlyPickupTm[0];
			newCrewArc->dutyTmAtEPU[1] = preFltTm;
			newCrewArc->blockTmAtEPU[1] = 0;
			newCrewArc->canStrtLtrAtEPU[1] = 1;
			newCrewArc->blockTmAtEPU[2] = newCrewArc->blockTmAtEPU[0];
			newCrewArc->earlyPickupTm[2] = newCrewArc->earlyPickupTm[0];
			newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[0];
			newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[0];
		}
		else{//neither pilot can rest before other is available. Add preFlight time. 
			newCrewArc->earlyPickupTm[0]+= preFltTm;
			newCrewArc->dutyTmAtEPU[0] += preFltTm;
			newCrewArc->earlyPickupTm[1]+= preFltTm;
			newCrewArc->dutyTmAtEPU[1] += preFltTm;
			//Set values for pair to worse case - latest earlyPickup, greatest block hours, greatest duty hours at (max) early pickup, 
			//and canStartLater based on greatest duty hours at (max) early pickup
			newCrewArc->earlyPickupTm[2] = (newCrewArc->earlyPickupTm[0] > newCrewArc->earlyPickupTm[1]? newCrewArc->earlyPickupTm[0]: newCrewArc->earlyPickupTm[1]);
			newCrewArc->blockTmAtEPU[2] = (newCrewArc->blockTmAtEPU[0] > newCrewArc->blockTmAtEPU[1]? newCrewArc->blockTmAtEPU[0]: newCrewArc->blockTmAtEPU[1]);
			//if pilot can pickup plane later than flight officer...
			if(newCrewArc->earlyPickupTm[0]> newCrewArc->earlyPickupTm[1]){
				//if flight officer has rested since traveling (or is just starting duty), so that he can just wait to pickup plane without increasing duty hours
				//then duty hours for pilot govern
				if(newCrewArc->canStrtLtrAtEPU[1] == 1 && newCrewArc->dutyTmAtEPU[1]<=optParam.firstPreFltTm){
					newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[0];
					newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[0];
				}
				else{
					if(newCrewArc->dutyTmAtEPU[1] + newCrewArc->earlyPickupTm[0] - newCrewArc->earlyPickupTm[1]> newCrewArc->dutyTmAtEPU[0]){
						newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[1] + newCrewArc->earlyPickupTm[0] - newCrewArc->earlyPickupTm[1];
						newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[1];
					}
					else{
						newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[0];
						newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[0];
					}
				}
			}
			else {//newCrewArc->earlyPickupTm[1] >= newCrewArc->earlyPickupTmAtEPU[0]  flight officer can pickup plane later than pilot
				//if pilot has rested since traveling (or is just starting duty), so that he can just wait to pickup plane without increasing duty hours
				//then duty hours for flight officer govern
				if(newCrewArc->canStrtLtrAtEPU[0] == 1 && newCrewArc->dutyTmAtEPU[0]<=optParam.firstPreFltTm){
					newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[1];
					newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[1];
				}
				else{
					if(newCrewArc->dutyTmAtEPU[0] + newCrewArc->earlyPickupTm[1] - newCrewArc->earlyPickupTm[0]> newCrewArc->dutyTmAtEPU[1]){
						newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[0] + newCrewArc->earlyPickupTm[1] - newCrewArc->earlyPickupTm[0];
						newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[0];
					}
					else{
						newCrewArc->dutyTmAtEPU[2] = newCrewArc->dutyTmAtEPU[1];
						newCrewArc->canStrtLtrAtEPU[2] = newCrewArc->canStrtLtrAtEPU[1];
					}
				}
			}	
		}
	} //end else if (pilots aren't together)
 	return 0;
}

/************************************************************************************
*	Function   checkReachableFromPrevIncl     Date last modified:  02/26/07 SWO		*
*	Purpose:	If a plane has inclusions (required trips) prior to a particular	*
*	day, check if plane can reach duty Nodes after covering previous inclusions		*
*	and set unreachableFlag as required.											*
************************************************************************************/
static void checkReachableFromPrevIncl(int acInd, Demand *prevIncl, int endAptID, int j, Duty *endDuty, int unreachableIndex)
//Note: modify this function - flexOS - 02/01/11 ANG
{
	int prevRepoFltTm = 0, prevRepoElpsTm=0, prevRepoBlkTm = 0,prevRepoStops = 0;

	if(prevIncl->inAirportID != endAptID){	
		getFlightTime(prevIncl->inAirportID, endAptID, acTypeList[j].aircraftTypeID, month, 0, &prevRepoFltTm, &prevRepoElpsTm, &prevRepoBlkTm,&prevRepoStops);
		if(getRepoArriveTm(prevIncl->inAirportID, endAptID,((int)(prevIncl->reqOut/60) + prevIncl->elapsedTm[j] + prevIncl->turnTime), prevRepoElpsTm)+ optParam.turnTime > endDuty->startTm[0])
			endDuty->unreachableFlag[unreachableIndex]=1;//flag as unreachable	
	}
	else if(((int)(prevIncl->reqOut/60) + prevIncl->elapsedTm[j] + prevIncl->turnTime) > endDuty->startTm[0])
		endDuty->unreachableFlag[unreachableIndex]=1;//flag as unreachable
	
	return;
}

/************************************************************************************
*	Function   checkCanReachFutureIncl		Date last modified:  2/26/07 SWO		*
*	Purpose:	If a plane has inclusions on a subsequent day, check if plane		*
*	can reach next inclusion after covering this duty node.  Set unreachable		*
*	flag for node to 1 if it can't and return -1 if it can't.						*
************************************************************************************/
static int checkCanReachFutureIncl(int acInd, Demand *nextIncl, int j, Duty *endDuty, int unreachableIndex)
//Note: modify this function - flexOS - 02/01/11 ANG
{
	int nextRepoFltTm = 0, nextRepoElpsTm=0, nextRepoBlkTm = 0, nextRepoStops = 0;
	int startAptID, startRepoTm;

	//determine last known time and place of aircraft after covering the endDuty 
	//(plus the next-day trip in the case of a final repo)
	if(endDuty->repoDemandInd == -1){
		startAptID = demandList[endDuty->lastDemInd].inAirportID;
		startRepoTm = endDuty->endTm + demandList[endDuty->lastDemInd].turnTime;
	}
	else{
		if( demandList[endDuty->repoDemandInd].demandID == nextIncl->demandID) //RLZ 05/02/2008 already cover the future nextIncl
			return 0;

		startAptID = demandList[endDuty->repoDemandInd].inAirportID;
		startRepoTm = demandList[endDuty->repoDemandInd].early[j]+ demandList[endDuty->repoDemandInd].elapsedTm[j] + demandList[endDuty->repoDemandInd].turnTime;
		//RLZ IF endDuty->repoDemandInd is a regular demand
	}
	if(nextIncl->outAirportID != startAptID){	
		getFlightTime(startAptID, nextIncl->outAirportID, acTypeList[j].aircraftTypeID, month, 0, &nextRepoFltTm, &nextRepoElpsTm, &nextRepoBlkTm,&nextRepoStops);
		if((getRepoArriveTm(startAptID, nextIncl->outAirportID, startRepoTm, nextRepoElpsTm) + optParam.turnTime) > (int)(nextIncl->reqOut/60)){//start time of inclusion is fixed
			endDuty->unreachableFlag[unreachableIndex]=1;//flag as unreachable	
			return -1;
		}
	}
	else{	
		if(startRepoTm > (int)(nextIncl->reqOut/60)){
			endDuty->unreachableFlag[unreachableIndex]=1;//flag as unreachable	
			return -1;
		}
	}
	
	return 0;
}

/********************************************************************************
*	Function   arcAlloc					 Date last modified:	07/09/06 SWO	*
*	Purpose:  	dynamically allocate memory for a single Network Arc list		*
********************************************************************************/
static NetworkArc *
arcAlloc(NetworkArc **arcList, int *arcCount)
{
	NetworkArc *aPtr;

	if(!(*arcList)) {
		// nothing has been allocated yet
		(*arcList) = (NetworkArc *) calloc(ArcAllocChunk, sizeof(NetworkArc));
		if(!(*arcList)) {
			logMsg(logFile,"%s Line %d, Out of Memory in arcAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		aPtr = (*arcList);
		(*arcCount)++;
		return(aPtr);
	}
	if((!(*(arcCount) % ArcAllocChunk))) {
		// time to realloc
		(*arcList) = (NetworkArc *) realloc((*arcList),
			(*arcCount * sizeof(NetworkArc)) + (ArcAllocChunk * sizeof(NetworkArc)));
		if(!(*arcList)) {
			logMsg(logFile,"%s Line %d, Out of Memory in arcAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	// return the next pre-allocated NetworkArc
	aPtr = (*arcList);
	aPtr += (*arcCount);
	(*arcCount)++;
	memset(aPtr,'\0',sizeof(NetworkArc));
	return(aPtr);
}

/********************************************************************************
*	Function   arcArrayAlloc	          Date last modified:	04/04/06 SWO	*
*	Purpose:  	dynamically allocate memory for an array of Network Arc lists	*
********************************************************************************/
static NetworkArc *
arcArrayAlloc(NetworkArc **arcList, int arcListInd)
{
	NetworkArc *aPtr;

	 if(! *(arcList + arcListInd)) {
		// nothing has been allocated yet
		*(arcList + arcListInd) = (NetworkArc *) calloc(ArcAllocChunk, sizeof(NetworkArc));
		if(!(*(arcList + arcListInd))) {
			logMsg(logFile,"%s Line %d, Out of Memory in arcArrayAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		aPtr = *(arcList + arcListInd);
		(*(countsPerArcList + arcListInd))++;
		return(aPtr);
	}
	if(!(*(countsPerArcList + arcListInd) % ArcAllocChunk)) {
		// time to realloc
		*(arcList + arcListInd) = (NetworkArc *) realloc(*(arcList + arcListInd),
			((*(countsPerArcList + arcListInd) * sizeof(NetworkArc)) + (ArcAllocChunk * sizeof(NetworkArc))));
		if(! *(arcList + arcListInd)) {
			logMsg(logFile,"%s Line %d, Out of Memory in arcArrayAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	// return the next pre-allocated NetworkArc
	aPtr = *(arcList + arcListInd);
	aPtr += *(countsPerArcList + arcListInd);
	(*(countsPerArcList + arcListInd))++;
	if((*(countsPerArcList + arcListInd))>maxArcAlloc)
		maxArcAlloc = (*(countsPerArcList + arcListInd));
	memset(aPtr,'\0',sizeof(NetworkArc));
	return(aPtr);
}

//DEBUG FUNCTION

/************************************************************************************************
*	Function	myDebugFunction								Date last modified:  05/08/06 ANG	*
*	Purpose:																					*
************************************************************************************************/

int
myDebugFunction ()
{
	//int cp, pl, arc, i;
	int day, j, k;

	fprintf(logFile, "Just for my debug: \n");

	/*for (cp = 0; cp < numOptCrewPairs; cp++){
		//crewPlaneList
		fprintf(logFile, "%d. crewPairID = %d, numPlaneArcs = %d:\n", cp, crewPairList[cp].crewPairID, crewPairList[cp].numPlaneArcs);
		for (pl = 0; pl < crewPairList[cp].numPlaneArcs; pl++){
			if(crewPairList[cp].crewPlaneList[pl].numArcs > 0){
				fprintf(logFile, "    %d. To aircraftID %d, having %d arcLists:\n", pl, acList[crewPairList[cp].crewPlaneList[pl].acInd].aircraftID, crewPairList[cp].crewPlaneList[pl].numArcs);  
				for (arc = 0; arc < crewPairList[cp].crewPlaneList[pl].numArcs; arc++){
					fprintf(logFile, "        %d. To dutyList[%d][%d], containing demandIDs:", arc, crewPairList[cp].acTypeIndex, crewPairList[cp].crewPlaneList[pl].arcList[arc].destDutyInd);
					for(i = 0; i < 4; i++){
						if(dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPlaneList[pl].arcList[arc].destDutyInd].demandInd[i] != -1){
							fprintf(logFile, " %d ", demandList[dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPlaneList[pl].arcList[arc].destDutyInd].demandInd[i]].demandID);
						}
					}
					fprintf(logFile, "\n");
				}
			}
		}
		//crewPUSList
		fprintf(logFile, "%d. crewPairID = %d, numPUStartArcs = %d:\n", cp, crewPairList[cp].crewPairID, crewPairList[cp].numPUStartArcs);
		for (pl = 0; pl < crewPairList[cp].numPUStartArcs; pl++){
			if(crewPairList[cp].crewPUSList[pl]->numArcs > 0){
				fprintf(logFile, "    %d. To START of demandID %d using aircraftID %d, having %d arcLists:\n", pl, demandList[crewPairList[cp].crewPUSList[pl]->demandInd].demandID, acList[crewPairList[cp].crewPUSList[pl]->acInd].aircraftID, crewPairList[cp].crewPUSList[pl]->numArcs);  
				for (arc = 0; arc < crewPairList[cp].crewPUSList[pl]->numArcs; arc++){
					fprintf(logFile, "        %d. To dutyList[%d][%d], containing demandIDs:", arc, crewPairList[cp].acTypeIndex, crewPairList[cp].crewPUSList[pl]->arcList[arc].destDutyInd);
					for(i = 0; i < 4; i++){
						if(dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPUSList[pl]->arcList[arc].destDutyInd].demandInd[i] != -1){
							fprintf(logFile, " %d ", demandList[dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPUSList[pl]->arcList[arc].destDutyInd].demandInd[i]].demandID);
						}
					}
					fprintf(logFile, "\n");
				}
			}
		}
		//crewPUEList
		fprintf(logFile, "%d. crewPairID = %d, numPUEndArcs = %d:\n", cp, crewPairList[cp].crewPairID, crewPairList[cp].numPUEndArcs);
		for (pl = 0; pl < crewPairList[cp].numPUEndArcs; pl++){
			if(crewPairList[cp].crewPUEList[pl]->numArcs > 0){
				fprintf(logFile, "    %d. To END of demandID %d using aircraftID %d, having %d arcLists:\n", pl, demandList[crewPairList[cp].crewPUEList[pl]->demandInd].demandID, acList[crewPairList[cp].crewPUEList[pl]->acInd].aircraftID, crewPairList[cp].crewPUEList[pl]->numArcs);  
				for (arc = 0; arc < crewPairList[cp].crewPUEList[pl]->numArcs; arc++){
					fprintf(logFile, "        %d. To dutyList[%d][%d], containing demandIDs:", arc, crewPairList[cp].acTypeIndex, crewPairList[cp].crewPUEList[pl]->arcList[arc].destDutyInd);
					for(i = 0; i < 4; i++){
						if(dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPUEList[pl]->arcList[arc].destDutyInd].demandInd[i] != -1){
							fprintf(logFile, " %d ", demandList[dutyList[crewPairList[cp].acTypeIndex][crewPairList[cp].crewPUEList[pl]->arcList[arc].destDutyInd].demandInd[i]].demandID);
						}
					}
					fprintf(logFile, "\n");
				}
			}
		}
	}*/

	//dutyList printout
	fprintf(logFile, "\n \n dutyList print out\n");
	for(day = 0; day < optParam.planningWindowDuration; day++){
		for (j = 0; j < numAcTypes; j++){
			//j = crewPairList[cp].acTypeIndex;
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){
				if( demandList[dutyList[j][k].demandInd[0]].demandID == 180040 )
					fprintf(logFile, "FOUND IT!\n");
				//fprintf(logFile, "%d. dutyList[%d][%d] contains demandID: ", k, j, k);
				//for(i = 0; i < 4; i++){
				//	if(dutyList[j][k].demandInd[i] != -1){
				//		fprintf(logFile, " %d ", demandList[dutyList[j][k].demandInd[i]].demandID);
				//	}
				//}
				//fprintf(logFile, "\n");
			}
		}
	}
	

	return 0;
}


/********************************************************************************
*	Function   minutesPastMidnightCheck	          Date last modified:	05/20/08 RLZ	*
*	Purpose:   to check if a time is before or after cutoffMPM	*
********************************************************************************/
//static bool minutesPastMidnightCheck(time_t zuluTime, int aptID, int cutoffMPM){
//	return (minutesPastMidnight(time_t zuluTime, int aptID) < cutoffMPM);
//}


/************************************************************************************************************
*	Function	calculateArcsToFirstDuties2					Date last modified:  05/19/08 ANG				*
*	Purpose:	Create arcs to first duties for infeasible existing solutions (short preFlight, etc)		*
************************************************************************************************************/
static int calculateArcsToFirstDuties2(int cp, int j, CrewArc *crewArc, NetworkArc *pickupArc, int k, int puStartdemandInd, NetworkArc *newArc)
{
	double overtimeCost;
	int halfDaysOT, c, crewInd, i;
	time_t departTm, dutyStartTm, arrivalTm; 
	int endAptID;
	double minusOne;
	int arcFeasible[2];
	int dutyEndTime, pickupTime;
	int repoASAPStartTm;
	//int repoEndTm;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int t, t1; 
	extern ExgTour *exgTourList;
	extern time_t endOfToday;
	extern int maxTripsPerDuty;
	int created = 0;
	int day, day1, day2; //FATIGUE - 02/05/10 ANG

	minusOne = -1.0;
	arcFeasible[0] = 0;
	arcFeasible[1] = 0;
	overtimeCost = 0;
	repoASAPStartTm = -1;

	//logMsg(logFile,"entering calculateArcsToFirstDuties for cp=%d, j=%d, k=%d, crewArc->acInd=%d\n", cp, j, k, crewArc->acInd);

	if (optParam.runWithoutExgSol) return -1;//test 02/01/11 ANG

	//find location where crew must pick up the plane
	if(crewArc->demandInd == -1)//if picking up plane when next available
		endAptID = acList[crewArc->acInd].availAirportID;
	else if(puStartdemandInd > -1)//if picking up plane at start of trip
		endAptID = demandList[puStartdemandInd].outAirportID;
	else //else we are picking up plane at end of trip
		endAptID = demandList[crewArc->demandInd].inAirportID; 

	//calculate pickupTime for crew.  If no repo to duty, crewStartTm considers whether first leg is an appointment not requiring crew
	pickupTime = pickupArc->startTm;
	i = dutyList[j][k].demandInd[0];

	////RLZ 01082008 Temp fix
	//if (i <= -1 || i > 100000){	
	if (i <= -1 || i > 100000){
		//logMsg(logFile,"Warning: location mismatched.  %s Line %d .\n", __FILE__, __LINE__);
		return -1;	
	}

	if(endAptID == demandList[i].outAirportID)
		pickupTime = dutyList[j][k].crewStartTm;

	//START - FATIGUE - 02/05/10 ANG
	day = -1;
	day1 = crewList[crewPairList[cp].crewListInd[0]].day;
	day2 = crewList[crewPairList[cp].crewListInd[1]].day;
	if( pickupTime*60 < firstEndOfDay + 86400*0 &&
		pickupTime*60 >= firstEndOfDay + 86400*(0-1))
		day = 0;
	else if( pickupTime*60 < firstEndOfDay + 86400*1 &&
		pickupTime*60 >= firstEndOfDay + 86400*(1-1))
		day = 1;
	else if( pickupTime*60 < firstEndOfDay + 86400*2 &&
		pickupTime*60 >= firstEndOfDay + 86400*(2-1))
		day = 2;
	//END - FATIGUE - 02/05/10 ANG

	//if (dutyList[j][k].crewStartTm > dutyList[j][k].crewEndTm) //RLZ 05/02/2008 pure appt
	//	pickupTime = dutyList[j][k].crewStartTm;

	//calculate dutyEndTime for crew including post flight time (and considering if last demand leg is appointment and no final repo)
	dutyEndTime = dutyList[j][k].crewEndTm;  //RLZ CHECK 04152008

	//if (dutyList[j][k].crewEndTm < dutyList[j][k].crewStartTm) // RLZ a pure appoint node
	//	dutyEndTime = max(crewPairList[cp].availDT/60,(dutyList[j][k].crewEndTm - optParam.maxDutyTm)); 

	if(dutyList[j][k].startTm[0] > ((int)firstEndOfDay/60 + (crewPairList[cp].endRegDay - 1)*24*60)) //Assuming first duty in first day? RLZ
		dutyEndTime += optParam.finalPostFltTm;
	else {
		dutyEndTime += optParam.postFlightTm;

		//START - FATIGUE - 02/05/10 ANG
		//if this is first day of tour for any member of crewPair, add shortDutyHrDif (note that we add postFlightTm above already
		if ((day1 >= 0 && k >= dutyTally[j][day1][0].startInd && k <= dutyTally[j][day1][8].endInd && ifCrewFirstDayStart5to8AM(day1, cp, (time_t)(60*pickupTime), endAptID)) ||
			(day2 >= 0 && k >= dutyTally[j][day2][0].startInd && k <= dutyTally[j][day2][8].endInd && ifCrewFirstDayStart5to8AM(day2, cp, (time_t)(60*pickupTime), endAptID)) ){
			dutyEndTime += optParam.shortDutyHrDif;

			//if 1st day 5-8AM 12hr rule is not satisfied, don't continue, return -1 instead
			//if(dutyEndTime - crewPairList[cp].availDT/60 + max (crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime) > optParam.maxDutyTm ){ - Replaced by one row below - 10/18/10 ANG
			if(dutyEndTime - pickupTime + max(optParam.firstPreFltTm, max(crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime)) > optParam.maxDutyTm ){
				return -1;//CHECK: do we want to do this since this function is called to force-create arc based on exg solution - FATIGUE - 02/26/10 ANG
			}
		}	
		//END - FATIGUE - 02/05/10 ANG
	}

	//if crew can start later AND has no duty hours (other than preFlight) at EPU (this means they are coming from rest without notification or home and
	//don't need to travel to plane), then we can just delay duty start until
	//pickupArc start time, and arc is feasible

	//START - 05/16/08 ANG
	//Creating arc if the first trip on the duty is assigned in to the crewpair on specific aircraft in the existing solution 
	//That is, dutyList[j][k].demandInd[0] is assigned to crewPairList[cp] on acList[crewArc->acInd] in one of the exgTourList 
	//if(dutyList[j][k].demandInd[0] > -1 && crewPairList[cp].exgTrInd > -1 && crewArc->acInd == exgTourList[crewPairList[cp].exgTrInd].acInd){
	if(dutyList[j][k].demandInd[0] > -1 && crewPairList[cp].exgTrInd > -1 && crewArc->acInd == exgTourList[crewPairList[cp].exgTrInd].acInd
		&& pickupTime*60 >= crewPairList[cp].pairStartTm && dutyEndTime*60 <= crewPairList[cp].pairEndTm){ //Fixed crewpair start before tour start because of airport assignment before tour start - 06/16/10 ANG
		//Count total demands in the duty
		for(t1 = 0; t1 < maxTripsPerDuty; t1++){
			if (dutyList[j][k].demandInd[t1] == dutyList[j][k].lastDemInd)
				break;
		}

		for(t = 0; t < t1+1; t++){
			if (exgTourList[crewPairList[cp].exgTrInd].demandInd[t] == dutyList[j][k].demandInd[t])
				continue;
			else
				return -1;
		}

		if (exgTourList[crewPairList[cp].exgTrInd].demandInd[t] > -1 &&
			demandList[exgTourList[crewPairList[cp].exgTrInd].demandInd[t]].reqOut < endOfToday){
			return -1;
		}

		if(t == t1+1){
				newArc->blockTm = pickupArc->blockTm;
				newArc->destDutyInd = k;
				newArc->startTm = pickupTime;
				newArc->cost = pickupArc->cost;
				if(optParam.withMac == 1){
					newArc->tempCostForMac = pickupArc->tempCostForMac;//MAC - 09/23/08 ANG
					newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
					newArc->macRepoFltTm = pickupArc->macRepoFltTm; 
					newArc->macRepoStop = pickupArc->macRepoStop; 
				}

				//fprintf(logFile, "Added arc for crewPairID %d, from aircraftID %d to dutyList[%d][%d] with first demandID %d.\n", crewPairList[cp].crewPairID, acList[crewArc->acInd].aircraftID, j, k, demandList[dutyList[j][k].demandInd[0]].demandID);
				created = 1;
		}
	}

	//if(dutyList[j][k].demandInd[0] > -1 && crewArc->acInd > -1){
		//for(t = 0; t < MAX_LEGS; t++){
		//	if (exgTourList[crewPairList[cp].exgTrInd].demandInd[t] == -1 && exgTourList[crewPairList[cp].exgTrInd].demandInd2[t] == -1)
		//		break;
		//	if (exgTourList[crewPairList[cp].exgTrInd].demandInd[t] == dutyList[j][k].demandInd[0] && 
		//		exgTourList[crewPairList[cp].exgTrInd].acInd == crewArc->acInd){
		//		//refine these values - ANG
		//		//arcFeasible[0] = 1;
		//		//arcFeasible[1] = 1;
		//		newArc->blockTm = pickupArc->blockTm;
		//		newArc->destDutyInd = k;
		//		newArc->startTm = pickupTime;
		//		newArc->cost = pickupArc->cost;
		//		//fprintf(logFile, "Added arc for crewPairID %d, from aircraftID %d to dutyList[%d][%d] with first demandID %d.\n", crewPairList[cp].crewPairID, acList[crewArc->acInd].aircraftID, j, k, demandList[dutyList[j][k].demandInd[0]].demandID);
		//		created = 1;
		//		//return 0;
		//	}
		//	else if (exgTourList[crewPairList[cp].exgTrInd].demandInd2[t] == dutyList[j][k].demandInd[0] &&
		//		exgTourList[crewPairList[cp].exgTrInd].acInd2 == crewArc->acInd){
		//		//refine these values - ANG
		//		//arcFeasible[0] = 1;
		//		//arcFeasible[1] = 1;
		//		newArc->blockTm = pickupArc->blockTm;
		//		newArc->destDutyInd = k;
		//		newArc->startTm = (repoASAPStartTm > -1? repoASAPStartTm: pickupTime);
		//		newArc->cost = pickupArc->cost;
		//		//fprintf(logFile, "Added arc for crewPairID %d, from aircraftID %d to dutyList[%d][%d] with first demandID %d.\n", crewPairList[cp].crewPairID, acList[crewArc->acInd].aircraftID, j, k, demandList[dutyList[j][k].demandInd[0]].demandID);
		//		created = 1;
		//		//return 0;
		//	}
		//}
	//}
	//END - 05/16/08 ANG

	if (created == 0)
		return -1;

	//if overtime calcs are irrelevant for both pilots AND arc is feasible from above, 
	//AND the arc has already been created, then we can just return here
	if(crewList[crewPairList[cp].crewListInd[0]].overtimeMatters == 0 && 
		crewList[crewPairList[cp].crewListInd[1]].overtimeMatters == 0){
		return 0;
	}

	//If the crew need not travel to the plane...
	if(crewList[crewPairList[cp].crewListInd[0]].availAirportID == endAptID && 
		crewList[crewPairList[cp].crewListInd[1]].availAirportID == endAptID){
			for(c = 0; c<2; c++){
				crewInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewInd].overtimeMatters == 1 && 60*pickupTime < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*pickupTime)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
	}

	//Otherwise, overtime is a consideration 
	//if pilots are together...  
	if(crewPairList[cp].availAptID > 0){//this field is only populated if crewPair is already together with the same hours at start of window)
		/*if crew is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > preFlight 
		AND canStrtLtr == 1 AND the duty doesn't start later than trip's earliest start, THEN we have already used the 
		latest commercial flight when generating the crew arc*/
		if(puStartdemandInd > -1){
			if(crewPairList[cp].availAptID != endAptID && crewArc->dutyTmAtEPU[2]>optParam.firstPreFltTm 
				&& crewArc->canStrtLtrAtEPU[2] == 1 && dutyList[j][k].startTm[0] == demandList[puStartdemandInd].early[j]){
					for(c = 0; c<2; c++){
						crewInd = crewPairList[cp].crewListInd[c];
						if(crewList[crewInd].overtimeMatters == 1 && 60*crewArc->earlyPickupTm[2] < crewList[crewInd].tourStartTm){
							halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*crewArc->earlyPickupTm[2])/(12*3600);//integer division truncates
							overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
						}
					}
			}
		}

		//can pilots travel to plane same day as duty (coming from rest or home/tour start)?
		if(!getCrewTravelDataLate(crewPairList[cp].availDT, 60*(pickupTime - optParam.firstPreFltTm), crewPairList[cp].availAptID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)
			&&(dutyEndTime - (int)dutyStartTm/60) < optParam.maxDutyTm){
			//then calculate overtime if necessary for each pilot based on this flight
			for(c = 0; c<2; c++){
				crewInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			arcFeasible[0] = 1;
			arcFeasible[1] = 1;

			if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
				//if 10hr rule is not satisfied, don't continue, return -1 instead
				if((dutyEndTime - (int)dutyStartTm/60) > optParam.shortDutyTm){
					arcFeasible[0] = 0;
					arcFeasible[1] = 0;
					return -1;
				}
			}
		}
		else{//else look at traveling to the plane as late as possible the day before the duty
			if(!getCrewTravelDataLate(crewPairList[cp].availDT, 60*(pickupTime - optParam.firstPreFltTm - optParam.minRestTm), crewPairList[cp].availAptID, 
				endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)){

				//calculate overtime if necessary for each pilot based on this flight
				for(c = 0; c<2; c++){
					crewInd = crewPairList[cp].crewListInd[c];
					if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
						halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
						overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
					}
				}
				arcFeasible[0] = 1;
				arcFeasible[1] = 1;

				if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if(dutyEndTime - (int)dutyStartTm/60 > optParam.shortDutyTm){
						arcFeasible[0] = 0;
						arcFeasible[1] = 0;
						return -1;
					}
				}
			}
		}
	}
									
	//Else, we must consider each pilot separately
	for(c = 0; c<2; c++){
		if(repoASAPStartTm > -1)
			arcFeasible[c] = 0; //arc is feasible above, but we must look at late start because of overtime
								//and we can't have one pilot start repo-only duty ASAP while the other starts at a later time, so assume we start later for both
								//finally, if overtime matters and arc was feasible if started repo ASAP, then arc should be feasible with a later start as well??
		crewInd = crewPairList[cp].crewListInd[c];
		/*if pilot is picking up at trip start AND has to travel to do so AND has duty hours at early pickup > preFlight 
		AND canStrtLtr == 1 AND the duty doesn't start later than trip's earliest start, THEN we have already used the 
		latest commercial flight for pilot when generating the crew arc*/
		if(puStartdemandInd > -1){
			if(crewList[crewInd].availAirportID != endAptID && crewArc->dutyTmAtEPU[c]>optParam.firstPreFltTm 
				&& crewArc->canStrtLtrAtEPU[c] == 1 && dutyList[j][k].startTm[0] == demandList[puStartdemandInd].early[j])
			{//if duty hours are no good, then return -1
					if(crewList[crewInd].overtimeMatters == 1 && 60*crewArc->earlyPickupTm[c] < crewList[crewInd].tourStartTm){
						halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*crewArc->earlyPickupTm[c])/(12*3600);//integer division truncates
						overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
					}
					arcFeasible[c] = 1;

					if(ifCrewStartBefore5AM(cp, (time_t)(crewArc->earlyPickupTm[c]-crewArc->dutyTmAtEPU[c]), crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
						//if 10hr rule is not satisfied, don't continue, return -1 instead
						if((crewArc->dutyTmAtEPU[c]+dutyEndTime-crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
							arcFeasible[c] = 0;
							return -1;
						}
					}

					continue;
			}
		}
		//if arc is feasible (from above) and overtime is not a consideration for this pilot, continue
		if(arcFeasible[c] == 1 && crewList[crewInd].overtimeMatters == 0)
			continue;
		//if arc is not feasible for pilot pair based on EPU from above, it may still be feasible for this pilot
		if(arcFeasible[c] == 0){ //(see all comments above for pilot pair)
			if(crewArc->canStrtLtrAtEPU[c] == 1 && crewArc->dutyTmAtEPU[c]<=optParam.firstPreFltTm){
				arcFeasible[c] = 1;

				if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if((crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			else if((crewArc->dutyTmAtEPU[c]+dutyEndTime-crewArc->earlyPickupTm[c])< optParam.maxDutyTm &&
				(crewArc->blockTmAtEPU[c]+ pickupArc->blockTm)< optParam.maxFlightTm){
				arcFeasible[c] = 1;

				//if (minutesPastMidnight(crewArc->earlyPickupTm[c]*60-crewArc->dutyTmAtEPU[c]*60, endAptID) <= optParam.cutoffForShortDuty && //Check short duty rule - 10/15/10 ANG
				//	(crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){ changed to below 11/11/10 ANG
				if (ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){ 
					if ((crewArc->dutyTmAtEPU[c] + dutyEndTime - crewArc->earlyPickupTm[c]) > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			else if((crewArc->earlyPickupTm[c]+ optParam.minRestTm) < pickupTime){
				arcFeasible[c] = 1;

				if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					//if(dutyEndTime - pickupTime + optParam.preFlightTm > optParam.shortDutyTm){ //note(!): preFlightTm vs firstPreFltTm
					if(dutyEndTime - pickupTime + acTypeList[j].preFlightTm > optParam.shortDutyTm){ // 07/17/2017 ANG
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
			//else if(crewArc->canStrtLtrAtEPU[c] == 0 && arcFeasible[c] == 0) - was not here before, copied from calculateArcsToFirstDuties() for sake of completeness
			//	return -1; //we won't ever hit this - would have returned -1 above
		}
		//if overtime calcs are irrelevant for this pilot AND duty is feasible assuming early pickup, 
		//then we can just continue on to next pilot
		if(crewList[crewInd].overtimeMatters == 0 && arcFeasible[c] ==1)
			continue;
		//if pilot need not travel... 
		if(crewList[crewInd].availAirportID == endAptID){
			//calc OT if necessary based on pickup start and continue
			if(crewList[crewInd].overtimeMatters == 1 && 60*pickupTime < crewList[crewInd].tourStartTm){
				halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - 60*pickupTime)/(12*3600);//integer division truncates
				overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
			}
			continue;
		}
		//Otherwise, either overtime is a consideration AND/OR the arc is not feasible based on early pickup for this pilot.
		//NOTE:  We know that pilot can start later than his available time(see above).  Therefore, pilot must be 
		//resting or at home prior to his avail time. We need to look at latest commercial flight for pilot.

		//can pilot travel to plane same day as duty (coming from rest or home/tour start)?
		if(!getCrewTravelDataLate(crewList[crewInd].availDT, 60*(pickupTime - optParam.firstPreFltTm), 
			crewList[crewInd].availAirportID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)
			&&(dutyEndTime - (int)dutyStartTm/60) < optParam.maxDutyTm){
			//then calculate overtime if necessary based on this flight
			if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			arcFeasible[c] = 1;

			if(ifCrewStartBefore5AM(cp, dutyStartTm, crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
				//if 10hr rule is not satisfied, don't continue, return -1 instead
				if((dutyEndTime - (int)dutyStartTm/60) > optParam.shortDutyTm){
					arcFeasible[c] = 0;
					return -1;
				}
			}
		}
		else{//else look at traveling to the plane as late as possible the day before the duty
			if(!getCrewTravelDataLate(crewList[crewInd].availDT, 60*(pickupTime - optParam.firstPreFltTm - optParam.minRestTm), 
				crewList[crewInd].availAirportID, endAptID, &departTm, &dutyStartTm, &arrivalTm, &minusOne, withOag)){
				//calculate overtime if necessary based on this flight
				if(crewList[crewInd].overtimeMatters == 1 && dutyStartTm < crewList[crewInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					overtimeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
				arcFeasible[c] = 1;

				if(ifCrewStartBefore5AM(cp, dutyStartTm, crewList[crewInd].availAirportID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
					//if 10hr rule is not satisfied, don't continue, return -1 instead
					if(dutyEndTime - (int)dutyStartTm/60 > optParam.shortDutyTm){
						arcFeasible[c] = 0;
						return -1;
					}
				}
			}
		}
	}

	if(ifCrewStartBefore5AM(cp, (time_t)(60*pickupTime), endAptID)){//Check short duty rule - FATIGUE - 10/12/10 ANG
		//if 10hr rule is not satisfied, don't continue, return -1 instead
		if(dutyEndTime - crewPairList[cp].availDT/60 + max (crewList[crewPairList[cp].crewListInd[0]].dutyTime, crewList[crewPairList[cp].crewListInd[1]].dutyTime) > optParam.shortDutyTm){
			return -1;
		}
	}

	//if we have gotten this far, arc is feasible and overtime matters
	newArc->startTm =  pickupTime;
	newArc->cost = pickupArc->cost + overtimeCost;
	if(optParam.withMac == 1){
		newArc->tempCostForMac = pickupArc->tempCostForMac + overtimeCost;//MAC - 09/23/08 ANG
		newArc->repoFromAptID = pickupArc->repoFromAptID; //MAC - 09/23/08 ANG
		newArc->macRepoFltTm = pickupArc->macRepoFltTm;
		newArc->macRepoStop = pickupArc->macRepoStop;
	}
	return 0;
}


/************************************************************************************************************
*	Function	ifCrewFirstDayStart5to8AM					Date last modified:  FATIGUE - 02/25/10 ANG		*
*	Purpose:	Check if this is crew 1st day of tour and crew starts start 5-8AM local time.				*
************************************************************************************************************/
static int 
ifCrewFirstDayStart5to8AM(int givenDay, int crewPairInd, time_t nextOutTime, int nextAptID)
{
	time_t estFirstDayStartTmGMT;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int estDutyTm; //est duty time burned before nextOutTime
	int mpm; //mintes past midnight
	int estStartAptID; //est start location for those with activityCode = 0
	int y, crewInd;
	time_t departTm = 0, dutyStartTm = 0, arrivalTm = 0; 
	double cost;

	//return 0 if shortDutyHrDiff equals zero
	if(optParam.shortDutyHrDif == 0)
		return 0;
	if(givenDay < 0 || !nextOutTime || nextAptID <= 0)
		return 0;

	for(y = 0; y < 2; y++){
		crewInd = crewPairList[crewPairInd].crewListInd[y];
		if(crewList[crewInd].day == givenDay){
			estFirstDayStartTmGMT = 0;
			estStartAptID = 0;
			estDutyTm = 0;
			if(crewList[crewInd].origActCode > 0){
				//crew is not yet started duty or in rest at the start of planning window
				//assume NO last activityLeg and NO need to consider last csTravelLeg

				//FIRST, get estFirstDayStartTmGMT
				//add preFlightTm or firstPreFltTm
				//estDutyTm = (crewList[crewInd].activityCode == 1) ? optParam.preFlightTm : optParam.firstPreFltTm;
				estDutyTm = (crewList[crewInd].activityCode == 1) ? acTypeList[crewList[crewInd].acTypeIndex].preFlightTm : optParam.firstPreFltTm; //07/17/2017 ANG
				//check if crew needs travel to nextAptID
				if (crewList[crewInd].availAirportID != nextAptID){
					//a travel is needed from availAptID to nextAptID (either by CS aircraft or comm airline)
					if(crewPairList[crewPairInd].availAptID >= 0){
						//crew is already together with another member of crewpair with an aircraft
						getFlightTime(crewList[crewInd].availAirportID, nextAptID, acTypeList[crewList[crewInd].acTypeIndex].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
						if(repoFltTm > 0){
							estDutyTm += repoFltTm + optParam.turnTime;
						}
						estFirstDayStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));
					}
					else {
						//else, consider crew travel alone by commerical airline
						//Call OAG function here
						if(!getCrewTravelDataLate(crewList[crewInd].availDT, nextOutTime - 60*estDutyTm, crewList[crewInd].availAirportID, nextAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
							estFirstDayStartTmGMT = dutyStartTm;
						else
							continue;
					}
				}
				else
					estFirstDayStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));

				//SECOND, get estStartAptID
				estStartAptID = crewList[crewInd].availAirportID;
			}
			else if (crewList[crewInd].origActCode == 0){
				//if crew cannot start later at the start of planning window at his first day
				//start at availAptID and have dutyTime information populated
				//FIRST, get estFirstDayStartTmGMT
				estFirstDayStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -crewList[crewInd].dutyTime, dt_time_tToDateTime(crewList[crewInd].availDT))); //double check if dutyTime includes preFlight - FATIGUE - 02/26/10 ANG

				//SECOND, get estStartAptID
				//4 possible cases:
				if (crewList[crewInd].lastActivityLeg_flag == 0 && !crewList[crewInd].lastCsTravelLeg)//no last activity leg information and no travel info
					estStartAptID = crewList[crewInd].availAirportID; 
				else if (crewList[crewInd].lastActivityLeg_flag == 0 && crewList[crewInd].lastCsTravelLeg) {//no last activity info, but there is travel info
					//check if travel relevant
					if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estFirstDayStartTmGMT &&
						DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ) //relevant travel
						estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
					else //irrelevant travel
						estStartAptID = crewList[crewInd].availAirportID; 
				}
				else if (crewList[crewInd].lastActivityLeg_flag == 1 && crewList[crewInd].lastCsTravelLeg){//here we have both last activity leg and travel leg
					//check if travel relevant
					if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estFirstDayStartTmGMT &&
						DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ){ //relevant travel
						//check which one is earlier
						if(crewList[crewInd].lastCsTravelLeg->travel_dptTm <= crewList[crewInd].firstLegOutTime)
							estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
						else
							estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
					}
					else //irrelevant travel
						estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
				}
				else //if (crewList[crewInd].lastActivityLeg_flag == 1 && !crewList[crewInd].lastCsTravelLeg)//here we have a last activity leg (could be MX or flight leg), but no travel info
					estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;

			}//end else if activityCode==0

			//check if crew starts between 5AM and 8AM local time
			mpm = minutesPastMidnight(estFirstDayStartTmGMT, estStartAptID);
			if (optParam.firstDayShortDutyStart <= mpm && mpm < optParam.firstDayShortDutyEnd)
				return 1;		

		}
	}

	return 0;
}


/************************************************************************************************************
*	Function	ifCrewStartBefore5AM					Date last modified:  FATIGUE - 10/12/10 ANG			*
*	Purpose:	Check if crew starts before 5AM local time.													*
************************************************************************************************************/
static int 
ifCrewStartBefore5AM(int crewPairInd, time_t nextOutTime, int nextAptID)
{
	time_t estStartTmGMT;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;
	int estDutyTm; //est duty time burned before nextOutTime
	int mpm; //minutes past midnight
	int estStartAptID; //est start location for those with activityCode = 0
	int y, crewInd;
	time_t departTm = 0, dutyStartTm = 0, arrivalTm = 0; 
	double cost;
	//Note: 5AM is represented as 'cutoffForShortDuty = 300'; 300 is minutes after midnight

	for(y = 0; y < 2; y++){
		crewInd = crewPairList[crewPairInd].crewListInd[y];
			estStartTmGMT = 0;
			estStartAptID = 0;
			estDutyTm = 0;
			if(crewList[crewInd].origActCode > 0){
				//crew is not yet started duty or in rest at the start of planning window
				//FIRST, get estStartTmGMT
				//add preFlightTm or firstPreFltTm
				//estDutyTm = (crewList[crewInd].activityCode <= 1) ? optParam.preFlightTm : optParam.firstPreFltTm;
				estDutyTm = (crewList[crewInd].activityCode <= 1) ? acTypeList[crewList[crewInd].acTypeIndex].preFlightTm : optParam.firstPreFltTm; //07/17/2017 ANG
				//check if crew needs travel to nextAptID
				if (crewList[crewInd].availAirportID != nextAptID){
					//a travel is needed from availAptID to nextAptID (either by CS aircraft or comm airline)
					if(crewPairList[crewPairInd].availAptID >= 0){
						//crew is already together with another member of crewpair with an aircraft
						getFlightTime(crewList[crewInd].availAirportID, nextAptID, acTypeList[crewList[crewInd].acTypeIndex].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
						if(repoFltTm > 0){
							estDutyTm += repoFltTm + optParam.turnTime;
						}
						estStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));
					}
					else {
						//else, consider crew travel alone by commerical airline
						//Call OAG function here
						if(!getCrewTravelDataLate(crewList[crewInd].availDT, nextOutTime - 60*estDutyTm, crewList[crewInd].availAirportID, nextAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
							estStartTmGMT = dutyStartTm;
						else
							continue;
					}
				}
				else
					estStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));

				//SECOND, get estStartAptID
				estStartAptID = crewList[crewInd].availAirportID;
			}
			else if (crewList[crewInd].origActCode == 0){
				//Check if there is a rest time between availDT and nextOutTime - FATIGUE - 02/07/11 ANG
				if (difftime(nextOutTime, crewList[crewInd].availDT) < optParam.minRestTm){ //if no rest time in between - FATIGUE - 02/07/11 ANG
					//if crew cannot start later at the start of planning window 
					//start at availAptID and have dutyTime information populated
					//FIRST, get estFirstDayStartTmGMT
					estStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -crewList[crewInd].dutyTime, dt_time_tToDateTime(crewList[crewInd].availDT)));

					//SECOND, get estStartAptID
					//4 possible cases:
					if (crewList[crewInd].lastActivityLeg_flag == 0 && !crewList[crewInd].lastCsTravelLeg)//no last activity leg information and no travel info
						estStartAptID = crewList[crewInd].availAirportID; 
					else if (crewList[crewInd].lastActivityLeg_flag == 0 && crewList[crewInd].lastCsTravelLeg) {//no last activity info, but there is travel info
						//check if travel relevant
						if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estStartTmGMT &&
							DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ) //relevant travel
							estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
						else //irrelevant travel
							estStartAptID = crewList[crewInd].availAirportID; 
					}
					else if (crewList[crewInd].lastActivityLeg_flag == 1 && crewList[crewInd].lastCsTravelLeg){//here we have both last activity leg and travel leg
						//check if travel relevant
						if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estStartTmGMT &&
							DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ){ //relevant travel
							//check which one is earlier
							if(crewList[crewInd].lastCsTravelLeg->travel_dptTm <= crewList[crewInd].firstLegOutTime)
								estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
							else
								estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
						}
						else //irrelevant travel
							estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
					}
					else //if (crewList[crewInd].lastActivityLeg_flag == 1 && !crewList[crewInd].lastCsTravelLeg)//here we have a last activity leg (could be MX or flight leg), but no travel info
						estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID; //Check this - 02/08/11 ANG
				}
				else{ //if there is rest time in between - FATIGUE - 02/07/11 ANG
					//FIRST, get estStartAptID
					//4 possible cases:
					if (crewList[crewInd].lastActivityLeg_flag == 0 && !crewList[crewInd].lastCsTravelLeg)//no last activity leg information and no travel info
						estStartAptID = crewList[crewInd].availAirportID; 
					else if (crewList[crewInd].lastActivityLeg_flag == 0 && crewList[crewInd].lastCsTravelLeg) {//no last activity info, but there is travel info
						//check if travel relevant
						if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estStartTmGMT &&
							DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ) //relevant travel
							estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
						else //irrelevant travel
							estStartAptID = crewList[crewInd].availAirportID; 
					}
					else if (crewList[crewInd].lastActivityLeg_flag == 1 && crewList[crewInd].lastCsTravelLeg){//here we have both last activity leg and travel leg
						//check if travel relevant
						if( DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) >= estStartTmGMT &&
							DateTimeToTime_t(crewList[crewInd].lastCsTravelLeg->travel_dptTm) <= nextOutTime ){ //relevant travel
							//check which one is earlier
							if(crewList[crewInd].lastCsTravelLeg->travel_dptTm <= crewList[crewInd].firstLegOutTime)
								estStartAptID = crewList[crewInd].lastCsTravelLeg->dpt_aptID; 
							else
								estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
						}
						else //irrelevant travel
							estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;
					}
					else //if (crewList[crewInd].lastActivityLeg_flag == 1 && !crewList[crewInd].lastCsTravelLeg)//here we have a last activity leg (could be MX or flight leg), but no travel info
						estStartAptID = (crewList[crewInd].firstOutAptID > 0) ? crewList[crewInd].firstOutAptID : crewList[crewInd].availAirportID;

					//SECOND, get estStartTmGMT
					//add preFlightTm or firstPreFltTm
					//estDutyTm = (crewList[crewInd].activityCode <= 1) ? optParam.preFlightTm : optParam.firstPreFltTm;
					estDutyTm = (crewList[crewInd].activityCode <= 1) ? acTypeList[crewList[crewInd].acTypeIndex].preFlightTm : optParam.firstPreFltTm; //07/17/2017 ANG
					//check if crew needs travel to nextAptID
					if (crewList[crewInd].availAirportID != estStartAptID){
						//a travel is needed from availAptID to estStartAptID (either by CS aircraft or comm airline)
						if(crewPairList[crewPairInd].availAptID >= 0){
							//crew is already together with another member of crewpair with an aircraft
							getFlightTime(crewList[crewInd].availAirportID, estStartAptID, acTypeList[crewList[crewInd].acTypeIndex].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
							if(repoFltTm > 0){
								estDutyTm += repoFltTm + optParam.turnTime;
							}
							estStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));
						}
						else {
							//else, consider crew travel alone by commerical airline
							//Call OAG function here
							if(!getCrewTravelDataLate(crewList[crewInd].availDT, nextOutTime - 60*estDutyTm, crewList[crewInd].availAirportID, estStartAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
								estStartTmGMT = dutyStartTm;
							else
								continue;
						}
					}
					else
						estStartTmGMT = DateTimeToTime_t(dt_addToDateTime(Minutes, -estDutyTm, dt_time_tToDateTime(nextOutTime)));
				}
			}//end else if activityCode==0

			//check if crew starts before 5AM local time
			mpm = minutesPastMidnight(estStartTmGMT, estStartAptID);
			if (mpm < optParam.cutoffForShortDuty)
				return 1;		

	}

	return 0;
}

/************************************************************************************
*	Function   checkReachableFromPrevInclFA    Date last modified:  03/14/11 fei FA	*
*	Purpose:	If a plane has inclusions (required trips) prior to a particular	*
*	day, check if plane can reach duty Nodes after covering previous inclusions		*
*	and set unreachableFlag as required.											*
************************************************************************************/
static void checkReachableFromPrevInclFA(int acInd, int demInd, int endAptID, int j, Duty *endDuty, int unreachableIndex)
{
	int prevRepoFltTm = 0, prevRepoElpsTm=0, prevRepoBlkTm = 0,prevRepoStops = 0;

	int i;
	Demand *newDemP;

	_ASSERTE( unreachableIndex >= 0 && !origDemInfos[demInd].skipIncl && origDemInfos[demInd].acInd == acInd && origDemInfos[demInd].numInd >= 1 );

	//fei Jan 2011
	//find a feasible copy
	//for(i=0; i < prevIncl->numInd; i ++)
	for(i=0; i < origDemInfos[demInd].numInd; i ++)
	{
		newDemP = &(demandList[origDemInfos[demInd].indices[i]]) ;

		if(newDemP->inAirportID != endAptID)
		{	
			getFlightTime(newDemP->inAirportID, endAptID, acTypeList[j].aircraftTypeID, month, 0, &prevRepoFltTm, &prevRepoElpsTm, &prevRepoBlkTm,&prevRepoStops);
			//if(getRepoArriveTm(newDemP->inAirportID, endAptID,((int)(newDemP->reqOut/60) + newDemP->elapsedTm[j] + newDemP->turnTime), prevRepoElpsTm)+ optParam.turnTime <= endDuty->startTm[0])
			if(getRepoArriveTm(newDemP->inAirportID, endAptID,((int)(newDemP->reqOut/60) - newDemP->earlyAdj + newDemP->elapsedTm[j] + newDemP->turnTime), prevRepoElpsTm)+ optParam.turnTime <= endDuty->startTm[0]) //fei FA
				break;
		//} else if(((int)(newDemP->reqOut/60) + newDemP->elapsedTm[j] + newDemP->turnTime) <= endDuty->startTm[0])
		} else if(((int)(newDemP->reqOut/60) - newDemP->earlyAdj + newDemP->elapsedTm[j] + newDemP->turnTime) <= endDuty->startTm[0]) //fei FA
			break;
	}

	if( i >= origDemInfos[demInd].numInd ) //no feasible copies
		endDuty->unreachableFlag[unreachableIndex]=1 ;

	return;
}

/************************************************************************************
*	Function   checkCanReachFutureInclFA		Date last modified: 03/14/11 fei FA	*
*	Purpose:	If a plane has inclusions on a subsequent day, check if plane		*
*	can reach next inclusion after covering this duty node.  Set unreachable		*
*	flag for node to 1 if it can't and return -1 if it can't.						*
************************************************************************************/
static int checkCanReachFutureInclFA(int acInd, int demInd, int j, Duty *endDuty, int unreachableIndex)
{
	int nextRepoFltTm = 0, nextRepoElpsTm=0, nextRepoBlkTm = 0, nextRepoStops = 0;
	int startAptID, startRepoTm;

	int i;
	Demand *newDemP;

	//determine last known time and place of aircraft after covering the endDuty 
	//(plus the next-day trip in the case of a final repo)
	if(endDuty->repoDemandInd == -1){
		startAptID = demandList[endDuty->lastDemInd].inAirportID;
		startRepoTm = endDuty->endTm + demandList[endDuty->lastDemInd].turnTime;
	}
	else{
		//if( demandList[endDuty->repoDemandInd].demandID == nextIncl->demandID) //RLZ 05/02/2008 already cover the future nextIncl
		if( demandList[endDuty->repoDemandInd].origDemInd == demInd ) //fei Jan 2011
			return 0;

		startAptID = demandList[endDuty->repoDemandInd].inAirportID;
		startRepoTm = demandList[endDuty->repoDemandInd].early[j]+ demandList[endDuty->repoDemandInd].elapsedTm[j] + demandList[endDuty->repoDemandInd].turnTime;
		//RLZ IF endDuty->repoDemandInd is a regular demand
	}

	//fei Jan 2011
	//find a feasible copy
	for(i=0; i < origDemInfos[demInd].numInd; i ++)
	{
		newDemP = &(demandList[origDemInfos[demInd].indices[i]]) ;

		if( newDemP->outAirportID != startAptID)
		{	
			getFlightTime(startAptID, newDemP->outAirportID, acTypeList[j].aircraftTypeID, month, 0, &nextRepoFltTm, &nextRepoElpsTm, &nextRepoBlkTm,&nextRepoStops);
			//if((getRepoArriveTm(startAptID, newDemP->outAirportID, startRepoTm, nextRepoElpsTm) + optParam.turnTime) <= (int)(newDemP->reqOut/60))//start time of inclusion is fixed
			if((getRepoArriveTm(startAptID, newDemP->outAirportID, startRepoTm, nextRepoElpsTm) + optParam.turnTime) <= (int)(newDemP->reqOut/60) + newDemP->lateAdj )//fei FA
				break ;
		} else
		{	
			//if(startRepoTm <= (int)(newDemP->reqOut/60))
			if(startRepoTm <= (int)(newDemP->reqOut/60) + newDemP->lateAdj)//fei FA
				break;
		}
	}

	if( i >= origDemInfos[demInd].numInd ) //no feasible copies found
	{
		endDuty->unreachableFlag[unreachableIndex]=1;//flag as unreachable	
		return -1;
	}

	return 0;
}
