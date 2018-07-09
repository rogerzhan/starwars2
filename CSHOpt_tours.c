#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtdbg.h>//fei Jan 2011
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
extern MacInfo *macInfoList; //Mac bonus per MAC - 12/14/10 ANG
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
extern OrigDemInfo *origDemInfos; //fei Jan 2011 - original demand list
extern int numOrigDem; //fei Jan 2011
extern int maxTripsPerDuty; //fei FA

//extern int countAddlExgTours; // 03/07/08 ANG
//extern McpAircraft *mcpAircraftList; // 03/12/08 ANG
//extern int numArcsToFirstDuties;
//extern NetworkArc *arcAlloc(NetworkArc **arcList, int *arcCount);
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
static double recalculateDutyCost(Duty *duty, Aircraft *plane, NetworkArc *arc); // MAC - 09/17/08 ANG
int buildAddlExistingTours(void);
int buildAddlExistingTours2(void);

//fei FA
int buildExistingTours(void);
int buildExistingToursFA(void);
int buildAddlExistingToursFA(void);
int buildAddlExistingTours2FA(void);
int getShortestToursFA(int iteration);

//up test
int deleteDemFromExistTour(const int acInd, const int pickupType, const int pickupInd, const int dropoffType, const int dropoffInd, int *demandInd
, double *costP);

/****************************************************************************************************
*	Function   buildExistingTours							Date last modified:  08/23/07 SWO		*
*	Purpose:	create tours from existing solution to use in initial solution						*
*   Note:		New fatigue rules are NOT checked in existing tour! - 02/05/10 ANG					*
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
	//int errNbr; // 05/23/08 ANG
	char tbuf[32]; // 05/23/08 ANG
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //For checking infeasibility of current schedule - 08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

	if((tourCount = (int *)calloc(1, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	(*tourCount) = 0;
	numExgTours = 0;
	num2ndTypeAddlExgTours = 0; // 03/27/08 ANG

	if(optParam.runWithoutExgSol) return 0; // test 02/01/11 ANG

	//allocate memory for list of existing tours
	//START - First, count the possible number of members of exgTourList - 04/23/08 ANG
	countExgTour = numOptCrewPairs;
	for(cp = 0; cp < numOptCrewPairs; cp++){
		if(crewPairList[cp].countAircraftID >= 2){
			countExgTour++;
		}
	}
	for (j = 0; j < numAircraft; j++){
		//if(acList[j].countCrPrToday > 1){
		if(acList[j].countCrPrToday >= 1){ //Retain more exg sol - 08/11/08 ANG
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

	logMsg(logFile, "countExgTour = %d\n", countExgTour);

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
		//Initialize all fields in exgTourList[i] with initial value = -1 - 11/02/08 ANG
		exgTourList[i].acInd = -1; 
		exgTourList[i].crewPairInd = -1;
		exgTourList[i].pickupInd = -1;
		exgTourList[i].pickupInd2 = -1;
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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
		crewPairList[cp].exgTrInd = -1; //initialize exgTrInd for each crewpair

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
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[crewPairList[cp].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							numPax = demandList[i].numPax;

							//START - check if there is a short turnTime between this leg and previous leg
							//if there is, adjust turnTime of the previous leg - 05/09/08 ANG
							if(optParam.inclInfeasExgSol == 1){
								if (d > 0 && demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
									difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
									demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);
									fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
								}
								//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
								//if (demandList[exgTourList[numExgTours].demandInd[d]].reqOut <= acList[acInd].availDT + 60*optParam.turnTime){
								if (demandList[exgTourList[numExgTours].demandInd[d]].reqOut < acList[acInd].availDT){												
								//if( max(acList[acInd].fixedTimeB4,demandList[exgTourList[numExgTours].demandInd[d]].reqOut) < acList[acInd].availDT){//fei FA
									fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
													//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
													//				  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

									acList[acInd].availDT = demandList[exgTourList[numExgTours].demandInd[d]].reqOut;// - 60*optParam.turnTime; RLZ no turn time here.
									//acList[acInd].availDT = max(acList[acInd].fixedTimeB4,demandList[exgTourList[numExgTours].demandInd[d]].reqOut) ; //fei FA
									fprintf(logFile, "to %s.\n", 
										
										//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
										// asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA
								}
							}
							//END - 05/09/08 ANG

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
			else{
				numPax = 0;
				repoIndicator = 1;//10/22/09 ANG
				repoLegInd = lg; //03/31/10 ANG
				//START - And we still need to check if we need turn time adjustment - 05/14/08 ANG
				if (optParam.inclInfeasExgSol == 1){
					if (d > 0 && demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
						legList[lg].schedOut >= demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn &&
						difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
						demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);
						fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
					}
					//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
					//if (legList[lg].schedOut <= acList[acInd].availDT + 60*optParam.turnTime){
					if (legList[lg].schedOut < acList[acInd].availDT){					
					//if (max(acList[acInd].fixedTimeB4, legList[lg].schedOut) < acList[acInd].availDT){ //fei FA					
						fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
										//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
										//					  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");

						(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

						acList[acInd].availDT = legList[lg].schedOut; // - 60*optParam.turnTime; RLZ no turn time here
						//acList[acInd].availDT = max(acList[acInd].fixedTimeB4, legList[lg].schedOut) ; //fei FA	
						fprintf(logFile, "to %s.\n", 
							
							//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
							//								  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");

						(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA
					}
				}
				//END - 05/14/08 ANG
			}

			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			//getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
			//	acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			//could directly use the orignal flight time. RLZ 03072008

			//modified how we calculate the flight time, particularly for those with outAptID==inAptID - 03/10/09 ANG
			if (legList[lg].outAirportID == legList[lg].inAirportID && legList[lg].demandInd >= 0){
				flightTm = (int)(demandList[legList[lg].demandInd].reqIn - demandList[legList[lg].demandInd].reqOut)/60;
				elapsedTm = demandList[legList[lg].demandInd].elapsedTm[j];
				blockTm = elapsedTm;  
				numStops = 0;
			}
			else{
				getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
					acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			}

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				else if (crewPairList[cp].schedLegIndList[x+1] > -1 && 
					     legList[crewPairList[cp].schedLegIndList[x+1]].acInd == acInd){ 
					//For repo flights, check if this is a repo to mac/non-mac demands - MAC - 01/06/09 ANG
					if ( acList[acInd].isMac == 1 && 
						demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].demandID > 0) //All repo on Mac to any demand costs Mac DOC - MAC - 09/15/10 ANG
						//(demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].isMacDemand == 0 || //All repo on Mac to any demand costs Mac DOC - MAC - 09/15/10 ANG
						//(demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].isMacDemand == 1 && demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].macID != acList[acInd].aircraftID)))
						oprCost = acList[acInd].macDOC; 
						//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
					else
						oprCost = acTypeList[j].operatingCost;
				}*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if (acList[acInd].isMac == 1)
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else //all repo will cost macDOC
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else //this applies all legs on non-mac
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost; // 10/22/09 ANG
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus; // 10/22/09 ANG
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else {
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost; // 10/22/09 ANG
			}
			//END - MAC - 08/19/08 ANG*/

			//revenue information - 06/05/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

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
					if(d == 0){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						//we need to check turntime (and adjust if needed) if there is a repo leg after this that was evaluated earlier - 06/04/08 ANG
						if(optParam.inclInfeasExgSol == 1){
							if(legList[lg].demandID == 0){
								if (demandList[exgTourList[numExgTours].demandInd[d]].isAppoint == 1 &&
									difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d]].turnTime){
									
									//demandList[exgTourList[numExgTours].demandInd[d]].turnTime = difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60;
									demandList[exgTourList[numExgTours].demandInd[d]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60);//fei FA
									
									fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d]].demandID, demandList[exgTourList[numExgTours].demandInd[d]].turnTime);
								}
							}
						}
					}
					else
					{
						//exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
						//exgTourList[numExgTours].demandInd[d-1]= z;

						//repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						//repoLegInd = -1; //03/31/10 ANG

						//START - Correction here: if demandID = 0, we don't need to switch place here - 05/14/08 ANG
						if(legList[lg].demandID > 0){
							exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
							exgTourList[numExgTours].demandInd[d-1]= z;
							if(optParam.inclInfeasExgSol == 1){
								if (demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
									difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
									
									//demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60;
									demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);//fei FA
					
									fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
								}
							}
						}
						else {
							exgTourList[numExgTours].demandInd[d] = z;
							if(optParam.inclInfeasExgSol == 1){
								if (demandList[exgTourList[numExgTours].demandInd[d]].isAppoint == 1 &&
									difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d]].turnTime){
									
									//demandList[exgTourList[numExgTours].demandInd[d]].turnTime = difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60;
									demandList[exgTourList[numExgTours].demandInd[d]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60); //fei FA
									
									fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d]].demandID, demandList[exgTourList[numExgTours].demandInd[d]].turnTime);
								}
								//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
								//if (legList[lg].schedOut <= acList[acInd].availDT + 60*optParam.turnTime){ //RLZ: No turn time for availDT
                                if (legList[lg].schedOut < acList[acInd].availDT){
								//if( max(acList[acInd].fixedTimeB4,legList[lg].schedOut) < acList[acInd].availDT){//fei FA
									fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
													//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
													//				  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");

									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

									//acList[acInd].availDT = legList[lg].schedOut - 60*optParam.turnTime; //RLZ: No turn time for availDT
									acList[acInd].availDT = legList[lg].schedOut; 
									//acList[acInd].availDT = max(acList[acInd].fixedTimeB4,legList[lg].schedOut) ; //fei FA
									fprintf(logFile, "to %s.\n", 
										//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
										//							  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

								}
							}
						}
						//END - 05/14/08 ANG*/
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
		
		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}


			

		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - changed to the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
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

			//RLZ CHECK Restrict the condition to drop the planes	
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 


			//10/10/2008 RLZ: could change adjSchedIn to schedIn
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}



	
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						d++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

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

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

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
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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
		crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
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

	buildAddlExistingTours(); // build existing tour for 1 aircraft, 2 crewpairs (both crewpairs are only assigned to that aircraft, one crew overlap) - 03/07/08 ANG
	buildAddlExistingTours2(); // build existing tour for a crewpair that was assigned to 2 aircraft (currently NO existing tour generated for the 2nd aircraft) - 03/07/08 ANG

	//up test
	if ( optParam.uptest )
	{
		int count=0 ;
		for(i=0; i < numExgTours; i ++)
		{
			//_ASSERTE( exgTourList[i].demandInd[0] >= 0 || exgTourList[i].demandInd2[0] >= 0 );
			if(( exgTourList[i].demandInd[0] < 0 ||deleteDemFromExistTour( exgTourList[i].acInd, exgTourList[i].pickupType, exgTourList[i].pickupInd
			, exgTourList[i].dropoffType, exgTourList[i].dropoffInd, exgTourList[i].demandInd, &(exgTourList[i].cost)))
			&&( exgTourList[i].demandInd2[0] < 0 || deleteDemFromExistTour( exgTourList[i].acInd, exgTourList[i].pickupType2, exgTourList[i].pickupInd2
			, exgTourList[i].dropoffType2, exgTourList[i].dropoffInd2, exgTourList[i].demandInd2, &(exgTourList[i].cost2) )))
			{   
				if( count < i )//keep and move tour i
					exgTourList[count] = exgTourList[i] ;
				count ++ ; //count
			}
		}//end for(i=0; i < numExgTours; i ++)
		numExgTours = count ;
	}

	return 0;
}

/****************************************************************************************************
*	Function   checkFeasibilityOfExisting						Date last modified:  02/28/07 SWO	*
*	Purpose:	simplified (incomplete) feasibility check of existing tours to help debug and test	*
****************************************************************************************************/
int checkFeasibilityOfExisting(void)
{
	int y, cp, i, j, z, lg, acInd, depMPM, arrMPM, x, turnTime1;
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
			turnTime1 = (int)(legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn);
			if( turnTime1 < 60*optParam.turnTime
				&& (legList[crewPairList[cp].schedLegIndList[y]].demandInd > -1 ? demandList[legList[crewPairList[cp].schedLegIndList[y]].demandInd].predDemID == 0 : 1)) //ensure that this is not really a fuel stop
			{  
				fprintf(logFile,"\nAircraftID %d has insufficient turn time (%d minutes) between legs %d and %d in existing tour.\n", 
				acList[crewPairList[cp].schedLegACIndList[y-1]].aircraftID, 
				turnTime1/60,
				y-1, y);  //it is possible that these two legs are on different aircraft, but unlikely, and probably an issue anyway
			    		
			    sprintf(writetodbstring1, "AircraftID %d has insufficient turn time (%d minutes) between legs %d and %d in existing tour.", 
				acList[crewPairList[cp].schedLegACIndList[y-1]].aircraftID, 
				turnTime1/60,
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
			}
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
				 {	
					 fprintf(logFile,"\nDemandID %d on AircraftID %d has schedOut incompatible with reqOut.\n", demandList[i].demandID, acList[acInd].aircraftID);
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
					//- 60*optParam.preFlightTm) >= availDT[z]-60){ //one minute tolerance
					- 60*acTypeList[crewList[crInd[z]].acTypeIndex].preFlightTm) >= availDT[z]-60){ //07/17/2017 ANG
						//dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : optParam.preFlightTm);
						dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : acTypeList[crewList[crInd[z]].acTypeIndex].preFlightTm); //07/17/2017 ANG
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
					//dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : optParam.preFlightTm);
					dutyTm[z] = (int)((legList[lg].schedIn - legList[lg].schedOut)/60) + (actCode[z]==2? optParam.firstPreFltTm : acTypeList[crewList[crInd[z]].acTypeIndex].preFlightTm); //07/17/2017 ANG
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
	if(optParam.withFlexOS)
		getShortestToursFA(iteration);
	else
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
	double tempRedCost, tempCost, tempMacCost;
	double newRedCost;
	double changePen;
	int helper = 0;
	int addTime = 0;//OCF - 10/18/11 ANG

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

		//Crews locked to XLS+ or CJ4 cannot be moved to other aircraft - DQ - 04/23/2012 ANG
		if(crewList[crewPairList[cp].crewListInd[0]].lockedAcID > 0 && crewList[crewPairList[cp].crewListInd[0]].lockedAcID != acList[acInd].aircraftID){
			if(acInd < 0 || !checkIfXlsPlus(acList[acInd].aircraftID))
				continue;
			else if(acInd < 0 || !checkIfCj4(acList[acInd].aircraftID))
				continue;
		}

		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPlaneList[x].cost;
		//tempCost -= getCPACBonus(acList[crewPairList[cp].crewPlaneList[x].acInd].aircraftID, cp); // 06/16/09 ANG
		tempCost += (acList[crewPairList[cp].crewPlaneList[x].acInd].cpIndCPACbonus == cp ? -optParam.exgCPACBonus : 0);//CPAC - 06/17/09 ANG
		tempRedCost = tempCost - crewPairList[cp].dual - acList[crewPairList[cp].crewPlaneList[x].acInd].dual;
		for(a = 0; a<crewPairList[cp].crewPlaneList[x].numArcs; a++){
			k = crewPairList[cp].crewPlaneList[x].arcList[a].destDutyInd;
			//update dutyNode if new path is shorter
			//arcs to first duties already include change penalties 
			
			//Check OCF for PUS.  Check for PUE and Plane Arc will be set, but should not be hit if all correctly coded - 10/12/11 ANG
			if(optParam.withOcf == 1){
				if(optParam.withFlexOS && acInd >= 0){
					if(acList[acInd].reqOCF == 1 && dutyList[j][k].demandInd[0]>=0){
						addTime = 0; //Note: addTime is not yet used here as of 10/19
						if (!checkOcfTiming(acList[acInd].availDT, acList[acInd].availAirportID, demandList[dutyList[j][k].demandInd[0]].reqOut, demandList[dutyList[j][k].demandInd[0]].outAirportID, j, crewPairList[cp].crewPlaneList[x].arcList[a].startTm, &addTime))
							continue;
					}
					else if(acList[acInd].reqOCF == 1 && dutyList[j][k].repoDemandInd>=0){
						if (!checkOcfTiming(acList[acInd].availDT, acList[acInd].availAirportID, demandList[dutyList[j][k].repoDemandInd].reqOut, demandList[dutyList[j][k].repoDemandInd].outAirportID, j, crewPairList[cp].crewPlaneList[x].arcList[a].startTm, &addTime))
							continue;
					}
				}
			}

			//START - MAC - 08/21/08 ANG
			if(optParam.withMac == 1){
				tempMacCost = recalculateDutyCost(&dutyList[j][k], &acList[crewPairList[cp].crewPlaneList[x].acInd], &crewPairList[cp].crewPlaneList[x].arcList[a]);
				newRedCost = tempRedCost - dutyList[j][k].sumDuals
					         + (crewPairList[cp].crewPlaneList[x].acInd > -1 && acList[crewPairList[cp].crewPlaneList[x].acInd].isMac == 1 ? 
								crewPairList[cp].crewPlaneList[x].arcList[a].tempCostForMac + tempMacCost :
								crewPairList[cp].crewPlaneList[x].arcList[a].cost);
			}
			else
				newRedCost = tempRedCost + crewPairList[cp].crewPlaneList[x].arcList[a].cost - dutyList[j][k].sumDuals;
			//END - MAC
			
			if(newRedCost < dutyList[j][k].spRedCost){
				dutyList[j][k].predType = 1;
				dutyList[j][k].predInd = x;

				//START - MAC - 08/21/08 ANG
				if(optParam.withMac == 1){
					dutyList[j][k].spCost = tempCost 
											+ (crewPairList[cp].crewPlaneList[x].acInd > -1 && acList[crewPairList[cp].crewPlaneList[x].acInd].isMac == 1 ? 
						  					   crewPairList[cp].crewPlaneList[x].arcList[a].tempCostForMac + tempMacCost : 
											   crewPairList[cp].crewPlaneList[x].arcList[a].cost);
				}
				else
					dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPlaneList[x].arcList[a].cost;
				//END - MAC

				dutyList[j][k].spRedCost = newRedCost;
				dutyList[j][k].spACInd = crewPairList[cp].crewPlaneList[x].acInd;
			}
		}
	}
	//pass through pickup at start (PUS) arcs out of crewPair node
	for(x = 0; x<crewPairList[cp].numPUStartArcs; x++){
		helper = 0;
		//if plane on arc is not part of the crew network we are currently considering (general fleet 
		//or plane / plane group with spec. connxn constraint), continue to next arc
		p = crewPairList[cp].crewPUSList[x]->acInd;//p equals -1, -acGroupInd, or acInd and should match network
		if(p != acInd)
			continue;

		//Crews locked to XLS+ or CJ4 cannot be moved to other aircraft - DQ - 04/23/2012 ANG
		if(crewList[crewPairList[cp].crewListInd[0]].lockedAcID > 0 && crewList[crewPairList[cp].crewListInd[0]].lockedAcID != acList[acInd].aircraftID){
			if(p < 0 || !checkIfXlsPlus(acList[acInd].aircraftID))
				continue;
			else if(p < 0 || !checkIfCj4(acList[acInd].aircraftID))
				continue;
		}

		//find index of demand.puSDual for fleet or plane.  Note that although planes with special connection constraints MAY
		//pick up a trip AFTER the special connection constraints are required, we will use special connection constraints for the plane
		//for all trips (for simplicity in coding)
		if(p == -1) //we are considering fleet
			dualInd = crewPairList[cp].acTypeIndex;
		else if(p < -1){ //we are considering a group of planes but may or may not need special connxn constr
			dualInd = acList[acGroupList[-p].acInd[0]].puTripListIndex;
			if(optParam.withMac == 1 && acList[acGroupList[-p].acInd[0]].isMac == 1){
				helper = 1;
			}
			if(acList[acGroupList[-p].acInd[0]].applyCPACbonus == 1 && acList[acGroupList[-p].acInd[0]].cpIndCPACbonus > -1){ //CPAC - 06/17/09 ANG
				helper = 2;
			}
		}
		else //p >= 0, and we are considering specific plane that may or may not need special connxn constr
			dualInd = acList[p].puTripListIndex;
		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPUSList[x]->cost;
		
		//CPAC - 06/17/09 ANG
		tempCost += ((crewPairList[cp].crewPUSList[x]->acInd > -1 && acList[crewPairList[cp].crewPUSList[x]->acInd].cpIndCPACbonus == cp) ?  
					- optParam.exgCPACBonus :
					((helper == 2 && acList[acGroupList[-p].acInd[0]].cpIndCPACbonus == cp) ? -optParam.exgCPACBonus : 0));

		tempRedCost = tempCost - crewPairList[cp].dual - demandList[crewPairList[cp].crewPUSList[x]->demandInd].puSDual[dualInd];
		for(a = 0; a<crewPairList[cp].crewPUSList[x]->numArcs; a++){
			k = crewPairList[cp].crewPUSList[x]->arcList[a].destDutyInd;
			 //update Node if new path is shorter
			//arcs to first duties already include change penalties

			//Check OCF for Plane Arc, should not be hit if all correctly coded - 10/12/11 ANG
			if(optParam.withOcf == 1){
				if(optParam.withFlexOS && p >= 0){
					if(acList[p].reqOCF == 1 && dutyList[j][k].demandInd[0]>=0){
						addTime = 0;//Note: addTime is not yet used here as of 10/19
						if (!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, demandList[dutyList[j][k].demandInd[0]].reqOut, demandList[dutyList[j][k].demandInd[0]].outAirportID, j, crewPairList[cp].crewPUSList[x]->arcList[a].startTm, &addTime))
							continue;
					}
					else if(acList[p].reqOCF == 1 && dutyList[j][k].demandInd[0]==-1 && dutyList[j][k].repoDemandInd>=0){
						if (!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, demandList[dutyList[j][k].repoDemandInd].reqOut, demandList[dutyList[j][k].repoDemandInd].outAirportID, j, crewPairList[cp].crewPUSList[x]->arcList[a].startTm, &addTime))
							continue;
					}
				}
			}

			//START - MAC - 08/21/08 ANG
			if(optParam.withMac == 1){
				tempMacCost = (crewPairList[cp].crewPUSList[x]->acInd > -1 && acList[crewPairList[cp].crewPUSList[x]->acInd].isMac == 1 ? 
					recalculateDutyCost(&dutyList[j][k], &acList[crewPairList[cp].crewPUSList[x]->acInd], &crewPairList[cp].crewPUSList[x]->arcList[a]) :
					( helper == 1 ? recalculateDutyCost(&dutyList[j][k], &acList[acGroupList[-p].acInd[0]], &crewPairList[cp].crewPUSList[x]->arcList[a]) : 0));

				newRedCost = tempRedCost - dutyList[j][k].sumDuals
					         + (crewPairList[cp].crewPUSList[x]->acInd > -1 && acList[crewPairList[cp].crewPUSList[x]->acInd].isMac == 1 ? 
								crewPairList[cp].crewPUSList[x]->arcList[a].tempCostForMac + tempMacCost :
								( helper == 1 ? crewPairList[cp].crewPUSList[x]->arcList[a].tempCostForMac + tempMacCost : crewPairList[cp].crewPUSList[x]->arcList[a].cost));
			}
			else 
				newRedCost = tempRedCost + crewPairList[cp].crewPUSList[x]->arcList[a].cost - dutyList[j][k].sumDuals;
			//END - MAC

			if(newRedCost < dutyList[j][k].spRedCost){
				dutyList[j][k].predType = 2;
				dutyList[j][k].predInd = x;

				//START - MAC - 08/21/08 ANG
				if(optParam.withMac == 1){
					dutyList[j][k].spCost = tempCost 
											+ (crewPairList[cp].crewPUSList[x]->acInd > -1 && acList[crewPairList[cp].crewPUSList[x]->acInd].isMac == 1 ? 
						  					   crewPairList[cp].crewPUSList[x]->arcList[a].tempCostForMac + tempMacCost : 
												( helper == 1 ? crewPairList[cp].crewPUSList[x]->arcList[a].tempCostForMac + tempMacCost : crewPairList[cp].crewPUSList[x]->arcList[a].cost));
				}
				else 
					dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPUSList[x]->arcList[a].cost;
				//END - MAC

				dutyList[j][k].spRedCost = newRedCost;
				dutyList[j][k].spACInd = crewPairList[cp].crewPUSList[x]->acInd;
			}
		}
	}
	//pass through pickup at end (PUE) arcs out of crewPair node
	for(x = 0; x<crewPairList[cp].numPUEndArcs; x++){
		helper = 0;
		//if plane on arc is not part of the crew network we are currently considering (general fleet 
		//or plane / plane group with spec. connxn constraint), continue to next arc
		p = crewPairList[cp].crewPUEList[x]->acInd; //p equals -1, -acGroupInd, or acInd and should match network
		if(p != acInd) 
			continue;

		//Crews locked to XLS+ or CJ4 cannot be moved to other aircraft - DQ - 04/23/2012 ANG
		if(crewList[crewPairList[cp].crewListInd[0]].lockedAcID > 0 && crewList[crewPairList[cp].crewListInd[0]].lockedAcID != acList[acInd].aircraftID){
			if(p < 0 || !checkIfXlsPlus(acList[acInd].aircraftID))
				continue;
			else if(p < 0 || !checkIfCj4(acList[acInd].aircraftID))
				continue;
		}

		//find index of demand.puEDual for fleet or plane.  Note that although planes with special connection constraints MAY
		//pick up a trip AFTER the special connection constraints are required, we will use special connection constraints for the plane
		//for all trips (for simplicity in coding)
		if(p == -1) //we are considering fleet
			dualInd = crewPairList[cp].acTypeIndex;
		else if(p < -1) { //we are considering a group of planes
			dualInd = acList[acGroupList[-p].acInd[0]].puTripListIndex;
			if(optParam.withMac == 1 && acList[acGroupList[-p].acInd[0]].isMac == 1) {
				helper = 1;
			}
			if(acList[acGroupList[-p].acInd[0]].applyCPACbonus == 1 && acList[acGroupList[-p].acInd[0]].cpIndCPACbonus > -1){ //CPAC - 06/17/09 ANG
				helper = 2;
			}
		}
		else //p >= 0, and we are considering specific plane
			dualInd = acList[p].puTripListIndex;
		tempCost = -crewPairList[cp].crewPairBonus + crewPairList[cp].crewPUEList[x]->cost;

		//CPAC - 06/17/09 ANG
		tempCost += ((crewPairList[cp].crewPUEList[x]->acInd > -1 && acList[crewPairList[cp].crewPUEList[x]->acInd].cpIndCPACbonus == cp) ?  
					- optParam.exgCPACBonus :
					((helper == 2 && acList[acGroupList[-p].acInd[0]].cpIndCPACbonus == cp) ? -optParam.exgCPACBonus : 0));

		tempRedCost = tempCost - crewPairList[cp].dual - demandList[crewPairList[cp].crewPUEList[x]->demandInd].puEDual[dualInd];
		for(a = 0; a<crewPairList[cp].crewPUEList[x]->numArcs; a++){
			k = crewPairList[cp].crewPUEList[x]->arcList[a].destDutyInd;
			//update Node if new path is shorter
			//arcs to first duties already include change penalties

			//Check OCF for PUE.  This should not be hit if all correctly coded - 10/12/11 ANG
			if(optParam.withOcf == 1){
				if(optParam.withFlexOS && p >= 0){
					addTime = 0; //Note: addTime is not yet used here as of 10/19
					if(acList[p].reqOCF == 1 && dutyList[j][k].demandInd[0]>=0){
						if (!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, demandList[dutyList[j][k].demandInd[0]].reqOut, demandList[dutyList[j][k].demandInd[0]].outAirportID, j, crewPairList[cp].crewPUEList[x]->arcList[a].startTm, &addTime))
							continue;
					}
					else if(acList[p].reqOCF == 1 && dutyList[j][k].repoDemandInd>=0){
						if (!checkOcfTiming(acList[p].availDT, acList[p].availAirportID, demandList[dutyList[j][k].repoDemandInd].reqOut, demandList[dutyList[j][k].repoDemandInd].outAirportID, j, crewPairList[cp].crewPUEList[x]->arcList[a].startTm, &addTime))
							continue;
					}
				}
			}

			//START - MAC - 08/21/08 ANG
			if(optParam.withMac == 1){
				tempMacCost = (crewPairList[cp].crewPUEList[x]->acInd > -1  && acList[crewPairList[cp].crewPUEList[x]->acInd].isMac == 1 ? 
					recalculateDutyCost(&dutyList[j][k], &acList[crewPairList[cp].crewPUEList[x]->acInd], &crewPairList[cp].crewPUEList[x]->arcList[a]) :
					( helper == 1 ? recalculateDutyCost(&dutyList[j][k], &acList[acGroupList[-p].acInd[0]], &crewPairList[cp].crewPUEList[x]->arcList[a]) : 0));

				newRedCost = tempRedCost - dutyList[j][k].sumDuals
					         + (crewPairList[cp].crewPUEList[x]->acInd > -1  && acList[crewPairList[cp].crewPUEList[x]->acInd].isMac == 1 ? 
								crewPairList[cp].crewPUEList[x]->arcList[a].tempCostForMac + tempMacCost :
								( helper == 1 ? crewPairList[cp].crewPUEList[x]->arcList[a].tempCostForMac + tempMacCost : crewPairList[cp].crewPUEList[x]->arcList[a].cost));
			}
			else
				newRedCost = tempRedCost + crewPairList[cp].crewPUEList[x]->arcList[a].cost - dutyList[j][k].sumDuals;
			//END - MAC

			if(newRedCost < dutyList[j][k].spRedCost){				
				dutyList[j][k].predType = 3;
				dutyList[j][k].predInd = x;

				//START - MAC - 08/21/08 ANG
				if(optParam.withMac == 1){
					dutyList[j][k].spCost = tempCost 
											+ (crewPairList[cp].crewPUEList[x]->acInd > -1 && acList[crewPairList[cp].crewPUEList[x]->acInd].isMac == 1? 
						  					   crewPairList[cp].crewPUEList[x]->arcList[a].tempCostForMac + tempMacCost : 
												( helper == 1 ? crewPairList[cp].crewPUEList[x]->arcList[a].tempCostForMac + tempMacCost : crewPairList[cp].crewPUEList[x]->arcList[a].cost));
				}
				else
					dutyList[j][k].spCost = tempCost + crewPairList[cp].crewPUEList[x]->arcList[a].cost;
				//END - MAC

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

			//RLZ: 06/10/2008

			if (!dutyList[j][kStart].countsPerArcList)
				continue;

			helper = 0; // 09/15/08 ANG
			//Determine which arc list to use out of duty node. 
			//If shortest path thus far is for fleet (acList index==-1), use arc list for fleet.  If node is tied to plane, there is
			//just a single arc list. In either case, arc list index == 0.
			if(dutyList[j][kStart].spACInd == -1 || dutyList[j][kStart].aircraftID > 0)
				arcListInd = 0;
			//else determine arc list from acList index or acGroup index and acList.dutyNodeArcIndex 
			else if(dutyList[j][kStart].spACInd >= 0)  //shortest path is associated with specific plane
				arcListInd = acList[dutyList[j][kStart].spACInd].dutyNodeArcIndex[day];
			else {//shortest path is associated with an aircraft group and spACInd = -acGroupInd
				arcListInd = acList[acGroupList[-dutyList[j][kStart].spACInd].acInd[0]].dutyNodeArcIndex[day];
				if(optParam.withMac == 1 && acList[acGroupList[-dutyList[j][kStart].spACInd].acInd[0]].isMac == 1){
					helper = 1;
				}
			}
			
			tempRedCost = dutyList[j][kStart].spRedCost;

			for(a = 0; a<dutyList[j][kStart].countsPerArcList[arcListInd]; a++){
				k = dutyList[j][kStart].arcList[arcListInd][a].destDutyInd;
				if(k > dutyTally[j][lastDay][8].endInd) //duty nodes and arcs are created in increasing day order
					break; //if we are looking at arcs to nodes after the last day, break (all other arcs will be after last day)
				//update Node if new path is shorter

				//START - MAC - 08/21/08 ANG
				if(optParam.withMac == 1){

					tempMacCost = dutyList[j][kStart].spACInd > -1 && acList[dutyList[j][kStart].spACInd].isMac == 1 ? 
								  recalculateDutyCost(&dutyList[j][k], &acList[dutyList[j][kStart].spACInd], &dutyList[j][kStart].arcList[arcListInd][a]) : 
								  (	helper == 1 ? recalculateDutyCost(&dutyList[j][k], &acList[acGroupList[-dutyList[j][kStart].spACInd].acInd[0]], &dutyList[j][kStart].arcList[arcListInd][a]) : 0);

					newRedCost = tempRedCost - dutyList[j][k].sumDuals +
										(dutyList[j][kStart].spACInd > -1 && acList[dutyList[j][kStart].spACInd].isMac == 1 ? 
										dutyList[j][kStart].arcList[arcListInd][a].tempCostForMac + tempMacCost : 
										(	helper == 1 ? dutyList[j][kStart].arcList[arcListInd][a].tempCostForMac + tempMacCost : dutyList[j][kStart].arcList[arcListInd][a].cost));

				}
				else
					newRedCost = tempRedCost + dutyList[j][kStart].arcList[arcListInd][a].cost - dutyList[j][k].sumDuals;
				//END - MAC

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

					//START - MAC - 08/21/08 ANG
					if(optParam.withMac == 1){
						dutyList[j][k].spCost = dutyList[j][kStart].spCost + changePen +
										(dutyList[j][kStart].spACInd > -1 && acList[dutyList[j][kStart].spACInd].isMac == 1 ? 
										dutyList[j][kStart].arcList[arcListInd][a].tempCostForMac + tempMacCost : 
										(	helper == 1 ? dutyList[j][kStart].arcList[arcListInd][a].tempCostForMac + tempMacCost : dutyList[j][kStart].arcList[arcListInd][a].cost));

					}
					else
						dutyList[j][k].spCost = dutyList[j][kStart].spCost + dutyList[j][kStart].arcList[arcListInd][a].cost + changePen;
					//END - MAC

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

/********************************************************************************
*	Function   getCPACBonus					Date last modified:	02/19/09 ANG	*
*	Purpose:   Give bonus to current crewpair-aircraft assignment				*
********************************************************************************/
/*double getCPACBonus(int aircraftID, int crewPairInd)
{
	double bonus = 0.0;
	int *acPtr;
	extern CrewPair *crewPairList;

	if(aircraftID != 0 && crewPairInd >= 0){ 
		//Check aircraftID list in crewPairList[crewPairInd]
		acPtr = crewPairList[crewPairInd].aircraftID;
		while(*acPtr){
			if((*acPtr) == aircraftID){
				bonus += optParam.exgCPACBonus;
				break;
			}
			++acPtr;
		}
	}
	return bonus;
}*/

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
		if(crewPairList[cp].endRegDay != PAST_WINDOW)  //RLZ why not on the last day?
			sendHome = 1;
		else
			sendHome = 0;

		//find the shortest Tours for the crew
		for(network = 0; network <= numSepNWByFleet[j]; network++){
			//reinitialize shortTourList for the crew network
			for(t = 0; t < numShTours; t++){
				shortTourList[t]->redCost = 0;  //RLZ, always negative...
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
									//repoStart1 = (dutyList[j][dutyList[j][k].predInd].endTm + optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm);
									repoStart1 = (dutyList[j][dutyList[j][k].predInd].endTm + optParam.postFlightTm + optParam.minRestTm + acTypeList[j].preFlightTm); //07/17/2017 ANG
									repoStart2 = (int)(max((firstEndOfDay + (day-1)*86400), optParam.windowStart)/60);
									dropPlane = getRepoArriveTm(demandList[dutyList[j][dutyList[j][k].predInd].lastDemInd].inAirportID, startLoc, max(repoStart1, repoStart2),repoElapsedTm);
									if(dropPlane == -1)
										continue;
									startTime = 60*(dropPlane + optParam.finalPostFltTm);
								}
								else //this is first (and last) duty for crewPair in this tour
									continue; //we don't create a crewArcToFirstDuty if the crew can't get home on time, so this only happens when overtime matters.
									//We will rely on existing tours to create tours where the first/last/only duty is a repo-only duty  RLZ ???
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
								else{
									endAptID = demandList[dutyList[j][k].lastDemInd].inAirportID;
									if (dutyList[j][k].lastDemInd == acList[acInd].inclDemandInd[y]) // if the dutyNode has the appt demand
										break;
								}
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

				/*if (cp == 2 || cp == 58) {//06/22/2017 ANG
				logMsg(logFile, "--> tour %d, j %d, k %d, crew pair: %d, last demand: %d, cost: %f, duty spcost: %f, sent home: %d, crew pair node start index: %d, go home cost: %f \n", 
					t, j, k
					, crewPairList[cp].crewPairID
					, demandList[dutyList[j][k].lastDemInd].demandID
					, shortTourList[t]->cost
					, dutyList[j][k].spCost
					, sendHome
					, crewPairList[cp].nodeStartIndex 
					, ( crewPairList[cp].getHomeCost == NULL ? -1.1 : crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] )) ;
				}*/

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
	time_t firstTime2, lastTime2;
	int update;
	int firstAptID2, lastAptID2;
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
		if(acList[k].countCrPrToday < 1) //Retain more exg sol - 08/11/08 ANG
			continue;

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
		//if(acList[k].countCrPrToday <= 1)
		//	continue;

		//determine end of first duty day of existing tour
		y = x+1;
		while(acList[k].schedLegIndList[y] > -1){
			//if crewPair has time to rest between legs
			if((int)((legList[acList[k].schedLegIndList[y]].schedOut - legList[acList[k].schedLegIndList[y-1]].schedIn)/60)
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[acList[k].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
			if (lastCrPrInd != firstCrPrInd)//Retain more exg sol - combine only the first 2 crewpairs - 08/11/08 ANG
				break;
			z++;
		}
		if (firstCrPrInd == lastCrPrInd)//Retain more exg sol - 08/11/08 ANG
			continue;

		//if NO crew overlap in firstCrPrInd and lastCrPrInd, don't add redundant tour - 05/02/08 ANG
		//if (crewPairList[firstCrPrInd].captainID != crewPairList[lastCrPrInd].captainID && 
		//	crewPairList[firstCrPrInd].captainID != crewPairList[lastCrPrInd].flightOffID &&
		//	crewPairList[firstCrPrInd].flightOffID != crewPairList[lastCrPrInd].captainID &&
		//	crewPairList[firstCrPrInd].flightOffID != crewPairList[lastCrPrInd].flightOffID)
		//	continue;

		exgTourList[numExgTours].crewPairInd = firstCrPrInd;
		exgTourList[numExgTours].crewPairInd2 = lastCrPrInd; //Populate info for second crewpair - 11/13/09 ANG
		cp = firstCrPrInd;

		acInd = legList[acList[k].schedLegIndList[x]].acInd;
		firstAptID = legList[acList[k].schedLegIndList[x]].outAirportID;
		firstTime = legList[acList[k].schedLegIndList[x]].schedOut;

		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = acList[k].schedLegIndList[x] - 1; y >= 0; y--){
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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

		exgTourList[numExgTours].acInd = k; //Retain more exg sol - 08/11/08 ANG

		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		d = 0;
		incl = 0;
		prevLegInd = -1;
		update = 1; // 05/02/08 ANG
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(acList[k].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			//if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd)
			//	break;

			//if leg has different crewpair than the first 2 crewpairs we want to create, skip - 08/13/08 ANG
			if( legList[acList[k].schedLegIndList[x]].demandID > 0 &&
				legList[acList[k].schedLegIndList[x]].crewPairInd != firstCrPrInd &&
				legList[acList[k].schedLegIndList[x]].crewPairInd != lastCrPrInd ){
				x++;
				break;
			}

			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = acList[k].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[k].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[k].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[k].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[k].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[k].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[k].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[k].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							numPax = demandList[i].numPax;
							d++;

							//Collect information for 1st leg flown by 2nd crewpair - 05/02/08 ANG
							if (update == 1 && acList[k].schedCrPrIndList[x] != firstCrPrInd && acList[k].schedCrPrIndList[x] == lastCrPrInd){
								firstAptID2 = legList[acList[k].schedLegIndList[x]].outAirportID;  //first airport for 2nd crewpair
								firstTime2 = legList[acList[k].schedLegIndList[x]].schedOut; // first outtime for 2nd crewpair
								lastAptID2 = legList[acList[k].schedLegIndList[x-1]].inAirportID; // last airport for 1st crewpair
								lastTime2 = legList[acList[k].schedLegIndList[x-1]].schedIn; // last intime for 1st crewpair
								update = 0;
							}
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
			else{
				numPax = 0;
				repoIndicator = 1; //10/22/09 ANG
				repoLegInd = lg; //03/31/10 ANG
			}
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			
			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus;
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else{
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			}
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
				z = acList[acInd].inclDemandInd[incl];
				if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					if(exgTourList[numExgTours].pickupType != 1)
						break;
				}

				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					if(d == 0){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
					}
					else{
						exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
						exgTourList[numExgTours].demandInd[d-1]= z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
					}
					d++;
				}
				incl++;
			}
			prevLegInd = lg;
			x++;
		} //end while(acList[k].schedLegIndList[x] > -1)

		//START - we dont need to populate dropoff info but need to check other covered airport assignment - 11/11/09 ANG
		lastAptID = legList[acList[k].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[acList[k].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = acList[k].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[firstCrPrInd].crewPairID && legList[y].crewPairID != crewPairList[lastCrPrInd].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[acList[k].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
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
					exgTourList[numExgTours].dropoffInd = acList[k].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				while(incl<=acList[k].lastIncl[optParam.planningWindowDuration - 1]){
					z = acList[k].inclDemandInd[incl];
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
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
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

			//RLZ CHECK Restrict the condition to drop the planes	
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 


			//10/10/2008 RLZ: could change adjSchedIn to schedIn
			if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[acList[k].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || acList[k].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}
	
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[k].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[k].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						d++;
						if(crewPairList[cp].endRegDay != PAST_WINDOW || acList[k].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

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

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}
		//END - 11/11/09 ANG

		//If no aircraft switch, return without calculating costs incurred by crewpairs - 06/13/08 ANG
		//But here there will be no aircraft switch, this case is for 1 aircraft, 2 crewpairs - 11/10/09 ANG
		if(update == 1){
			numExgTours++;
			continue; //Retain more exg sol - 08/11/08 ANG
		}

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
						//dutyStartTm = firstTime - 60*optParam.firstPreFltTm; //Need to set dutyStartTm here - 05/02/08 ANG
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
			//earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			earlyDpt = lastTime2+ optParam.finalPostFltTm*60; //if crew is no longer on duty and is sent home from lastAptID2 at lastTime2 - 05/02/08 ANG
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				//if crew needs to be in lastCrPrInd, do not count get home cost - 05/01/08 ANG
				if (crewPairList[cp].crewListInd[c] == crewPairList[lastCrPrInd].crewListInd[0] ||
					crewPairList[cp].crewListInd[c] == crewPairList[lastCrPrInd].crewListInd[1] ){
						continue;
				}

				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID2, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else{ //if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID2, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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

		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[acList[k].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[acList[k].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[acList[k].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours

		cp = lastCrPrInd;
		exgTourList[numExgTours].crewPairInd2 = lastCrPrInd;
		exgTourList[numExgTours].cost += - crewPairList[cp].crewPairBonus;

		//for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
		for(y = acList[k].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
				if(legList[acList[k].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
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
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
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
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			//	|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){

			//RLZ restrict to drop planes only on the last day 
			//if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 

			if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400 
				|| optParam.prohibitStealingPlanes == 0 )){ //assume in this kind of case, crewPair will not switch airplane again
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}



			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						d++;
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || //RLZ
						if( z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

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

		//START - Cost for 2nd crewPair
		//Note: for 2nd pair, it starts in firstAptID2 at firstTime2 and ends in lastAptID at lastTime
		for(c = 0; c<2; c++){
			//if there is an overlapping crew in firstCrPrInd and lastCrPrInd, do not double count the cost- 05/01/08 ANG
			if (crewPairList[cp].crewListInd[c] == crewPairList[firstCrPrInd].crewListInd[0] ||
				crewPairList[cp].crewListInd[c] == crewPairList[firstCrPrInd].crewListInd[1] ){
					continue;
			}
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID2){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime2 - 60*optParam.firstPreFltTm;
					}
					//Note: We don't need firstDutyEnd for second or first crewpair, the same is used since we have an overlaping crew
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)
						//dutyStartTm = firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm); //Need to set dutyStartTm here - 05/02/08 ANG
				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime2 - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID2){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
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
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					crewGetHomeTm[c] = earlyDpt;
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID, crewList[crewListInd].endLoc);
				}
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

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

		fprintf(logFile, "Created additional existing tour (1 aircraft, 2 crewpairs) with existing tour index = %d, aircraftID = %d.\n", numExgTours, acList[k].aircraftID);
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
	int cp, x, y, z, lg, i, b, d, j, found, incl, numPax, acInd,  firstAptID, lastAptID, prevLegInd, c, halfDaysOT, crewListInd;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	time_t firstTime, lastTime, firstDutyEnd, earlyDpt, lateArr, departTm, dutyStartTm, arrivalTm;
	int *crewPrIDPUAM; //array of crewPair IDs for crewPairs that pickup plane after maintenance or other airport appt
	int *demIndPUAM;  //array of demand indices corresponding to above maintenance or airport appts
	int numMaintPU = 0;
	double straightHmCst, getHomeCost, cost;
	time_t crewGetHomeTm[2];
	extern int numCrewPairs;
	int x2, acInd2, firstAptID2, lastAptID2;
	time_t firstTime2, lastTime2;
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
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
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[crewPairList[cp].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
			}
			else
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){
				z = acList[acInd].inclDemandInd[incl];
				if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					if(exgTourList[numExgTours].pickupType != 1)
						break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
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

		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//3a. POPULATE DROPOFF INFO - 1st Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours

		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
						//(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
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
			//RLZ
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}		
				
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						d++;
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
						if(z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)]|| crewPairList[cp].schedLegIndList[x] != -1 ||
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

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
		//if there is no existing (unlocked) tour for this crewPair, move to next crewPair
		x = x2;
		//x = 0;
		while(crewPairList[cp].schedLegIndList[x] > -1){//
			if(legList[crewPairList[cp].schedLegIndList[x]].inLockedTour == 1)
				x++;
			else
				break;
		}

		if(crewPairList[cp].schedLegIndList[x]==-1)
			continue;

		//determine end of first duty day of existing tour
		//y = x+1;
		//while(crewPairList[cp].schedLegIndList[y] > -1){
		//	//if crewPair has time to rest between legs
		//	if((int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60)
		//		>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
		//		break;
		//	}
		//	y++;
		//}
		//firstDutyEnd2 = legList[crewPairList[cp].schedLegIndList[y-1]].schedIn + optParam.postFlightTm*60;

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
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
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
			else{
				numPax = 0;
				repoIndicator = 1;
				repoLegInd = lg; //03/31/10 ANG
			}
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost2 +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost2 -= optParam.macBonus;
					exgTourList[numExgTours].cost2 -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus; //10/22/09 ANG
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else{
				exgTourList[numExgTours].cost2 +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			}
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost2 -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){
				z = acList[acInd].inclDemandInd[incl];
				if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					break;
				if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn){
					incl++;
					continue;
				}
				//at the start of aircraft switch, we don't need to consider appointment/mx - 05/07/08 ANG
				if(prevLegInd == -1 && exgTourList[numExgTours].demandInd2[0] == -1){ 
					incl++;
					continue;
				}
				//if this is first leg for the crew
				if(prevLegInd == -1){
					//if crew is picking up plane when next available
					//if(exgTourList[numExgTours].pickupType2 == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
						incl++;
						continue;
					}
					//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
					//if(exgTourList[numExgTours].pickupType2 != 1)
					//	break;
				}
				//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
				if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
					//if(d2 == 0)
					if(d == 0){
						exgTourList[numExgTours].demandInd2[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
					}
					else{
						exgTourList[numExgTours].demandInd2[d] = exgTourList[numExgTours].demandInd2[d-1];
						exgTourList[numExgTours].demandInd2[d-1]= z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
					}
					d++; //d2++;
				}
				incl++;
			}
			//end checking maintenance

			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)


		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost2 += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//3b. POPULATE DROPOFF INFO - 2nd Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID2 = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime2 = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime2 = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						break;
					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime2){
						incl++;
						continue;
					}
					//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
					//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
						exgTourList[numExgTours].demandInd2[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
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
			//RLZ
		//	if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (exgTourList[numExgTours].demandInd[d-1] > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType2= 3;
				exgTourList[numExgTours].dropoffInd2 = exgTourList[numExgTours].demandInd2[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
				z = acList[acInd].inclDemandInd[y];
				if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID2 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime2){
						exgTourList[numExgTours].demandInd2[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						d++; //d2++;
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 ||
						if(z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] || crewPairList[cp].schedLegIndList[x] != -1 || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

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

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost2 -= lastRepoCost;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

		//4b. UPDATE CREW COST
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
			earlyDpt = lastTime2+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID2, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID2, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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
		fprintf(logFile, "Created additional existing tour (2 aircraft, 1 crewpair) with existing tour index = %d, crewPairID = %d.\n", numExgTours, crewPairList[cp].crewPairID);
		crewPairList[cp].exgTrInd = numExgTours; // 05/16/08 ANG
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

/****************************************************************************************
*	Function:   recalculateDutyCost					Date last modified:	09/17/08 ANG	*
*	Purpose:  	calculate cost of duty node depending on the aircraft it was assigned to*
*			(!) whether a repo need is determined by initAptID
*	Modified: Change repo cost on Mac to MacDOC if picking up owner demand - 
*			  MAC - 09/15/10 ANG
****************************************************************************************/
//static double recalculateDutyCost(Duty *duty, Aircraft *plane, int initAptID)
static double recalculateDutyCost(Duty *duty, Aircraft *plane, NetworkArc *arc)
{
	int y = 0;
	double cost = 0;
	int acTypeInd = plane->acTypeIndex;
	Demand *lastTrip = NULL;
	int repoFltTm=0, repoStops = 0;
	int initAptID = arc->repoFromAptID;
	//int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0;

	if(plane->isMac == 1){
		if(duty->lastDemInd >= 0){
			//check if a repo is needed from initLoc to origin of the first trip in the duty
			if(initAptID > 0 && initAptID != demandList[duty->demandInd[y]].outAirportID){
				//getFlightTime(initAptID, demandList[duty->demandInd[y]].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
				repoFltTm = arc->macRepoFltTm;
				repoStops = arc->macRepoStop;
				cost += //(demandList[duty->demandInd[y]].macID == plane->aircraftID) ? 
						demandList[duty->demandInd[y]].isAppoint == 1 ?
						//(repoFltTm*acTypeList[acTypeInd].macOwnerCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
						(repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
						(repoFltTm*plane->macDOC)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
						//(repoFltTm*acTypeList[acTypeInd].macOprCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
			}
			while(duty->demandInd[y] != duty->lastDemInd){
				if(lastTrip){
					if(lastTrip->inAirportID != demandList[duty->demandInd[y]].outAirportID){
						//calculate repo cost						
						//getFlightTime(lastTrip->inAirportID, demandList[duty->demandInd[y]].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
						repoFltTm = duty->repoFltTm[y-1];
						repoStops = duty->repoStop[y-1];
						//if(demandList[duty->demandInd[y]].isAppoint == 0)
							cost +=	//(demandList[duty->demandInd[y]].macID == plane->aircraftID) ?
									demandList[duty->demandInd[y]].isAppoint == 1 ?
									//(repoFltTm*acTypeList[acTypeInd].macOwnerCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
									(repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
									(repoFltTm*plane->macDOC)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
									//(repoFltTm*acTypeList[acTypeInd].macOprCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
						//else
						//	cost += (repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
					}
				}
				//calculate cost of demand
				if(demandList[duty->demandInd[y]].isAppoint == 0)
					cost += (demandList[duty->demandInd[y]].macID == plane->aircraftID) ? 
							//demandList[duty->demandInd[y]].macOwnerCost[acTypeInd] : 
							//demandList[duty->demandInd[y]].cost[acTypeInd];
							//demandList[duty->demandInd[y]].cost[acTypeInd] - optParam.macBonus : 
							demandList[duty->demandInd[y]].cost[acTypeInd] - ((optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[plane->macIndex].macBonus) : //Mac bonus per MAC - 12/14/10 ANG
							(plane->macIndex > -1 ? demandList[duty->demandInd[y]].macOprCost[plane->macIndex] : demandList[duty->demandInd[y]].macOprCost[acTypeInd]);//MacDOC per MAC - 05/20/2009 ANG
							//demandList[duty->demandInd[y]].macOprCost[acTypeInd]; //MacDOC per MAC - 05/20/2009 ANG
				lastTrip = &demandList[duty->demandInd[y]];
				y++;
			}
			if(lastTrip){
				if(lastTrip->inAirportID != demandList[duty->lastDemInd].outAirportID){
					//calculate cost of repo to the last trip
					//getFlightTime(lastTrip->inAirportID, demandList[duty->lastDemInd].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
					repoFltTm = duty->repoFltTm[y-1]; //?
					repoStops = duty->repoStop[y-1]; //?
					//if(demandList[duty->lastDemInd].isAppoint == 0)
						cost +=	//(demandList[duty->lastDemInd].macID == plane->aircraftID) ?
								demandList[duty->lastDemInd].isAppoint == 1 ?
								//(repoFltTm*acTypeList[acTypeInd].macOwnerCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
								(repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
								(repoFltTm*plane->macDOC)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
								//(repoFltTm*acTypeList[acTypeInd].macOprCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
					//else
					//		cost += (repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
				}
			}
			if (demandList[duty->lastDemInd].isAppoint == 0)
				cost += (demandList[duty->lastDemInd].macID == plane->aircraftID) ? 
						//demandList[duty->lastDemInd].macOwnerCost[acTypeInd] : 
						//demandList[duty->lastDemInd].cost[acTypeInd];
						//demandList[duty->lastDemInd].cost[acTypeInd] - optParam.macBonus : 
						demandList[duty->lastDemInd].cost[acTypeInd] - ((optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[plane->macIndex].macBonus) : //Mac bonus per MAC - 12/14/10 ANG
						(plane->macIndex > -1 ? demandList[duty->lastDemInd].macOprCost[plane->macIndex] : demandList[duty->lastDemInd].macOprCost[acTypeInd]);//MacDOC per MAC - 05/20/2009 ANG
						//demandList[duty->lastDemInd].macOprCost[acTypeInd];//MacDOC per MAC - 05/20/2009 ANG

			//add repo cost, if any
			if (duty->repoDemandInd >= 0){
				if(demandList[duty->lastDemInd].inAirportID != demandList[duty->repoDemandInd].outAirportID){
					//getFlightTime(demandList[duty->lastDemInd].inAirportID, demandList[duty->repoDemandInd].outAirportID , acTypeList[acTypeInd].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
					repoFltTm = duty->macRepoFltTm;
					repoStops = duty->macRepoStop;
					cost +=	//(demandList[duty->repoDemandInd].macID == plane->aircraftID) ?
							demandList[duty->repoDemandInd].isAppoint == 1 ?
							(repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
							(repoFltTm*plane->macDOC)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
							//(repoFltTm*acTypeList[acTypeInd].macOprCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
				}
			}

		}
		else if (duty->repoDemandInd >= 0){ //&& duty->repoFromAptID > 0){
			//calculate repo cost
			//getFlightTime(duty->repoFromAptID, demandList[duty->repoDemandInd].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
			repoFltTm = duty->repoFromAptID > 0 ? duty->macRepoFltTm : (initAptID > 0 ? arc->macRepoFltTm : 0);
			repoStops = duty->repoFromAptID > 0 ? duty->macRepoStop : (initAptID > 0 ? arc->macRepoStop : 0);
			//if(demandList[duty->repoDemandInd].isAppoint == 0)
				cost +=	//(demandList[duty->repoDemandInd].macID == plane->aircraftID) ?
						demandList[duty->repoDemandInd].isAppoint == 1 ?
						//(repoFltTm*acTypeList[acTypeInd].macOwnerCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
						(repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost :
						(repoFltTm*plane->macDOC)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
						//(repoFltTm*acTypeList[acTypeInd].macOprCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;//MacDOC per MAC - 05/20/2009 ANG
			//else
			//	cost += (repoFltTm*acTypeList[acTypeInd].operatingCost)/60 + (repoStops+1)*acTypeList[acTypeInd].taxiCost;
		}
	}

	return cost;
}

/****************************************************************************************************
*	Function   buildExistingToursFA						Date last modified:  03/14/11 fei FA		*
*	Purpose:	create tours from existing solution to use in initial solution						*
*   Note:		New fatigue rules are NOT checked in existing tour! - 02/05/10 ANG					*
****************************************************************************************************/
int buildExistingToursFA(void)
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
	//int errNbr; // 05/23/08 ANG
	char tbuf[32]; // 05/23/08 ANG
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //For checking infeasibility of current schedule - 08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

	int curInclInd ; //fei Jan 2011

	if((tourCount = (int *)calloc(1, sizeof(int))) == NULL) {
		logMsg(logFile,"%s Line %d, Out of Memory in buildExistingTours().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}
	(*tourCount) = 0;

	numExgTours = 0;
	num2ndTypeAddlExgTours = 0;
	if(optParam.runWithoutExgSol) 
		return 0;

	//allocate memory for list of existing tours
	//START - First, count the possible number of members of exgTourList - 04/23/08 ANG
	countExgTour = numOptCrewPairs;
	for(cp = 0; cp < numOptCrewPairs; cp++){
		if(crewPairList[cp].countAircraftID >= 2){
			countExgTour++;
		}
	}
	for (j = 0; j < numAircraft; j++){
		//if(acList[j].countCrPrToday > 1){
		if(acList[j].countCrPrToday >= 1){ //Retain more exg sol - 08/11/08 ANG
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
		//Initialize all fields in exgTourList[i] with initial value = -1 - 11/02/08 ANG
		exgTourList[i].acInd = -1; 
		exgTourList[i].crewPairInd = -1;
		exgTourList[i].pickupInd = -1;
		exgTourList[i].pickupInd2 = -1;
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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
		crewPairList[cp].exgTrInd = -1; //initialize exgTrInd for each crewpair

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
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[crewPairList[cp].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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
		curInclInd = 0 ; //fei Jan 2011: track index of the last added inclusion in the inclusion list of this ac
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
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							numPax = demandList[i].numPax;

							//START - check if there is a short turnTime between this leg and previous leg
							//if there is, adjust turnTime of the previous leg - 05/09/08 ANG
							if(optParam.inclInfeasExgSol == 1){
								if (d > 0 && demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
									difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
									demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);
									fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
								}
								//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
								//if (demandList[exgTourList[numExgTours].demandInd[d]].reqOut <= acList[acInd].availDT + 60*optParam.turnTime){
								//if (demandList[exgTourList[numExgTours].demandInd[d]].reqOut < acList[acInd].availDT){			
								if( max(acList[acInd].fixedTimeB4,demandList[exgTourList[numExgTours].demandInd[d]].reqOut) < acList[acInd].availDT){//fei FA
									fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
									//				(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
									//								  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

									//acList[acInd].availDT = demandList[exgTourList[numExgTours].demandInd[d]].reqOut;// - 60*optParam.turnTime; RLZ no turn time here.
									acList[acInd].availDT = max(acList[acInd].fixedTimeB4,demandList[exgTourList[numExgTours].demandInd[d]].reqOut) ; //fei FA
									fprintf(logFile, "to %s.\n", 
									//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
									//								  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
									(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA
								}
							}
							//END - 05/09/08 ANG

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
			else{
				numPax = 0;
				repoIndicator = 1;//10/22/09 ANG
				repoLegInd = lg; //03/31/10 ANG
				//START - And we still need to check if we need turn time adjustment - 05/14/08 ANG
				if (optParam.inclInfeasExgSol == 1){
					if (d > 0 && demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
						legList[lg].schedOut >= demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn &&
						difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
						demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);
						fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
					}
					//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
					//if (legList[lg].schedOut <= acList[acInd].availDT + 60*optParam.turnTime){
					//if (legList[lg].schedOut < acList[acInd].availDT){	
					if (max(acList[acInd].fixedTimeB4, legList[lg].schedOut) < acList[acInd].availDT){ //fei FA			
						fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
						//				(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
						//									  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
						(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

						//acList[acInd].availDT = legList[lg].schedOut; // - 60*optParam.turnTime; RLZ no turn time here
						acList[acInd].availDT = max(acList[acInd].fixedTimeB4, legList[lg].schedOut) ; //fei FA	
						fprintf(logFile, "to %s.\n", 
						//(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
						//									  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");
						(acList[acInd].availDT) ?  
									dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA
					}
				}
				//END - 05/14/08 ANG
			}

			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			//getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
			//	acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			//could directly use the orignal flight time. RLZ 03072008

			//modified how we calculate the flight time, particularly for those with outAptID==inAptID - 03/10/09 ANG
			if (legList[lg].outAirportID == legList[lg].inAirportID && legList[lg].demandInd >= 0){
				flightTm = (int)(demandList[legList[lg].demandInd].reqIn - demandList[legList[lg].demandInd].reqOut)/60;
				elapsedTm = demandList[legList[lg].demandInd].elapsedTm[j];
				blockTm = elapsedTm;  
				numStops = 0;
			}
			else{
				getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
					acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			}

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				else if (crewPairList[cp].schedLegIndList[x+1] > -1 && 
					     legList[crewPairList[cp].schedLegIndList[x+1]].acInd == acInd){ 
					//For repo flights, check if this is a repo to mac/non-mac demands - MAC - 01/06/09 ANG
					if ( acList[acInd].isMac == 1 && 
						demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].demandID > 0) //All repo on Mac to any demand costs Mac DOC - MAC - 09/15/10 ANG
						//(demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].isMacDemand == 0 || //All repo on Mac to any demand costs Mac DOC - MAC - 09/15/10 ANG
						//(demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].isMacDemand == 1 && demandList[legList[crewPairList[cp].schedLegIndList[x+1]].demandInd].macID != acList[acInd].aircraftID)))
						oprCost = acList[acInd].macDOC; 
						//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
					else
						oprCost = acTypeList[j].operatingCost;
				}*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if (acList[acInd].isMac == 1)
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else //all repo will cost macDOC
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else //this applies all legs on non-mac
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost; // 10/22/09 ANG
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus; // 10/22/09 ANG
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else {
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost; // 10/22/09 ANG
			}
			//END - MAC - 08/19/08 ANG*/

			//revenue information - 06/05/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]) {
			incl = curInclInd ; //fei Jan 2011
			while(incl < acList[acInd].numIncl ) { //fei Jan 2011

				//fei Jan 2011: if there are copies for multiple locations and days, a single copy suffices, assuming order of inclusions
				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)//fei Jan 2011//for each copy
				{
					//z = acList[acInd].inclDemandInd[y];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: z --> new index

					//if(demandList[z].reqOut >= legList[lg].schedOut)
					if( (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut) 
					{
						//RLZ: Preserve infeasiblity.
						//curInclInd = incl; //fei Jan 2011
						break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
						//fei Jan 2011: note: break, assume copies are sorted
					}
					//if(prevLegInd > -1 && demandList[z].reqOut < legList[prevLegInd].schedIn){
					if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn)
					{
						//incl++;//fei Jan 2011
						continue; //fei Jan 2011: note: inclusion is before the first demand, continue to the next one
					}
					//if this is first leg for the crew
					if(prevLegInd == -1){
						//if crew is picking up plane when next available
						//if(exgTourList[numExgTours].pickupType == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
						if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
							//incl++;//fei Jan 2011
							continue; //fei Jan 2011: note: inclusion is before ac available time, continue to the next one
						}
						//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
						if(exgTourList[numExgTours].pickupType != 1)
						{
							//incl = acList[acInd].lastIncl[optParam.planningWindowDuration - 1] + 1 ; //fei Jan 2011: for breaking outer loop//note: +1 unnecessary
							curInclInd = incl ; //fei Jan 2011
							incl = acList[acInd].numIncl ; //fei Jan 2011: for breaking outer loop
							break;//fei Jan 2011: break inner loop
						}
					}
					//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
					//if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
					if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID 
					&&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut)+ demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
						//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
						if(d == 0){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							//we need to check turntime (and adjust if needed) if there is a repo leg after this that was evaluated earlier - 06/04/08 ANG
							if(optParam.inclInfeasExgSol == 1){
								if(legList[lg].demandID == 0){
									if (demandList[exgTourList[numExgTours].demandInd[d]].isAppoint == 1 &&
										difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d]].turnTime){
										demandList[exgTourList[numExgTours].demandInd[d]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60);
										fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d]].demandID, demandList[exgTourList[numExgTours].demandInd[d]].turnTime);
									}
								}
							}
						}
						else
						{
							//exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
							//exgTourList[numExgTours].demandInd[d-1]= z;

							//repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							//repoLegInd = -1; //03/31/10 ANG

							//START - Correction here: if demandID = 0, we don't need to switch place here - 05/14/08 ANG
							if(legList[lg].demandID > 0){
								exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
								exgTourList[numExgTours].demandInd[d-1]= z;
								if(optParam.inclInfeasExgSol == 1){
									if (demandList[exgTourList[numExgTours].demandInd[d-1]].isAppoint == 1 &&
										difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime){
										demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime = (int)(difftime(demandList[exgTourList[numExgTours].demandInd[d]].reqOut, demandList[exgTourList[numExgTours].demandInd[d-1]].reqIn)/60);
										fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d-1]].demandID, demandList[exgTourList[numExgTours].demandInd[d-1]].turnTime);
									}
								}
							}
							else {
								exgTourList[numExgTours].demandInd[d] = z;
								if(optParam.inclInfeasExgSol == 1){
									if (demandList[exgTourList[numExgTours].demandInd[d]].isAppoint == 1 &&
										difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn) < 60*demandList[exgTourList[numExgTours].demandInd[d]].turnTime){
										demandList[exgTourList[numExgTours].demandInd[d]].turnTime = (int)(difftime(legList[lg].schedOut, demandList[exgTourList[numExgTours].demandInd[d]].reqIn)/60);
										fprintf(logFile, "turnTime for demandID %d is adjusted to %d.\n", demandList[exgTourList[numExgTours].demandInd[d]].demandID, demandList[exgTourList[numExgTours].demandInd[d]].turnTime);
									}
									//check if we need to adjust the availDT for the aircraft to cover infeasible cases- 05/22/08 ANG
									//if (legList[lg].schedOut <= acList[acInd].availDT + 60*optParam.turnTime){ //RLZ: No turn time for availDT
									//if (legList[lg].schedOut < acList[acInd].availDT){
									if (max(acList[acInd].fixedTimeB4, legList[lg].schedOut) < acList[acInd].availDT){ //fei FA			
										fprintf(logFile, "availDT for aircraftID %d is adjusted from %s ", acList[acInd].aircraftID, 
										//				(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
										//								  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");

										(acList[acInd].availDT) ?  
										dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

										//acList[acInd].availDT = legList[lg].schedOut - 60*optParam.turnTime; //RLZ: No turn time for availDT
										//acList[acInd].availDT = legList[lg].schedOut; 
										acList[acInd].availDT = max(acList[acInd].fixedTimeB4, legList[lg].schedOut) ; //fei FA	
										fprintf(logFile, "to %s.\n", 
										//	(acList[acInd].availDT) ? dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
										//								  asctime(gmtime(&(acList[acInd].availDT))), NULL, &errNbr),tbuf,"%Y/%m/%d %H:%M") : "0000/00/00 00:00");

										(acList[acInd].availDT) ?  
										dt_DateTimeToDateTimeString(dt_time_tToDateTime(acList[acInd].availDT), tbuf, "%Y/%m/%d %H:%M") : "0000/00/00 00:00"); //fei FA

									}
								}
							}
							//END - 05/14/08 ANG*/
						}
						d++;
				
						//curInclInd = incl ; //fei Jan 2011
						curInclInd = incl + 1; //fei FA: note: this inclusion is added, no more copies from this inclusion, continue to the next inclusion
						break;//fei Jan 2011: only one copy per inclusion, break inner loop

					}
				}//end for(c=0; 
				incl++;
			}
			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)

		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		
		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - changed to the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
				//while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
				incl = curInclInd ; //fei Jan 2011
				while(incl < acList[acInd].numIncl ){ //fei Jan 2011
					for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)
					{
						//z = acList[acInd].inclDemandInd[incl];
						z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: new index

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						{
							//curInclInd = incl; //fei Jan 2011
							break;//fei Jan 2011: note: break, assume copies are sorted
						}

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime)
						{
							//incl++;//fei Jan 2011
							continue;
						}

						//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
						//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
						if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
						((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++;
							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
							crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
							demIndPUAM[numMaintPU] = z;
							numMaintPU++;
							//don't break here, because there may be more than one appoint leg between tours

							//curInclInd = incl; //fei Jan 2011
							curInclInd = incl + 1; //fei Jan 2011
							break; //fei Jan 2011: one copy, break inner loop, 
						}
					}//end for(c=0; 
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

			//RLZ CHECK Restrict the condition to drop the planes	
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 


			//10/10/2008 RLZ: could change adjSchedIn to schedIn
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}



	
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			//for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
			//for(y=0; y<=acList[acInd].numIncl ; y++){
			for(y=0; y < acList[acInd].numIncl ; y++){ //FlexOS - 03/31/11 ANG
				
				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; c ++)//fei Jan 2011
				{
					//z = acList[acInd].inclDemandInd[y];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].indices[c] ; //fei Jan 2011: new index
					
					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++;
							if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
								optParam.prohibitStealingPlanes == 0 ||
								(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

								exgTourList[numExgTours].dropoffType= 3;
								exgTourList[numExgTours].dropoffInd = z;
							}

							break; //fei Jan 2011: one copy, break inner loop, 

					}
				}//end for(c=0
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

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
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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
		crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
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

	buildAddlExistingToursFA(); // build existing tour for 1 aircraft, 2 crewpairs (both crewpairs are only assigned to that aircraft, one crew overlap) - 03/07/08 ANG
	buildAddlExistingTours2FA(); // build existing tour for a crewpair that was assigned to 2 aircraft (currently NO existing tour generated for the 2nd aircraft) - 03/07/08 ANG

	//up test
	if ( optParam.uptest )
	{
		int count=0 ;
		for(i=0; i < numExgTours; i ++)
		{
			//_ASSERTE( exgTourList[i].demandInd[0] >= 0 || exgTourList[i].demandInd2[0] >= 0 );
			if(( exgTourList[i].demandInd[0] < 0 ||deleteDemFromExistTour( exgTourList[i].acInd, exgTourList[i].pickupType, exgTourList[i].pickupInd
			, exgTourList[i].dropoffType, exgTourList[i].dropoffInd, exgTourList[i].demandInd, &(exgTourList[i].cost)))
			&&( exgTourList[i].demandInd2[0] < 0 || deleteDemFromExistTour( exgTourList[i].acInd, exgTourList[i].pickupType2, exgTourList[i].pickupInd2
			, exgTourList[i].dropoffType2, exgTourList[i].dropoffInd2, exgTourList[i].demandInd2, &(exgTourList[i].cost2) )))
			{   
				if( count < i )//keep and move tour i
					exgTourList[count] = exgTourList[i] ;
				count ++ ; //count
			}
		}//end for(i=0; i < numExgTours; i ++)
		numExgTours = count ;
	}

	return 0;
}

/****************************************************************************************************
*	Function   getShortestToursFA								Date last modified:  03/14/11 fei FA	*
*	Purpose:	find shortest tours (lowest reduced cost columns) for each crewPair					*
****************************************************************************************************/
int getShortestToursFA(int iteration)
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

	int i, demInd;

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
		if(crewPairList[cp].endRegDay != PAST_WINDOW)  //RLZ why not on the last day?
			sendHome = 1;
		else
			sendHome = 0;

		//find the shortest Tours for the crew
		for(network = 0; network <= numSepNWByFleet[j]; network++){
			//reinitialize shortTourList for the crew network
			for(t = 0; t < numShTours; t++){
				shortTourList[t]->redCost = 0;  //RLZ, always negative...
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
									//repoStart1 = (dutyList[j][dutyList[j][k].predInd].endTm + optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm);
									repoStart1 = (dutyList[j][dutyList[j][k].predInd].endTm + optParam.postFlightTm + optParam.minRestTm + acTypeList[j].preFlightTm); //07/17/2017 ANG
									repoStart2 = (int)(max((firstEndOfDay + (day-1)*86400), optParam.windowStart)/60);
									dropPlane = getRepoArriveTm(demandList[dutyList[j][dutyList[j][k].predInd].lastDemInd].inAirportID, startLoc, max(repoStart1, repoStart2),repoElapsedTm);
									if(dropPlane == -1)
										continue;
									startTime = 60*(dropPlane + optParam.finalPostFltTm);
								}
								else //this is first (and last) duty for crewPair in this tour
									continue; //we don't create a crewArcToFirstDuty if the crew can't get home on time, so this only happens when overtime matters.
									//We will rely on existing tours to create tours where the first/last/only duty is a repo-only duty  RLZ ???
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

						//fei FA debug
						//fprintf(logFile, "--> tour %d, crew pair: %d, last demand: %d, cost: %f, duty spcost: %f, sent home: %d, crew pair node start index: %d, go home cost: %f \n", t, crewPairList[cp].crewPairID
						//, demandList[dutyList[j][k].lastDemInd].demandID, shortTourList[t]->cost, dutyList[j][k].spCost, sendHome, crewPairList[cp].nodeStartIndex 
						//, ( crewPairList[cp].getHomeCost == NULL ? -1.1 : crewPairList[cp].getHomeCost[k - crewPairList[cp].nodeStartIndex] )) ;
					}
					//check if we can create a tour which is the same as above tour + an appointment/maintenance leg at the end
					if(acInd > -1){ //if there is a specific plane associated with tour

						//fei FA //check if this maint/app is already in the tour
						//get the last inclusion in the tour
						x = k ; //current duty index
						_ASSERTE( x >= 0 );
						if( dutyList[j][x].lastInclInd == -1 ) //last duty has no inclusion, search for the previous duties
						{
							while(dutyList[j][x].predType == 4) //there are previous duties
							{
								x = dutyList[j][x].predInd;
								_ASSERTE( x >= 0 );
								if( dutyList[j][x].lastInclInd >= 0 ) //found
									break;
							}//end while
							_ASSERTE( x >= 0 );
						}
						//end //get the last inclusion in the tour


						//y = 0;
						y = dutyList[j][x].lastInclInd + 1 ; //fei FA //start from the next inclusion

						//while(y <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
						while( y < acList[acInd].numIncl ){ //fei Jan 2011

							//determine if the next inclusion for the plane is an appointment/maintenance leg at the location where the tour ends
							for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; i ++)
							{
								demInd = origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].indices[i];//fei Jan 2011: new index

								//if((int)demandList[acList[acInd].inclDemandInd[y]].reqOut/60 > dutyList[j][k].endTm){
								if((int)demandList[demInd].reqOut/60 > dutyList[j][k].endTm){//fei Jan 2011
									if(dutyList[j][k].repoDemandInd > -1)
										endAptID = demandList[dutyList[j][k].repoDemandInd].outAirportID;
									else{
										endAptID = demandList[dutyList[j][k].lastDemInd].inAirportID;
										//if (dutyList[j][k].lastDemInd == acList[acInd].inclDemandInd[y]) // if the dutyNode has the appt demand
										//if (dutyList[j][k].lastDemInd == demInd ) // if the dutyNode has the appt demand
										/*
										if (demandList[dutyList[j][k].lastDemInd].origDemInd == acList[acInd].inclInfoP->origDemIndices[y] ) // if the dutyNode has the appt demand
										{
											//y = acList[acInd].lastIncl[optParam.planningWindowDuration - 1] + 1 ; //fei Jan 2011: for breaking outer loop//note: +1 unnecessary
											y = acList[acInd].numIncl ; //fei Jan 2011: for breaking outer loop
											break;
										}
										*/
										_ASSERTE( demandList[dutyList[j][k].lastDemInd].origDemInd != acList[acInd].inclInfoP->origDemIndices[y] );
									}
									//if(demandList[acList[acInd].inclDemandInd[y]].isAppoint && demandList[acList[acInd].inclDemandInd[y]].outAirportID == endAptID){
									if(demandList[demInd].isAppoint && demandList[demInd].outAirportID == endAptID) {//fei Jan 2011
										//calculate reduced cost for new tour which includes dual for covering appointment/maintenance, 
										//plus new dropoff dual (if applicable)instead of old
										//redCost = preDropRedCost - demandList[acList[acInd].inclDemandInd[y]].dual;
										redCost = preDropRedCost - demandList[demInd].dual;//fei Jan 2011
										//Note that dropPlane may already be non-zero from above
										if(!dropPlane && 
											//((demandList[acList[acInd].inclDemandInd[y]].isAppoint > 0 && demandList[acList[acInd].inclDemandInd[y]].elapsedTm[j]>optParam.maintTmForReassign)
										((demandList[demInd].isAppoint > 0 && demandList[demInd].elapsedTm[j]>optParam.maintTmForReassign)//fei Jan 2011
										//|| demandList[acList[acInd].inclDemandInd[y]].reqIn > (firstEndOfDay + (crewPairList[cp].endRegDay -1)*86400)))
										|| demandList[demInd].reqIn > (firstEndOfDay + (crewPairList[cp].endRegDay -1)*86400)))//fei Jan 2011
											dropPlane = 1;
										if(dropPlane)
											//redCost += demandList[acList[acInd].inclDemandInd[y]].puEDual[acList[acInd].puTripListIndex];
											redCost += demandList[demInd].puEDual[acList[acInd].puTripListIndex];
										
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
											//shortTourList[t]->finalApptDemInd = acList[acInd].inclDemandInd[y];
											shortTourList[t]->finalApptDemInd = demInd;

											 //fei Jan 2011
											y = acList[acInd].numIncl ; //for breaking outer loop
											break; //we have found the next inclusion that occurs after tour and can look no further
											//fei Jan 2011: note: one inclusion allowed

										}
									}
									
									//y = acList[acInd].numIncl ; //fei Jan 2011: for breaking outer loop
									//break; //we have found the next inclusion that occurs after tour and can look no further
									
								}//end if((int)demandList[demInd].reqOut/60 > dutyList[j][k].endTm)
								//else{
								//	y++;
								//	break; //FlexOS - 03/31/11 ANG
								//}

								//fei Jan 2011: note: no break, different from the case of non-flex&&no-copies, in which inclusions are in the order of increasing start time (fixed)
								//fei Jan 2011: note: break only when an inclusion is added

							}//end for(i=0; i

							y ++ ;
						
						}//end while( y < acList[acInd].numIncl ){
					}//end if(acInd > -1)
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
*	Function   buildAddlExistingToursFA	Date last modified:	03/14/11 fei FA	*
*	Purpose:  	create additional existing tours from aircraft with multi crprs	*
********************************************************************************/
int buildAddlExistingToursFA(void)
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
	time_t firstTime2, lastTime2;
	int update;
	int firstAptID2, lastAptID2;
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

	int curInclInd; //fei Jan 2011: current inclusion index

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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
		if(acList[k].countCrPrToday < 1) //Retain more exg sol - 08/11/08 ANG
			continue;

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
		//if(acList[k].countCrPrToday <= 1)
		//	continue;

		//determine end of first duty day of existing tour
		y = x+1;
		while(acList[k].schedLegIndList[y] > -1){
			//if crewPair has time to rest between legs
			if((int)((legList[acList[k].schedLegIndList[y]].schedOut - legList[acList[k].schedLegIndList[y-1]].schedIn)/60)
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[acList[k].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
			if (lastCrPrInd != firstCrPrInd)//Retain more exg sol - combine only the first 2 crewpairs - 08/11/08 ANG
				break;
			z++;
		}
		if (firstCrPrInd == lastCrPrInd)//Retain more exg sol - 08/11/08 ANG
			continue;

		//if NO crew overlap in firstCrPrInd and lastCrPrInd, don't add redundant tour - 05/02/08 ANG
		//if (crewPairList[firstCrPrInd].captainID != crewPairList[lastCrPrInd].captainID && 
		//	crewPairList[firstCrPrInd].captainID != crewPairList[lastCrPrInd].flightOffID &&
		//	crewPairList[firstCrPrInd].flightOffID != crewPairList[lastCrPrInd].captainID &&
		//	crewPairList[firstCrPrInd].flightOffID != crewPairList[lastCrPrInd].flightOffID)
		//	continue;

		exgTourList[numExgTours].crewPairInd = firstCrPrInd;
		exgTourList[numExgTours].crewPairInd2 = lastCrPrInd; //Populate info for second crewpair - 11/13/09 ANG
		cp = firstCrPrInd;

		acInd = legList[acList[k].schedLegIndList[x]].acInd;
		firstAptID = legList[acList[k].schedLegIndList[x]].outAirportID;
		firstTime = legList[acList[k].schedLegIndList[x]].schedOut;

		//populate pickup info for plane...
		//check if there is an earlier leg in an unlocked tour for the plane
		for(y = acList[k].schedLegIndList[x] - 1; y >= 0; y--){
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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

		exgTourList[numExgTours].acInd = k; //Retain more exg sol - 08/11/08 ANG

		//initialize cost (will calculate below)
		exgTourList[numExgTours].cost = - crewPairList[cp].crewPairBonus;

		d = 0;
		prevLegInd = -1;
		update = 1; // 05/02/08 ANG
		
		incl = 0;
		curInclInd = 0 ;//fei Jan 2011: 
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(acList[k].schedLegIndList[x] > -1){
			//if this is a different plane than flown previously, break without adding to tour 
			//(we consider only one plane per crewPair in optimization)
			//if(legList[crewPairList[cp].schedLegIndList[x]].acInd != acInd)
			//	break;

			//if leg has different crewpair than the first 2 crewpairs we want to create, skip - 08/13/08 ANG
			if( legList[acList[k].schedLegIndList[x]].demandID > 0 &&
				legList[acList[k].schedLegIndList[x]].crewPairInd != firstCrPrInd &&
				legList[acList[k].schedLegIndList[x]].crewPairInd != lastCrPrInd ){
				x++;
				break;
			}

			//if this is a demand leg, add index to list, and add early/late penalties to cost. determine numPax for flight time calc
			lg = acList[k].schedLegIndList[x];
			if(legList[lg].demandID > 0){
				found = 0;
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[k].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[k].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[k].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[k].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[k].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[k].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[k].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							numPax = demandList[i].numPax;
							d++;

							//Collect information for 1st leg flown by 2nd crewpair - 05/02/08 ANG
							if (update == 1 && acList[k].schedCrPrIndList[x] != firstCrPrInd && acList[k].schedCrPrIndList[x] == lastCrPrInd){
								firstAptID2 = legList[acList[k].schedLegIndList[x]].outAirportID;  //first airport for 2nd crewpair
								firstTime2 = legList[acList[k].schedLegIndList[x]].schedOut; // first outtime for 2nd crewpair
								lastAptID2 = legList[acList[k].schedLegIndList[x-1]].inAirportID; // last airport for 1st crewpair
								lastTime2 = legList[acList[k].schedLegIndList[x-1]].schedIn; // last intime for 1st crewpair
								update = 0;
							}
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
			else{
				numPax = 0;
				repoIndicator = 1; //10/22/09 ANG
				repoLegInd = lg; //03/31/10 ANG
			}
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
			
			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus;
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else{
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			}
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){ //fei Jan 2011
			incl = curInclInd ; //fei Jan 2011: start from the next inclusion 
			while(incl < acList[acInd].numIncl ){ //fei Jan 2011
				for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; i ++)
				{
					//z = acList[acInd].inclDemandInd[incl];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[i];//fei Jan 2011: new index

					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					{
						//curInclInd = incl; //fei Jan 2011
						break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
						//fei Jan 2011: note: break, assume copies are sorted
					}
					
					if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn)
					{
						//incl++;//fei Jan 2011
						continue;
					}
					//if this is first leg for the crew
					if(prevLegInd == -1){
						//if crew is picking up plane when next available
						if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
							//incl++;//fei Jan 2011
							continue;
						}
						//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
						if(exgTourList[numExgTours].pickupType != 1)
						{
							curInclInd = incl; //fei Jan 2011
							incl = acList[acInd].numIncl ; //fei Jan 2011
							break;
						}
					}

					//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
					if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
						//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
						if(d == 0){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
						}
						else{
							exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
							exgTourList[numExgTours].demandInd[d-1]= z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
						}
						d++;

						//curInclInd = incl ; //fei Jan 2011: keep current inclusion index
						curInclInd = incl + 1 ; //fei Jan 2011: keep current inclusion index

						break; //fei Jan 2011: one copy per inclusion, break out of inner loop

					}
				}//for(i=0
				incl++;
			}
			prevLegInd = lg;
			x++;
		} //end while(acList[k].schedLegIndList[x] > -1)

		//START - we dont need to populate dropoff info but need to check other covered airport assignment - 11/11/09 ANG
		lastAptID = legList[acList[k].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[acList[k].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = acList[k].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[firstCrPrInd].crewPairID && legList[y].crewPairID != crewPairList[lastCrPrInd].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				if(legList[acList[k].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
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
					exgTourList[numExgTours].dropoffInd = acList[k].schedLegIndList[x-1];
					repoConnxnList[numRepoConnxns] = exgTourList[numExgTours].dropoffInd;
					numRepoConnxns ++;
				}
				//check if appointment / maintenance leg is covered after first crew's tour (before second crew's)
				//while(incl <= acList[k].lastIncl[optParam.planningWindowDuration - 1]){	//app Jan 2011
				
				incl = curInclInd ; //fei Jan 2011: start from the next inclusion 
				while(incl < acList[k].numIncl ){	//fei Jan 2011
					for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; i ++)
					{
						//z = acList[k].inclInfoP->origDemIndices[incl];
						z = origDemInfos[acList[k].inclInfoP->origDemIndices[incl]].indices[i];//fei Jan 2011: new index

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						{
							//curInclInd = incl; //fei Jan 2011
							break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
							//fei Jan 2011: note: break, assume copies are sorted
						}

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime)
						{
							//incl++;//fei Jan 2011
							continue;
						}
						//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
						//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
						if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
							((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++;
							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
							crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
							demIndPUAM[numMaintPU] = z;
							numMaintPU++;
							//don't break here, because there may be more than one appoint leg between tours

							//curInclInd = incl ; //fei Jan 2011: keep current inclusion index
							curInclInd = incl + 1; //fei Jan 2011: keep current inclusion index

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop
						}
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

			//RLZ CHECK Restrict the condition to drop the planes	
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 


			//10/10/2008 RLZ: could change adjSchedIn to schedIn
			if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[acList[k].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || acList[k].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}
	
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			//for(y=0; y<=acList[k].lastIncl[optParam.planningWindowDuration - 1]; y++){ //app Jan 2011
			for(y=0; y < acList[k].numIncl ; y++){ //fei Jan 2011
				for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; i ++)
				{
					//z = acList[k].inclInfoP->origDemIndices[y];
					z = origDemInfos[acList[k].inclInfoP->origDemIndices[y]].indices[i];//fei Jan 2011: new index

					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++;
							if(crewPairList[cp].endRegDay != PAST_WINDOW || acList[k].schedLegIndList[x] != -1 || 
								optParam.prohibitStealingPlanes == 0 ||
								(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

								exgTourList[numExgTours].dropoffType= 3;
								exgTourList[numExgTours].dropoffInd = z;
							}

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop

					}
				}//end for(i=0;
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}
		//END - 11/11/09 ANG

		//If no aircraft switch, return without calculating costs incurred by crewpairs - 06/13/08 ANG
		//But here there will be no aircraft switch, this case is for 1 aircraft, 2 crewpairs - 11/10/09 ANG
		if(update == 1){
			numExgTours++;
			continue; //Retain more exg sol - 08/11/08 ANG
		}

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
						//dutyStartTm = firstTime - 60*optParam.firstPreFltTm; //Need to set dutyStartTm here - 05/02/08 ANG
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
			//earlyDpt = lastTime+ optParam.finalPostFltTm*60;
			earlyDpt = lastTime2+ optParam.finalPostFltTm*60; //if crew is no longer on duty and is sent home from lastAptID2 at lastTime2 - 05/02/08 ANG
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				//if crew needs to be in lastCrPrInd, do not count get home cost - 05/01/08 ANG
				if (crewPairList[cp].crewListInd[c] == crewPairList[lastCrPrInd].crewListInd[0] ||
					crewPairList[cp].crewListInd[c] == crewPairList[lastCrPrInd].crewListInd[1] ){
						continue;
				}

				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID2, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID2, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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

		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[acList[k].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[acList[k].schedLegIndList[x-1]].adjSchedIn; //legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[acList[k].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours

		cp = lastCrPrInd;
		exgTourList[numExgTours].crewPairInd2 = lastCrPrInd;
		exgTourList[numExgTours].cost += - crewPairList[cp].crewPairBonus;

		//for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
		for(y = acList[k].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
				//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
				if(legList[acList[k].schedLegIndList[x-1]].demandID > 0){  //if last leg for crewPair is a demand leg, drop off at end of demand leg
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
				//while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){ //app Jan 2011
				
				incl = curInclInd ; //fei Jan 2011
				while( incl < acList[acInd].numIncl ){ //fei Jan 2011
					for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; i ++)
					{
						//z = acList[k].inclInfoP->origDemIndices[incl];
						z = origDemInfos[acList[k].inclInfoP->origDemIndices[incl]].indices[i];//fei Jan 2011: new index

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						{
							//curInclInd = incl; //fei Jan 2011
							break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
							//fei Jan 2011: note: break, assume copies are sorted
						}

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime)
						{
							//incl++; //fei Jan 2011	
							continue;
						}
						//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
						//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
						if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
							((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							exgTourList[numExgTours].demandInd[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++;
							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
							crewPrIDPUAM[numMaintPU] = legList[y].crewPairID; // 03/19/08 ANG
							demIndPUAM[numMaintPU] = z;
							numMaintPU++;
							//don't break here, because there may be more than one appoint leg between tours

							//curInclInd = incl ; //fei Jan 2011: keep current inclusion index
							curInclInd = incl + 1; //fei Jan 2011: keep current inclusion index

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop
						}
					}//end for(i=0; 

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
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			//	|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){

			//RLZ restrict to drop planes only on the last day 
			//if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 

			if(legList[acList[k].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400 
				|| optParam.prohibitStealingPlanes == 0 )){ //assume in this kind of case, crewPair will not switch airplane again
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}



			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			//for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){ //app Jan 2011
			for(y=0; y < acList[acInd].numIncl ; y++){ //fei Jan 2011

				for(i=0; i < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; i ++)
				{
					//z = acList[acInd].inclInfoP->origDemIndices[y];
					z = origDemInfos[acList[k].inclInfoP->origDemIndices[y]].indices[i];//fei Jan 2011: new index

					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
						exgTourList[numExgTours].demandInd[d] = z;
						repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
						repoLegInd = -1; //03/31/10 ANG
						d++;
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
						//if(crewPairList[cp].endRegDay != PAST_WINDOW || //RLZ
						if( z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] || 
							optParam.prohibitStealingPlanes == 0 ||
							(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
						}

						break; //fei Jan 2011: one copy per inclusion, break out of inner loop

					}
				}//end for(i=0; 
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType == 0){
				exgTourList[numExgTours].dropoffType= 1;
				exgTourList[numExgTours].dropoffInd = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//START - Cost for 2nd crewPair
		//Note: for 2nd pair, it starts in firstAptID2 at firstTime2 and ends in lastAptID at lastTime
		for(c = 0; c<2; c++){
			//if there is an overlapping crew in firstCrPrInd and lastCrPrInd, do not double count the cost- 05/01/08 ANG
			if (crewPairList[cp].crewListInd[c] == crewPairList[firstCrPrInd].crewListInd[0] ||
				crewPairList[cp].crewListInd[c] == crewPairList[firstCrPrInd].crewListInd[1] ){
					continue;
			}
			crewListInd = crewPairList[cp].crewListInd[c];
			//determine if we must calculate overtime for pilot (pilot hasn't started tour and has volunteered for OT)
			if(crewList[crewListInd].overtimeMatters == 1){
				//If must travel to plane, travel that day if possible, else day before.  
				//Add cost of any travel to plane, and calculate dutyStartTime
				if(crewList[crewListInd].availAirportID != firstAptID2){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					//calculate earliest departure time from home
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					if(!getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime2 - 60*optParam.firstPreFltTm;
					}
					//Note: We don't need firstDutyEnd for second or first crewpair, the same is used since we have an overlaping crew
					//for latest flight to plane, check that duty hours aren't exceeded, and update cost (keep dutyStartTm as set by function)
					else if((int)((firstDutyEnd - dutyStartTm)/60) < optParam.maxDutyTm)
					{
						exgTourList[numExgTours].cost += cost;
					}
					 //if duty hours are exceeded, look for flights the day before
					else if(!getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm)), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag)){
						//if can't find flight to plane, just assume feasible and assume zero travel time to plane
						exgTourList[numExgTours].cost += cost;
						dutyStartTm = firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm);
					}
					else
						exgTourList[numExgTours].cost += cost; //(keep dutyStartTm as set by function)
						//dutyStartTm = firstTime2 - 60*(optParam.firstPreFltTm + optParam.minRestTm); //Need to set dutyStartTm here - 05/02/08 ANG
				}
				else //pilot need not travel to plane; just calculate dutyStartTm
					dutyStartTm = firstTime2 - 60*optParam.firstPreFltTm;
				//Add any overtime cost for pilot.
				if(crewList[crewListInd].overtimeMatters == 1 && dutyStartTm < crewList[crewListInd].tourStartTm){
					halfDaysOT = 1+ (int)(crewList[crewListInd].tourStartTm - dutyStartTm)/(12*3600);//integer division truncates
					exgTourList[numExgTours].cost += optParam.overTimeCost*halfDaysOT/2 + (optParam.overTimeHalfCost - optParam.overTimeCost/2)*(halfDaysOT%2);
				}
			}
			else { //just add cost of travel to plane
				if(crewList[crewListInd].availAirportID != firstAptID2){
					departTm = 0;
					dutyStartTm = 0;
					arrivalTm = 0;
					cost = 0.0;
					earlyDpt = (time_t)(crewList[crewListInd].tourStartTm - crewList[crewListInd].startEarly*86400);
					getCrewTravelDataLate(earlyDpt, (firstTime2 - 60*optParam.firstPreFltTm), crewList[crewListInd].availAirportID, firstAptID2, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag);
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
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost -= lastRepoCost;
			if(repoLegInd >= 0) //Indicate leg that dont need to be written to output if exgTour got picked - 03/31/10 ANG
				legList[repoLegInd].exgTourException = 1;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

		fprintf(logFile, "Created additional existing tour (1 aircraft, 2 crewpairs) with existing tour index = %d, aircraftID = %d.\n", numExgTours, acList[k].aircraftID);
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
*	Function   buildAddlExistingTours2FA	Date last modified:	03/14/11 fei FA	*
*	Purpose:   create additional existing tours from crpr with multi aircraft   *
********************************************************************************/
int buildAddlExistingTours2FA(void)
{ 
	int cp, x, y, z, lg, i, b, d, j, found, incl, numPax, acInd,  firstAptID, lastAptID, prevLegInd, c, halfDaysOT, crewListInd;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	time_t firstTime, lastTime, firstDutyEnd, earlyDpt, lateArr, departTm, dutyStartTm, arrivalTm;
	int *crewPrIDPUAM; //array of crewPair IDs for crewPairs that pickup plane after maintenance or other airport appt
	int *demIndPUAM;  //array of demand indices corresponding to above maintenance or airport appts
	int numMaintPU = 0;
	double straightHmCst, getHomeCost, cost;
	time_t crewGetHomeTm[2];
	extern int numCrewPairs;
	int x2, acInd2, firstAptID2, lastAptID2;
	time_t firstTime2, lastTime2;
	double oprCost; //MAC - 08/19/08 ANG
	int skip; //08/28/09 ANG
	int repoIndicator; double lastRepoCost;// exclude last repo to nothing - 10/22/09 ANG
	int repoLegInd; //used to exclude repo-to-nothing leg being written to output - 03/31/10 ANG

	int curInclInd; //fei Jan 2011
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
		repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
		repoLegInd = -1; //03/31/10 ANG
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
				//>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
				>(optParam.minRestTm+optParam.postFlightTm+acTypeList[crewPairList[cp].acTypeIndex].preFlightTm)){ //07/17/2017 ANG
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
						exgTourList[numExgTours].acInd = acInd;  //08/13/08 ANG
						//exgTourList[numExgTours].acInd = -1;  //for LP/MIP constraints, we are picking up a generic plane from the fleet
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
		prevLegInd = -1;

		incl = 0;
		curInclInd = 0 ; //fei Jan 2011
		//add demand indices and cost of each leg to exgTour, and check if any appointment / maintenancelegs are covered
		while(crewPairList[cp].schedLegIndList[x] > -1) {
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

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
					//exgTourList[numExgTours].cost -= optParam.macBonus;
					exgTourList[numExgTours].cost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
			}
			else
				exgTourList[numExgTours].cost +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			incl = curInclInd ;  //fei Jan 2011
			while(incl < acList[acInd].numIncl ){ //fei Jan 2011
				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)//fei Jan 2011
				{
				//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){
					//z = acList[acInd].inclInfoP->origDemIndices[incl];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: new index

					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					{
						//curInclInd = incl; //fei Jan 2011
						break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
						//fei Jan 2011: note: break, assume copies are sorted
					}

					if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn)
					{
						//incl++; //fei Jan 2011: new index
						continue; 
					}
					//if this is first leg for the crew
					if(prevLegInd == -1){
						//if crew is picking up plane when next available
						if(exgTourList[numExgTours].pickupType == 1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
							//incl++; //fei Jan 2011
							continue;
						}
						//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
						if(exgTourList[numExgTours].pickupType != 1)
						{
							curInclInd = incl; //fei Jan 2011
							//incl = acList[acInd].lastIncl[optParam.planningWindowDuration - 1] + 1 ; //fei Jan 2011: break outer loop
							incl = acList[acInd].numIncl ; //fei Jan 2011: break outer loop
							break;
						}
					}
					//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
					if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
						//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
						if(d == 0)
							exgTourList[numExgTours].demandInd[d] = z;
						else{
							exgTourList[numExgTours].demandInd[d] = exgTourList[numExgTours].demandInd[d-1];
							exgTourList[numExgTours].demandInd[d-1]= z;
						}
						d++;

						//curInclInd = incl; //fei Jan 2011
						curInclInd = incl + 1; //fei Jan 2011
						break; //fei Jan 2011: one copy per inclusion, break out of inner loop

					}
				}//end for(c=0;  //fei Jan 2011
				incl++;
			}
			//end checking maintenance

			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)

		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//3a. POPULATE DROPOFF INFO - 1st Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours

		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
				//while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){ //fei Jan 2011

				incl = curInclInd ; //fei Jan 2011
				while(incl < acList[acInd].numIncl ){ //fei Jan 2011
					for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)//fei Jan 2011
					{
						
						//z = acList[acInd].inclInfoP->origDemIndices[incl];
						z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: new demand index

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						{
							//curInclInd = incl; //fei Jan 2011
							break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
							//fei Jan 2011: note: break, assume copies are sorted
						}

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime)
						{
							//incl++; //fei Jan 2011
							continue;
						}
						//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
						//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
						if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
							//(demandList[z].reqOut + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							exgTourList[numExgTours].demandInd[d] = z;
							d++;
							exgTourList[numExgTours].dropoffType= 3;
							exgTourList[numExgTours].dropoffInd = z;
							crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
							demIndPUAM[numMaintPU] = z;
							numMaintPU++;
							//don't break here, because there may be more than one appoint leg between tours

							//curInclInd = incl; //fei Jan 2011
							curInclInd = incl + 1; //fei Jan 2011
							break; //fei Jan 2011: one copy per inclusion, break out of inner loop

						}
					}//end for(c=0
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
			//RLZ
			//if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && 
				((firstEndOfDay + crewPairList[cp].endRegDay * 86400 - legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn) < 86400
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType= 3;
				exgTourList[numExgTours].dropoffInd = exgTourList[numExgTours].demandInd[d-1];
			}		
				
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			//for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
			for(y=0; y < acList[acInd].numIncl ; y++){
			
				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; c ++)//fei Jan 2011
				{
					
					//z = acList[acInd].origDemIndices[y];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].indices[c] ;//fei Jan 2011

					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime){
							exgTourList[numExgTours].demandInd[d] = z;
							d++;
							//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 || 
							if(z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)]|| crewPairList[cp].schedLegIndList[x] != -1 ||
								optParam.prohibitStealingPlanes == 0 ||
								(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

								exgTourList[numExgTours].dropoffType= 3;
								exgTourList[numExgTours].dropoffInd = z;
							}

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop

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
		//if there is no existing (unlocked) tour for this crewPair, move to next crewPair
		x = x2;
		//x = 0;
		while(crewPairList[cp].schedLegIndList[x] > -1){//
			if(legList[crewPairList[cp].schedLegIndList[x]].inLockedTour == 1)
				x++;
			else
				break;
		}

		if(crewPairList[cp].schedLegIndList[x]==-1)
			continue;

		//determine end of first duty day of existing tour
		//y = x+1;
		//while(crewPairList[cp].schedLegIndList[y] > -1){
		//	//if crewPair has time to rest between legs
		//	if((int)((legList[crewPairList[cp].schedLegIndList[y]].schedOut - legList[crewPairList[cp].schedLegIndList[y-1]].schedIn)/60)
		//		>(optParam.minRestTm+optParam.postFlightTm+optParam.preFlightTm)){
		//		break;
		//	}
		//	y++;
		//}
		//firstDutyEnd2 = legList[crewPairList[cp].schedLegIndList[y-1]].schedIn + optParam.postFlightTm*60;

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
		curInclInd = 0 ; //fei Jan 2011
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
				repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
				repoLegInd = -1; //03/31/10 ANG

				//START - Check infeasibility of scheduled trips - 08/28/09 ANG
				skip = 0;
				//1. Infeasible sequence positions
			    if(acTypeList[acList[acInd].acTypeIndex].sequencePosn < demandList[legList[lg].demandInd].sequencePosn){
					fprintf(logFile, "Infeasible Sequence demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
					//skip = 1; 
				}
				//2. Demand-Aircraft exclusion and Aircraft-State exclusion and Aircraft-Country exclusion (excl types 4 & 5 & 6) violation
				else if (acList[acInd].lastExcl[optParam.planningWindowDuration-1] >= 0){
					for(b=0; b <= acList[acInd].lastExcl[optParam.planningWindowDuration-1]; b++){
						if(legList[lg].demandInd == acList[acInd].exclDemandInd[b]){
							fprintf(logFile, "Exclusion violation for demandid %d, acID: %d \n", legList[lg].demandID, acList[acInd].aircraftID);
							skip = 1;
							break;
						}
					}
				}
				//3. Possibly checking curfew exclusions here
				//4. Possibly checking airport exclusions here

				if(skip == 1 && legList[lg].planeLocked == 0){
					fprintf(logFile, "Exgtour generation is stopped for acID %d at demandID %d \n", acList[acInd].aircraftID, legList[lg].demandID);
					legList[lg].dropped = 1;
					break;
				}
				//END - Check infeasibility of scheduled trips - 08/28/09 ANG

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
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
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
			else{
				numPax = 0;
				repoIndicator = 1;
				repoLegInd = lg; //03/31/10 ANG
			}
			//add operating and taxi cost for this leg (assume no missing repo legs per Bill Hall 6/19)
			j = acList[acInd].acTypeIndex;
			getFlightTime(legList[lg].outAirportID, legList[lg].inAirportID, 
				acTypeList[j].aircraftTypeID, month, numPax, &flightTm, &blockTm, &elapsedTm, &numStops);

			//START - MAC - 08/19/08 ANG
			if(optParam.withMac == 1){
				oprCost = 0.0;
				//commented out and replaced by new codes below - MAC - 09/21/10 ANG
				/*if ( acList[acInd].isMac == 1 && 
					(demandList[legList[lg].demandInd].isMacDemand == 0 || 
					(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID != acList[acInd].aircraftID)))
					oprCost = acList[acInd].macDOC;
					//oprCost = acTypeList[j].macOprCost; //MacDOC per MAC - 05/20/2009 ANG
				else if (acList[acInd].isMac == 1 && demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC - MAC - 01/25/10 ANG
					oprCost = 0.8 * acTypeList[j].operatingCost;
				*/
				//START - MAC - 09/21/10 ANG
				//if this is any live leg on Mac
				if ( acList[acInd].isMac == 1 )
					if (demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID)
						oprCost = acTypeList[j].operatingCost;
					else if (demandList[legList[lg].demandInd].ownerID == 87359) //Contingency demands on MAC have lower DOC
						oprCost = 0.8 * acTypeList[j].operatingCost;
					else
						oprCost = acList[acInd].macDOC;
				//END - MAC - 09/21/10 ANG
				else 
					oprCost = acTypeList[j].operatingCost;
				exgTourList[numExgTours].cost2 +=(flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*oprCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				//calculate mac bonus
				if(demandList[legList[lg].demandInd].isMacDemand == 1 && demandList[legList[lg].demandInd].macID == acList[acInd].aircraftID){
					//exgTourList[numExgTours].cost2 -= optParam.macBonus;
					exgTourList[numExgTours].cost2 -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
					if(repoIndicator == 1)
						//lastRepoCost -= optParam.macBonus; //10/22/09 ANG
						lastRepoCost -= (optParam.useSeparateMacBonus == 0) ? optParam.macBonus : macInfoList[acList[acInd].macIndex].macBonus; //Mac bonus per MAC - 12/14/10 ANG
				}
			}
			else{
				exgTourList[numExgTours].cost2 +=(flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
				if(repoIndicator == 1)
					lastRepoCost = (flightTm*acTypeList[j].operatingCost)/60 + (numStops+1)*acTypeList[j].taxiCost;
			}
			//END - MAC - 08/19/08 ANG

			//revenue information - 06/18/2009 ANG
			if (legList[lg].demandInd >= 0){
				exgTourList[numExgTours].cost2 -= (demandList[legList[lg].demandInd].incRev[j] ? demandList[legList[lg].demandInd].incRev[j] : 0);
			}

			//check if an appointment/maintenance leg can be covered between this and previous leg, ignoring turn time
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){
			incl = curInclInd; //fei Jan 2011
			while(incl < acList[acInd].numIncl ){ //fei Jan 2011
			//while(incl <= acList[acInd].lastIncl[optParam.planningWindowDuration - 1] && switchAircraftFlag == 0){

				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)//fei Jan 2011
				{
					//z = acList[acInd].origDemIndices[incl];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: new index

					if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[lg].schedOut)
					{	
						//curInclInd = incl; //fei Jan 2011
						break;  //fei Jan 2011: note: inclusion is after the second demand, break inner loop
						//fei Jan 2011: note: break, assume copies are sorted
					}

					if(prevLegInd > -1 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < legList[prevLegInd].schedIn){
						//incl++; //fei Jan 2011
						continue; 
					}

					//at the start of aircraft switch, we don't need to consider appointment/mx - 05/07/08 ANG
					if(prevLegInd == -1 && exgTourList[numExgTours].demandInd2[0] == -1){ 
						//incl++;  //fei Jan 2011
						continue; //fei Jan 2011: note: break
					}
					//if this is first leg for the crew
					if(prevLegInd == -1){
						//if crew is picking up plane when next available
						//if(exgTourList[numExgTours].pickupType2 == 1 && demandList[z].reqOut < (acList[acInd].availDT - optParam.turnTime*60)){
						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < (acList[acInd].availDT - optParam.turnTime*60)){
							//incl++;  //fei Jan 2011
							continue;
						}
						//if crew is picking up plane from another crew, then we will assume that any appointment leg before first leg is covered in other crew's tour
						//if(exgTourList[numExgTours].pickupType2 != 1)
						//	break;
					}
					//if inclusion for plane is appointment leg at this airport and timing is right, assume appointment leg is covered
					if(demandList[z].isAppoint &&  demandList[z].inAirportID == legList[lg].outAirportID &&((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[lg].schedOut){
						//insert into list of demand legs BEFORE last demand (maintain last demand for use in populating drop-off info below
						//if(d2 == 0)
						if(d == 0){
							exgTourList[numExgTours].demandInd2[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
						}
						else{
							exgTourList[numExgTours].demandInd2[d] = exgTourList[numExgTours].demandInd2[d-1];
							exgTourList[numExgTours].demandInd2[d-1]= z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
						}
						d++; //d2++;

						//curInclInd = incl; //fei Jan 2011
						curInclInd = incl + 1; //fei Jan 2011

						break; //fei Jan 2011: one copy per inclusion, break out of inner loop
					}
				}//end for(c=0; c

				incl++;
			}
			//end checking maintenance

			prevLegInd = lg;
			x++;
		} //end while(crewPairList[cp].schedLegIndList[x] > -1)


		if (x == 0){ //RLZ: To make sure there is a leg in previous loop. If the loop "breaks", x could be 0. skip case. 09/01/2009
			exgTourList[numExgTours].cost2 += SMALL_INCENTIVE; //for not picking this column;
			crewPairList[cp].exgTrInd = numExgTours; //05/16/08 ANG
			numExgTours++;
			continue;
		}

		//3b. POPULATE DROPOFF INFO - 2nd Aircraft
		//populate drop off info for plane...
		//determine if there is a later leg for the plane
		lastAptID2 = legList[crewPairList[cp].schedLegIndList[x-1]].inAirportID;
		//lastTime2 = legList[crewPairList[cp].schedLegIndList[x-1]].adjSchedIn;//legList[crewPairList[cp].schedLegIndList[x-1]].schedIn;
		lastTime2 = legList[crewPairList[cp].schedLegIndList[x-1]].schedIn; 
		//10/10/2008 RLZ restore this piece back to schedIn while deal with existing tours
		for(y = crewPairList[cp].schedLegIndList[x-1] + 1; y <= numLegs; y++){
			//if(legList[y].acInd == acInd){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane - replaced by the following row - 03/11/09 ANG
			if(legList[y].acInd == acInd && legList[y].crewPairID > 0 && legList[y].crewPairID != crewPairList[cp].crewPairID){ //if there is a later leg for this plane (with another crew) in the exg solution, crewPair must drop plane
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
				//while(incl<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]){

				incl = curInclInd ; //fei Jan 2011
				while(incl < acList[acInd].numIncl ){ //fei Jan 2011
					for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].numInd; c ++)//fei Jan 2011
					{
						//z = acList[acInd].origDemIndices[incl];
						z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[incl]].indices[c] ; //fei Jan 2011: new index

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= legList[y].schedOut)
						{
							//break;//fei Jan 2011
							break;
						}

						if((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) < lastTime2)
						{
							//incl++; //fei Jan 2011
							continue;
						}

						//if inclusion for plane is appoint/maint leg at this airport and timing is right, assume appoint leg is covered
						//and plane is dropped off at end (and will be picked up by next crew at end).  Reset dropoff info for tour.
						if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID && 
							((demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) + demandList[z].elapsedTm[j] * 60) < legList[y].schedOut){
							exgTourList[numExgTours].demandInd2[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++; //d2++;
							exgTourList[numExgTours].dropoffType2= 3;
							exgTourList[numExgTours].dropoffInd2 = z;
							crewPrIDPUAM[numMaintPU] = legList[y].crewPairID;
							demIndPUAM[numMaintPU] = z;
							numMaintPU++;
							//don't break here, because there may be more than one appoint leg between tours

							curInclInd = incl + 1; //fei Jan 2011

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop

						}
					}//end for(c=0;
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
			//RLZ
		//	if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (crewPairList[cp].endRegDay != PAST_WINDOW 
			if(legList[crewPairList[cp].schedLegIndList[x-1]].demandID > 0 && (exgTourList[numExgTours].demandInd[d-1] > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] 
				|| optParam.prohibitStealingPlanes == 0 || crewPairList[cp].schedLegIndList[x] != -1)){
				exgTourList[numExgTours].dropoffType2= 3;
				exgTourList[numExgTours].dropoffInd2 = exgTourList[numExgTours].demandInd2[d-1];
			}
			//check if last leg for crewPair leaves plane at scheduled maintenance / appointment location. If so, cover maintenance. And, drop plane if any of three
			//any of four criteria listed above are met.
			//for(y=0; y<=acList[acInd].lastIncl[optParam.planningWindowDuration - 1]; y++){
			for(y=0; y < acList[acInd].numIncl ; y++){

				for(c=0; c < origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].numInd; c ++)//fei Jan 2011
				{
					//z = acList[acInd].origDemIndices[y];
					z = origDemInfos[acList[acInd].inclInfoP->origDemIndices[y]].indices[c] ; //fei Jan 2011: new index

					if(demandList[z].isAppoint && demandList[z].outAirportID == lastAptID2 && (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut) >= lastTime2){
							exgTourList[numExgTours].demandInd2[d] = z;
							repoIndicator = 0; lastRepoCost = 0.0; //10/22/09 ANG
							repoLegInd = -1; //03/31/10 ANG
							d++; //d2++;
							//if(crewPairList[cp].endRegDay != PAST_WINDOW || crewPairList[cp].schedLegIndList[x] != -1 ||
							if(z > lastTripOfDay[max(0, crewPairList[cp].endRegDay - 1)] || crewPairList[cp].schedLegIndList[x] != -1 || 
								optParam.prohibitStealingPlanes == 0 ||
								(int)(demandList[z].reqIn - (demandList[z].isAppoint == 2? demandList[z].reqOut_actual : demandList[z].reqOut))/60 > optParam.maintTmForReassign){

								exgTourList[numExgTours].dropoffType2= 3;
								exgTourList[numExgTours].dropoffInd2 = z;
							}

							break; //fei Jan 2011: one copy per inclusion, break out of inner loop

					}
				}//end for
			}
			//if we still haven't set dropoffType, assume plane is not dropped off by crewPair
			if(exgTourList[numExgTours].dropoffType2 == 0){
				exgTourList[numExgTours].dropoffType2 = 1;
				exgTourList[numExgTours].dropoffInd2 = -1;
			}
		} //end if(if(exgTourList[numExgTours].dropoffType == 0) (OUTER LOOP)

		//Adjust cost to exclude last repo if it is a repo to nothing - 10/22/09 ANG
		if(repoIndicator == 1 && lastRepoCost > 0.0){
			exgTourList[numExgTours].cost2 -= lastRepoCost;
			repoIndicator = 0; lastRepoCost = 0.0;
			repoLegInd = -1; //03/31/10 ANG
		}

		//4b. UPDATE CREW COST
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
			earlyDpt = lastTime2+ optParam.finalPostFltTm*60;
			//calculate get-home for crew members
			for(c = 0; c<2; c++){
				crewListInd = crewPairList[cp].crewListInd[c];
				if(crewList[crewListInd].endRegDay == PAST_WINDOW)
					continue;
				departTm = 0;
				dutyStartTm = 0;
				arrivalTm = 0;
				cost = 0.0;
				
				lateArr = (time_t)min((crewList[crewListInd].tourEndTm + crewList[crewListInd].stayLate*86400 + optParam.maxCrewExtension*60), optParam.windowEnd + 86400);
				if(!getCrewTravelDataEarly(earlyDpt, lateArr, lastAptID2, crewList[crewListInd].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag))
					crewGetHomeTm[c] = arrivalTm;
				else {//if we can't find a flight home set crewGetHomeTm to earlyDpt
					//(we assume exg solution is feasible)
					fprintf(logFile, "Existing solution warning: Crew %d cannot get home from airportID %d to base %d \n", crewList[crewListInd].crewID, lastAptID2, crewList[crewListInd].endLoc);
					crewGetHomeTm[c] = earlyDpt;
				}
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
		fprintf(logFile, "Created additional existing tour (2 aircraft, 1 crewpair) with existing tour index = %d, crewPairID = %d.\n", numExgTours, crewPairList[cp].crewPairID);
		crewPairList[cp].exgTrInd = numExgTours; // 05/16/08 ANG
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

//up test, return 1 if tour is not empty after deleting the dem
int deleteDemFromExistTour(const int acInd, const int pickupType, const int pickupInd, const int dropoffType, const int dropoffInd, int *demandInd
, double *costP)
{
	double cost=0;
	int typeInd, k, pInd, cInd, slot, pApt, nApt, end, firstApt=0, lastApt=0 ;
	int flightTm= 0, blockTm = 0, elapsedTm= 0, numStops= 0;
	const int notDropOff = -1 ;

	_ASSERTE( demandInd[0] >= 0 ) ;
	typeInd = acList[acInd].acTypeIndex ;
	switch ( pickupType )//get the ac avilable airport
	{
		case 1:
			firstApt = acList[acInd].availAirportID;
			break;
		case 2:
			_ASSERTE( pickupInd >= 0 );
			firstApt = demandList[pickupInd].outAirportID;
			break;
		case 3:
			_ASSERTE( pickupInd >= 0 );
			firstApt = demandList[pickupInd].inAirportID;
			break;
		case 4:
			_ASSERTE( legList != NULL &&  pickupInd >= 0 && pickupInd < numLegs );
			firstApt = legList[pickupInd].inAirportID;
			break;
		default:
			_ASSERTE( 1==0);
	}

	switch ( dropoffType )//get the last airport of this tour
	{
		case 1:
			lastApt = notDropOff; //not dropped off
			break;
		case 2:
			_ASSERTE( dropoffInd >= 0 );
			lastApt = demandList[dropoffInd].outAirportID;
			break;
		case 3:
			_ASSERTE( dropoffInd >= 0 );
			lastApt = demandList[dropoffInd].inAirportID;
			break;
		case 4:
			_ASSERTE( legList != NULL && dropoffInd >= 0 && dropoffInd < numLegs );
			lastApt = legList[dropoffInd].inAirportID;
			break;
		default:
			_ASSERTE( 1==0);
	}

	pInd = -1 ;//previous dem ind in the original exgTourList[i].demandInd
	slot = 0; //index in exgTourList[i].demandInd to add the current demand: cInd
	cost = 0; //add cost
	for(end=0; end < MAX_LEGS ; end ++)
		if( demandInd[end] < 0 )
			break;

	for(cInd=0; cInd < end ; cInd ++)
	{
		if( ( demandList[demandInd[cInd]].upRestrict == 2 && demandList[demandInd[cInd]].sequencePosn >= acTypeList[typeInd].sequencePosn)
		|| ( demandList[demandInd[cInd]].upRestrict == 1 && demandList[demandInd[cInd]].sequencePosn != acTypeList[typeInd].sequencePosn) )
			continue;//this demand will be deleted

		if( pInd + 1 < cInd )//demands between pInd and cInd are deleted, calculate the cost of legs between pInd and cInd
		{
			pApt = ( pInd >= 0 ? demandList[demandInd[pInd]].inAirportID : firstApt );
			nApt = demandList[demandInd[cInd]].outAirportID ;

			if (pApt != nApt )//new repo leg from pApt to current demand, add this cost
			{
				getFlightTime(pApt, nApt, acTypeList[typeInd].aircraftTypeID, month, 0, &flightTm, &blockTm, &elapsedTm, &numStops);
				cost += ((flightTm*acTypeList[typeInd].operatingCost)/60 + (numStops+1)*acTypeList[typeInd].taxiCost);
			}
			for( k = pInd; k < cInd; k ++ )//cost of each skipped leg, substract  this cost
			{
				//pApt is the previous airport
				if( pApt != (nApt = demandList[demandInd[k+1]].outAirportID ))//repo leg between demand k and k+1
				{
					getFlightTime(pApt, nApt, acTypeList[typeInd].aircraftTypeID, month, 0, &flightTm, &blockTm, &elapsedTm, &numStops);
					cost -= ((flightTm*acTypeList[typeInd].operatingCost)/60 + (numStops+1)*acTypeList[typeInd].taxiCost);
				}
				if( k > pInd //cost of demand k
				&& (pApt=demandList[demandInd[k]].outAirportID) != (nApt=demandList[demandInd[k]].inAirportID))
				{
					getFlightTime(pApt, nApt, acTypeList[typeInd].aircraftTypeID, month, demandList[demandInd[k]].numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
					cost -= ((flightTm*acTypeList[typeInd].operatingCost)/60 + (numStops+1)*acTypeList[typeInd].taxiCost);
				}
				pApt = demandList[demandInd[k]].inAirportID; //update airport
			}//end for( k = pInd; k < cInd; k ++ )//substract cost of each skipped leg
		}//end if( pInd + 1 < cInd )

		demandInd[slot] = demandInd[cInd] ;//move demand ind
		slot ++;
		pInd = cInd ;//index in the orig demandInd list
	}//for(cInd=0; cInd < MAX_LEGS, exgTourList[i].demandInd[cInd] >= 0 ; cInd ++)
	
	if( pInd >= 0 && pInd + 1 < end )//calculate legs after pInd //pInd is the last kept demand in the orig dem list
	{
		_ASSERTE( slot > 0 && demandInd[pInd] >= 0 && demandInd[pInd] == demandInd[slot-1] );
		for( k = pInd; k < end; k ++ )//substract cost of each skipped leg
		{
			pApt = demandList[demandInd[k]].inAirportID ;
			nApt = (k + 1 < end ? demandList[demandInd[k+1]].outAirportID : lastApt) ; 
			
			if( nApt != notDropOff && pApt != nApt )//repo leg after demand k
			{
				getFlightTime( pApt, nApt, acTypeList[typeInd].aircraftTypeID, month, 0, &flightTm, &blockTm, &elapsedTm, &numStops);
				cost -= ((flightTm*acTypeList[typeInd].operatingCost)/60 + (numStops+1)*acTypeList[typeInd].taxiCost);
			}

			//demand k
			if( k > pInd && (pApt = demandList[demandInd[k]].outAirportID) != (nApt = demandList[demandInd[k]].inAirportID))
			{
				getFlightTime( pApt, nApt, acTypeList[typeInd].aircraftTypeID, month, demandList[demandInd[k]].numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
				cost -= ((flightTm*acTypeList[typeInd].operatingCost)/60 + (numStops+1)*acTypeList[typeInd].taxiCost);
			}
		}//end for( k = pInd; k < cInd; k ++ )//substract cost of each skipped leg
	}

	if( slot >0 ) 
	{
		for( k = slot; k < end; k ++ )
			demandInd[k] = -1 ;//reset
		(*costP) += cost;
		return 1 ;
	} 
	return 0 ;
}