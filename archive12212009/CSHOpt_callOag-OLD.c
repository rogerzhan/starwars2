#include "os_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <string.h>
#include "datetime.h"
#include "logMsg.h"
#include "localTime.h"
#include "airportLatLon.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_define.h"
#include "CSHOpt_buildOagOD.h"
#include "CSHOpt_processInput.h" //for getflightTime
#include "memory.h"

#define DEBUGGING 1

extern FILE *logFile;
//extern Airport2 aptList[TOTAL_AIRPORTS_NUM + 1];
extern Airport2 *aptList;
extern struct optParameters optParam;
extern ODEntry *oDTable;
extern int *oDHashTable;
extern int numOAGCallsEarly;
extern int numOAGCallsLate;
extern int month;

//FUNCTION DECLARATIONS
static int queryOD(int origAptID, int destAptID, time_t earlyDpt, time_t lateArr, int earlyFlag, time_t *commDpt, time_t *commArr, double *cost);
static int getODHashIndex(int orig, int dest);
static int findODIndex(int orig, int dest, ODEntry *oDTable, int *oDHash); 
int getCrewTravelDataEarlyNoOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost);
int getCrewTravelDataLateNoOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost);
int getCrewTravelDataLateOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost);
int getCrewTravelDataEarlyOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost);
//static int blockedItinerary(int startAptID, int endAptID, time_t earlyDpt, time_t lateArr);

/********************************************************************************************************
*	Function	getCrewTravelDataEarlyOAG							Date last modified:  08/19/07 SWO	*
*	Purpose:	Start and end airports, earliest departure and latest arrival are sent to this 			*
*		function.	Given the earliest departure time, the function returns the earliest arrival		*
*		time of the pilot at the destination airport, unless he can't get there before					*
*		the latest arrival time, in which case -1 is returned.											*
*		   For a pilot starting his tour, the function also returns the duty start time.  Duty			*
*		hours start at one hour before the scheduled departure of the commercial flight UNLESS there	*
*		is ground travel to the flight, in which case duty starts at start of ground travel.			* 															
*			Note that one or both ground travel legs may be unnecessary, or there might be ONLY			*
*		ground travel and no commercial flight.															*
*			This function also returns the total estimated cost of the ground travel and commercial		* 
*		flight legs between start airport and end airport.	OAG data is used in this function.											*																	
********************************************************************************************************/
int getCrewTravelDataEarlyOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost)
{
	int x, y, orig, dest;//, oDInd;
	//int z, canDrive;
	time_t tempArr;
	time_t commArr, commDpt;
	double commCost;

	numOAGCallsEarly ++;
	
	(*arrivalTm) = lateArr + 3600; //initialize at lateArr + one hour 
	commArr = (*arrivalTm); //initializing for call to queryOD function
	(*departTm) = earlyDpt; //initialize at earliest possible departure time
	commDpt = earlyDpt;  //initializing for call to queryOD function
	commCost = 0.0;
	(*cost) = 0.0;
	
	//check that specific itinerary has not been blocked by Travel Dept
	//if(blockedItinerary(startAptID, endAptID, earlyDpt, lateArr))
	//	return -1;

	//check if it is possible to travel by ground only
	x = 0;
	while(x < aptList[startAptID].numMaps){
		if(aptList[startAptID].aptMapping[x].airportID == endAptID){
			if((tempArr = (earlyDpt + 60*aptList[startAptID].aptMapping[x].duration))<(*arrivalTm)){
				(*departTm) = earlyDpt;
				(*dutyStartTm) = earlyDpt;
				(*arrivalTm) = tempArr;
				//(*cost) = min((*cost), aptList[startAptID].aptMapping[x].cost);
				//We could take the minimum cost for group travel, as long as the time is feasbile. RLZ 03/03/2008
				//But we opt to do it in the post-process to speed up the process
				(*cost) = aptList[startAptID].aptMapping[x].cost;
				break;
			}
		}
		x++;
	}
	if((*arrivalTm) <= lateArr)
		return 0;
	//if it was possible to travel by ground only (arrivalTm was updated) but that wasn't quick enough (from above)
	//then assume pilot can't get there on time
	if((*arrivalTm) < lateArr + 3600)
		return -1;

	//check all commercial itineraries
	x = 0;
	y = 0;
	while(x < aptList[startAptID].numMaps){
		if(aptList[aptList[startAptID].aptMapping[x].airportID].commFlag == 1 && aptList[startAptID].aptMapping[x].groundOnly == 0){
			orig = aptList[startAptID].aptMapping[x].airportID;
			while(y < aptList[endAptID].numMaps){
				//if mapped to commercial apt and not designated as "ground only", get commercial flights between orig and dest
				if(aptList[aptList[endAptID].aptMapping[y].airportID].commFlag == 1 && aptList[endAptID].aptMapping[y].groundOnly == 0){
					dest = aptList[endAptID].aptMapping[y].airportID;
					if(!queryOD(orig, dest, earlyDpt + 60*(aptList[startAptID].aptMapping[x].duration + optParam.preBoardTime), lateArr - 60*(aptList[endAptID].aptMapping[y].duration + optParam.postArrivalTime), 1, &commDpt, &commArr, &commCost))
					{
						if((tempArr = commArr+ 60*(optParam.postArrivalTime + aptList[endAptID].aptMapping[y].duration))<(*arrivalTm)){
							(*departTm) = commDpt - 60*(aptList[startAptID].aptMapping[x].duration + optParam.preBoardTime);
							(*dutyStartTm) = (aptList[startAptID].aptMapping[x].duration > 0? (*departTm) : commDpt - 3600); 
							(*arrivalTm) = tempArr;
							(*cost) = aptList[startAptID].aptMapping[x].cost + aptList[endAptID].aptMapping[y].cost + commCost;
							break;
							//We could take the minimum cost for group travel, as long as the time is feasbile. RLZ 03/03/2008
			                //But we opt to do it in the post-process to speed up the process. Here only feasible solution is needed.
						}
					}
				} //end if(aptList[startAptID].aptMapping[y].groundOnly == 0)
				y++;
			} //end while(y < aptList[endAptID].numMaps)
		} //end if(aptList[startAptID].aptMapping[x].groundOnly == 0)
		x++;
	} //end while(x < aptList[startAptID].numMaps)
	if((*arrivalTm) > lateArr)
		return -1; //pilot can't get there on time
	return 0;
}

/********************************************************************************************************
*	Function	getCrewTravelDataLateOAG			  				Date last modified:  08/19/07 SWO	*
*	Purpose:	Start and end airports, earliest departure and latest arrival are sent to this 			*
*		function.	Given the latest arrival time, the function returns the latest departure time		*
*		of the pilot from the origin airport, unless he must leave before the earliest departure		*
*		time, in which case -1 is returned.																*
*		   For a pilot starting his tour, the function also returns the duty start time.  Duty			*
*		hours start at one hour before the scheduled departure of the commercial flight UNLESS there	*
*		is ground travel to the flight, in which case duty starts at start of ground travel.			* 															
*			Note that one or both ground travel legs may be unnecessary, or there might be ONLY			*
*		ground travel and no commercial flight.															*
*			This function also returns the total estimated cost of the ground travel and commercial		* 
*		flight legs between start airport and end airport.	OAG data is used in this function.											*																	
********************************************************************************************************/
int getCrewTravelDataLateOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost)
{
	int x, y, orig, dest;//, oDInd;
	//int z, canDrive;
	time_t tempDpt;
	time_t commArr, commDpt;
	double commCost;

	numOAGCallsLate ++;
	
	(*arrivalTm) = lateArr; //initialize at latest possible arrival time
	commArr = lateArr; //initializing for call to queryOD function
	(*departTm) = earlyDpt - 3600; //initialize at earlyDpt - one hour 
	commDpt = (*departTm);  //initializing for call to queryOD function
	commCost = 0.0;
	(*cost) = 0.0;
	
	//check that specific itinerary has not been blocked by Travel Dept
//	if(blockedItinerary(startAptID, endAptID, earlyDpt, lateArr))
//		return -1;

	//check if it is possible to travel by ground only
	x = 0;
	while(x < aptList[startAptID].numMaps){
		if(aptList[startAptID].aptMapping[x].airportID == endAptID){
			if((tempDpt = (lateArr - 60*aptList[startAptID].aptMapping[x].duration))>(*departTm)){
				(*departTm) = tempDpt;
				(*dutyStartTm) = tempDpt;
				(*arrivalTm) = lateArr;
				(*cost) = aptList[startAptID].aptMapping[x].cost;
				break; //we could take the minimum cost
			}
		}
		x++;
	}
	if((*departTm) >= earlyDpt)
		return 0;
	//if it was possible to travel by ground only (departTm was updated) but that wasn't quick enough (from above)
	//then assume pilot can't leave that early
	if((*departTm) > earlyDpt - 3600)
		return -1;

	//check all commercial itineraries
	x = 0;
	y = 0;
	while(x < aptList[startAptID].numMaps){
		if(aptList[aptList[startAptID].aptMapping[x].airportID].commFlag == 1 && aptList[startAptID].aptMapping[x].groundOnly == 0){
			orig = aptList[startAptID].aptMapping[x].airportID;
			while(y < aptList[endAptID].numMaps){
				//if mapped to commercial airport and not designated as "ground only", get commercial flights between orig and dest
				if(aptList[aptList[endAptID].aptMapping[y].airportID].commFlag == 1&& aptList[endAptID].aptMapping[y].groundOnly == 0){
					dest = aptList[endAptID].aptMapping[y].airportID;

					if(!queryOD(orig, dest, earlyDpt + 60*(aptList[startAptID].aptMapping[x].duration + optParam.preBoardTime), lateArr - 60*(aptList[endAptID].aptMapping[y].duration + optParam.postArrivalTime), 0, &commDpt, &commArr, &commCost))
					{
						if((tempDpt = commDpt - 60*(optParam.preBoardTime + aptList[startAptID].aptMapping[x].duration))>(*departTm)){
							(*departTm) = tempDpt;
							(*dutyStartTm) = (aptList[startAptID].aptMapping[x].duration > 0? (*departTm) : commDpt - 3600); 
							(*arrivalTm) = commArr + 60*(aptList[endAptID].aptMapping[y].duration + optParam.postArrivalTime);
							(*cost) = aptList[startAptID].aptMapping[x].cost + aptList[endAptID].aptMapping[y].cost + commCost;
							break; //we could take the minimum cost here. RLZ 03/05/2009
						}
					}
				} //end if(aptList[startAptID].aptMapping[y].groundOnly == 0)
				y++;
			} //end while(y < aptList[endAptID].numMaps)
		} //end if(aptList[startAptID].aptMapping[x].groundOnly == 0)
		x++;
	} //end while(x < aptList[startAptID].numMaps)
	if((*departTm) < earlyDpt)
		return -1; //pilot can't leave that early
	return 0;
}


static int getODHashIndex(int orig, int dest)
{
	int index;

	index = (orig*1000 + dest)%149993;
	return index;
}

/********************************************************************************************************
*	Function	findODIndex											Date last modified:  07/07/07 SWO	*
*	Purpose:	Get index of entry in OD table;  return -1 if there is no entry for that origin			* 
*				and destination.																		*																	*
********************************************************************************************************/
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


/********************************************************************************************************
*	Function	queryOD												Date last modified:  08/16/07 SWO	*
*	Purpose:	     If earlyFlag is set to 1, this function returns commArr, the earliest arrival time	*
*				of a commercial flight itinerary from origAptID to destAptID, departing no earlier than	*
*				earlyDpt and arriving no later than lateArr.  It also returns commDpt, the associated	*
*				commercial flight departure time.  If there is no such commercial itinerary,			*
*				the function returns -1.																*
*				     If the earlyFlag is set to 0, this function returns commDpt, the latest departure	*
*				time of a commercial flight itinerary from origAptID to destAptID, departing no earlier *
*				than earlyDpt and arriving no later than lateArr.  It also returns commArr, the			*
*				associated commercial flight arrival time.  If there is no commercial itinerary,	 	*
*				the function returns -1.																*
*					This code assumes that the oagList is sorted by increasing commDpt.  It also		*
*				assumes that the list has been pruned of bad itineraries.								*
*				An itinerary 1 is "bad" if there exists another itinerary 2 such that					*	
*				commDpt_2 >= commDpt_1 and commArr_2 <= commArr_1.										*			
********************************************************************************************************/
static int queryOD(int origAptID, int destAptID, time_t earlyDpt, time_t lateArr, int earlyFlag, time_t *commDpt, time_t *commArr, double *cost)
{
	int oDInd, i, numOag;
	OagEntry *oagList;
	int high, low, mid, cond, found;

	if((oDInd = findODIndex(origAptID, destAptID, oDTable, oDHashTable)) == -1){ //get index of entry in OD table
		//error message if OD Entry was not found
		//logMsg(logFile,"%s Line %d, No OD Entry found for origin apt %d, destn apt  %d.\n", __FILE__,__LINE__, origAptID, destAptID);
		//THERE CAN BE A VALID REASON FOR THIS (CAN DRIVE)
		return -1;
	}

	oagList = oDTable[oDInd].oagList;
	numOag = oDTable[oDInd].numOag;
	if(numOag == 0)
		return -1;

	if(earlyFlag == 1){
		if(numOag == 0)
			return -1;
		//find oag entry with earliest commDpt >= earlyDpt
		if(earlyDpt <= oagList[0].dptTm)
			high = 0;
		else{
			high = numOag - 1;
			if(earlyDpt > oagList[high].dptTm)
				return -1;
		}
		low = 0;
		found = 0;
		while(low < high - 1){
			mid = low + (high - low)/2;
			if((cond = (int)(earlyDpt - oagList[mid].dptTm)) < 0)
				high = mid;
			else if(cond > 0)
				low = mid;
			else{//answer is stored at [mid]
				found = 1;
				break;
			}
		}
		if(found == 1)
			i = mid;
		else
			i = high;

		(*commArr) = oagList[i].arrTm;
		if((*commArr) > lateArr)
			return -1;
		(*commDpt)= oagList[i].dptTm;
	}
	else{ //earlyFlag == 0
		if(numOag == 0)
			return -1;
		//find oag entry with latest commArr <= lateArr
		if(lateArr >= oagList[numOag-1].arrTm)
			low = numOag - 1;
		else{
			low = 0;
			if(lateArr < oagList[0].arrTm)
				return -1;
		}
		high = numOag - 1;

		found = 0;
		while(low < high - 1){
			mid = low + (high - low)/2;
			if((cond = (int)(lateArr - oagList[mid].arrTm)) < 0)
				high = mid;
			else if(cond > 0)
				low = mid;
			else{//answer is stored at [mid]
				found = 1;
				break;
			}
		}
		if(found == 1)
			i = mid;
		else
			i = low;

		(*commDpt)= oagList[i].dptTm;
		if(*commDpt < earlyDpt)
			return -1;
		(*commArr) = oagList[i].arrTm;
	}
	(*cost) = oDTable[oDInd].cost;
	return 0;
}

/********************************************************************************************************
*	Function	getCrewTravelDataEarlyNoOAG								Date last modified:  06/05/07 SWO	*
*	Purpose:	Origin and destination airports are sent to this function.								*
*		This function returns the start and end times for the pilot's travel INCLUDING the		*
*			following components:	Ground travel to commercial airport, pre-board time (per optParam), *
*			commercial flight time, post-arrival time (per optParam), and ground travel from			*
*			commercial airport.																			*
*		For a pilot starting his tour, the time at which duty hours start is also returned.  Duty		*
*			hours start at the scheduled departure of the commercial flight UNLESS there is ground		*
*			travel to the flight, in which case duty starts at start of ground travel.					*
*		The itinerary returned is the one with the earliest arrivalTm.	If arrivalTm > lateArr,			*
*			then -1 is returned.																		*
*		Time calculations are based on the earliest possible commercial flight.							*
*		Unless cost (argument as sent to function) == -1, function also returns the total estimated cost* 
*			of the ground travel and commercial flight legs between start airport and end airport.		*																	*
********************************************************************************************************/
//TEMPORARY FUNCTION UNTIL FUNCTION USING OAG DATA IS CODED
int getCrewTravelDataEarlyNoOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost)
{	
	int defltGrTravTime = 43;  //default ground travel time in minutes
	double defltGrTravCost = 59.0; //default ground travel cost in dollars
	int scheduleDelayTm = 77;  //extra time in minutes that pilot must wait for commercial flight departure, 
								//or wait after commercial flight arrival, due to flight schedules
	double sovFactor = 1.105; //variable component of commercial flight time (multiplier to Sovereign flight time)
	int fixedTime = 20;  //fixed component (minutes) of commercial flight time
	int blackoutStart = 1351; //minutes after midnight local time at which last commercial flight arrives or departs
	int blackoutEnd = 429;  //minutes after midnight local time at which earliest commercial flight departs or arrives

	int startGrTime, endGrTime;
	double startGrCost, endGrCost;
	int flightTm, elapsedTm, blockTm, numStops;
	int arrMPM, depMPM;
	
	//get flight time from startApt to endApt for Sovereign (approximately equal to flight time for Sovereign between commercial airports)
	if(startAptID != endAptID){
		if(getFlightTime(startAptID, endAptID, 52,month, 0, &flightTm, &blockTm, &elapsedTm, &numStops))
			return -1;  // return -1 if can't get from startAptID to endAptID
	}
	else{
		flightTm = blockTm = elapsedTm = numStops = 0;
	}


	//if startAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
	if(startAptID == 3453 || startAptID == 1030 || startAptID == 1170 || startAptID == 1973 || startAptID == 850){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 2008 || startAptID == 3160 || startAptID == 1455 || startAptID == 4363 || startAptID == 538){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 936 || startAptID == 989 || startAptID == 4577 || startAptID == 2533 || startAptID == 1827 || startAptID == 2886){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 696 || startAptID == 766 || startAptID == 773 || startAptID == 2529 || startAptID == 4244 || startAptID == 3492){
		startGrTime = 0;
		startGrCost = 0;}
	else if(startAptID == 1018 || startAptID == 3281 || startAptID == 4412 || startAptID == 487 || startAptID == 4692){
		startGrTime = 0;
		startGrCost = 0.0;}
	else{
		startGrTime = defltGrTravTime;
		startGrCost = defltGrTravCost;}
	//if endAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
	if(endAptID == 3453 || endAptID == 1030 || endAptID == 1170 || endAptID == 1973 || endAptID == 850){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 2008 || endAptID == 3160 || endAptID == 1455 || endAptID == 4363 || endAptID == 538){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 936 || endAptID == 989 || endAptID == 4577 || endAptID == 2533 || endAptID == 1827 || endAptID == 2886){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 696 || endAptID == 766 || endAptID == 773 || endAptID == 2529 || endAptID == 4244 || endAptID == 3492){
		endGrTime = 0;
		endGrCost = 0;}
	else if(endAptID == 1018 || endAptID == 3281 || endAptID == 4412 || endAptID == 487 || endAptID == 4692){
		endGrTime = 0;
		endGrCost = 0.0;}
	else{
		endGrTime = defltGrTravTime;
		endGrCost = defltGrTravCost;}

	if((*cost) != -1){
		//we should actually call getCommercialFlightCost with the start and end COMMERCIAL airports, which may or may not be the same as the startAptID and endAptID
		//that are sent to getCrewTravelData (because there may be ground travel involved on one or both ends)
		(*cost) = getCommercialFlightCost(startAptID, endAptID);
		//add ground travel cost
		(*cost) += startGrCost + endGrCost;
	}

    //For ground travel only airport pair. RLZ

	if ( (startAptID == 696 && endAptID == 694)||(startAptID == 694 && endAptID == 696)||
		 (startAptID == 850 && endAptID == 840)||(startAptID == 840 && endAptID == 850)||
		 (startAptID == 1170 && endAptID == 1110)||(startAptID == 1110 && endAptID == 1170)||
		 (startAptID == 1455 && endAptID == 1454)||(startAptID == 1454 && endAptID == 1455)||
		 (startAptID == 1607 && endAptID == 4959)||(startAptID == 4959 && endAptID == 1607)||
		 (startAptID == 1827 && endAptID == 3487)||(startAptID == 3487 && endAptID == 1827)||
		 (startAptID == 1973 && endAptID == 1978)||(startAptID == 1978 && endAptID == 1973)||
		 (startAptID == 2008 && endAptID == 936)||(startAptID == 936 && endAptID == 2008)||
		 (startAptID == 2529 && endAptID == 1814)||(startAptID == 1814 && endAptID == 2529)||
		 (startAptID == 3160 && endAptID == 3453)||(startAptID == 3453 && endAptID == 3160)||
		 (startAptID == 3492 && endAptID == 3487)||(startAptID == 3487 && endAptID == 3492)||
		 (startAptID == 4363 && endAptID == 4362)||(startAptID == 4362 && endAptID == 4363)||
		 (startAptID == 4412 && endAptID == 4362)||(startAptID == 4362 && endAptID == 4412)||
		 (startAptID == 4692 && endAptID == 840)||(startAptID == 840 && endAptID == 4692)
		  || flightTm <= 30 //RLZ 02/26/2009 default for ground travel, if flightTm on SOV <=30) 
		 ){
			 //if ( earlyDpt + 45*60 <= (*arrivalTm)){ //RLZ 02/26/09
               if ( earlyDpt + 45*60 <= (lateArr)){
				(*departTm) = earlyDpt;
				(*dutyStartTm) = earlyDpt;
				(*arrivalTm) = earlyDpt + 45*60;			
			    (*cost) = defltGrTravCost;
				return 0;
				}
		 }

	if (startAptID== endAptID){
		(*departTm) = earlyDpt;
		(*dutyStartTm) = earlyDpt;
		(*arrivalTm) = earlyDpt;			
		(*cost) = 0;
		return 0;
	}

	
	if(earlyDpt > 0){
		//check if we fall into blackout period and must wait until morning
		//calculate earliest commercial depart and arrive time in minutes past midnight, local time 
		depMPM = minutesPastMidnight((earlyDpt + 60*startGrTime + 60*optParam.preBoardTime), startAptID);
		arrMPM = minutesPastMidnight((earlyDpt + 60*startGrTime + 60*optParam.preBoardTime+ 
			60*fixedTime + (int)(60*flightTm*sovFactor)), endAptID);
		
		if(blackoutStart < blackoutEnd){ //if blackoutStart is at or after midnight
			//if earliest commercial departure or arrival falls into blackout period
			if((depMPM > blackoutStart && depMPM < blackoutEnd) || (arrMPM > blackoutStart && depMPM < blackoutEnd))
				(*departTm) = earlyDpt + 60*(blackoutEnd - depMPM);
			else
				(*departTm) = earlyDpt + 60*scheduleDelayTm; //assume pilot waits to depart for airport due to flight schedules
		}
		else{ //if blackoutStart is before midnight
			if(depMPM > blackoutStart || arrMPM > blackoutStart)
				(*departTm) = earlyDpt + 60*(blackoutEnd + 1440 - depMPM); //if earliest commercial dep/arr falls in late PM blackout period
			else if(depMPM < blackoutEnd)
				(*departTm) = earlyDpt + 60*(blackoutEnd - depMPM); //if earliest commercial departure falls early AM blackout period
			else if(arrMPM < blackoutEnd)
				(*departTm) = earlyDpt + 60*(blackoutEnd + 1440 - depMPM); //if earliest comm arr falls early AM blackout period but dep is before blackout
			else
				(*departTm) = earlyDpt + 60*scheduleDelayTm; //assume pilot waits to depart for airport due to flight schedules
		}

		numOAGCallsEarly ++;
		
		(*arrivalTm) = (*departTm) + 60*startGrTime + 60*optParam.preBoardTime + 60*fixedTime + (int)(60*flightTm*sovFactor) + 60*optParam.postArrivalTime + 60*endGrTime;
		if((*arrivalTm) > lateArr)
			return -1;
		
		if(startGrTime == 0)
			(*dutyStartTm) = (*departTm) + 60*optParam.preBoardTime;  //if no ground travel before flight, this is scheduled commercial departure
		else
			(*dutyStartTm) = (*departTm); //else this is ground travel start

	}
	else{ //if earlyDpt == 0, just return 0 for arrivalTm, dutyStartTm and departTm - we are only interested in the cost
		(*arrivalTm) = 0;
		(*dutyStartTm) = 0;
		(*departTm) = 0;
	}

	return 0;
}

/********************************************************************************************************
*	Function	getCrewTravelDataLateNoOAG								Date last modified:  06/05/07 SWO	*
*	Purpose:	Origin and destination airports are sent to this function.								*
*		This function returns the start and end times for the pilot's travel INCLUDING the		*
*			following components:	Ground travel to commercial airport, pre-board time (per optParam), *
*			commercial flight time, post-arrival time (per optParam), and ground travel from			*
*			commercial airport.																			*
*		For a pilot starting his tour, the time at which duty hours start is also returned.  Duty		*
*			hours start at the scheduled departure of the commercial flight UNLESS there is ground		*
*			travel to the flight, in which case duty starts at start of ground travel.					*
*		The itinerary returned is the one with the latest departTm.	 If departTm < earlyDpt,			*
*			then -1 is returned.																		*								*
*		Time calculations are based on the latest possible commercial flight.							*
*		Unless cost (argument as sent to function) == -1, function also returns the total estimated cost* 
*			of the ground travel and commercial flight legs between start airport and end airport.		*																	*
********************************************************************************************************/
//TEMPORARY FUNCTION UNTIL FUNCTION USING OAG DATA IS CODED
int getCrewTravelDataLateNoOAG(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost)
{	
	int defltGrTravTime = 43;  //default ground travel time in minutes
	double defltGrTravCost = 59.0; //default ground travel cost in dollars
	int scheduleDelayTm = 77;  //extra time in minutes that pilot must wait for commercial flight departure, 
								//or wait after commercial flight arrival, due to flight schedules
	double sovFactor = 1.105; //variable component of commercial flight time (multiplier to Sovereign flight time)
	int fixedTime = 20;  //fixed component (minutes) of commercial flight time
	int blackoutStart = 1351; //minutes after midnight local time at which last commercial flight arrives or departs
	int blackoutEnd = 429;  //minutes after midnight local time at which earliest commercial flight departs or arrives

	int startGrTime, endGrTime;
	double startGrCost, endGrCost;
	int flightTm, elapsedTm, blockTm, numStops;
	int arrMPM, depMPM;
	

	if(startAptID != endAptID){//Update 041508 RLZ
		if(getFlightTime(startAptID, endAptID, 52,month, 0, &flightTm, &blockTm, &elapsedTm, &numStops))
			return -1;  // return -1 if can't get from startAptID to endAptID
	}
	else{
		flightTm = blockTm = elapsedTm = numStops = 0;
	}

	//if startAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
	if(startAptID == 3453 || startAptID == 1030 || startAptID == 1170 || startAptID == 1973 || startAptID == 850){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 2008 || startAptID == 3160 || startAptID == 1455 || startAptID == 4363 || startAptID == 538){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 936 || startAptID == 989 || startAptID == 4577 || startAptID == 2533 || startAptID == 1827 || startAptID == 2886){
		startGrTime = 0;
		startGrCost = 0.0;}
	else if(startAptID == 696 || startAptID == 766 || startAptID == 773 || startAptID == 2529 || startAptID == 4244 || startAptID == 3492){
		startGrTime = 0;
		startGrCost = 0;}
	else if(startAptID == 1018 || startAptID == 3281 || startAptID == 4412 || startAptID == 487 || startAptID == 4692){
		startGrTime = 0;
		startGrCost = 0.0;}
	else{
		startGrTime = defltGrTravTime;
		startGrCost = defltGrTravCost;}
	//if endAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
	if(endAptID == 3453 || endAptID == 1030 || endAptID == 1170 || endAptID == 1973 || endAptID == 850){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 2008 || endAptID == 3160 || endAptID == 1455 || endAptID == 4363 || endAptID == 538){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 936 || endAptID == 989 || endAptID == 4577 || endAptID == 2533 || endAptID == 1827 || endAptID == 2886){
		endGrTime = 0;
		endGrCost = 0.0;}
	else if(endAptID == 696 || endAptID == 766 || endAptID == 773 || endAptID == 2529 || endAptID == 4244 || endAptID == 3492){
		endGrTime = 0;
		endGrCost = 0;}
	else if(endAptID == 1018 || endAptID == 3281 || endAptID == 4412 || endAptID == 487 || endAptID == 4692){
		endGrTime = 0;
		endGrCost = 0.0;}
	else{
		endGrTime = defltGrTravTime;
		endGrCost = defltGrTravCost;}

	if((*cost) != -1){
		//we should actually call getCommercialFlightCost with the start and end COMMERCIAL airports, which may or may not be the same as the startAptID and endAptID
		//that are sent to getCrewTravelData (because there may be ground travel involved on one or both ends)
		(*cost) = getCommercialFlightCost(startAptID, endAptID);
		//add ground travel cost
		(*cost) += startGrCost + endGrCost;
	}

	  //For ground travel only airport pair. RLZ

    //For ground travel only airport pair. RLZ

	if ( (startAptID == 696 && endAptID == 694)||(startAptID == 694 && endAptID == 696)||
		 (startAptID == 850 && endAptID == 840)||(startAptID == 840 && endAptID == 850)||
		 (startAptID == 1170 && endAptID == 1110)||(startAptID == 1110 && endAptID == 1170)||
		 (startAptID == 1455 && endAptID == 1454)||(startAptID == 1454 && endAptID == 1455)||
		 (startAptID == 1607 && endAptID == 4959)||(startAptID == 4959 && endAptID == 1607)||
		 (startAptID == 1827 && endAptID == 3487)||(startAptID == 3487 && endAptID == 1827)||
		 (startAptID == 1973 && endAptID == 1978)||(startAptID == 1978 && endAptID == 1973)||
		 (startAptID == 2008 && endAptID == 936)||(startAptID == 936 && endAptID == 2008)||
		 (startAptID == 2529 && endAptID == 1814)||(startAptID == 1814 && endAptID == 2529)||
		 (startAptID == 3160 && endAptID == 3453)||(startAptID == 3453 && endAptID == 3160)||
		 (startAptID == 3492 && endAptID == 3487)||(startAptID == 3487 && endAptID == 3492)||
		 (startAptID == 4363 && endAptID == 4362)||(startAptID == 4362 && endAptID == 4363)||
		 (startAptID == 4412 && endAptID == 4362)||(startAptID == 4362 && endAptID == 4412)||
		 (startAptID == 4692 && endAptID == 840)||(startAptID == 840 && endAptID == 4692)
		 || flightTm <= 30 //RLZ 02/26/2009 default for ground travel, if flightTm on SOV <=30) 
		 ){
			 if ( lateArr - 45*60 <= (*departTm)){
	            (*departTm) = lateArr - 45*60;
				(*dutyStartTm) = lateArr - 45*60;
				(*arrivalTm) = lateArr;		
			    (*cost) = defltGrTravCost;
				return 0;
				}
		 }

	if (startAptID== endAptID){
		(*departTm) = lateArr;
		(*dutyStartTm) = lateArr;
		(*arrivalTm) = lateArr;		
		(*cost) = 0;
		return 0;
	}

	if(lateArr > 0){
		//check if we fall into blackout period and must wait until morning
		//calculate latest commercial arrive and depart time in minutes past midnight, local time 
		arrMPM = minutesPastMidnight((lateArr - 60*endGrTime - 60*optParam.postArrivalTime), endAptID);
		depMPM = minutesPastMidnight((lateArr - 60*endGrTime - 60*optParam.postArrivalTime 
			- 60*fixedTime - (int)(60*flightTm*sovFactor)), startAptID);
		if(blackoutStart < blackoutEnd){ //if blackoutStart is at or after midnight
			//if latest commercial departure or arrival falls into blackout period
			if((depMPM > blackoutStart && depMPM < blackoutEnd) || (arrMPM > blackoutStart && depMPM < blackoutEnd))
				(*arrivalTm) = lateArr - 60*(arrMPM - blackoutStart);
			else
				(*arrivalTm) = lateArr - 60*scheduleDelayTm; //assume pilot arrives early at airport due to flight schedules
		}
		else{ //if blackoutStart is before midnight
			if(depMPM < blackoutEnd || arrMPM < blackoutEnd) //if latest comm dep/arrival falls in early AM blackout
				(*arrivalTm) = lateArr - 60*(arrMPM + 1440 - blackoutStart);
			else if(arrMPM > blackoutStart)//if latest comm arrival falls in late PM blackout period
				(*arrivalTm) = lateArr - 60*(arrMPM - blackoutStart);
			else if(depMPM > blackoutStart)//if latest comm departure falls in late PM blackout period but arr is after blackout
				(*arrivalTm) = lateArr - 60*(arrMPM + 1440 - blackoutStart);
			else
				(*arrivalTm) = lateArr - 60*scheduleDelayTm; //assume pilot arrives early due to flight schedules
		}

		numOAGCallsLate ++;

		(*departTm) =  (*arrivalTm) - 60*endGrTime - 60*optParam.postArrivalTime - 60*fixedTime -(int)(60*flightTm*sovFactor) - 60*optParam.preBoardTime - 60*startGrTime;	
		if((*departTm) < earlyDpt)
			return -1;

		if(startGrTime == 0)
			(*dutyStartTm) = (*departTm) + 60*optParam.preBoardTime;  //if no ground travel before flight, this is scheduled commercial departure
		else
			(*dutyStartTm) = (*departTm); //else this is ground travel start

	}

	return 0;
}

/********************************************************************************************************
*	Function	getCrewTravelDataEarly			  				Date last modified:  11/28/07 Jintao	*
*	Purpose:	This function will proovde option to swith between 	getCrewTravelDataEarlyOAG and	    *	
*               getCrewTravelDataEarlyNoOAG, the previous function is used in regular optimizer, the    *
*               latter is used in optimizer with OAG implementated.                                     *
********************************************************************************************************/
int getCrewTravelDataEarly(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost, int use_oag)
{  int ret;
	if(use_oag==1)
	{ret=getCrewTravelDataEarlyOAG(earlyDpt, lateArr, startAptID, endAptID, departTm, dutyStartTm, arrivalTm, cost);
	 return(ret);
	}
	else
	{ret=getCrewTravelDataEarlyNoOAG(earlyDpt, lateArr, startAptID, endAptID, departTm, dutyStartTm, arrivalTm, cost);
	 return(ret);
	}
}

/********************************************************************************************************
*	Function	getCrewTravelDataLate			  				Date last modified:  11/28/07 Jintao	*
*	Purpose:	This function will proovde option to swith between 	getCrewTravelDataLateOAG and	    *	
*               getCrewTravelDataLateNoOAG, the previous function is used in regular optimizer, the     *
*               latter is used in optimizer with OAG implementated.                                     *
********************************************************************************************************/
int getCrewTravelDataLate(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost, int use_oag)
{  int ret;
   if (use_oag==1)
    { ret=getCrewTravelDataLateOAG(earlyDpt, lateArr, startAptID, endAptID, departTm, dutyStartTm, arrivalTm, cost);
	  return(ret);
    }
   else
   { ret=getCrewTravelDataLateNoOAG(earlyDpt, lateArr, startAptID, endAptID, departTm, dutyStartTm, arrivalTm, cost);
     return(ret);
   }
}
///********************************************************************************************************
//*	Function	blockedItinerary									Date last modified:  05/08/07 SWO	*
//*	Purpose:	check that a specific itinerary has not been blocked out as infeasible by				*
//*				the travel department.																	*
//********************************************************************************************************/
//static int blockedItinerary(int startAptID, int endAptID, time_t earlyDpt, time_t lateArr)
//{	/*
//	blockItinList must be sorted by:
//	 (1) increasing startAptID, then by 
//	 (2) increasing endAptID, then by 
//	 (3) decreasing start
//	 */
//	int x;
//	//assume that blockItinList is very small and brute force is sufficient to find entry (change later if necessary)
//	x=0;
//	while(x < numBlockItin && blockItinList[x].startAptID < startAptID)
//		x++;
//	if(x == numBlockItin || blockItinList[x].startAptID != startAptID)
//		return 0;  //itinerary is not blocked
//	while(x < numBlockItin && blockItinList[x].startAptID == startAptID && blockItinList[x].endAptID < endAptID)
//		x++;
//	if(x == numBlockItin || blockItinList[x].startAptID != startAptID || blockItinList[x].endAptID != endAptID)
//		return 0;  //itinerary is not blocked
//	//the current blocked itinerary matches both startAptID and endAptID
//	while(x < numBlockItin && blockItinList[x].startAptID == startAptID && blockItinList[x].endAptID == endAptID
//		&& blockItinList[x].start > earlyDpt)
//			x++;
//	if(x == numBlockItin || startAptID != blockItinList[x].startAptID || endAptID != blockItinList[x].endAptID)
//		return 0;  //itinerary is not blocked
//	//the current blocked itinerary matches both startAptID and endAptID, and the start <= earlyDpt
//	while(x < numBlockItin && blockItinList[x].startAptID == startAptID && blockItinList[x].endAptID == endAptID){
//		//blockItinList[x].start <=earlyDpt for remainder of blocked itineraries with matching airports due to sort
//			if(blockItinList[x].end >= lateArr)
//				return -1; //itinerary is blocked
//			else
//				x++;
//	}
//	return 0; //itinerary is not blocked
//
//	//the following code is more concise, but involves more comparison operations and would be slower
//	//for(x = 0; x<numBlockItn; x++){
//	//	if(blockItinList[x].startAptID < startAptID)
//	//		continue;
//	//	if(blockItinList[x].startAptID != startAptID)
//	//		return 0;
//	//	if(blockItinList[x].endAptID < endAptID)
//	//		continue;
//	//	if(blockItinList[x].endAptID != endAptID)
//	//		return 0;
//	//	if(blockItinList[x].start > earlyDpt)
//	//		continue;
//	//	if(blockItinList[x].end >= lateArr)
//	//		return -1;
//	//}
//	//return 0;
//}

///********************************************************************************************************
//*	Function	getCrewTravelDataLate								Date last modified:  06/05/07 SWO	*
//*	Purpose:	Origin and destination airports are sent to this function.								*
//*		This function returns returns the start and end times for the pilot's travel INCLUDING the		*
//*			following components:	Ground travel to commercial airport, pre-board time (per optParam), *
//*			commercial flight time, post-arrival time (per optParam), and ground travel from			*
//*			commercial airport.																			*
//*		For a pilot starting his tour, the time at which duty hours start is also returned.  Duty		*
//*			hours start at the scheduled departure of the commercial flight UNLESS there is ground		*
//*			travel to the flight, in which case duty starts at start of ground travel.					*
//*		The itinerary returned is the one with the latest departTm.	 If departTm < earlyDpt,			*
//*			then -1 is returned.																		*								*
//*		Time calculations are based on the latest possible commercial flight.							*
//*		Unless cost (argument as sent to function) == -1, function also returns the total estimated cost* 
//*			of the ground travel and commercial flight legs between start airport and end airport.		*																	*
//********************************************************************************************************/
////TEMPORARY FUNCTION UNTIL FUNCTION USING OAG DATA IS CODED
//int getCrewTravelDataLate(time_t earlyDpt, time_t lateArr, int startAptID, int endAptID, time_t *departTm, time_t *dutyStartTm, time_t *arrivalTm, double *cost)
//{	
//	int defltGrTravTime = 43;  //default ground travel time in minutes
//	double defltGrTravCost = 59.0; //default ground travel cost in dollars
//	int scheduleDelayTm = 77;  //extra time in minutes that pilot must wait for commercial flight departure, 
//								//or wait after commercial flight arrival, due to flight schedules
//	double sovFactor = 1.105; //variable component of commercial flight time (multiplier to Sovereign flight time)
//	int fixedTime = 20;  //fixed component (minutes) of commercial flight time
//	int blackoutStart = 1351; //minutes after midnight local time at which last commercial flight arrives or departs
//	int blackoutEnd = 429;  //minutes after midnight local time at which earliest commercial flight departs or arrives
//
//	int startGrTime, endGrTime;
//	double startGrCost, endGrCost;
//	int flightTm, elapsedTm, blockTm, numStops;
//	int arrMPM, depMPM;
//	
//	//get flight time from startApt to endApt for Sovereign (approximately equal to flight time for Sovereign between commercial airports)
//	if(getFlightTime(startAptID, endAptID, 52,month, 0, &flightTm, &blockTm, &elapsedTm, &numStops))
//		return -1;  // return -1 if can't get from startAptID to endAptID
//
//	//if startAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
//	if(startAptID == 3453 || startAptID == 1030 || startAptID == 1170 || startAptID == 1973 || startAptID == 850){
//		startGrTime = 0;
//		startGrCost = 0.0;}
//	else if(startAptID == 2008 || startAptID == 3160 || startAptID == 1455 || startAptID == 4363 || startAptID == 538){
//		startGrTime = 0;
//		startGrCost = 0.0;}
//	else if(startAptID == 936 || startAptID == 989 || startAptID == 4577 || startAptID == 2533 || startAptID == 1827 || startAptID == 2886){
//		startGrTime = 0;
//		startGrCost = 0.0;}
//	else if(startAptID == 696 || startAptID == 766 || startAptID == 773 || startAptID == 2529 || startAptID == 4244 || startAptID == 3492){
//		startGrTime = 0;
//		startGrCost = 0;}
//	else if(startAptID == 1018 || startAptID == 3281 || startAptID == 4412 || startAptID == 487 || startAptID == 4692){
//		startGrTime = 0;
//		startGrCost = 0.0;}
//	else{
//		startGrTime = defltGrTravTime;
//		startGrCost = defltGrTravCost;}
//	//if endAptID is a pilot base, we know it is a commercial airport and ground travel won't be needed
//	if(endAptID == 3453 || endAptID == 1030 || endAptID == 1170 || endAptID == 1973 || endAptID == 850){
//		endGrTime = 0;
//		endGrCost = 0.0;}
//	else if(endAptID == 2008 || endAptID == 3160 || endAptID == 1455 || endAptID == 4363 || endAptID == 538){
//		endGrTime = 0;
//		endGrCost = 0.0;}
//	else if(endAptID == 936 || endAptID == 989 || endAptID == 4577 || endAptID == 2533 || endAptID == 1827 || endAptID == 2886){
//		endGrTime = 0;
//		endGrCost = 0.0;}
//	else if(endAptID == 696 || endAptID == 766 || endAptID == 773 || endAptID == 2529 || endAptID == 4244 || endAptID == 3492){
//		endGrTime = 0;
//		endGrCost = 0;}
//	else if(endAptID == 1018 || endAptID == 3281 || endAptID == 4412 || endAptID == 487 || endAptID == 4692){
//		endGrTime = 0;
//		endGrCost = 0.0;}
//	else{
//		endGrTime = defltGrTravTime;
//		endGrCost = defltGrTravCost;}
//
//	if((*cost) != -1){
//		//we should actually call getCommercialFlightCost with the start and end COMMERCIAL airports, which may or may not be the same as the startAptID and endAptID
//		//that are sent to getCrewTravelData (because there may be ground travel involved on one or both ends)
//		(*cost) = getCommercialFlightCost(startAptID, endAptID);
//		//add ground travel cost
//		(*cost) += startGrCost + endGrCost;
//	}
//
//	if(lateArr > 0){
//		//check if we fall into blackout period and must wait until morning
//		//calculate latest commercial arrive and depart time in minutes past midnight, local time 
//		arrMPM = minutesPastMidnight((lateArr - 60*endGrTime - 60*optParam.postArrivalTime), endAptID);
//		depMPM = minutesPastMidnight((lateArr - 60*endGrTime - 60*optParam.postArrivalTime 
//			- 60*fixedTime - (int)(60*flightTm*sovFactor)), startAptID);
//		if(blackoutStart < blackoutEnd){ //if blackoutStart is at or after midnight
//			//if latest commercial departure or arrival falls into blackout period
//			if((depMPM > blackoutStart && depMPM < blackoutEnd) || (arrMPM > blackoutStart && depMPM < blackoutEnd))
//				(*arrivalTm) = lateArr - 60*(arrMPM - blackoutStart);
//			else
//				(*arrivalTm) = lateArr - 60*scheduleDelayTm; //assume pilot arrives early at airport due to flight schedules
//		}
//		else{ //if blackoutStart is before midnight
//			if(depMPM < blackoutEnd || arrMPM < blackoutEnd) //if latest comm dep/arrival falls in early AM blackout
//				(*arrivalTm) = lateArr - 60*(arrMPM + 1440 - blackoutStart);
//			else if(arrMPM > blackoutStart)//if latest comm arrival falls in late PM blackout period
//				(*arrivalTm) = lateArr - 60*(arrMPM - blackoutStart);
//			else if(depMPM > blackoutStart)//if latest comm departure falls in late PM blackout period but arr is after blackout
//				(*arrivalTm) = lateArr - 60*(arrMPM + 1440 - blackoutStart);
//			else
//				(*arrivalTm) = lateArr - 60*scheduleDelayTm; //assume pilot arrives early due to flight schedules
//		}
//
//		numOAGCallsLate ++;
//
//		(*departTm) =  (*arrivalTm) - 60*endGrTime - 60*optParam.postArrivalTime - 60*fixedTime -(int)(60*flightTm*sovFactor) - 60*optParam.preBoardTime - 60*startGrTime;	
//		if((*departTm) < earlyDpt)
//			return -1;
//
//		if(startGrTime == 0)
//			(*dutyStartTm) = (*departTm) + 60*optParam.preBoardTime;  //if no ground travel before flight, this is scheduled commercial departure
//		else
//			(*dutyStartTm) = (*departTm); //else this is ground travel start
//
//	}
//
//	return 0;
//}
//
//
///********************************************************************************************************
//*	Function	getCommercialFlightCost								Date last modified:  05/05/06 SWO	*
//*	Purpose:	for a given pair of airports, estimate cost of a commercial flight						*																		*
//********************************************************************************************************/
//static double getCommercialFlightCost(int startAptID, int endAptID)
//{
//	double cost, temp, dist;
//	AirportLatLon *start_all, *end_all;
//
//	cost = 0.0;
//
//	if((start_all = getAirportLatLonInfoByAptID(startAptID)) == NULL) {
//		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, startAptID);
//		exit(1);
//		}
//	if((end_all = getAirportLatLonInfoByAptID(endAptID)) == NULL) {
//		logMsg(logFile,"%s Line %d, airportID %d not found\n", __FILE__,__LINE__, endAptID);
//		exit(1);
//	}
//	//calculate distance between origin and destination in miles   (3959 is earth's radius in miles)
//	temp = (double) cos(start_all->lat * M_PI / 180.0) * cos(end_all->lat * M_PI / 180.0) * cos((start_all->lon - end_all->lon) * M_PI / 180.0) + sin(start_all->lat * M_PI / 180.0) * sin(end_all->lat * M_PI / 180.0);
//	dist = (3959 * atan(sqrt(1 - temp * temp) / temp)); 
//	//calculate approximate commercial flight cost given the distance
//	cost = optParam.ticketCostFixed + dist*optParam.ticketCostVar;
//	return cost;
//}

