#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "datetime.h"
#include "logMsg.h"
#include "airportLatLon.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_processInput.h"
#include "CSHOpt_dutyNodes.h"
#include "CSHOpt_define.h"
#include "CSHOpt_arcs.h"
#include "localTime.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_callOag.h"
#include "CSHOpt_tours.h"
#include "CSHOpt_output.h"

extern FILE *logFile;
extern Demand *demandList;
extern Duty **dutyList;
extern Aircraft *acList;
extern AircraftType *acTypeList;
extern CrewPair *crewPairList;
extern Crew *crewList;
extern Leg *legList;
extern struct optParameters optParam;
extern int **aptExcl;
extern int numOptCrewPairs;
extern int numMaintenanceRecord;
extern int numAircraft;
extern int numLegs;
extern int numOptDemand;
extern struct listMarker dutyTally[MAX_AC_TYPES][MAX_WINDOW_DURATION][9];
extern int lastTripOfDay[MAX_WINDOW_DURATION];
extern int month;
extern int firstEndOfDay;
extern int separateNWByFleet[MAX_AC_TYPES][MAX_PLANES_PER_FLEET];
extern int numSepNWByFleet[MAX_AC_TYPES];
extern int **aptCurf; //stores airport curfews (first (row) index is airport, first column is number of curfews, remaining cols are start and end of curfews
extern AcGroup *acGroupList;
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;
extern int withOag;
//extern int countAddlExgTours; // 03/07/08 ANG
//extern McpAircraft *mcpAircraftList; // 03/12/08 ANG
ShortTour **shortTourList;
int *tourCount;
Tour *tourList;

int numExgTours;
int num2ndTypeAddlExgTours; // 03/27/08 ANG
ExgTour *exgTourList;
int repoConnxnList[maxRepoConnxns];
int numRepoConnxns=0;

//FUNCTION DECLARATIONS
static int getShortestPathsToNodes(int crewPairInd, int acInd);
static int getShortestTours(int iteration);
static Tour *tourAlloc(Tour **tourList, int *tourCount);
static int checkFeasibilityOfExisting(void);

/****************************************************************************************************
*	Function   buildExistingTours							Date last modified:  08/23/07 SWO		*
*	Purpose:	create tours from existing solution to use in initial solution						*
****************************************************************************************************/
int buildExistingTours(void)
{ 
	int cp, x, y, z, lg, i, b, d, j, found, incl, numPax, acInd, firstAptID, lastAptID, prevLegInd, c, halfDaysOT, crewListInd;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	time_t firstTime, lastTime, firstDutyEnd, earlyDpt, lateArr, departTm, dutyStartTm, arrivalTm;
	int *crewPrIDPUAM; //array of crewPair IDs for crewPairs that pickup plane after maintenance or other airport appt
	int *demIndPUAM;  //array of demand indices corresponding to above maintenance or airport appts
	int numMaintPU = 0;
	double straightHmCst, getHomeCost, cost;
	time_t crewGetHomeTm[2];
	int countExgTour; // 04/23/08 ANG

	if((tourCount = (int *)calloc(1, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	(*tourCount) = 0;

	//allocate memory for list of existing tours
	//START - First, count the possible number of members of exgTourList - 04/23/08 ANG
	countExgTour = numOptCrewPairs;
	for(cp = 0; cp < numOptCrewPairs; cp++){
		if(crewPairList[cp].countAircraftID >= 2){
			countExgTour++;
		}
	}
	for (j = 0; j < numAircraft; j++){
		if(acList[j].countCrPrToday > 1){
			countExgTour++;
		}
	}
	//END - First, count the possible number of members of exgTourList - 04/23/08 ANG

	//if((exgTourList = (ExgTour *)calloc((numOptCrewPairs), sizeof(ExgTour))) == NULL) {
	//if((exgTourList = (ExgTour *)calloc((2*numOptCrewPairs+numAircraft), sizeof(ExgTour))) == NULL) {  // 03/17/08 ANG
	if((exgTourList = (ExgTour *)calloc((countExgTour), sizeof(ExgTour))) == NULL) {  // 04/23/08 ANG
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	numExgTours = 0;
	num2ndTypeAddlExgTours = 0; // 03/27/08 ANG

	//for (i=0; i<numOptCrewPairs; i++)
	for (i=0; i<countExgTour; i++) // 04/23/08 ANG
	{
		for (j=0; j<MAX_LEGS; j++)
		{
			exgTourList[i].demandInd[j] = -1;
			exgTourList[i].demandInd2[j] = -1; // 03/27/08 ANG
		}
		exgTourList[i].crewPairInd2 = -1; // 03/17/08 ANG
		exgTourList[i].acInd2 = -1; // 03/25/08 ANG
	}

	//allocate memory and initialize arrays to store info on pickups that occur after maintenance or airport appointments (see below for usage)
	if((crewPrIDPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((demIndPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(x = 0; x<numMaintenanceRecord; x++){
		crewPrIDPUAM[x] = 0;
		demIndPUAM[x] = -1;
	}
	numMaintPU = 0;

	for(cp = 0; cp< numOptCrewPairs; cp++){
		//if there is no existing (unlocked) tour for this crewPair, move to next crewPair
		x = 0;
		while(crewPairList[cp].schedLegIndList[x] > -1){
			if(legList[crewPairList[cp].schedLegIndList[x]].inLockedTour == 1)
				x++;
			else
				break;
		}
		if(crewPairList[cp].schedLegIndList[x]==-1)
			continue;
		//determine end of first duty day of existing tour
		y = x+1;
		while(crewPairList[cp].schedLegIndList[y] > -1){
			//if crewPair has time to rest between legs
			if((int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60)
				>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				break;
			}
			y++;
		}
		firstDutyEnd = legList[crewPairList[cp].schedLegIndList[y-1]].schedIn + optParam.postFlightTm*60;
		
		exgTourList[numExgTours].crewPairInd = cp;
		acInd = legList[crewPairList[cp].schedLegIndList[x]].acInd;
		firstAptID = legList[crewPairList[cp].schedLegIndList[x]].outAirportID;
		firstTime = legList[crewPairList[cp].schedLegIndList[x]].schedOut;
		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = crewPairList[cp].schedLegIndList[x] - 1; y >= 0; y--){
			if(legList[y].acInd == acInd){
				if(legList[y].inLockedTour == 1){
					//all previous legs for plane must be locked, so this crewPair picks up the plane when it is next avail
					exgTourList[numExgTours].pickupType = 1;
					exgTourList[numExgTours].pickupInd = -1;
					exgTourList[numExgTours].acInd = acInd;
				}
				else{ //this is NOT the first leg in an unlocked tour for this plane, so crewPair picks up plane after another crew's tour
					if(legList[y].demandID > 0){  //pickup at end of demand leg
						exgTourList[numExgTours].pickupType = 3;
						for(i = 0; i<numOptDemand; i++){
							if(legList[y].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}
					else if(legList[crewPairList[cp].schedLegIndList[x]].demandID > 0){ //pickup at start of demand leg
						exgTourList[numExgTours].pickupType = 2; 
						for(i = 0; i<numOptDemand; i++){
							if(legList[crewPairList[cp].schedLegIndList[x]].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}		
					else {  //pickup at end of repo leg
						exgTourList[numExgTours].pickupType = 4;
						exgTourList[numExgTours].pickupInd = y;
						repoConnxnList[numRepoConnxns] = y;
						numRepoConnxns ++;
					}
					//populate acInd for exgTour
					if(acList[acInd].specConnxnConstr[MAX_WINDOW_DURATION] > 0){
						if(acList[acInd].acGroupInd > 1)//if plane is part of an acGroup
							//for LP/MIP constraints, we are picking up a plane from this acGroup
							exgTourList[numExgTours].acInd = -acList[acInd].acGroupInd;
						else
							exgTourList[numExgTours].acInd = acInd;
					}
					else
						exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
				}
				break;
			}
		}
		if(exgTourList[numExgTours].pickupType == 0){
			//if we haven't yet set pickupType, we didn't find an earlier leg on that plane
			//which indicates crewPair picks up the plane when it is next avail
			exgTourList[numExgTours].pickupType = 1;
			exgTourList[numExgTours].pickupInd = -1;
			exgTourList[numExgTours].acInd = acInd;
		}
		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		d = 0;
		incl = 0;
		prevLegInd = -1;
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(crewPairList[cp].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd)
				break;
			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = crewPairList[cp].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				for (b=0; b<d; b++)
				{
					if (legList[lg].demandID == demandList[exgTourList[numExgTours].demandInd[b]].demandID)
						break;
				}	
				if (b == d)
				{
					for(i = 0; i<numOptDemand; i++){
						if(legList[lg].demandID == demandList[i].demandID){
							found = 1;
							exgTourList[numExgTours].demandInd[d] = i;
							numPax = demandList[i].numPax;
							d++;
							break;
						}
					}
				}
				//if we added a new demandID to tour, add early/late penalty cost for demand
				if(found == 1){
					if(legList[lg].schedOut > demandList[i].reqOut)
					{
						exgTourList[numExgTours].cost+= (optParam.lateCostPH *(legList[lg].schedOut - demandList[i].reqOut)/60)/60;
					}
					else
					{
						exgTourList[numExgTours].cost += (optParam.earlyCostPH * (demandList[i].reqOut - legList[lg].schedOut)/60)/60;
					}
				}
			}
			else
				numPax = 0;
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			//could directly use the orignal flight time. RLZ 03072008
			exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			
			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
				z = acList[acInd].inclDemandInd[incl];
				//if(demandList[z].reqOut >= legList[lg].schedOut)
				if( (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut) 
					//RLZ: Preserve infeasiblity.
					break;
				//if(prevLegInd > -1 && demandList[z].reqOut < legList[prevLegInd].schedIn){
				if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					//if(exgTourList[numExgTours].pickupType == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
					if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){

						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					if(exgTourList[numExgTours].pickupType != 1)
						break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				//if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut)+ demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					if(d == 0)
						exgTourList[numExgTours].demandInd[d] = z;
					else{
						exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
						exgTourList[numExgTours].demandInd[d-1]= z;
					}
					d++;
				}
				incl++;
			}
			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)

		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
					exgTourList[numExgTours].dropoffType = 3;
					exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
				}
				else if(legList[y].demandID > 0){ //drop off at start of demand leg
					exgTourList[numExgTours].dropoffType = 2;
					for(i = 0; i<numOptDemand; i++){
						if(legList[y].demandID == demandList[i].demandID){
							exgTourList[numExgTours].dropoffInd = i;
							break;
						}
					}
				}
				else{ //dropoff at end of repo leg (before another repo)
					exgTourList[numExgTours].dropoffType = 4;
					exgTourList[numExgTours].dropoffInd = crewPairList[cp].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
					z = acList[acInd].inclDemandInd[incl];
					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						break;
					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime){
						incl++;
						continue;
					}
					//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
					//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						exgTourList[numExgTours].dropoffType= 3;
						exgTourList[numExgTours].dropoffInd = z;
						crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
						demIndPUAM[numMaintPU] = z;
						numMaintPU++;
						//don't break here, because there may be more than one appoint leg between tours
					}
					incl++;
				} //end while(incl<=acList[
				break;
			} //end if(legList[y].acInd == acInd)
		} //end for(y = crewPairList[cp].schedLegIndList[x-1] 
		if(exgTourList[numExgTours].dropoffType == 0){
			//if we haven't yet set dropoffType, we didn't find a later leg on that plane in the existing solution, and we can only pickup plane again 
			//in the optimization if we leave it at the end of a demand leg OR an appointment leg, otherwise, assume we don't drop off.  We can assume 
			//plane is dropped if (1)crewPairList.endRegDay is within window, or if (2) optParam.prohibitStealingPlanes == 0, or if 
			//(3) crewPair has another leg in existing solution (on another plane)in the window, or if (4) plane is dropped at long maintenance/appointment

			//check if last leg for crewPair is a demandLeg AND (endRegDay for crewPair is within window OR 
			//optParam.prohibitStealingPlanes == 0 OR crewPair flies another leg on a different plane)
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && demandList[z].reqOut >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - demandList[z].reqOut)/60 > optParam.maintTmForReassign){

							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
						}
				}
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//Add cost of travel to plane and any start-early overtime for crew members
		//For each crew member...
		for(c = 0; c<2; c++){
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
					}
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)

				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
					exgTourList[numExgTours].cost += cost;
				}
			}
		}
		//add cost of getHome (including overtime) if endRegDay for crewPair is within planning window
		if(crewPairList[cp].endRegDay != PAST_WINDOW){
			getHomeCost = 0.0;
			straightHmCst = getStraightHomeCost(cp, -1, 0);
			if(straightHmCst > (INFINITY - 1))
			   straightHmCst = 5000.00;
			//calculate earliest departure and latest arrival time of a trip home
			earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else //if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					crewGetHomeTm[c] = earlyDpt;
				getHomeCost += cost;
				//add any overtime cost to get-home cost 
				//if crew member gets home on overtime, determine the number of overtime days  
				if(crewGetHomeTm[c] > crewList[crewListInd].tourEndTm){
					halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewListInd].tourEndTm)/(12*3600);//integer division truncates
					//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
					getHomeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}//end for(c = 
			//subtract cost of sending crew straight home
			getHomeCost -= straightHmCst;
			exgTourList[numExgTours].cost += getHomeCost;
		} //end if(crewPairList[cp].endRegDay
		numExgTours++;
	}  // end for(cp = 0..

	//update pickup information for crewPairs picking up after appointment/maintenance dropoff
	for(x = 0; x < numExgTours; x++){
		for(y = 0; y < numMaintPU; y++){
			if(crewPairList[exgTourList[x].crewPairInd].crewPairID == crewPrIDPUAM[y]){
				exgTourList[x].pickupType = 3;
				exgTourList[x].pickupInd = demIndPUAM[y];
				//don't break here because there may be more than one appointment/maintenance leg between tours
			}
		}
	}
	//CHECK FEASIBILITY OF EXG TOURS
	checkFeasibilityOfExisting();
	fflush (logFile);

	buildAddlExistingTours(); // build existing tour for 1 aircraft, 2 crewpairs (both crewpairs are only assigned to that aircraft) - 03/07/08 ANG
	buildAddlExistingTours2(); // build existing tour for a crewpair that was assigned to 2 aircraft (currently NO existing tour generated for the 2nd aircraft) - 03/07/08 ANG
	return 0;
}


/****************************************************************************************************
*	Function   checkFeasibilityOfExisting						Date last modified:  02/28/07 SWO	*
*	Purpose:	simplified (incomplete) feasibility check of existing tours to help debug and test	*
****************************************************************************************************/
int checkFeasibilityOfExisting(void)
{
	int y, cp, i, j, z, lg, acInd, depMPM, arrMPM, x;
	int crInd[2];
	int blockTm[2], dutyTm[2], actCode[2];
	time_t availDT[2];
	char writetodbstring1[200];

	for(cp = 0; cp<numOptCrewPairs; cp++){
		j = crewPairList[cp].acTypeIndex;
		if(crewPairList[cp].schedLegIndList[0] > -1 && crewPairList[cp].schedLegACIndList[0] > -1 && crewPairList[cp].lockTour[0]!= 1){
			//note that we can't check schedOut against acList[].availDT if tour was locked, as we have already updated acList[].availDT in that case
			if(legList[crewPairList[cp].schedLegIndList[0]].schedOut < acList[crewPairList[cp].schedLegACIndList[0]].availDT)
				fprintf(logFile,"\nAircraftID %d leaves %d minutes before it is available in the existing tour.\n", 
				acList[crewPairList[cp].schedLegACIndList[0]].aircraftID, 
				(int)((legList[crewPairList[cp].schedLegIndList[0]].schedOut - acList[crewPairList[cp].schedLegACIndList[0]].availDT)/60));
			    sprintf(writetodbstring1, "AircraftID %d leaves %d minutes before it is available in the existing tour.", 
				acList[crewPairList[cp].schedLegACIndList[0]].aircraftID, 
				(int)((legList[crewPairList[cp].schedLegIndList[0]].schedOut - acList[crewPairList[cp].schedLegACIndList[0]].availDT)/60));
			    if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
					   }
				  }
				else
			      {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting().\n", __FILE__,__LINE__);
		              writeWarningData(myconn); exit(1);
	                 }
				  }	
				   initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
				   errorinfoList[errorNumber].aircraftid=acList[crewPairList[cp].schedLegACIndList[0]].aircraftID;
				   errorinfoList[errorNumber].minutes=(int)((legList[crewPairList[cp].schedLegIndList[0]].schedOut - acList[crewPairList[cp].schedLegACIndList[0]].availDT)/60);
			      errorinfoList[errorNumber].format_number=12;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                   errorNumber++;
		}
		y = 1;
		while(crewPairList[cp].schedLegIndList[y]> -1 && crewPairList[cp].schedLegACIndList[y]>-1)
		{
			if((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn) < 60*optParam.turnTime
				&& demandList[legList[crewPairList[cp].schedLegIndList[y]].demandInd].predDemID == 0) //ensure that this is not really a fuel stop

				fprintf(logFile,"\nAircraftID %d has insufficient turn time (%d minutes) between legs %d and %d in existing tour.\n", 
				acList[crewPairList[cp].schedLegACIndList[y-1]].aircraftID, 
				(int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60),
				y-1, y);  //it is possible that these two legs are on different aircraft, but unlikely, and probably an issue anyway
			    sprintf(writetodbstring1, "AircraftID %d has insufficient turn time (%d minutes) between legs %d and %d in existing tour.", 
				acList[crewPairList[cp].schedLegACIndList[y-1]].aircraftID, 
				(int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60),
				y-1, y);
                if(errorNumber==0)
				  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		               {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting().\n", __FILE__,__LINE__);
		                writeWarningData(myconn); exit(1);
					   }
				  }
				else
			      {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting().\n", __FILE__,__LINE__);
		              writeWarningData(myconn); exit(1);
	                 }
				  }	 
				initializeWarningInfo(&errorinfoList[errorNumber]);
				errorinfoList[errorNumber].local_scenarioid = local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name, "group_aircraft");
				   errorinfoList[errorNumber].aircraftid=acList[crewPairList[cp].schedLegACIndList[y-1]].aircraftID;
                   errorinfoList[errorNumber].minutes=(int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60);
			       errorinfoList[errorNumber].format_number=13;
				   errorinfoList[errorNumber].leg1id=y-1;
				   errorinfoList[errorNumber].leg2id=y;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
			y++;	    
		}
		for(z = 0; z<2; z++){
			crInd[z] = crewPairList[cp].crewListInd[z];
			availDT[z] = crewList[crInd[z]].availDT;
			blockTm[z] = crewList[crInd[z]].blockTm;
			dutyTm[z] = crewList[crInd[z]].dutyTime;
			actCode[z] = crewList[crInd[z]].activityCode;
		}

		y = 0;
		while(crewPairList[cp].schedLegIndList[y]> -1 && crewPairList[cp].schedLegACIndList[y]>-1){
			lg = crewPairList[cp].schedLegIndList[y];
			acInd = crewPairList[cp].schedLegACIndList[y];
			i = legList[lg].demandInd;
			if(i  > -1){
				//flex time / vector legs
				if(legList[lg].outAirportID == demandList[i].outAirportID && ((legList[lg].schedOut - demandList[i].reqOut) > (demandList[i].lateAdj*60 + 60)) || 
					((legList[lg].schedOut - demandList[i].reqOut) < (-demandList[i].earlyAdj*60 - 60)))
				 {	fprintf(logFile,"\nDemandID %d on AircraftID %d has schedOut incompatible with reqOut.\n", demandList[i].demandID, acList[acInd].aircraftID);
				    sprintf(writetodbstring1, "DemandID %d on AircraftID %d has schedOut incompatible with reqOut.", demandList[i].demandID, acList[acInd].aircraftID);
					if(errorNumber==0)
						    {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
						       }
						    }
					else
						    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
	                           }
						    }
					initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_demand");
				   errorinfoList[errorNumber].demandid=demandList[i].demandID;
				   errorinfoList[errorNumber].aircraftid=acList[acInd].aircraftID;
			       errorinfoList[errorNumber].format_number=14;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
				 }
				//fleet - airport (runway) exclusions
				if(demandList[i].aircraftTypeID != acList[acInd].aircraftTypeID && aptExcl[demandList[i].inAirportID][acList[acInd].acTypeIndex] ==1)
				 {	fprintf(logFile,"\nAircraftID %d has a fleet-aircraft exclusion with airportID %d in existing tour.\n", acList[acInd].aircraftID, demandList[i].inAirportID);
				    sprintf(writetodbstring1, "AircraftID %d has a fleet-aircraft exclusion with airportID %d in existing tour.", acList[acInd].aircraftID, demandList[i].inAirportID);
                    if(errorNumber==0)
						    {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
						       }
						    }
					else
						    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
	                           }
						    }
					initializeWarningInfo(&errorinfoList[errorNumber]);
                   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
				   errorinfoList[errorNumber].aircraftid= acList[acInd].aircraftID;
				   errorinfoList[errorNumber].airportid=demandList[i].inAirportID;
			       errorinfoList[errorNumber].format_number=15;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
				 }
				if(demandList[i].aircraftTypeID != acList[acInd].aircraftTypeID && aptExcl[demandList[i].outAirportID][acList[acInd].acTypeIndex]==1)
				   {fprintf(logFile,"\nAircraftID %d has a fleet-aircraft exclusion with airportID %d in existing tour.\n", acList[acInd].aircraftID, demandList[i].outAirportID);
				    sprintf(writetodbstring1, "AircraftID %d has a fleet-aircraft exclusion with airportID %d in existing tour.", acList[acInd].aircraftID, demandList[i].outAirportID);
                    if(errorNumber==0)
						    {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
						       }
						    }
					else
						    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
	                           }
						    }
					initializeWarningInfo(&errorinfoList[errorNumber]);
                   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
				   errorinfoList[errorNumber].aircraftid= acList[acInd].aircraftID;
				   errorinfoList[errorNumber].airportid=demandList[i].outAirportID;
			       errorinfoList[errorNumber].format_number=15;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				   errorNumber++;
				 }
				if(acTypeList[j].sequencePosn < demandList[i].sequencePosn)
				{	fprintf(logFile,"\nAircraftID %d has too low a sequence position for demandID %d in existing tour.\n", acList[acInd].aircraftID, demandList[i].demandID);
				    sprintf(writetodbstring1, "AircraftID %d has too low a sequence position for demandID %d in existing tour.", acList[acInd].aircraftID, demandList[i].demandID);
                    if(errorNumber==0)
						    {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
						       }
						    }
					else
						    {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                       {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                        writeWarningData(myconn); exit(1);
	                           }
						    }
					initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
				   errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
				   errorinfoList[errorNumber].demandid=demandList[i].demandID;
			       errorinfoList[errorNumber].format_number=16;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                   errorNumber++;
				}
			}
			if(i == -1){ //check repositioning legs against airport curfews
				if(aptCurf[legList[lg].outAirportID][0] > 0){
					depMPM = minutesPastMidnight(legList[lg].schedOut, legList[lg].outAirportID);
					x=0;
					while(x < aptCurf[legList[lg].outAirportID][0]){
						//if start of curfew (in minutes past Midnight)is greater than end of curfew, start is < midnite and end is after
						if(aptCurf[legList[lg].outAirportID][2*x+1] > aptCurf[legList[lg].outAirportID][2*x+2])
						{
							if(depMPM > aptCurf[legList[lg].outAirportID][2*x+1])
							{
								fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].outAirportID);		
								sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].outAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].outAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
								break;
							}
							if(depMPM < aptCurf[legList[lg].outAirportID][2*x+2])
							{
								fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].outAirportID);	
								sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].outAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].outAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
								break;
							}
						}
						else if(depMPM > aptCurf[legList[lg].outAirportID][2*x+1] && depMPM < aptCurf[legList[lg].outAirportID][2*x+2])
						{  //curfew doesn't cross midnight
							fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].outAirportID);	
							sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].outAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].outAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
							break;
						}
					x++;
					}
				}
				if(aptCurf[legList[lg].inAirportID][0] > 0){
					arrMPM = minutesPastMidnight(legList[lg].schedIn, legList[lg].inAirportID);
					x=0;
					while(x < aptCurf[legList[lg].inAirportID][0]){
						//if start of curfew (in minutes past Midnight) is greater than end of curfew, start is < midnite and end is after
						if(aptCurf[legList[lg].inAirportID][2*x+1] > aptCurf[legList[lg].inAirportID][2*x+2])
						{
							if(arrMPM > aptCurf[legList[lg].inAirportID][2*x+1])
							{
								fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].inAirportID);	
								sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].inAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].inAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
								break;
							}
							if(arrMPM < aptCurf[legList[lg].inAirportID][2*x+2])
							{
								fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].inAirportID);
								sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].inAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].inAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
								break;
							}
						}
						else if(arrMPM > aptCurf[legList[lg].inAirportID][2*x+1] && arrMPM < aptCurf[legList[lg].inAirportID][2*x+2])
						{  //curfew doesn't cross midnight
							fprintf(logFile,"\nAircraftID %d violates curfew at airportID %d in existing tour.\n", acList[acInd].aircraftID, legList[lg].inAirportID);
							sprintf(writetodbstring1, "AircraftID %d violates curfew at airportID %d in existing tour.", acList[acInd].aircraftID, legList[lg].inAirportID);
                                if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					            else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
								initializeWarningInfo(&errorinfoList[errorNumber]);
								errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                                strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
								errorinfoList[errorNumber].aircraftid = acList[acInd].aircraftID;
								errorinfoList[errorNumber].airportid = legList[lg].inAirportID;
			                    errorinfoList[errorNumber].format_number=17;
                                strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                                errorNumber++;
							break;
						}
						x++;
					}
				}
			} //end if(i === -1){ //check repositioning...

			//block time and simplified check of duty time
			for(z = 0; z<2; z++){
				//check if crew member is resting or at home OR can rest before doing this leg
				if(actCode[z] > 0 ||(legList[lg].schedOut - 60*(blockTm[z] > 0? optParam.postFlightTm : 0) - 60*optParam.minRestTm
					- 60*optParam.preFlightTm) >= availDT[z]-60){ //one minute tolerance
						dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : optParam.preFlightTm);
					blockTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60);
				}
				//else update fields assuming no rest before leg
				else{
					dutyTm[z] += (int)((legList[lg].schedIn - availDT[z])/60);
					blockTm[z] += (int)((legList[lg].schedIn - legList[lg].schedOut)/60);
				}
				actCode[z] = 0;
				availDT[z] = legList[lg].schedIn;

				if (dutyTm[z] > optParam.maxDutyTm + optParam.minRestTm){ //There is a rest. Also assuming such bad cases should not exist in the existing tour
					dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : optParam.preFlightTm);
					blockTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60);
					availDT[z] += optParam.minRestTm*60;  
				}
				else {
					if(blockTm[z] > optParam.maxFlightTm)
					 {	fprintf(logFile,"\nBlock time for crewID %d (%d minutes) exceeds limit in existing tour.\n", crewList[crInd[z]].crewID, blockTm[z] - optParam.maxFlightTm);
					    sprintf(writetodbstring1, "Block time for crewID %d (%d minutes) exceeds limit in existing tour.", crewList[crInd[z]].crewID, blockTm[z] - optParam.maxFlightTm);
						if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					    else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
						initializeWarningInfo(&errorinfoList[errorNumber]);
				        errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                        strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				        errorinfoList[errorNumber].crewid=crewList[crInd[z]].crewID;
				        errorinfoList[errorNumber].minutes=blockTm[z] - optParam.maxFlightTm;
			            errorinfoList[errorNumber].format_number=18;
                        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
				        errorNumber++;
					}
					if(dutyTm[z] + optParam.postFlightTm > optParam.maxDutyTm)
					{ 	fprintf(logFile,"\nDuty time for crewID %d (%d minutes) exceeds limit in existing tour.\n", crewList[crInd[z]].crewID, dutyTm[z]+ optParam.postFlightTm - optParam.maxDutyTm);
					    sprintf(writetodbstring1, "Duty time for crewID %d (%d minutes) exceeds limit in existing tour.", crewList[crInd[z]].crewID, dutyTm[z]+ optParam.postFlightTm - optParam.maxDutyTm);
						if(errorNumber==0)
						          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
						             }
						          }
					    else
						          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		                             {logMsg(logFile,"%s Line %d, Out of Memory in checkFeasibilityOfExisting()().\n", __FILE__,__LINE__);
		                              writeWarningData(myconn); exit(1);
	                                 }
						          }
						initializeWarningInfo(&errorinfoList[errorNumber]);
						errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                        strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				        errorinfoList[errorNumber].crewid=crewList[crInd[z]].crewID;
				        errorinfoList[errorNumber].minutes=dutyTm[z]+ optParam.postFlightTm - optParam.maxDutyTm;
			            errorinfoList[errorNumber].format_number=19;
                        strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
						errorNumber++;
				    }
				}
			}
			y++;
		}	
	}
	return 0;
}



/****************************************************************************************************
*	Function   getNewColumns								Date last modified:  08/18/06 SWO		*
*	Purpose:	find shortest paths to each node, and shortest tours, for each crewPair				*
****************************************************************************************************/
int getNewColumns(int iteration, int *start, int *end)
{
	(*start) = (*tourCount);
	getShortestTours(iteration);
	(*end) = (*tourCount)-1;
	return 0;
}

/****************************************************************************************************
*	Function   getShortestPathsToNodes():					Date last modified:  03/13/07 SWO		*
*	Purpose:	find shortest paths to each node for each crewPair									*
****************************************************************************************************/
int getShortestPathsToNodes(int crewPairInd, int acInd)
{
	int cp, a, k, j, x, day, arcListInd, kStart, lastDay, p, dualInd;
	double tempRedCost, tempCost;
	double newRedCost;
	double changePen;

	cp = crewPairInd;

	j = crewPairList[cp].acTypeIndex;
	lastDay = (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));
	
	//initialize / reset dutyNodes
	for(k = crewPairList[cp].nodeStartIndex; k<=dutyTally[j][lastDay][8].endInd; k++)
		dutyList[j][k].spRedCost = INFINITY;

	//first pass through arc lists out of crewPair node:  plane, PUE, and PUS

	//pass through plane arcs out of crewPair node
	for(x = 0; x<crewPairList[cp].numPlaneArcs; x++){
		//if plane on arc is not part of the crew network we are currently considering (general fleet, 
		//OR specific aircraft or aircraft group with spec. connxn constraint), continue to next arc
		if(acInd == -1){ // we are considering general fleet network
			if(acList[crewPairList[cp].crewPlaneList[x].acInd].sepNW > 0)
				continue;
		}
		else if(acInd < -1){
			if(acList[crewPairList[cp].crewPlaneList[x].acInd].acGroupInd != -acInd)
				continue;
		}
		else if(acInd != crewPairList[cp].crewPlaneList[x].acInd)
			continue;
		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPlaneList[x].cost;
		tempRedCost = tempCost - crewPairList[cp].dual - acList[crewPairList[cp].crewPlaneList[x].acInd].dual;
		for(a = 0; a<crewPairList[cp].crewPlaneList[x].numArcs; a++){
			k = crewPairList[cp].crewPlaneList[x].arcList[a].destDutyInd;
			//update dutyNode if new path is shorter
			//arcs to first duties already include change penalties
			newRedCost = tempRedCost + crewPairList[cp].crewPlaneList[x].arcList[a].cost - dutyList[j][k].sumDuals;
			if(newRedCost < dutyList[j][k].spRedCost){
				dutyList[j][k].predType = 1;
				dutyList[j][k].predInd = x;
				dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPlaneList[x].arcList[a].cost;
				dutyList[j][k].spRedCost = newRedCost;
				dutyList[j][k].spACInd = crewPairList[cp].crewPlaneList[x].acInd;
			}
		}
	}
	//pass through pickup at start (PUS) arcs out of crewPair node
	for(x = 0; x<crewPairList[cp].numPUStartArcs; x++){
		//if plane on arc is not part of the crew network we are currently considering (general fleet 
		//or plane / plane group with spec. connxn constraint), continue to next arc
		p = crewPairList[cp].crewPUSList[x]->acInd;//p equals -1, -acGroupInd, or acInd and should match network
		if(p != acInd)
			continue;
		//find index of demand.puSDual for fleet or plane.  Note that although planes with special connection constraints MAY
		//pick up a trip AFTER the special connection constraints are required, we will use special connection constraints for the plane
		//for all trips (for simplicity in coding)
		if(p == -1) //we are considering fleet
			dualInd = crewPairList[cp].acTypeIndex;
		else if(p < -1) //we are considering a group of planes but may or may not need special connxn constr
			dualInd = acList[acGroupList[-p].acInd[0]].puTripListIndex;
		else //p >= 0, and we are considering specific plane that may or may not need special connxn constr
			dualInd = acList[p].puTripListIndex;
		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPUSList[x]->cost;
		tempRedCost = tempCost - crewPairList[cp].dual - demandList[crewPairList[cp].crewPUSList[x]->demandInd].puSDual[dualInd];
		for(a = 0; a<crewPairList[cp].crewPUSList[x]->numArcs; a++){
			k = crewPairList[cp].crewPUSList[x]->arcList[a].destDutyInd;
			 //update Node if new path is shorter
			//arcs to first duties already include change penalties
			newRedCost = tempRedCost + crewPairList[cp].crewPUSList[x]->arcList[a].cost - dutyList[j][k].sumDuals;
			if(newRedCost < dutyList[j][k].spRedCost){
				dutyList[j][k].predType = 2;
				dutyList[j][k].predInd = x;
				dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPUSList[x]->arcList[a].cost;
				dutyList[j][k].spRedCost = newRedCost;
				dutyList[j][k].spACInd = crewPairList[cp].crewPUSList[x]->acInd;
			}
		}
	}
	//pass through pickup at end (PUE) arcs out of crewPair node
	for(x = 0; x<crewPairList[cp].numPUEndArcs; x++){
		//if plane on arc is not part of the crew network we are currently considering (general fleet 
		//or plane / plane group with spec. connxn constraint), continue to next arc
		p = crewPairList[cp].crewPUEList[x]->acInd; //p equals -1, -acGroupInd, or acInd and should match network
		if(p != acInd)
			continue;
		//find index of demand.puEDual for fleet or plane.  Note that although planes with special connection constraints MAY
		//pick up a trip AFTER the special connection constraints are required, we will use special connection constraints for the plane
		//for all trips (for simplicity in coding)
		if(p == -1) //we are considering fleet
			dualInd = crewPairList[cp].acTypeIndex;
		else if(p < -1) //we are considering a group of planes
			dualInd = acList[acGroupList[-p].acInd[0]].puTripListIndex;
		else //p >= 0, and we are considering specific plane
			dualInd = acList[p].puTripListIndex;
		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPUEList[x]->cost;
		tempRedCost = tempCost - crewPairList[cp].dual - demandList[crewPairList[cp].crewPUEList[x]->demandInd].puEDual[dualInd];
		for(a = 0; a<crewPairList[cp].crewPUEList[x]->numArcs; a++){
			k = crewPairList[cp].crewPUEList[x]->arcList[a].destDutyInd;
			//update Node if new path is shorter
			//arcs to first duties already include change penalties
			newRedCost = tempRedCost + crewPairList[cp].crewPUEList[x]->arcList[a].cost - dutyList[j][k].sumDuals;
			if(newRedCost < dutyList[j][k].spRedCost){				dutyList[j][k].predType = 3;
				dutyList[j][k].predInd = x;
				dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPUEList[x]->arcList[a].cost;
				dutyList[j][k].spRedCost = newRedCost;
				dutyList[j][k].spACInd = crewPairList[cp].crewPUEList[x]->acInd;
			}
		}
	}		
	//pass thru arcs out of each reachable node
	//by examining nodes by each day in turn, topological order is maintained
	for(day = crewPairList[cp].startDay; day < lastDay; day++){ 
		//we don't need to scan OUT of nodes on last day, as we are 
		//not considering arcs between same-day duties.
		for(kStart = dutyTally[j][day][0].startInd; kStart <= dutyTally[j][day][8].endInd; kStart++){
			//if start node is not reachable (reduced cost of shortest path is INFINITY), continue to next node
			if(dutyList[j][kStart].spRedCost > (INFINITY-1))
				continue;
			//If start node ends with a trip that has a succeeding (following) trip tied to it, then the following trip is clearly not part of the duty
			//and we have not created any arcs out of the duty. 
			if(dutyList[j][kStart].lastDemInd > -1 && demandList[dutyList[j][kStart].lastDemInd].succDemID > 0)
				continue;
			//Determine which arc list to use out of duty node. 
			//If shortest path thus far is for fleet (acList index==-1), use arc list for fleet.  If node is tied to plane, there is
			//just a single arc list. In either case, arc list index == 0.
			if(dutyList[j][kStart].spACInd == -1 || dutyList[j][kStart].aircraftID > 0)
				arcListInd = 0;
			//else determine arc list from acList index or acGroup index and acList.dutyNodeArcIndex 
			else if(dutyList[j][kStart].spACInd >= 0)  //shortest path is associated with specific plane
				arcListInd = acList[dutyList[j][kStart].spACInd].dutyNodeArcIndex[day];
			else //shortest path is associated with an aircraft group and spACInd = -acGroupInd
				arcListInd = acList[acGroupList[-dutyList[j][kStart].spACInd].acInd[0]].dutyNodeArcIndex[day];
			
			tempRedCost = dutyList[j][kStart].spRedCost;
			for(a = 0; a<dutyList[j][kStart].countsPerArcList[arcListInd]; a++){
				k = dutyList[j][kStart].arcList[arcListInd][a].destDutyInd;
				if(k > dutyTally[j][lastDay][8].endInd) //duty nodes and arcs are created in increasing day order
					break; //if we are looking at arcs to nodes after the last day, break (all other arcs will be after last day)
				//update Node if new path is shorter
				newRedCost = tempRedCost + dutyList[j][kStart].arcList[arcListInd][a].cost - dutyList[j][k].sumDuals;
				changePen = 0.0;
				if(dutyList[j][k].changePenalty > 0){
					changePen = getChangePenalty(&dutyList[j][k], acList[dutyList[j][kStart].spACInd].aircraftID, crewPairList[cp].crewPairID);
					newRedCost += changePen;
				}
				if(newRedCost < dutyList[j][k].spRedCost){
					dutyList[j][k].predType = 4;
					dutyList[j][k].predInd = kStart;
					dutyList[j][k].spACInd = dutyList[j][kStart].spACInd;
					dutyList[j][k].spRedCost = newRedCost;
					dutyList[j][k].spCost = dutyList[j][kStart].spCost + dutyList[j][kStart].arcList[arcListInd][a].cost + changePen;
				}
			} //end for(a = 0
		} //end for(n = 
	} //end for(day = 		
	return 0;
}


/********************************************************************************
*	Function   getChangePenalty				Date last modified:	06/19/06 SWO	*
*	Purpose:  																	*
********************************************************************************/
double getChangePenalty(Duty *duty, int aircraftID, int crewPairID)
{
	int x;
	double penalty = 0.0;

	if(aircraftID == 0){ //If no plane is associated with path, use reduced Penalty for 
		//crew if found, else use default change penalty.
		x=0;
		penalty = duty->changePenalty;  //this is the penalty if no crew match is found
		while(duty->redPenACList[x]!= 0 ){//there will not be a redPen crew without a redPen plane
			if(duty->redPenCrewPairList[x] == crewPairID){
				penalty = duty->redPenaltyList[x];
				break;
			}
			x++;
		}
	}
	else{ //There is a plane associated with the path.  Use reduced penalty for crew/plane combo if found,
		//else use reduced penalty for plane/null crew if found, else use default change penalty.
		x=0;
		penalty = duty->changePenalty;  //this is the penalty if no exact or plane/null crew match is found
		while(duty->redPenACList[x]!= 0 ){//there will not be a redPen crew without a redPen plane
			//if the redPenAC on duty matches plane associated with path
			if(duty->redPenACList[x] == aircraftID){
				if(duty->redPenCrewPairList[x] == crewPairID){//if crew also matches..
					penalty = duty->redPenaltyList[x];
					break;
				}
				else if(duty->redPenCrewPairList[x] == 0)//else if crew is null (in duty redPen list)
					penalty = duty->redPenaltyList[x]; //this will be the penalty if no plane/crew match is found
			}
			x++;
		}
	}
	return penalty;
}



/****************************************************************************************************
*	Function   getShortestTours									Date last modified:  8/23/07 SWO	*
*	Purpose:	find shortest tours (lowest reduced cost columns) for each crewPair					*
****************************************************************************************************/
int getShortestTours(int iteration)
{
	int cp, network, j, t, lastDay, firstDay, day, numShTours, sendHome;
	int dropPlane, k, x, y, x2, dualInd, acInd, endAptID;
	int reverseDuties[MAX_WINDOW_DURATION - 1];
	double cost, redCost, preDropRedCost;
	Tour *newTour;
	ShortTour *tempTour;
	time_t startTime;
	int startLoc;
	int repoFltTm, repoElapsedTm, repoBlkTm, repoStops;
	int repoStart1, repoStart2;
	double getHmCst, straightHmCst;

	numShTours = optParam.numToursPerCrewPerItn;

	//if this is the first iteration, allocate memory for the list of ShortTours
	if(iteration == 1){
		if((shortTourList = (ShortTour **)calloc(numShTours, sizeof(ShortTour *))) == NULL){
			logMsg(logFile,"%s Line %d, Out of Memory in getShortestTours().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		for(t = 0; t<numShTours; t++){
			if((shortTourList[t] = (ShortTour *)calloc(1, sizeof(ShortTour))) == NULL){
				logMsg(logFile,"%s Line %d, Out of Memory in getShortestTours().\n", __FILE__, __LINE__);
				writeWarningData(myconn); exit(1);
			}
		}
	}
	//for each crewPair
	for(cp = 0; cp < numOptCrewPairs; cp++){
//		if(crewList[crewPairList[cp].crewListInd[0]].availDT > optParam.windowEnd || crewList[crewPairList[cp].crewListInd[1]].availDT > optParam.windowEnd)
		if(crewPairList[cp].startDay == PAST_WINDOW)	
			continue;
		j = crewPairList[cp].acTypeIndex;
		//find last possible day to end tour
		lastDay= (crewPairList[cp].endDay < (optParam.planningWindowDuration - 1)? crewPairList[cp].endDay : (optParam.planningWindowDuration - 1));
		firstDay = crewPairList[cp].startDay;
		//send crewPair "home" if endRegDay for crewPair is within planning window
		if(crewPairList[cp].endRegDay != PAST_WINDOW)
			sendHome = 1;
		else
			sendHome = 0;

		//find the shortest Tours for the crew
		for(network = 0; network <= numSepNWByFleet[j]; network++){
			//reinitialize shortTourList for the crew network
			for(t = 0; t < numShTours; t++){
				shortTourList[t]->redCost = 0;
				shortTourList[t]->dutyInd = -1;
				shortTourList[t]->finalApptDemInd = -1;
				shortTourList[t]->cost = 0;
				shortTourList[t]->dropPlane = 0;
			}
			if(network == 0)
				acInd = -1;
			else
				acInd = separateNWByFleet[j][network-1];
			
			//get shortest path to each duty node for network (crewPair and fleet or plane)
			getShortestPathsToNodes(cp, acInd);

			for(day = firstDay; day <=lastDay; day++){
				//if the last duty day is later than or equal to the crewPair's endRegDay OR if optParam.prohibitStealingPlanes = no,
				//then we will drop off plane at end, and we must add dual variable for drop
				if(optParam.prohibitStealingPlanes == 1 && day < crewPairList[cp].endRegDay)
					dropPlane = 0;
				else
					dropPlane = 1;
				//pass through list of duty nodes...
				for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++){
					//if this is not a reachable node for the crew, continue
					if(dutyList[j][k].spRedCost > (INFINITY - 1))
						continue;
					if(sendHome ==1){ //check get-home feasiblity and add get-home cost
						if(crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY - 1) && dutyList[j][k].demandInd[0] > -1)
							//if crew can't get home on time from this node AND it is not a reposition-only node which might be flown earlier, continue
							continue;
						else if(dutyList[j][k].demandInd[0] == -1 && (crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] > (INFINITY - 1) || 
							crewList[crewPairList[cp].crewListInd[0]].stayLate > 0 || crewList[crewPairList[cp].crewListInd[1]].stayLate > 0)){
							//if this IS a repo-only node and crew can't get home on time OR overtime matters, repo might be done earlier 
							//and crew starts for home earlier)
								//calculate elapsed time for reposition
								if(dutyList[j][k].predType == 4){
									startLoc = demandList[dutyList[j][k].repoDemandInd].outAirportID;
									//(we know that preceding duty does not end with a repo, as we don't repo to a repo)
									getFlightTime(demandList[dutyList[j][dutyList[j][k].predInd].lastDemInd].inAirportID, startLoc, acTypeList[j].aircraftTypeID, month, 0,  &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
									//for this special case, we set dropPlane equal to the time (as time_t/60) at which the plane is dropped 
									//(for use in creating managed legsin scheduleSolver.c)
									repoStart1 = (dutyList[j][dutyList[j][k].predInd].endTm + optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm);
									repoStart2 = (int)(max((firstEndOfDay + (day-1)*86400), optParam.windowStart)/60);
									dropPlane = getRepoArriveTm(demandList[dutyList[j][dutyList[j][k].predInd].lastDemInd].inAirportID, startLoc, max(repoStart1, repoStart2),repoElapsedTm);
									if(dropPlane == -1)
										continue;
									startTime = 60*(dropPlane + optParam.finalPostFltTm);
								}
								else //this is first (and last) duty for crewPair in this tour
									continue; //we don't create a crewArcToFirstDuty if the crew can't get home on time, so this only happens when overtime matters.
									//We will rely on existing tours to create tours where the first/last/only duty is a repo-only duty
								getHmCst = getStraightHomeCost(cp, startLoc, startTime);
								if(getHmCst > (INFINITY -1))
									continue;
								straightHmCst = getStraightHomeCost(cp, -1, 0);
								if(straightHmCst > (INFINITY - 1))
									straightHmCst = 5000.00;
								getHmCst -= straightHmCst; //subtract cost of sending crew home without doing tour
								cost = dutyList[j][k].spCost + getHmCst;
								redCost = dutyList[j][k].spRedCost + getHmCst;
						}
						else{
							cost = dutyList[j][k].spCost + crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex];
							redCost = dutyList[j][k].spRedCost + crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex];
						}
					}
					else{
						cost = dutyList[j][k].spCost;
						redCost = dutyList[j][k].spRedCost;
					}
					preDropRedCost = redCost;
					//determine if crew must drop off plane (even if not the end of their regular tour) 
					//because plane has subsequent locked legs with another crew
					if(dropPlane == 0 && acInd > -1){ //acInd > -1 only if we are working with a specific plane, 
						//not an aircraft group or the fleet. And, a plane with a locked leg is never part of fleet or group. 
						if(acList[acInd].specConnxnConstr[MAX_WINDOW_DURATION] == 2){
							if(acList[acInd].specConnxnConstr[day] > 0)
								dropPlane = 1;
							else //continue if there are specific days on which crew must drop plane and this is not one of those days
								continue;
						}
					}

					//ADD dual variable for plane dropoff at end of duty if required
					if(dropPlane){
						//find index of demand.puSDual/puEDual for fleet or plane.  Note that although planes with special connection constraints MAY
						//pick up a trip AFTER the special connection constraints are required, we will use special connection constraints for the plane
						//for all trips (for simplicity in coding)
						if(acInd > -1)
							dualInd = acList[acInd].puTripListIndex;
						else if(acInd == -1)
							dualInd = crewPairList[cp].acTypeIndex;
						else //acInd < -1
							dualInd = acList[acGroupList[-acInd].acInd[0]].puTripListIndex;
						//if duty ends with final repo to start of trip...
						if(dutyList[j][k].repoDemandInd > -1)
							redCost += demandList[dutyList[j][k].repoDemandInd].puSDual[dualInd];
						else
							redCost += demandList[dutyList[j][k].lastDemInd].puEDual[dualInd];
					}
					t = numShTours-1;
					//if new tour is cheaper than most expensive short tour (with some tolerance - say 50 cents)...
					if(redCost < (shortTourList[t]->redCost - 0.5)){
						tempTour = shortTourList[t];
						while(t > 0 && redCost < shortTourList[t-1]->redCost){
							shortTourList[t] = shortTourList[t-1];
							t = t-1;
						}
						//insert new tour
						shortTourList[t] = tempTour;
						shortTourList[t]->dropPlane = dropPlane;
						shortTourList[t]->cost = cost;
						shortTourList[t]->redCost = redCost;
						shortTourList[t]->dutyInd = k;
						shortTourList[t]->finalApptDemInd = -1;
					}
					//check if we can create a tour which is the same as above tour + an appointment/maintenance leg at the end
					
					if(acInd > -1){ //if there is a specific plane associated with tour
						y = 0;
						while(y <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
							//determine if the next inclusion for the plane is an appointment/maintenance leg at the location where the tour ends
							if((int)demandList[acList[acInd].inclDemandInd[y]].reqOut/60 > dutyList[j][k].endTm){
								if(dutyList[j][k].repoDemandInd > -1)
									endAptID = demandList[dutyList[j][k].repoDemandInd].outAirportID;
								else
									endAptID = demandList[dutyList[j][k].lastDemInd].inAirportID;
								if(demandList[acList[acInd].inclDemandInd[y]].isAppoint && demandList[acList[acInd].inclDemandInd[y]].outAirportID == endAptID){
									//calculate reduced cost for new tour which includes dual for covering appointment/maintenance, 
									//plus new dropoff dual (if applicable)instead of old
									redCost = preDropRedCost - demandList[acList[acInd].inclDemandInd[y]].dual;
									//Note that dropPlane may already be non-zero from above
									if(!dropPlane && 
										((demandList[acList[acInd].inclDemandInd[y]].isAppoint > 0 && demandList[acList[acInd].inclDemandInd[y]].elapsedTm[j]>optParam.maintTmForReassign)
										|| demandList[acList[acInd].inclDemandInd[y]].reqIn > (firstEndOfDay + (crewPairList[cp].endRegDay -1)*86400)))
										dropPlane = 1;
									if(dropPlane)
										redCost += demandList[acList[acInd].inclDemandInd[y]].puEDual[acList[acInd].puTripListIndex];
									
									t = numShTours-1;
									//if new tour is cheaper than most expensive short tour (with some tolerance - say 50 cents)...
									if(redCost < (shortTourList[t]->redCost - 0.5)){
										tempTour = shortTourList[t];
										while(t > 0 && redCost < shortTourList[t-1]->redCost){
											shortTourList[t] = shortTourList[t-1];
											t = t-1;
										}
										//insert new tour
										shortTourList[t] = tempTour;
										shortTourList[t]->dropPlane = dropPlane;
										shortTourList[t]->cost = cost;
										shortTourList[t]->redCost = redCost;
										shortTourList[t]->dutyInd = k;
										shortTourList[t]->finalApptDemInd = acList[acInd].inclDemandInd[y];
									}
								}
								break; //we have found the next inclusion that occurs after tour and can look no further
							}
							else
								y++;
						}
					}
					
				}  //end for(k = ...
			}  //end for(day = ...
			//if no negative reduced cost tours has been found, move to next crew network
			if(shortTourList[0]->dutyInd == -1)
				continue;

			//create new Tour structures from the shortTour list for the crew network
			for(t = 0; t<numShTours; t++){
				//check if there ARE that many new tours
				if(shortTourList[t]->dutyInd == -1)
					break;
				//store all but the last dutyList index in reverse order in reverseDuties
				x = -1;
				k = shortTourList[t]->dutyInd;
				while(dutyList[j][k].predType == 4){
					x++;
					reverseDuties[x] = dutyList[j][k].predInd;
					k = reverseDuties[x];
				}
				//allocate memory for new Tour
				newTour = tourAlloc(&tourList, tourCount);
				newTour->crewPairInd = cp;
				//store crewArc info with Tour
				newTour->crewArcType = dutyList[j][k].predType;
				newTour->crewArcInd = dutyList[j][k].predInd;
				newTour->cost = shortTourList[t]->cost;
				newTour->finalApptDemInd = shortTourList[t]->finalApptDemInd;
				newTour->dropPlane = shortTourList[t]->dropPlane;
				newTour->redCost = shortTourList[t]->redCost;
				//initialize duties
				for(y = 0; y < MAX_WINDOW_DURATION; y++)
					newTour->duties[y] = -1;
				//store dutyList indices in chronological order in the Tour structure
				//last duty is the duty associated with the final node
				newTour->duties[x+1] = shortTourList[t]->dutyInd;
				for(x2 = x; x2>=0; x2--)
					newTour->duties[x - x2] = reverseDuties[x2];
			} //end for(t = 
		} //end for(network = 0...		
	} // end for(cp = 
	return 0;
}


/********************************************************************************
*	Function   tourAlloc					Date last modified:	07/11/06 SWO	*
*	Purpose:  	dynamically allocate memory for tourList						*
********************************************************************************/
static Tour *
tourAlloc(Tour **tourList, int *tourCount)
{
	Tour *tPtr;

	if(!(*tourList)) {
		// nothing has been allocated yet
		(*tourList) = (Tour *) calloc(TourAllocChunk, sizeof(Tour));
		if(!(*tourList)) {
			logMsg(logFile,"%s Line %d, Out of Memory in tourAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
		tPtr = (*tourList);
		(*tourCount) ++;
		return(tPtr);
	}
	if((!(*(tourCount) % TourAllocChunk))) {
		// time to realloc
		(*tourList) = (Tour *) realloc((*tourList),
			(*tourCount * sizeof(Tour)) + (TourAllocChunk * sizeof(Tour)));
		if(!(*tourList)) {
			logMsg(logFile,"%s Line %d, Out of Memory in tourAlloc().\n", __FILE__, __LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	// return the next pre-allocated Tour
	tPtr = (*tourList);
	tPtr += (*tourCount);
	(*tourCount)++;
	memset(tPtr,'\0',sizeof(Tour));
	return(tPtr);
}

/********************************************************************************
*	Function   buildAddlExistingTours		Date last modified:	03/07/08 ANG	*
*	Purpose:  	create additional existing tours from aircraft with multi crprs	*
********************************************************************************/
int buildAddlExistingTours(void)
{ 
	int k, cp, x, y, z, lg, i, b, d, j, found, incl, numPax, acInd, firstAptID, lastAptID, prevLegInd, c, halfDaysOT, crewListInd;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	time_t firstTime, lastTime, firstDutyEnd, earlyDpt, lateArr, departTm, dutyStartTm, arrivalTm;
	int *crewPrIDPUAM; //array of crewPair IDs for crewPairs that pickup plane after maintenance or other airport appt
	int *demIndPUAM;  //array of demand indices corresponding to above maintenance or airport appts
	int numMaintPU = 0;
	double straightHmCst, getHomeCost, cost;
	time_t crewGetHomeTm[2];
	extern time_t endOfToday;
	int firstCrPrInd = 0, lastCrPrInd = 0;

	//if((tourCount = (int *)calloc(1, sizeof(int))) == NULL) {
	//	logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
	//	writeWarningData(myconn); exit(1);
	//}
	//(*tourCount) = 0;

	//allocate memory for list of existing tours
	//if((exgTourList = (ExgTour *)calloc((numOptCrewPairs), sizeof(ExgTour))) == NULL) {
	//	logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
	//	writeWarningData(myconn); exit(1);
	//}
	//numExgTours = 0;

	//for (i=0; i<numOptCrewPairs; i++)
	//{
	//	for (j=0; j<MAX_LEGS; j++)
	//	{
	//		exgTourList[i].demandInd[j] = -1;
	//	}
	//}

	//allocate memory and initialize arrays to store info on pickups that occur after maintenance or airport appointments (see below for usage)
	if((crewPrIDPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((demIndPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(x = 0; x<numMaintenanceRecord; x++){
		crewPrIDPUAM[x] = 0;
		demIndPUAM[x] = -1;
	}
	numMaintPU = 0;

	//for(cp = 0; cp< numOptCrewPairs; cp++){
	for(k = 0; k < numAircraft; k++){
		//if there is no existing (unlocked) tour for this crewPair, move to next crewPair
		x = 0;
		while(acList[k].schedLegIndList[x] > -1){//
			if(legList[acList[k].schedLegIndList[x]].inLockedTour == 1)
				x++;
			else
				break;
		}

		//if(crewPairList[cp].schedLegIndList[x]==-1)
		//	continue;

		//if(acList[k].schedLegIndList[1] == -1 || mcpAircraftList[k].numCrPairs < 2 || mcpAircraftList[k].cprInd[2] == -1)
		//	continue;
		if(acList[k].countCrPrToday <= 1)
			continue;

		//determine end of first duty day of existing tour
		y = x+1;
		while(acList[k].schedLegIndList[y] > -1){
			//if crewPair has time to rest between legs
			if((int)((legList[acList[k].schedLegIndList[y]].schedOut - legList[acList[k].schedLegIndList[y-1]].schedIn)/60)
				>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				break;
			}
			y++;
		}
		firstDutyEnd = legList[acList[k].schedLegIndList[y-1]].schedIn + optParam.postFlightTm*60;

		//populate firstCrPrInd and lastCrPrInd
		firstCrPrInd = acList[k].schedCrPrIndList[0];
		z = 0;
		while (z < MAX_LEGS && acList[k].schedLegIndList[z+1] > -1){
			lastCrPrInd = acList[k].schedCrPrIndList[z+1];
			z++;
		}

		exgTourList[numExgTours].crewPairInd = firstCrPrInd;
		cp = firstCrPrInd;

		acInd = legList[acList[k].schedLegIndList[x]].acInd;
		firstAptID = legList[acList[k].schedLegIndList[x]].outAirportID;
		firstTime = legList[acList[k].schedLegIndList[x]].schedOut;

		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = crewPairList[cp].schedLegIndList[x] - 1; y >= 0; y--){
			if(legList[y].acInd == acInd){
				if(legList[y].inLockedTour == 1){
					//all previous legs for plane must be locked, so this crewPair picks up the plane when it is next avail
					exgTourList[numExgTours].pickupType = 1;
					exgTourList[numExgTours].pickupInd = -1;
					exgTourList[numExgTours].acInd = acInd;
				}
				else{ //this is NOT the first leg in an unlocked tour for this plane, so crewPair picks up plane after another crew's tour
					if(legList[y].demandID > 0){  //pickup at end of demand leg
						exgTourList[numExgTours].pickupType = 3;
						for(i = 0; i<numOptDemand; i++){
							if(legList[y].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}
					//else if(legList[crewPairList[cp].schedLegIndList[x]].demandID > 0){ //pickup at start of demand leg
					else if(legList[acList[k].schedLegIndList[x]].demandID > 0){ //pickup at start of demand leg
						exgTourList[numExgTours].pickupType = 2; 
						for(i = 0; i<numOptDemand; i++){
							if(legList[acList[k].schedLegIndList[x]].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}		
					else {  //pickup at end of repo leg
						exgTourList[numExgTours].pickupType = 4;
						exgTourList[numExgTours].pickupInd = y;
						repoConnxnList[numRepoConnxns] = y;
						numRepoConnxns ++;
					}
					//populate acInd for exgTour
					if(acList[acInd].specConnxnConstr[MAX_WINDOW_DURATION] > 0){
						if(acList[acInd].acGroupInd > 1)//if plane is part of an acGroup
							//for LP/MIP constraints, we are picking up a plane from this acGroup
							exgTourList[numExgTours].acInd = -acList[acInd].acGroupInd;
						else
							exgTourList[numExgTours].acInd = acInd;
					}
					else
						exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
				}
				break;
			}
		}
		if(exgTourList[numExgTours].pickupType == 0){
			//if we haven't yet set pickupType, we didn't find an earlier leg on that plane
			//which indicates crewPair picks up the plane when it is next avail
			exgTourList[numExgTours].pickupType = 1;
			exgTourList[numExgTours].pickupInd = -1;
			exgTourList[numExgTours].acInd = acInd;
		}

		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		d = 0;
		incl = 0;
		prevLegInd = -1;
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(acList[k].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			//if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd)
			//	break;
			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = acList[k].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				for (b=0; b<d; b++)
				{
					if (legList[lg].demandID == demandList[exgTourList[numExgTours].demandInd[b]].demandID)
						break;
				}	
				if (b == d)
				{
					for(i = 0; i<numOptDemand; i++){
						if(legList[lg].demandID == demandList[i].demandID){
							found = 1;
							exgTourList[numExgTours].demandInd[d] = i;
							numPax = demandList[i].numPax;
							d++;
							break;
						}
					}
				}
				//if we added a new demandID to tour, add early/late penalty cost for demand
				if(found == 1){
					if(legList[lg].schedOut > demandList[i].reqOut)
					{
						exgTourList[numExgTours].cost+= (optParam.lateCostPH *(legList[lg].schedOut - demandList[i].reqOut)/60)/60;
					}
					else
					{
						exgTourList[numExgTours].cost += (optParam.earlyCostPH * (demandList[i].reqOut - legList[lg].schedOut)/60)/60;
					}
				}
			}
			else
				numPax = 0;
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			
			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
				z = acList[acInd].inclDemandInd[incl];
				if(demandList[z].reqOut >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && demandList[z].reqOut < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					if(exgTourList[numExgTours].pickupType == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					if(exgTourList[numExgTours].pickupType != 1)
						break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					if(d == 0)
						exgTourList[numExgTours].demandInd[d] = z;
					else{
						exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
						exgTourList[numExgTours].demandInd[d-1]= z;
					}
					d++;
				}
				incl++;
			}
			prevLegInd = lg;
			x++;
		} //end while(acList[k].schedLegIndList[x] > -1)

		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[acList[k].schedLegIndList[x-1]].inAirportID;
		lastTime = legList[acList[k].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;

		cp = lastCrPrInd;
		exgTourList[numExgTours].crewPairInd2 = lastCrPrInd;
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
				//if(legList[acList[k].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
					exgTourList[numExgTours].dropoffType = 3;
					exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
				}
				else if(legList[y].demandID > 0){ //drop off at start of demand leg
					exgTourList[numExgTours].dropoffType = 2;
					for(i = 0; i<numOptDemand; i++){
						if(legList[y].demandID == demandList[i].demandID){
							exgTourList[numExgTours].dropoffInd = i;
							break;
						}
					}
				}
				else{ //dropoff at end of repo leg (before another repo)
					exgTourList[numExgTours].dropoffType = 4;
					exgTourList[numExgTours].dropoffInd = crewPairList[cp].schedLegIndList[x-1];
					//exgTourList[numExgTours].dropoffInd = acList[k].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
					z = acList[acInd].inclDemandInd[incl];
					if(demandList[z].reqOut >= legList[y].schedOut)
						break;
					if(demandList[z].reqOut < lastTime){
						incl++;
						continue;
					}
					//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
					//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						exgTourList[numExgTours].dropoffType= 3;
						exgTourList[numExgTours].dropoffInd = z;
						crewPrIDPUAM[numMaintPU] = legList[y].crewPairID; // 03/19/08 ANG
						demIndPUAM[numMaintPU] = z;
						numMaintPU++;
						//don't break here, because there may be more than one appoint leg between tours
					}
					incl++;
				} //end while(incl<=acList[
				break;
			} //end if(legList[y].acInd == acInd)
		} //end for(y = crewPairList[cp].schedLegIndList[x-1] 

		if(exgTourList[numExgTours].dropoffType == 0){
			//if we haven't yet set dropoffType, we didn't find a later leg on that plane in the existing solution, and we can only pickup plane again 
			//in the optimization if we leave it at the end of a demand leg OR an appointment leg, otherwise, assume we don't drop off.  We can assume 
			//plane is dropped if (1)crewPairList.endRegDay is within window, or if (2) optParam.prohibitStealingPlanes == 0, or if 
			//(3) crewPair has another leg in existing solution (on another plane)in the window, or if (4) plane is dropped at long maintenance/appointment

			//check if last leg for crewPair is a demandLeg AND (endRegDay for crewPair is within window OR 
			//optParam.prohibitStealingPlanes == 0 OR crewPair flies another leg on a different plane)
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && demandList[z].reqOut >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - demandList[z].reqOut)/60 > optParam.maintTmForReassign){

							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
						}
				}
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//Add cost of travel to plane and any start-early overtime for crew members
		///For each crew member...
		//START - Cost for 1st crewPair
		cp = firstCrPrInd;
		for(c = 0; c<2; c++){
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
					}
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)

				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
					exgTourList[numExgTours].cost += cost;
				}
			}
		}
		//add cost of getHome (including overtime) if endRegDay for crewPair is within planning window
		if(crewPairList[cp].endRegDay != PAST_WINDOW){
			getHomeCost = 0.0;
			straightHmCst = getStraightHomeCost(cp, -1, 0);
			if(straightHmCst > (INFINITY - 1))
			   straightHmCst = 5000.00;
			//calculate earliest departure and latest arrival time of a trip home
			earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else //if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					crewGetHomeTm[c] = earlyDpt;
				getHomeCost += cost;
				//add any overtime cost to get-home cost 
				//if crew member gets home on overtime, determine the number of overtime days  
				if(crewGetHomeTm[c] > crewList[crewListInd].tourEndTm){
					halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewListInd].tourEndTm)/(12*3600);//integer division truncates
					//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
					getHomeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}//end for(c = 
			//subtract cost of sending crew straight home
			getHomeCost -= straightHmCst;
			exgTourList[numExgTours].cost += getHomeCost;
		} //end if(crewPairList[cp].endRegDay
		//END - Cost for 1st crewPair

		//START - Cost for 2nd crewPair
		cp = firstCrPrInd;
		for(c = 0; c<2; c++){
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
					}
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)

				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
					exgTourList[numExgTours].cost += cost;
				}
			}
		}
		//add cost of getHome (including overtime) if endRegDay for crewPair is within planning window
		if(crewPairList[cp].endRegDay != PAST_WINDOW){
			getHomeCost = 0.0;
			straightHmCst = getStraightHomeCost(cp, -1, 0);
			if(straightHmCst > (INFINITY - 1))
			   straightHmCst = 5000.00;
			//calculate earliest departure and latest arrival time of a trip home
			earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else //if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					crewGetHomeTm[c] = earlyDpt;
				getHomeCost += cost;
				//add any overtime cost to get-home cost 
				//if crew member gets home on overtime, determine the number of overtime days  
				if(crewGetHomeTm[c] > crewList[crewListInd].tourEndTm){
					halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewListInd].tourEndTm)/(12*3600);//integer division truncates
					//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
					getHomeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}//end for(c = 
			//subtract cost of sending crew straight home
			getHomeCost -= straightHmCst;
			exgTourList[numExgTours].cost += getHomeCost;
		} //end if(crewPairList[cp].endRegDay
		//END - Cost for 2nd crewPair

		numExgTours++;
	}  // end for(k = 0..

	//update pickup information for crewPairs picking up after appointment/maintenance dropoff
	for(x = 0; x < numExgTours; x++){
		for(y = 0; y < numMaintPU; y++){
			if(crewPairList[exgTourList[x].crewPairInd].crewPairID == crewPrIDPUAM[y]){
				exgTourList[x].pickupType = 3;
				exgTourList[x].pickupInd = demIndPUAM[y];
				//don't break here because there may be more than one appointment/maintenance leg between tours
			}
		}
	}
	//CHECK FEASIBILITY OF EXG TOURS
	//checkFeasibilityOfExisting();
	fflush (logFile);
	return 0;
}

/********************************************************************************
*	Function   buildAddlExistingTours2		Date last modified:	03/07/08 ANG	*
*	Purpose:   create additional existing tours from crpr with multi aircraft   *
********************************************************************************/
int buildAddlExistingTours2(void)
{ 
	int cp, x, y, z, lg, i, b, d, j, found, incl, numPax, acInd, firstAcInd, firstAptID, lastAptID, prevLegInd, c, halfDaysOT, crewListInd;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	time_t firstTime, lastTime, firstDutyEnd, earlyDpt, lateArr, departTm, dutyStartTm, arrivalTm;
	int *crewPrIDPUAM; //array of crewPair IDs for crewPairs that pickup plane after maintenance or other airport appt
	int *demIndPUAM;  //array of demand indices corresponding to above maintenance or airport appts
	int numMaintPU = 0;
	double straightHmCst, getHomeCost, cost;
	time_t crewGetHomeTm[2];
	char tbuf1[32], tbuf2[32];
	int errNbr1, errNbr2;
	extern int numCrewPairs;
	int x2, d2, acInd2, firstAptID2, lastAptID2;
	time_t firstTime2, lastTime2;

	//allocate memory and initialize arrays to store info on pickups that occur after maintenance or airport appointments (see below for usage)
	if((crewPrIDPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((demIndPUAM = (int *)calloc((numMaintenanceRecord), sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	for(x = 0; x<numMaintenanceRecord; x++){
		crewPrIDPUAM[x] = 0;
		demIndPUAM[x] = -1;
	}
	numMaintPU = 0;

	populateCrewPairCountAcID(); // 03/25/08 ANG

	for(cp = 0; cp< numOptCrewPairs; cp++){
		if(crewPairList[cp].countAircraftID != 2){// for now, assume crewpair flies at most 2 aircraft within the planning window
			continue;
		}

		//if there is no existing (unlocked) tour for this crewPair, move to next crewPair
		x = 0;
		while(crewPairList[cp].schedLegIndList[x] > -1){//
			if(legList[crewPairList[cp].schedLegIndList[x]].inLockedTour == 1)
				x++;
			else
				break;
		}

		if(crewPairList[cp].schedLegIndList[x]==-1)
			continue;
		//determine end of first duty day of existing tour
		y = x+1;

		while(crewPairList[cp].schedLegIndList[y] > -1){
			//if crewPair has time to rest between legs
			if((int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60)
				>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				break;
			}
			y++;
		}
		firstDutyEnd = legList[crewPairList[cp].schedLegIndList[y-1]].schedIn + optParam.postFlightTm*60;
		
		//1a. POPULATE PICKUP INFO - 1st Aircraft
		exgTourList[numExgTours].crewPairInd = cp;
		acInd = legList[crewPairList[cp].schedLegIndList[x]].acInd;
		firstAptID = legList[crewPairList[cp].schedLegIndList[x]].outAirportID;
		firstTime = legList[crewPairList[cp].schedLegIndList[x]].schedOut;
		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = crewPairList[cp].schedLegIndList[x] - 1; y >= 0; y--){
			if(legList[y].acInd == acInd){
				if(legList[y].inLockedTour == 1){
					//all previous legs for plane must be locked, so this crewPair picks up the plane when it is next avail
					exgTourList[numExgTours].pickupType = 1;
					exgTourList[numExgTours].pickupInd = -1;
					exgTourList[numExgTours].acInd = acInd;
				}
				else{ //this is NOT the first leg in an unlocked tour for this plane, so crewPair picks up plane after another crew's tour
					if(legList[y].demandID > 0){  //pickup at end of demand leg
						exgTourList[numExgTours].pickupType = 3;
						for(i = 0; i<numOptDemand; i++){
							if(legList[y].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}
					else if(legList[crewPairList[cp].schedLegIndList[x]].demandID > 0){ //pickup at start of demand leg
						exgTourList[numExgTours].pickupType = 2; 
						for(i = 0; i<numOptDemand; i++){
							if(legList[crewPairList[cp].schedLegIndList[x]].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd = i;
								break;
							}
						}
					}		
					else {  //pickup at end of repo leg
						exgTourList[numExgTours].pickupType = 4;
						exgTourList[numExgTours].pickupInd = y;
						repoConnxnList[numRepoConnxns] = y;
						numRepoConnxns ++;
					}
					//populate acInd for exgTour
					if(acList[acInd].specConnxnConstr[MAX_WINDOW_DURATION] > 0){
						if(acList[acInd].acGroupInd > 1)//if plane is part of an acGroup
							//for LP/MIP constraints, we are picking up a plane from this acGroup
							exgTourList[numExgTours].acInd = -acList[acInd].acGroupInd;
						else
							exgTourList[numExgTours].acInd = acInd;
					}
					else
						exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
				}
				break;
			}
		}
		if(exgTourList[numExgTours].pickupType == 0){
			//if we haven't yet set pickupType, we didn't find an earlier leg on that plane
			//which indicates crewPair picks up the plane when it is next avail
			exgTourList[numExgTours].pickupType = 1;
			exgTourList[numExgTours].pickupInd = -1;
			exgTourList[numExgTours].acInd = acInd;
		}
		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		//2a. INCLUDE LEGS (INCL. MX) - 1st Aircraft
		d = 0;
		incl = 0;
		prevLegInd = -1;
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(crewPairList[cp].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd){
				x2 = x;
				acInd2 = legList[crewPairList[cp].schedLegIndList[x]].acInd;
				exgTourList[numExgTours].acInd2 = acInd2;
				break; // 03/25/08 ANG
				//firstAcInd = acInd;
				//acInd = legList[crewPairList[cp].schedLegIndList[x]].acInd;
				//exgTourList[numExgTours].acInd2 = acInd;
				//switchAircraftFlag = 1;
			}
			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = crewPairList[cp].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				for (b=0; b<d; b++)
				{
					if (legList[lg].demandID == demandList[exgTourList[numExgTours].demandInd[b]].demandID)
						break;
				}	
				if (b == d)
				{
					for(i = 0; i<numOptDemand; i++){
						if(legList[lg].demandID == demandList[i].demandID){
							found = 1;
							exgTourList[numExgTours].demandInd[d] = i;
							numPax = demandList[i].numPax;
							d++;
							break;
						}
					}
				}
				//if we added a new demandID to tour, add early/late penalty cost for demand
				if(found == 1){
					if(legList[lg].schedOut > demandList[i].reqOut)
					{
						exgTourList[numExgTours].cost+= (optParam.lateCostPH *(legList[lg].schedOut - demandList[i].reqOut)/60)/60;
					}
					else
					{
						exgTourList[numExgTours].cost += (optParam.earlyCostPH * (demandList[i].reqOut - legList[lg].schedOut)/60)/60;
					}
				}
			}
			else
				numPax = 0;
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){
				z = acList[acInd].inclDemandInd[incl];
				if(demandList[z].reqOut >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && demandList[z].reqOut < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					if(exgTourList[numExgTours].pickupType == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					if(exgTourList[numExgTours].pickupType != 1)
						break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					if(d == 0)
						exgTourList[numExgTours].demandInd[d] = z;
					else{
						exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
						exgTourList[numExgTours].demandInd[d-1]= z;
					}
					d++;
				}
				incl++;
			}
			//end checking maintenance

			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)

		//3a. POPULATE DROPOFF INFO - 1st Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
					exgTourList[numExgTours].dropoffType = 3;
					exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
				}
				else if(legList[y].demandID > 0){ //drop off at start of demand leg
					exgTourList[numExgTours].dropoffType = 2;
					for(i = 0; i<numOptDemand; i++){
						if(legList[y].demandID == demandList[i].demandID){
							exgTourList[numExgTours].dropoffInd = i;
							break;
						}
					}
				}
				else{ //dropoff at end of repo leg (before another repo)
					exgTourList[numExgTours].dropoffType = 4;
					exgTourList[numExgTours].dropoffInd = crewPairList[cp].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
					z = acList[acInd].inclDemandInd[incl];
					if(demandList[z].reqOut >= legList[y].schedOut)
						break;
					if(demandList[z].reqOut < lastTime){
						incl++;
						continue;
					}
					//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
					//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						exgTourList[numExgTours].dropoffType= 3;
						exgTourList[numExgTours].dropoffInd = z;
						crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
						demIndPUAM[numMaintPU] = z;
						numMaintPU++;
						//don't break here, because there may be more than one appoint leg between tours
					}
					incl++;
				} //end while(incl<=acList[
				break;
			} //end if(legList[y].acInd == acInd)
		} //end for(y = crewPairList[cp].schedLegIndList[x-1] 
		if(exgTourList[numExgTours].dropoffType == 0){
			//if we haven't yet set dropoffType, we didn't find a later leg on that plane in the existing solution, and we can only pickup plane again 
			//in the optimization if we leave it at the end of a demand leg OR an appointment leg, otherwise, assume we don't drop off.  We can assume 
			//plane is dropped if (1)crewPairList.endRegDay is within window, or if (2) optParam.prohibitStealingPlanes == 0, or if 
			//(3) crewPair has another leg in existing solution (on another plane)in the window, or if (4) plane is dropped at long maintenance/appointment

			//check if last leg for crewPair is a demandLeg AND (endRegDay for crewPair is within window OR 
			//optParam.prohibitStealingPlanes == 0 OR crewPair flies another leg on a different plane)
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && demandList[z].reqOut >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - demandList[z].reqOut)/60 > optParam.maintTmForReassign){

							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
						}
				}
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//1b. POPULATE PICKUP INFO - 2nd Aircraft
		x = x2;
		acInd = acInd2;
		firstAptID2 = legList[crewPairList[cp].schedLegIndList[x]].outAirportID;
		firstTime2 = legList[crewPairList[cp].schedLegIndList[x]].schedOut;
		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = crewPairList[cp].schedLegIndList[x] - 1; y >= 0; y--){
			if(legList[y].acInd == acInd){
				if(legList[y].inLockedTour == 1){
					//all previous legs for plane must be locked, so this crewPair picks up the plane when it is next avail
					exgTourList[numExgTours].pickupType2 = 1;
					exgTourList[numExgTours].pickupInd2 = -1;
					//exgTourList[numExgTours].acInd2 = acInd;
				}
				else{ //this is NOT the first leg in an unlocked tour for this plane, so crewPair picks up plane after another crew's tour
					if(legList[y].demandID > 0){  //pickup at end of demand leg
						exgTourList[numExgTours].pickupType2 = 3;
						for(i = 0; i<numOptDemand; i++){
							if(legList[y].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd2 = i;
								break;
							}
						}
					}
					else if(legList[crewPairList[cp].schedLegIndList[x]].demandID > 0){ //pickup at start of demand leg
						exgTourList[numExgTours].pickupType2 = 2; 
						for(i = 0; i<numOptDemand; i++){
							if(legList[crewPairList[cp].schedLegIndList[x]].demandID == demandList[i].demandID){
								exgTourList[numExgTours].pickupInd2 = i;
								break;
							}
						}
					}		
					else {  //pickup at end of repo leg
						exgTourList[numExgTours].pickupType2 = 4;
						exgTourList[numExgTours].pickupInd2 = y;
						repoConnxnList[numRepoConnxns] = y; //??????
						numRepoConnxns ++; //??????
					}
					//populate acInd for exgTour
					//if(acList[acInd].specConnxnConstr[MAX_WINDOW_DURATION] > 0){
					//	if(acList[acInd].acGroupInd > 1)//if plane is part of an acGroup
					//		//for LP/MIP constraints, we are picking up a plane from this acGroup
					//		exgTourList[numExgTours].acInd = -acList[acInd].acGroupInd;
					//	else
					//		exgTourList[numExgTours].acInd = acInd;
					//}
					//else
					//	exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
				}
				break;
			}
		}
		if(exgTourList[numExgTours].pickupType2 == 0){
			//if we haven't yet set pickupType, we didn't find an earlier leg on that plane
			//which indicates crewPair picks up the plane when it is next avail
			exgTourList[numExgTours].pickupType2 = 1;
			exgTourList[numExgTours].pickupInd2 = -1;
			//exgTourList[numExgTours].acInd = acInd;
		}
		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost2 = - crewPairList[cp].crewPairBonus;
		

		//2b. INCLUDE LEGS (INCL. MX) - 2nd Aircraft
		d = 0;
		incl = 0;
		prevLegInd = -1;
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(crewPairList[cp].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd){
				break; // 03/25/08 ANG
			}
			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = crewPairList[cp].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				for (b=0; b<d; b++)
				{
					if (legList[lg].demandID == demandList[exgTourList[numExgTours].demandInd2[b]].demandID)
						break;
				}	
				if (b == d)
				{
					for(i = 0; i<numOptDemand; i++){
						if(legList[lg].demandID == demandList[i].demandID){
							found = 1;
							exgTourList[numExgTours].demandInd2[d] = i;
							numPax = demandList[i].numPax;
							d++; //d2++;
							break;
						}
					}
				}
				//if we added a new demandID to tour, add early/late penalty cost for demand
				if(found == 1){
					if(legList[lg].schedOut > demandList[i].reqOut)
					{
						exgTourList[numExgTours].cost2+= (optParam.lateCostPH *(legList[lg].schedOut - demandList[i].reqOut)/60)/60;
					}
					else
					{
						exgTourList[numExgTours].cost2 += (optParam.earlyCostPH * (demandList[i].reqOut - legList[lg].schedOut)/60)/60;
					}
				}
			}
			else
				numPax = 0;
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			exgTourList[numExgTours].cost2 +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){
				z = acList[acInd].inclDemandInd[incl];
				if(demandList[z].reqOut >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && demandList[z].reqOut < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					//if(exgTourList[numExgTours].pickupType2 == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
					if(demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					//if(exgTourList[numExgTours].pickupType2 != 1)
					//	break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					//if(d2 == 0)
					if(d == 0)
						exgTourList[numExgTours].demandInd2[d] = z;
					else{
						exgTourList[numExgTours].demandInd2[d] = exgTourList[numExgTours].demandInd2[d-1];
						exgTourList[numExgTours].demandInd2[d-1]= z;
					}
					d++; //d2++;
				}
				incl++;
			}
			//end checking maintenance

			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)

		//3b. POPULATE DROPOFF INFO - 2nd Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID2 = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		lastTime2 = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
					exgTourList[numExgTours].dropoffType2 = 3;
					exgTourList[numExgTours].dropoffInd2 = exgTourList[numExgTours].demandInd2[d-1];
				}
				else if(legList[y].demandID > 0){ //drop off at start of demand leg
					exgTourList[numExgTours].dropoffType2 = 2;
					for(i = 0; i<numOptDemand; i++){
						if(legList[y].demandID == demandList[i].demandID){
							exgTourList[numExgTours].dropoffInd2 = i;
							break;
						}
					}
				}
				else{ //dropoff at end of repo leg (before another repo)
					exgTourList[numExgTours].dropoffType2 = 4;
					exgTourList[numExgTours].dropoffInd2 = crewPairList[cp].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd2;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
					z = acList[acInd].inclDemandInd[incl];
					if(demandList[z].reqOut >= legList[y].schedOut)
						break;
					if(demandList[z].reqOut < lastTime2){
						incl++;
						continue;
					}
					//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
					//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
						exgTourList[numExgTours].demandInd2[d] = z;
						d++; //d2++;
						exgTourList[numExgTours].dropoffType2= 3;
						exgTourList[numExgTours].dropoffInd2 = z;
						crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
						demIndPUAM[numMaintPU] = z;
						numMaintPU++;
						//don't break here, because there may be more than one appoint leg between tours
					}
					incl++;
				} //end while(incl<=acList[
				break;
			} //end if(legList[y].acInd == acInd)
		} //end for(y = crewPairList[cp].schedLegIndList[x-1] 
		if(exgTourList[numExgTours].dropoffType2 == 0){
			//if we haven't yet set dropoffType, we didn't find a later leg on that plane in the existing solution, and we can only pickup plane again 
			//in the optimization if we leave it at the end of a demand leg OR an appointment leg, otherwise, assume we don't drop off.  We can assume 
			//plane is dropped if (1)crewPairList.endRegDay is within window, or if (2) optParam.prohibitStealingPlanes == 0, or if 
			//(3) crewPair has another leg in existing solution (on another plane)in the window, or if (4) plane is dropped at long maintenance/appointment

			//check if last leg for crewPair is a demandLeg AND (endRegDay for crewPair is within window OR 
			//optParam.prohibitStealingPlanes == 0 OR crewPair flies another leg on a different plane)
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType2= 3;
				exgTourList[numExgTours].dropoffInd2 = exgTourList[numExgTours].demandInd2[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && demandList[z].reqOut >= lastTime){
						exgTourList[numExgTours].demandInd2[d] = z;
						d++; //d2++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - demandList[z].reqOut)/60 > optParam.maintTmForReassign){

							exgTourList[numExgTours].dropoffType2= 3;
							exgTourList[numExgTours].dropoffInd2 = z;
						}
				}
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType2 == 0){
				exgTourList[numExgTours].dropoffType2 = 1;
				exgTourList[numExgTours].dropoffInd2 = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//4. UPDATE CREW COST
		//Add cost of travel to plane and any start-early overtime for crew members
		//For each crew member...
		for(c = 0; c<2; c++){
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
					}
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)

				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
					exgTourList[numExgTours].cost += cost;
				}
			}
		}
		//add cost of getHome (including overtime) if endRegDay for crewPair is within planning window
		if(crewPairList[cp].endRegDay != PAST_WINDOW){
			getHomeCost = 0.0;
			straightHmCst = getStraightHomeCost(cp, -1, 0);
			if(straightHmCst > (INFINITY - 1))
			   straightHmCst = 5000.00;
			//calculate earliest departure and latest arrival time of a trip home
			earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else //if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					crewGetHomeTm[c] = earlyDpt;
				getHomeCost += cost;
				//add any overtime cost to get-home cost 
				//if crew member gets home on overtime, determine the number of overtime days  
				if(crewGetHomeTm[c] > crewList[crewListInd].tourEndTm){
					halfDaysOT = 1+ (int)(crewGetHomeTm[c] - crewList[crewListInd].tourEndTm)/(12*3600);//integer division truncates
					//overtime cost is the sum of whole day cost * number of whole days plus half-day cost * halfdays.
					getHomeCost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost-optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}//end for(c = 
			//subtract cost of sending crew straight home
			getHomeCost -= straightHmCst;
			exgTourList[numExgTours].cost2 += getHomeCost;
		} //end if(crewPairList[cp].endRegDay
		numExgTours++;
		num2ndTypeAddlExgTours++;
	}  // end for(cp = 0..

	//update pickup information for crewPairs picking up after appointment/maintenance dropoff
	for(x = 0; x < numExgTours; x++){
		for(y = 0; y < numMaintPU; y++){
			if(crewPairList[exgTourList[x].crewPairInd].crewPairID == crewPrIDPUAM[y]){
				exgTourList[x].pickupType = 3;
				exgTourList[x].pickupInd = demIndPUAM[y];
				//don't break here because there may be more than one appointment/maintenance leg between tours
			}
		}
	}
	//CHECK FEASIBILITY OF EXG TOURS
	//checkFeasibilityOfExisting();
	fflush (logFile);

	return 0;
}