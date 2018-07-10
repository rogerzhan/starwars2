#include "os_config.h"
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <math.h>
#include "CSHOpt_pairCrews.h"
#include "datetime.h"
#include "logMsg.h"
#include "CSHOpt_readInput.h"
#include "CSHOpt_arcs.h"
#include "CSHOpt_callOag.h"
#include "CSHOpt_define.h"
#include "CSHOpt_processInput.h"
#include "localTime.h"
#include "CSHOpt_pairingSolver.h"
#include "CSHOpt_struct.h"
#include "pair.h"
#include "CSHOpt_define.h"
#include "CSHOpt_output.h"

/*
	This struct is used to store potential crewswap locations.
*/
typedef struct
{
	int airportID;		    // Airport at which the incoming crew picks up the plane.
	int lastAptID;          // Airport ID of destination of last leg in current tour.
	int acID;			    // ID of aircraft that is being used in the swap.
	double benefit;		    // Cost of sending the outgoing crew home minus overtime savings.
	time_t swapStart;	    // Start of time window in which the crew swap must occur (between two legs).
	time_t swapEnd;  	    // End of time window in which the crew swap must occur (between two legs).
	time_t lastSchedInDuty; // Sched in of last leg for outgoing crew pair on acID plane that day.
	time_t lastSchedInTour;	// Sched in of last leg in tour for outgoing crew pair on acID plane.
	time_t lastPeakStart;   // Used for local time calculation at destination of lastAptID.  
	time_t firstPeakStart;  // Used for local time calculation at airportID.
	int acTypeID;		    // Type of aircraft being swapped.
	int isLastLeg;          
	time_t outCrewOvertimeEnd;	// End of overtime of outgoing crew. 0 if no outgoing crew associated with crew swap.
	int crewInd;
} CrewSwap;

/*
	This struct is used to store results of OAG queries when the same query is going to be used multiple times.
	The fields here correspond to the arguments passed to the getCrewTravelData() function.
*/
typedef struct
{
	time_t earlyDepTime; 
	time_t lateArrTime;
	int fromAptID;
	int toAptID;
	time_t departTm; 
	time_t dutyStartTm; 
	time_t arrivalTm; 
	double cost;
	int feasible;         /* Indicates whether the trip is feasible, i.e., whether departure and arrival times are
							 within bounds (earliest departure, tour start, tour end, swap deadline, etc.) */

} OAG;

static OAG **oag;         // List that stores OAG recs.
static int numOagRecs;    // Number of OAG records.
static OAG **pilotOAGs;

extern int verbose;

extern time_t firstMidnight; // First midnight in GMT after window start.
extern time_t firstEndOfDay;
extern time_t endOfToday;
extern int month;

extern FILE *logFile;
extern OptParameters optParam;

extern CrewPair *crewPairList;
extern int numCrewPairs;
int numOptCrewPairs;
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;
extern int withOag;

extern Aircraft *vacList; // VAC - 12/09/10 ANG
extern int numVac; // VAC - 12/09/10 ANG

extern AircraftType *acTypeList; //sorted by sequencePosition

CrewPair *oldCrewPairList;	     // Stores the old crew pair list after new crew pair list has been generated.
int numOldCrewPairs;		     // Stores the number of crew pairs in the old crew pair list.

extern PairConstraint *pairConstraintList;
extern int numPairConstraints;

extern Crew *crewList;
extern int numCrew;

extern Leg *legList;
extern int numLegs;

extern Demand *demandList;
extern int numDemand;

extern MaintenanceRecord *maintList;
extern int numMaintenanceRecord;

extern Aircraft *acList;
extern int numAircraft;

extern time_t firstEndOfDay;

extern time_t maxArr;

static double **crewPairMatrix;	
/* 
*	numCrew x numCrew matrix that indicates which crew members can be paired to each other. 
*	Element (i,j) of the matrix is the benefit of pairing crewIndex i with			
*	crewIndex j. A negative value indicates that the pairing is not considered; default 0.			
*/

static int **legsFlownTogetherToday;
/*
*	numCrew x numCrew matrix; element (i,j) is the number of legs crew members i and j will fly with each other today.
*	Used to compute the benefit of keeping this pairing.
*/
static int **legsFlownTogetherTomorrow;
/*
*	numCrew x numCrew matrix; element (i,j) is the number of legs crew members i and j will fly with each other tomorrow.
*	Used to compute the benefit of keeping this pairing.
*/

static CrewSwap *crewSwaps;

static int numCrewSwaps;
/*
*	List and number of crew swap possibilities identified. See definition above.
*/

static MatchingArc *matchingArcs;
/*
*	The arcs in the bipartite graph sent to the optimizer. See definition in CSHOpt_pairCrews.h.
*/
static int numMatchingArcs;

static int *optMatching;
/*
*	A {0,1} array of length numMatchingArcs returned by the optimizer. 
*	Equals 1 if the arc is in the matching; 0 otherwise.
*/

static int **pairingPriority;
/*
*	numCrew x numCrew matrix; element (i,j) is the priority of pairing pilots i and j.
*	Constructed from the pairConstraintList. (See definition of pairConstraint in CSHOpt_struct.h).
*/

static int *legAvailable;
/*
*	An array of length numLegs that indicates if a swap can occur at the end of that leg.
*	0 or -1 indicate no swap can occur, while 1 and 2 indicate that the swap can occur. 
*/

static int *acAvailable;
/*
*	An array of length numAircraft that indicates if a swap can occur at
*	the next available location and datetime for an aircraft.  1 indicates that a swap can occur.
*/

static double *swapBenefitLeg;
/*
*	An array of length numLegs that contains the benefit from swapping at the end of each leg where a swap
*	is allowed.
*/

static double *swapBenefitAC;
/*
*	An array of length numAircraft that contains the benefit from swapping at the next available location/DT for an
*	aircraft where a swap is allowed.
*/

static double *swapGetHomeSavings;
/*  An array of length numCrew that contains the cost of commercial travel to send a pilot home at the end of their 
*	currently scheduled tour.  This cost is subtracted from the cost to send the outgoing crew home from a crew
*	swap location.
*/

static int *reuseCrewPair;
/*
*	A 0-1 array of length numCrewPairs (before it has been modified by the optimizer) that indicates
*	which crew pairs from the current scenario are to be retained in the new crew pair list.
*/

static int maxCPID;
/*
*	Max crew pair ID. Used to generate new crew pair IDs.
*/

static int *hasHardPairing; //an array of length numCrew that indicates a pilot has a hard pairing constraint

static int freeAndNullMemory (void);

static int addVacPairToCrewPairList (void);

static int reprintFinalCrewPairList (void);

FILE *pairCrewsLogFile;


/************************************************************************************************
*	Function	comparePairConstraints						Date last modified:  05/02/07 BGC	*
*	Purpose:	Compares two pair constraints. Used in the qsort routine that sorts the			*
*				pairConstraintList list. Sorts in increasing order --- first by crew1ID,        *
*				then by start time, then by priority.                                           *
************************************************************************************************/

static int
comparePairConstraints (const PairConstraint *a, const PairConstraint *b)
{
	int compare=0;
	if ((compare = (int) difftime (a->startTm, b->startTm)) != 0)
		return compare;
	if ((compare = (a->crew1ID - b->crew1ID)) != 0)
		return compare;
	return (a->priority - b->priority);
}

/************************************************************************************************
*	Function	compareCrewPairIDs							Date last modified:  05/02/07 BGC	*
*	Purpose:	Compares two crew pairs to sort by crew pair ID.								*
************************************************************************************************/

static int
compareCrewPairIDs (const CrewPair *a, const CrewPair *b)
{
	return (a->crewPairID - b->crewPairID);
}


/************************************************************************************************
*	Function	compareCrewswaps							Date last modified:  05/10/08 BGC	*
*	Purpose:	Compares two crew swap locations by aircraft, then swapstart time.				*
************************************************************************************************/

static int
compareCrewSwaps (const CrewSwap *a, const CrewSwap *b)
{
	int compare;

	if ((compare = (a->acID - b->acID)) != 0)
	{
		return compare;
	}
	return (int) difftime (a->swapStart, b->swapStart);
}


/************************************************************************************************
*	Function	computeOvertimeCost							Date last modified:  5/02/07 BGC	*
*	Purpose:	Computes the overtime cost of a given number of overtime days.					*
*				This was coded as a separate function because the manner in which overtime		*
*				is computed could change.														*
************************************************************************************************/

static double 
computeOvertimeCost (const double otDays) 
{
	if ((ceil(otDays) - otDays) >= 0.5)
	{
		return (optParam.overTimeCost * floor (otDays) + optParam.overTimeHalfCost);
	}

	return (optParam.overTimeCost * ceil (otDays));
}

/************************************************************************************************
*	Function	getPeakGMTOverlap							Date last modified:  05/03/07 BGC	*
*	Purpose:	Computes the overlap (in hours) given a start and end time in GMT, based on		*
*				optParam GMTpeak time parameters.												*
************************************************************************************************/

static double
getPeakGMTOverlap (const time_t startWindow, const time_t finishWindow)
{
	double overlap, peakStart, begin = (double) startWindow, end = (double) finishWindow;

	if (end <= begin)
	{
		return 0;
	}

	peakStart = ((double) firstMidnight) - 86400.0 + 60.0 * optParam.peakGMTStart;
	// Start time of the first peak period before the first midnight in the planning window.

	while ((peakStart + 86400.0) < begin)
	{
		peakStart += 86400.0;
	}
	// peakStart is now the beginning of the peak period before "begin".

	if (end <= peakStart + 86400)
	{// "end" is before the start of the next peak period.
		overlap = max(0, min(peakStart + 60.0 * optParam.peakDuration, end) - begin);
		return (overlap / 3600.0);
	}
	else
	{// "end" is after the start of the second peak period.
		overlap = max (0,	(peakStart + 60.0 * optParam.peakDuration) - begin);
		peakStart += 86400;
	}

	while ((end - 86400) > peakStart)
	{/*
	 *	For each full day of overlap (between two successive peakStarts, 
	 *	peak overlap increases by optParam.peakDuration. 
	 */
		overlap += optParam.peakDuration * 60.0;
		peakStart += 86400.0;
	}
	// peakStart is now the beginning of the last peak period before "end".

	// Now computing the overlap on the last day.
	overlap += (min(end, peakStart + 60.0 * optParam.peakDuration) - peakStart);

	return (overlap / 3600.0);
}

/************************************************************************************************
*	Function	getPeakLocalOverlap							Date last modified:  05/03/07 BGC	*
*	Purpose:	Computes the overlap (in hours) given a start and end time in GMT, local peak	*
*				start time, and remaining duty time.											*
************************************************************************************************/

static double
getPeakLocalOverlap (const time_t startWindow, const time_t finishWindow, const time_t localPeakStart, const time_t remainingDuty)
{
	double overlap = 0, begin = (double) startWindow, end = (double) finishWindow, peakStart = (double) localPeakStart, remainingDutyTm = (double) remainingDuty;

	if (end <= begin)
	{
		return 0;
	}

	while ((peakStart + 86400.0) < begin)
	{
		peakStart += 86400.0;
	}
	// peakStart is now the beginning of the peak period just before "begin"

	if (end <= (peakStart + 86400.0))
	{// "end" happend before the beginning of the next peak period.
		overlap = min (remainingDutyTm, max(0, min(end, peakStart + 60.0 * optParam.peakDuration) - begin));
		/*
		*	Overlap is the minimum of overlap on the first day and the remaining duty time (which is applicable only
		*	to the first day.
		*/
		return (overlap / 3600.0);
	}
	else
	{ // "end" occurs after start of the next peak period.
		overlap = max (0,	peakStart + 60.0 * optParam.peakDuration - begin);
		/*
		*	Overlap is minimum of local overlap on first day and remaining duty time, plus GMT overlap from end
		*	of first local peak end until "end".
		*/
		return ( ( min (overlap, remainingDutyTm) / 3600.0) + 
					getPeakGMTOverlap ((time_t) (peakStart + 60.0 * optParam.peakDuration), finishWindow) );
	}
}

/************************************************************************************************
*	Function	computePeakTimeOverlapBenefit				Date last modified:  8/XX/06 BGC	*
*	Purpose:	Computes the overlap (in seconds) benefit.										*
************************************************************************************************/

static double 
computePeakTimeOverlapBenefit (time_t begin, time_t peakStart, time_t end, int acID, time_t remainingDutyTm)
{
	int i;
	time_t start = max(optParam.windowStart, begin), stop = min(optParam.windowEnd, end);

	double overlap = 0;

	if (begin >= end)
		return 0;

	while (peakStart + 86400 < begin)
		peakStart += 86400;
	// peakStart is now the start of local peak period prior to "begin"

	/*
	*	Overlap during window is the peak overlap with plane minus peak time spent by plane in maintenance/appointments during
	*	the overlap window.
	*/

	for (i=0; i<numMaintenanceRecord; i++)
	{
		if (start >= stop)
		{
			return (optParam.peakOverlapBenefit) * overlap;
		}
		else if (stop <= maintList[i].startTm)
		{
			if (start < peakStart + 60*optParam.peakDuration)
			{	// If this is the first day, remaining duty time and peak local overlap matter.
				overlap += getPeakLocalOverlap (start, stop, peakStart, remainingDutyTm);
			}
			else
			{ // If not the first day, we only compute peak GMT overlap; remainingDutyTm is irrelevant.
				overlap += getPeakGMTOverlap (start, stop);
			}
			return (optParam.peakOverlapBenefit) * overlap;
		}

		if (maintList[i].aircraftID == acID)
		{
			if ((maintList[i].startTm <= start) && (maintList[i].endTm <= stop))
			{// peak window resumes at the end of maintenance/appointment.
				start = maintList[i].endTm;
			}
			else if (maintList[i].startTm > start)
			{
				if (start < peakStart + 60*optParam.peakDuration)
				{	// If this is the first day, remaining duty time and peak local overlap matter.
					overlap += getPeakLocalOverlap (start, maintList[i].startTm, peakStart, remainingDutyTm);
				}
				else
				{ // If not the first day, we only compute peak GMT overlap; remainingDutyTm is irrelevant.
					overlap += getPeakGMTOverlap (start, maintList[i].startTm);
				}
				start = maintList[i].endTm;
				// peak window resumes at end of maintenance/appointment.
			}
		}
	}

	if (start < peakStart + 60*optParam.peakDuration)
	{// First peak period so local time and remaining duty time matter.
		overlap += getPeakLocalOverlap (start, stop, peakStart, remainingDutyTm);
	}
	else
	{
		overlap += getPeakGMTOverlap (start, stop);
	}

	return (optParam.peakOverlapBenefit) * overlap;
}

/************************************************************************************************
*	Function	initializeCrewPairMatrix					Date last modified:  6/14/06 BGC	*
*	Purpose:	Creates and initializes the crewPairMatrix (see notes on top).					*
*************************************************************************************************/

static int 
initializeCrewPairMatrix (void)
{
	int i, j;

	if ((crewPairMatrix = (double **) calloc (numCrew, sizeof (double *))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in initializeCrewPairMatrix().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrew; i++)
	{
		if ((crewPairMatrix[i] = (double *) calloc (numCrew, sizeof (double))) == NULL)
		{
				logMsg(logFile,"%s Line %d: Out of Memory in initializeCrewPairMatrix().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
		}
		for (j=0; j<numCrew; j++)
		{
			crewPairMatrix [i][j] = 0;
		}
	}

	return 0;
}

/************************************************************************************************
*	Function	buildlegsFlownTogetherMatrices				Date last modified:  6/14/06 BGC	*
*	Purpose:	Creates two numCrew x numCrew matrices (see notes on top).						*
*				Element (i,j) of the matrix is the number of legs (in the current schedule)		*
*				flown together by crew pair [i,j].												*
************************************************************************************************/

static int 
buildLegsFlownTogetherMatrices (void)
{
	int i, j, lg;

	if ((legsFlownTogetherToday = (int **) calloc (numCrew, sizeof (int *))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in buildLegsFlownTogetherMatrices().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrew; i++)
	{
		if ((legsFlownTogetherToday[i] = (int *) calloc (numCrew, sizeof (int))) == NULL)
		{
				logMsg(logFile,"%s Line %d: Out of Memory in buildLegsFlownTogetherMatrices().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
		}

		for (j=0; j<numCrew; j++)
		{
			legsFlownTogetherToday[i][j] = 0;
		}
	}

	if ((legsFlownTogetherTomorrow = (int **) calloc (numCrew, sizeof (int *))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in buildLegsFlownTogetherMatrices().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrew; i++)
	{
		if ((legsFlownTogetherTomorrow[i] = (int *) calloc (numCrew, sizeof (int))) == NULL)
		{
				logMsg(logFile,"%s Line %d: Out of Memory in buildLegsFlownTogetherMatrices().\n", __FILE__,__LINE__);
				writeWarningData(myconn); exit(1);
		}

		for (j=0; j<numCrew; j++)
		{
			legsFlownTogetherTomorrow[i][j] = 0;
		}
	}

	for (lg = 0; lg < numLegs; lg ++)
	{
		if (legList[lg].crewPairInd >= 0)
		{
			if (legList[lg].schedOut < endOfToday)
			{
				legsFlownTogetherToday[crewPairList[legList[lg].crewPairInd].crewListInd[0]][crewPairList[legList[lg].crewPairInd].crewListInd[1]] ++;
				legsFlownTogetherToday[crewPairList[legList[lg].crewPairInd].crewListInd[1]][crewPairList[legList[lg].crewPairInd].crewListInd[0]] ++;
				// Both matrices are symmetric.
			}
			else if (legList[lg].schedOut < (endOfToday + 86400))
			{
				legsFlownTogetherTomorrow[crewPairList[legList[lg].crewPairInd].crewListInd[0]][crewPairList[legList[lg].crewPairInd].crewListInd[1]] ++;
				legsFlownTogetherTomorrow[crewPairList[legList[lg].crewPairInd].crewListInd[1]][crewPairList[legList[lg].crewPairInd].crewListInd[0]] ++;
			}
		}
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+\n");
		fprintf (pairCrewsLogFile, "| Crew 1 | Crew 2 | Today  |Tomorrow|\n");
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+\n");

		for (i=0; i<numCrew; i++)
		{
			for (j=0; j<numCrew; j++)
			{
				if ((legsFlownTogetherToday[i][j]) || (legsFlownTogetherTomorrow[i][j]))
				{
					fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d |\n",
						crewList[i].crewID,
						crewList[j].crewID,
						legsFlownTogetherToday[i][j],
						legsFlownTogetherTomorrow[i][j]);
				}
			}
		}
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+\n");
	}

	return 0;
}


/************************************************************************************************
*	Function	tourOverlap								Date last modified:  5/11/07 BGC		*
*	Purpose:	Returns the overlap between the tours (including overtime) of two pilots.		*
************************************************************************************************/

static double
tourOverlap (const int p1, const int p2)
{
	double overlapStart, overlapEnd;

	overlapStart = max ((double) crewList[p1].tourStartTm - 86400.0 * crewList[p1].startEarly,
						(double) crewList[p2].tourStartTm - 86400.0 * crewList[p2].startEarly);
	overlapEnd   = min ((double) crewList[p1].tourEndTm + 86400.0 * crewList[p1].stayLate,
						(double) crewList[p2].tourEndTm + 86400.0 * crewList[p2].stayLate);

	return (overlapEnd - overlapStart);
}

/************************************************************************************************
*	Function	identifyImpossiblePairings		Date last modified:  5/04/07 BGC, 04/24/07 SWO	*
*	Purpose:	Sets impossible pairing (i,j) to a negative number.								*
************************************************************************************************/

static int
identifyImpossiblePairings (void)
{
	int i, j, capInd, foInd, cati, catj;

	if ((reuseCrewPair = (int *) calloc (numCrewPairs, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in identifyImpossiblePairings().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrew; i++)
	{
		crewPairMatrix[i][i] = -1; // Pilot cannot be paired with himself.

		//exclude vac crews from pairing - VAC - 11/15/11 ANG
		if(crewList[i].vacCrew == 1){
			for (j=i+1; j<numCrew; j++)
			{
				crewPairMatrix [i][j] = -1;
				crewPairMatrix [j][i] = -1;
			}
		}
		
		//AD20171018
		//crew cannot be paired with crew from different qualification 
		//  for excel(actypeid 50) : 0  
		//  for king (actypeid 1)  : 1: proline, 2, fusion, 0 or 3 for dual qualified
		//RLZ 20171129 Fix: 3 is dual qualify and can be paired with 1 or 2.
		for (j=i+1; j<numCrew; j++)
		{
			if(crewList[i].qualification + crewList[j].qualification== 3 && crewList[i].qualification > 0 && crewList[j].qualification > 0){
				crewPairMatrix [i][j] = -1;
				crewPairMatrix [j][i] = -1;
			}
		}

		//exclude crews locked to certain aircraft (e.g. CJ4, XLS+) - DQ - 05/03/12 ANG
		if(crewList[i].lockedAcID > 0){
			for (j=i+1; j<numCrew; j++)
			{
				crewPairMatrix [i][j] = -1;
				crewPairMatrix [j][i] = -1;
			}
		}

		if (((crewList[i].tourStartTm - crewList[i].startEarly*86400) > optParam.windowEnd) ||
			(crewList[i].availDT > optParam.windowEnd) ||	
			((crewList[i].tourEndTm + crewList[i].stayLate*86400) < optParam.windowStart))
		{ // Pilot has no intersection with planning window.
			if (verbose)
			{
				fprintf (pairCrewsLogFile, "Pilot %d has no intersection with planning window.\n", crewList[i].crewID);
			}
			for (j=i+1; j<numCrew; j++)
			{
				crewPairMatrix [i][j] = -1;
				crewPairMatrix [j][i] = -1;
			}
		}

		if (hasHardPairing[i] == 1)
		{ // If pilot i is crewID1 in one or more hard pairing constraints, then he cannot be paired with any pilot not in the hard pairing set.
			if (verbose)
			{
				fprintf (pairCrewsLogFile, "hasHardPairing[]=1 for pilot %d.\n", crewList[i].crewID);
			}
			for (j=0; j<numCrew; j++)
			{
				if (pairingPriority[i][j] != 1)
				{
					crewPairMatrix[i][j] = -1;
					crewPairMatrix[j][i] = -1;
				}
			}
		}
	}

	for (i=0; i<numCrewPairs; i++)
	{
		reuseCrewPair[i] = 0;

		capInd = crewPairList[i].crewListInd[0];
		foInd = crewPairList[i].crewListInd[1];

		if (crewList[capInd].lockHome) // crew index capInd cannot be paired with anyone if locked to home.
		{
			if (verbose)
			{
				fprintf (pairCrewsLogFile, "Pilot %d locked to home.\n", crewList[i].crewID);
			}
			for (j=0; j<numCrew; j++) 
			{
				crewPairMatrix[capInd][j] = -2;
				crewPairMatrix[j][capInd] = -2;
			}
			reuseCrewPair[i] = 2;
		}

		if (crewList[foInd].lockHome) // crew index foInd cannot be paired with anyone if locked to home.
		{
			if (verbose)
			{
				fprintf (pairCrewsLogFile, "Pilot %d locked to home.\n", crewList[i].crewID);
			}
			for (j=0; j<numCrew; j++) 
			{
				crewPairMatrix[foInd][j] = -2;
				crewPairMatrix[j][foInd] = -2;
			}
			reuseCrewPair[i] = 2;
		}

		if ((crewPairList[i].optAircraftID > 0) || (optParam.pairingLevel == 1))
		{ // If a crew is locked to a plane, or if the pairing level is 1 so that all existing pairs are 
			//effectively locked, then the pair will be reused and the pilots will not be considered for new pairs 
			
			//EXCEPTION 1:  if the existing crew pair is NOT locked to a leg, and the crew pair violates
			//hard pairing constraints where one of the pilots is crewID1, then we will NOT keep the existing pair
			// - the hard constraint governs (see above)
			if (((crewPairList[i].inclDemandInd[0] == -1) && (((hasHardPairing[capInd]== 1) || (hasHardPairing[foInd]== 1)) && pairingPriority[capInd][foInd] != 1)))
			{
				continue;
			}
			//EXCEPTION 2:  if the existing crew pair (pilotA, pilotB) is NOT locked to a leg, and pilotA is crewID2 in a 
			//hard pairing constraint not satisfied by the existing crew pair, then we will keep the existing
			//pair, but we will also consider the hard pairing constraint for pilotA, and other (general) pairings for pilot B.
			else if ((crewPairList[i].inclDemandInd[0] == -1) && ((hasHardPairing[capInd]== 2) || (hasHardPairing[foInd]== 2)) && (pairingPriority[capInd][foInd] != 1))
			{
				if ((!crewList[foInd].lockHome) && (!crewList[capInd].lockHome))
				{
					reuseCrewPair[i] = 1;

					if (hasHardPairing[capInd] == 2)
					{
						for (j=0; j<numCrew; j++)
						{
							if ((pairingPriority[capInd][j] != 1) && (j != foInd))
							{
								crewPairMatrix[capInd][j] = -2;
								crewPairMatrix[j][capInd] = -2;
							}
							if (pairingPriority[capInd][j] == 1)
							{
								//set pairingPriority to -1 to mark this pair - we will use a slightly smaller priorityBenefit bonus
								//when generating pairings later in the code
								pairingPriority[capInd][j] = -1;
								pairingPriority[j][capInd] = -1;
							}	
						}
					}

					if (hasHardPairing[foInd] == 2)
					{
						for (j=0; j<numCrew; j++)
						{
							if ((pairingPriority[foInd][j]!=1) && (j!=capInd))
							{
								crewPairMatrix[foInd][j] = -2;
								crewPairMatrix[j][foInd] = -2;
							}
							if (pairingPriority[foInd][j] == 1)
							{
								//set pairingPriority to -1 to mark this pair - we will use a slightly smaller priorityBenefit bonus
								//when generating pairings later in the code
								pairingPriority[foInd][j] = -1;
								pairingPriority[j][foInd] = -1;
							}
						}
					}
					continue;
				}
			}
			//ELSE (NOT ONE OF THE EXCEPTIONS)
			else if ((!crewList[foInd].lockHome) && (!crewList[capInd].lockHome))
			{
				reuseCrewPair[i] = 1; 
				for (j=0; j<numCrew; j++)
				{	//pilots are not available for other pairs
					crewPairMatrix[capInd][j] = -2;
					crewPairMatrix[j][capInd] = -2;

					crewPairMatrix[foInd][j] = -2;
					crewPairMatrix[j][foInd] = -2;
				}
			}	
		}
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "%s, %d:\n", __FILE__, __LINE__);
		fprintf (pairCrewsLogFile, "Crew pair matrix\n");
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+---------+---------+\n");
		fprintf (pairCrewsLogFile,"| Crew1  | Crew 2 | C1acTp | C2acTp |   Value    | C1 qual | C2 qual |\n");
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+---------+---------+\n");

		for (i=0; i<numCrew; ++i)
		{
			for (j=0; j<numCrew; ++j)
			{
				fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d | %10.3f | %7d | %7d | %d %d\n", // DQ - 12/09/2009 ANG, XLS+ - 06/07/11 ANG
					crewList[i].crewID,
					crewList[j].crewID,
					crewList[i].aircraftTypeID,
					crewList[j].aircraftTypeID,
					crewPairMatrix[i][j],
					crewList[i].qualification,
					crewList[j].qualification,
					crewList[i].isDup, //XLS+ - 06/07/11 ANG
					crewList[j].isDup);
			}
		}
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+---------+---------+\n");
	}

	for (i=0; i<numCrew-1; i++){
		if ((cati = crewList[i].categoryID) == -1)
			cati = 11;

		for (j=i+1; j<numCrew; j++)
		{
			if ((catj = crewList[j].categoryID) == -1)
				catj = 11;
			
			if (crewList[i].aircraftTypeID != crewList[j].aircraftTypeID)
			{ // Both pilots must fly the same aircraft type.
				crewPairMatrix[i][j] = -1;
				crewPairMatrix[j][i] = -1;
			}
			
			else if (!isAllowablePairing (cati, catj))
			{
				/*
				*	Enforces pairing rules for check airmen, unrestricted, etc.
				*	(See definition of allowablePairings at top.)
				*	Note that this is hard-coded in pair.c and will have to be re-compiled if the
				*	pairing rules change.
				*/
				crewPairMatrix[i][j] = -1;
				crewPairMatrix[j][i] = -1;
			}

			if (tourOverlap (i, j) <= 0)
			{
				crewPairMatrix[i][j] = -1;
				crewPairMatrix[j][i] = -1;
			}
		}
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "%s, %d:\n", __FILE__, __LINE__);
		fprintf (pairCrewsLogFile, "Crew pair matrix\n");
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+\n");
		fprintf (pairCrewsLogFile,"| Crew1  | Crew 2 | C1acTp | C2acTp |   Value    |\n");
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+\n");

		for (i=0; i<numCrew; ++i)
		{
			for (j=0; j<numCrew; ++j)
			{
				fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d | %10.3f | %d %d\n", //XLS+ - 06/07/11 ANG
					crewList[i].crewID,
					crewList[j].crewID,
					crewList[i].aircraftTypeID,
					crewList[j].aircraftTypeID,
					crewPairMatrix[i][j],
					crewList[i].isDup,
					crewList[j].isDup);
			}
		}
		fprintf (pairCrewsLogFile,"+--------+--------+--------+--------+------------+\n");
	}

	for (i=0; i<numCrewPairs; i++)
	{
		j=0;
		while (*(crewPairList[i].aircraftID+j))
		{
			if (*(crewPairList[i].lockTour+j))
			{
				reuseCrewPair[i] = 2;
			}
			j ++;
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	computePairingBenefit						Date last modified:  4/24/07 SWO	*
*																				 5/10/07 BGC	*	
*	Purpose:	Given crewIndex i and j, computes the benefit of pairing pilots i and j.		*
************************************************************************************************/

static double
computePairingBenefit (const int p1, const int p2)
{
	double benefit = 0;
	time_t endTm;
	int repoFltTm=0, repoElapsedTm=0, repoBlkTm = 0, repoStops = 0; //For close pilot base bonus - 10/08/09 ANG

	// -- Hard/soft pairing benefit.
	if (pairingPriority[p1][p2] > 0)
	{
		benefit += optParam.priorityBenefit[pairingPriority[p1][p2]-1]; // pairingPriority is 1..4 but priorityBenefit is 0..3
	}
	else if (pairingPriority[p1][p2] == -1) 
	{
		//we previously set pairingPriority = -1 in identifyImpossiblePairings
		benefit += 0.5*(optParam.priorityBenefit[0] + optParam.priorityBenefit[1]);
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "| %6d | %6d | %10.3f | (Pairing priority)\n",
			crewList[p1].crewID,
			crewList[p2].crewID,
			benefit,
			crewList[p1].isDup, //XLS+ - 06/09/11 ANG
			crewList[p2].isDup);
	}
	//else (pairingPriority[p1][p2] == 0)

	// -- Benefit for not splitting a crew pair that is scheduled to fly legs in the current schedule.

	benefit += optParam.changeTodayPenalty * legsFlownTogetherToday[p1][p2] + 
				optParam.changeNxtDayPenalty * legsFlownTogetherTomorrow[p1][p2];

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "| %6d | %6d | %10.3f | %d %d (Change penalty) \n",
			crewList[p1].crewID,
			crewList[p2].crewID,
			benefit,
			crewList[p1].isDup, //XLS+ - 06/09/11 ANG
			crewList[p2].isDup);
	}
	// -- Benefit for intersection outside the planning window.

	endTm = min (crewList[p1].tourEndTm + (time_t) crewList[p1].stayLate * 86400, 
									 crewList[p2].tourEndTm + (time_t) crewList[p2].stayLate * 86400);

	benefit += optParam.beyondPlanningWindowBenefit * (max (difftime(endTm, optParam.windowEnd), 0))/3600.0;

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "| %6d | %6d | %10.3f | %d %d (Beyond planning window benefit)\n",
			crewList[p1].crewID,
			crewList[p2].crewID,
			benefit,
			crewList[p1].isDup, //XLS+ - 06/09/11 ANG
			crewList[p2].isDup);
	}

	//START - Bonus for pilots having the same or close by bases - 10/08/09 ANG
	//-- Add benefit for having close-by bases
	//Check distant between pilots' bases, if the distance is less than certain threshold value, grant the bonus

	if(crewList[p1].startLoc == crewList[p2].startLoc && crewList[p1].endLoc == crewList[p2].endLoc){
		benefit += optParam.pairSameBaseBonus[0];
	}
	else {
		//get flight time between base
		getFlightTime(crewList[p1].startLoc, crewList[p2].startLoc, crewList[p1].aircraftTypeID, month, 0, &repoFltTm, &repoBlkTm, &repoElapsedTm, &repoStops);
		if(repoFltTm <= 15)
			benefit += optParam.pairSameBaseBonus[1];
		else if (repoFltTm <= 30)
			benefit += optParam.pairSameBaseBonus[2];
		else if (repoFltTm <= 60)
			benefit += optParam.pairSameBaseBonus[3];
		else if (repoFltTm <= 120)
			benefit += optParam.pairSameBaseBonus[4];
	}
    
	if (verbose)
	{
		fprintf (pairCrewsLogFile, "| %6d | %6d | %10.3f | %d %d (Close pilot base benefit)\n",
			crewList[p1].crewID,
			crewList[p2].crewID,
			benefit,
			crewList[p1].isDup, //XLS+ - 06/09/11 ANG
			crewList[p2].isDup);
	}
	//END - Bonus for pilots having the same or close by bases - 10/08/09 ANG

	return benefit;
}


/************************************************************************************************
*	Function	getCrewIndex								Date last modified:  5/03/07 BGC	*
*	Purpose:	Returns the index of a given crew ID. Could possibly be done by binary			*
*				search, but the number of calls is small and numCrew is small.					*	
************************************************************************************************/

static int
//getCrewIndex (const int crewID)
getCrewIndex (const int crewID, const int isDup)
{
	int i;

	for (i=(numCrew-1); i>=0; i--)
	{
		//if (crewList[i].crewID == crewID)
		if (crewList[i].crewID == crewID && crewList[i].isDup == isDup)
		{
			return i;
		}
	}
	return -1;
}

/************************************************************************************************
*	Function	tourLocked						Date last modified:  05/03/07 BGC; 9/7/06 SWO	*
*	Purpose:	Checks if a given pair of pilots have had a tour locked.						*	
************************************************************************************************/

static int
tourLocked (const int crewID1, const int crewID2)
{
	int i, j;

	for (i=0; i<numCrewPairs; i++)
	{
		if (((crewPairList[i].captainID == crewID1) && (crewPairList[i].flightOffID == crewID2)) 
			|| ((crewPairList[i].captainID == crewID2) && (crewPairList[i].flightOffID == crewID1)))
		{
			for (j=0; crewPairList[i].aircraftID[j]; j++)
			{
				if (crewPairList[i].lockTour[j])
				{
					return 1;
				}
			}
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	tourLocked2						Date last modified: 4/5/07 SWO;	05/03/07 BGC	*
*	Purpose:	Checks if there is a locked tour that satisfies a pair constraint for a pilot	*
*				and a crew category.															*	
************************************************************************************************/

static int
tourLocked2 (const int crewID1, const int categoryID)
{
	int i, j;

	for (i=0; i<numCrewPairs; i++)
	{
		if (((crewPairList[i].captainID == crewID1) && (crewList[crewPairList[i].crewListInd[1]].categoryID == categoryID)) 
			|| ((crewPairList[i].flightOffID == crewID1) && (crewList[crewPairList[i].crewListInd[0]].categoryID == categoryID)))
		{
			for (j=0; crewPairList[i].aircraftID[j]; j++)
			{
				if (crewPairList[i].lockTour[j])
				{
					return 1;
				}
			}
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	getPairingPriorities			Date last modified:  05/03/07 BGC, 04/27/07 SWO	*
*	Purpose:	Gets the pairing priorities from pairConstraints and populates					*
*				the pairing priority matrix.													*
************************************************************************************************/

static int
getPairingPriorities (void)
{
	int i, j, k, p1, p2, *considerPairing;

	if ((pairingPriority = (int **) calloc (numCrew, sizeof (int *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getPairingPriorities().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((hasHardPairing = (int *) calloc (numCrew, sizeof (int))) == NULL){
			logMsg(logFile,"%s Line %d: Out of Memory in getPairingPriorities().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrew; i++)
	{
		if ((pairingPriority[i] = (int *) calloc (numCrew, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getPairingPriorities().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		hasHardPairing[i] = 0;

		for (j=0; j<numCrew; j++)
		{
			pairingPriority [i][j] = 0;
		}
	}	

	if (!numPairConstraints)
	{
		return 0;
	}

	qsort(pairConstraintList, numPairConstraints, sizeof(PairConstraint), comparePairConstraints);
	// Pair constraint list has been sorted by increasing start time, then by crew1ID, then by priority.


	if (verbose)
	{
		char opbuf[1024];
		DateTime startTime;
		fprintf (pairCrewsLogFile, "Num pair constraints: %d\n", numPairConstraints);
		for (i=0; i<numPairConstraints; ++i)
		{
			startTime = dt_time_tToDateTime (pairConstraintList[i].startTm);
			fprintf (pairCrewsLogFile, "ConsiderPairing[%d] = 1: Pilot %d with pilot %d or category %d with priority %d at time %s\n", 
				i,
				pairConstraintList[i].crew1ID,
				pairConstraintList[i].crew2ID,
				pairConstraintList[i].categoryID,
				pairConstraintList[i].priority, 
				dt_DateTimeToDateTimeString(startTime, opbuf, "%Y/%m/%d %H:%M"));
		}
	}

	if ((considerPairing = (int *) calloc (numPairConstraints, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getPairingPriorities().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	
	for (i=0; i<numPairConstraints; i++)
	{
		considerPairing[i] = 1;
	}

	for (i=0; i<numPairConstraints; i++)
	{
		// If a crew pair already has a locked tour that satisfies a pair constraint, there is no need to consider the constraint
		if (pairConstraintList[i].crew2ID > 0)
		{
			if (tourLocked (pairConstraintList[i].crew1ID, pairConstraintList[i].crew2ID))
			{	
				considerPairing[i] = 0;
			}
		}
		else 
		{//pairConstraintList[i].crew2ID == 0
			if (tourLocked2 (pairConstraintList[i].crew1ID, pairConstraintList[i].categoryID))
			{	
				considerPairing[i] = 0;
			}
		}
		//if a pair constraint has been satisfied by a locked tour, ALL pair constraints for the same pilot, 
		//same date, and same or lower priority have been satisfied and all can be ignored 
		//(there can be multiple constraints for a pilot on one date,
		// for instance if he should be paired with someone from one of two categories)
		if (considerPairing[i] == 0)
		{ 
			for (k=0; k<numPairConstraints; k++)
			{
				if ((pairConstraintList[k].crew1ID == pairConstraintList[i].crew1ID)
					&& (pairConstraintList[k].startTm == pairConstraintList[i].startTm) 
					&& (pairConstraintList[k].priority >= pairConstraintList[i].priority))
				{
					considerPairing[k] = 0;
				}
			}
		}
	}

	// If there is an (unsatisfied) hard pairing constraint for a pilot, ignore any pair constraints with a later date.
	for (i=0; i<numPairConstraints; i++)
	{
		if ((considerPairing[i] == 1) && (pairConstraintList[i].priority == 1))
		{
			for(k=i+1; k<numPairConstraints; k++)
			{//constraints are sorted by date
				if (pairConstraintList[k].startTm > pairConstraintList[i].startTm)
				{
					if ((pairConstraintList[i].crew1ID == pairConstraintList[k].crew1ID) ||
						(pairConstraintList[i].crew1ID == pairConstraintList[k].crew2ID))					
					{
						considerPairing[k] = 0;
					}

					if ((pairConstraintList[i].crew2ID > 0) &&
						((pairConstraintList[i].crew2ID == pairConstraintList[k].crew1ID) || 
						(pairConstraintList[i].crew2ID == pairConstraintList[k].crew2ID)))
					{
						considerPairing[k] = 0;
					}
				}
			}
		}
	}

	for (i=0; i<numPairConstraints; i++)
	{
		if (considerPairing[i])
		{
			if (verbose)
			{
				fprintf (pairCrewsLogFile, "ConsiderPairing[%d] = 1: Pilot %d with pilot %d or category %d with priority %d\n", 
					i,
					pairConstraintList[i].crew1ID,
					pairConstraintList[i].crew2ID,
					pairConstraintList[i].categoryID,
					pairConstraintList[i].priority);
			}

			//p1 = getCrewIndex (pairConstraintList[i].crew1ID);
			p1 = getCrewIndex (pairConstraintList[i].crew1ID, 0);
	
			if (p1 < 0)
			{
				logMsg(logFile,"%s Line %d: Crew ID %d in pair constraint %d not found.\n",
					__FILE__, __LINE__, pairConstraintList[i].crew1ID,pairConstraintList[i].pairConstraintID);
				continue;
			}

			if (pairConstraintList[i].priority == 1)
			{
				hasHardPairing[p1] = 1;
			}

			if (pairConstraintList[i].crew2ID > 0)
			{
				//p2 = getCrewIndex (pairConstraintList[i].crew2ID);
				p2 = getCrewIndex (pairConstraintList[i].crew2ID, crewList[p1].isDup);

				if (p2 < 0)
				{
					logMsg(logFile,"%s Line %d: Crew ID %d in pair constraint %d not found.\n",
						__FILE__, __LINE__, pairConstraintList[i].crew2ID,pairConstraintList[i].pairConstraintID);
					continue;
				}
				pairingPriority[p1][p2] = pairConstraintList[i].priority;

				if ((pairConstraintList[i].priority == 1) && (hasHardPairing[p2] == 0))
				{
					hasHardPairing[p2] = 2;
				}
			}
			else
			{// pilot has to be paired with category
				for (j=0; j<numCrew; j++)
				{
					if (crewList[j].categoryID == pairConstraintList[i].categoryID)
					{
						pairingPriority[p1][j] = (pairingPriority[p1][j] == 0? 
												  pairConstraintList[i].priority : 
												  min(pairingPriority[p1][j], pairConstraintList[i].priority));
					}
				}
			}
		}
	}

	// Pairing priority of [i,j] is the minimum of pairing priorities [i,j] and [j,i] (unless either of them is zero).
	for (i=0; i<numCrew; i++)
	{
		for (j=0; j<numCrew; j++)
		{
			if (pairingPriority[i][j]) 
			{
				if (pairingPriority[j][i])
				{
					pairingPriority[i][j] = min (pairingPriority[i][j], pairingPriority[j][i]);
				}
			}
			else
			{
				pairingPriority[i][j] = pairingPriority[j][i];
			}
		}
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "Read the following pairing priorities.\n");
		fprintf (pairCrewsLogFile, "+--------+--------+--------+\n");
		fprintf (pairCrewsLogFile, "| Crew 1 | Crew 2 |Priority|\n");
		fprintf (pairCrewsLogFile, "+--------+--------+--------+\n");
		for (i=0; i<numCrew-1; ++i)
		{
			for (j=i+1; j<numCrew; ++j)
			{
				if (pairingPriority[i][j])
				{
					fprintf (pairCrewsLogFile, "| %6d | %6d | %6d |\n",
						crewList[i].crewID,
						crewList[j].crewID,
						pairingPriority[i][j]);
				}
			}
		}
		fprintf (pairCrewsLogFile, "+--------+--------+--------+\n");

		for (i=0; i<numCrew; i++)
		{
			if (hasHardPairing[i])
			{
				fprintf (pairCrewsLogFile, "hasHardPairing for pilot %d = %d\n", crewList[i].crewID, hasHardPairing[i]);
			}
		}
	}

	free(considerPairing);
	considerPairing = NULL;

	return 0;
}

/************************************************************************************************
*	Function	copyExistingCrewPair			Date last modified:  05/03/07 BGC, 4/24/07 SWO	*
*	Purpose:	Makes a copy of an existing crew pair (with index i) and places it				*
*				in the new crew pair list cpList in index position *ncp.						*
************************************************************************************************/

static int
copyExistingCrewPair (int i, CrewPair *cpList, int *ncp)
{
	int j, k;

	cpList[*(ncp)].crewPairID = oldCrewPairList[i].crewPairID;
	cpList[*(ncp)].captainID = oldCrewPairList[i].captainID;
	cpList[*(ncp)].flightOffID = oldCrewPairList[i].flightOffID;
	cpList[*(ncp)].crewListInd[0] = oldCrewPairList[i].crewListInd[0];
	cpList[*(ncp)].crewListInd[1] = oldCrewPairList[i].crewListInd[1];

	for (k=0; k<numAircraft; k++)
	{
		if (oldCrewPairList[i].aircraftID[k] == 0)
			break;
	}
	k ++;

	if ((cpList[*(ncp)].aircraftID = (int *) calloc (k+1, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in copyExistingCrewPair().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (j=0; j<k+1; j++)
	{
		cpList[*(ncp)].aircraftID[j] = oldCrewPairList[i].aircraftID[j];
	}

	if ((cpList[*(ncp)].lockTour = (int *) calloc (k+1, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in copyExistingCrewPair().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (j=0; j<k+1; j++)
	{
		cpList[*(ncp)].lockTour[j] = oldCrewPairList[i].lockTour[j];
	}

	cpList[*(ncp)].hasFlownFirst = oldCrewPairList[i].hasFlownFirst;
	cpList[*(ncp)].optAircraftID = oldCrewPairList[i].optAircraftID;
	cpList[*(ncp)].acInd = oldCrewPairList[i].acInd;
	cpList[*(ncp)].acTypeIndex = oldCrewPairList[i].acTypeIndex;
	cpList[*(ncp)].pairStartTm = oldCrewPairList[i].pairStartTm;
	cpList[*(ncp)].pairEndTm = oldCrewPairList[i].pairEndTm;
	cpList[*(ncp)].crewPairBonus = oldCrewPairList[i].crewPairBonus;  // NOT NEEDED - crewPairBonuses are set in getPairingBonusesAndSort at end
	cpList[*(ncp)].availAptID = oldCrewPairList[i].availAptID;
	cpList[*(ncp)].availDT = oldCrewPairList[i].availDT;
	cpList[*(ncp)].activityCode = oldCrewPairList[i].activityCode;
	cpList[*(ncp)].dutyTime = oldCrewPairList[i].dutyTime;
	cpList[*(ncp)].blockTm = oldCrewPairList[i].blockTm;
	for (j=0; j<MAX_LEGS; j++)
	{
		cpList[*(ncp)].schedLegIndList[j] = oldCrewPairList[i].schedLegIndList[j];
		cpList[*(ncp)].schedLegACIndList[j] = oldCrewPairList[i].schedLegACIndList[j];
	}
	for (j=0; j<MAX_LEG_INCL; j++)
	{
		cpList[*(ncp)].inclDemandInd[j] = oldCrewPairList[i].inclDemandInd[j];
	}
	cpList[*(ncp)].numIncl = oldCrewPairList[i].numIncl;	
	cpList[*(ncp)].crewPlaneList = oldCrewPairList[i].crewPlaneList;
	cpList[*(ncp)].crewPUSList = oldCrewPairList[i].crewPUSList;
	cpList[*(ncp)].crewPUEList = oldCrewPairList[i].crewPUEList;
	cpList[*(ncp)].numPlaneArcs = oldCrewPairList[i].numPlaneArcs;
	cpList[*(ncp)].numPUStartArcs = oldCrewPairList[i].numPUStartArcs;
	cpList[*(ncp)].numPUEndArcs = oldCrewPairList[i].numPUEndArcs;
	cpList[*(ncp)].getHomeCost = oldCrewPairList[i].getHomeCost;
	cpList[*(ncp)].nodeStartIndex = oldCrewPairList[i].nodeStartIndex;
	cpList[*(ncp)].startDay= oldCrewPairList[i].startDay;
	cpList[*(ncp)].endDay = oldCrewPairList[i].endDay;
	cpList[*(ncp)].endRegDay = oldCrewPairList[i].endRegDay;

	(*ncp) ++;

	if(optParam.pairingLevel != 3)
	{
		reuseCrewPair[i] = 0;
	}

	return 0;
}

/************************************************************************************************
*	Function	addCrewPair				Date last modified:  6/25/06 BGC,  updated 4/24/07 SWO	*
*	Purpose:	Adds a crew pair to the new crew pair list. If this crew pair already			*
*				exists in the old crew pair list, the record is simply copied into				*
*				the new crew pair list. If it doesn't exist, a new one is created.				*
************************************************************************************************/

static int 
addCrewPair (MatchingArc *matArc, CrewPair *cpList, int *ncp)
{
	int i, j;
	for (i=0; i<numCrewPairs; i++)
	{
		//if (((crewList[matArc->p1].crewID == oldCrewPairList[i].captainID) && 
		//	(crewList[matArc->p2].crewID == oldCrewPairList[i].flightOffID)) ||
		//	((crewList[matArc->p1].crewID == oldCrewPairList[i].flightOffID) && 
		//	(crewList[matArc->p2].crewID == oldCrewPairList[i].captainID)))
		//{
		//	copyExistingCrewPair (i, cpList, ncp);
		//	return 0;
		//}

		//START - Above check is modified to accommodate DQ pairs - DQ - 12/09/2009 ANG
		if ((((crewList[matArc->p1].crewID == oldCrewPairList[i].captainID) && 
			(crewList[matArc->p2].crewID == oldCrewPairList[i].flightOffID)) ||
			((crewList[matArc->p1].crewID == oldCrewPairList[i].flightOffID) && 
			(crewList[matArc->p2].crewID == oldCrewPairList[i].captainID))) && 
			(crewList[matArc->p1].acTypeIndex == oldCrewPairList[i].acTypeIndex) &&
			(crewList[matArc->p2].acTypeIndex == oldCrewPairList[i].acTypeIndex) &&
			(crewList[matArc->p1].isDup == crewList[oldCrewPairList[i].crewListInd[0]].isDup) && //XLS+ - 06/07/11 ANG
			(crewList[matArc->p2].isDup == crewList[oldCrewPairList[i].crewListInd[1]].isDup))
		{ 
			copyExistingCrewPair (i, cpList, ncp);
			return 0;
		}
		//END - DQ - 12/09/2009 ANG
	}

	//START - 12/11/2009 ANG
	//Next, check if the new crew pair is already added
	//Should never happened, but check anyway, just in case - ANG
	/*for (i=numCrewPairs; i<(*(ncp)); i++)
	{
		if (((crewList[matArc->p1].crewID == cpList[i].captainID) && 
			(crewList[matArc->p2].crewID == cpList[i].flightOffID)) ||
			((crewList[matArc->p1].crewID == cpList[i].flightOffID) && 
			(crewList[matArc->p2].crewID == cpList[i].captainID)) && 
			(crewList[matArc->p1].acTypeIndex == cpList[i].acTypeIndex) &&
			(crewList[matArc->p2].acTypeIndex == cpList[i].acTypeIndex))
		{
			//could add print out here - ANG
			return 0;
		}
	}*/
	//END - 12/11/2009 ANG

	maxCPID ++; // The new crew pair id is the maximum of those so far + 1.
	cpList[*(ncp)].crewPairID = maxCPID;
	if (crewList[matArc->p1].categoryID < crewList[matArc->p2].categoryID)
	{
		cpList[*(ncp)].captainID = crewList[matArc->p1].crewID;
		cpList[*(ncp)].flightOffID = crewList[matArc->p2].crewID;
		cpList[*(ncp)].crewListInd[0] = matArc->p1;
		cpList[*(ncp)].crewListInd[1] = matArc->p2;
	}
	else
	{
		cpList[*(ncp)].captainID = crewList[matArc->p2].crewID;
		cpList[*(ncp)].flightOffID = crewList[matArc->p1].crewID;
		cpList[*(ncp)].crewListInd[0] = matArc->p2;
		cpList[*(ncp)].crewListInd[1] = matArc->p1;
	}

	if ((cpList[*(ncp)].aircraftID = (int *) calloc (1, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in addCrewPair().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	cpList[*(ncp)].aircraftID[0] = 0;
	cpList[*(ncp)].hasFlownFirst = 0;
	if ((cpList[*(ncp)].lockTour = (int *) calloc (1, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in addCrewPair().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	cpList[*(ncp)].lockTour[0] = 0;
	cpList[*(ncp)].optAircraftID = 0;
	cpList[*(ncp)].acInd = -1;
	cpList[*(ncp)].acTypeIndex = crewList[matArc->p1].acTypeIndex;
	cpList[*(ncp)].pairStartTm = (time_t) max((crewList[matArc->p1].tourStartTm - 86400*crewList[matArc->p1].startEarly), 
						(crewList[matArc->p2].tourStartTm - 86400*crewList[matArc->p2].startEarly));
	cpList[*(ncp)].pairEndTm = (time_t) min((crewList[matArc->p1].tourEndTm + 86400*crewList[matArc->p1].stayLate), 
						(crewList[matArc->p2].tourEndTm + 86400*crewList[matArc->p2].stayLate));
	//NOT NEEDED - crewPairBonuses are set in getPairingBonusesAndSort at end
	if (pairingPriority[matArc->p1][matArc->p2] > 0)
	{
		cpList[*(ncp)].crewPairBonus =  optParam.priorityBenefit[pairingPriority[matArc->p1][matArc->p2]-1];
	}
	else
	{
		cpList[*(ncp)].crewPairBonus = 0;
	}

	// If two crew members have the same airport, start and end times,...., then populate availApt ID, etc.
	if(crewList[cpList[*(ncp)].crewListInd[0]].availAirportID == crewList[cpList[*(ncp)].crewListInd[1]].availAirportID &&
		crewList[cpList[*(ncp)].crewListInd[0]].availDT == crewList[cpList[*(ncp)].crewListInd[1]].availDT && 
		crewList[cpList[*(ncp)].crewListInd[0]].dutyTime == crewList[cpList[*(ncp)].crewListInd[1]].dutyTime)
	{
		cpList[*(ncp)].availAptID = crewList[cpList[*(ncp)].crewListInd[0]].availAirportID;
		cpList[*(ncp)].availDT = crewList[cpList[*(ncp)].crewListInd[0]].availDT;
		cpList[*(ncp)].blockTm =  crewList[cpList[*(ncp)].crewListInd[0]].blockTm;
		cpList[*(ncp)].dutyTime = crewList[cpList[*(ncp)].crewListInd[0]].dutyTime;
		cpList[*(ncp)].activityCode = crewList[cpList[*(ncp)].crewListInd[0]].activityCode;
	}
	else {
		cpList[*(ncp)].availAptID = 0;
		cpList[*(ncp)].availDT = 0;
		cpList[*(ncp)].activityCode = -1;
		cpList[*(ncp)].dutyTime = 0;
		cpList[*(ncp)].blockTm = 0;
	}
	for (j=0; j<MAX_LEGS; j++)
	{
		cpList[*(ncp)].schedLegIndList[j] = -1;
		cpList[*(ncp)].schedLegACIndList[j] = -1;
	}
	for (j=0; j<MAX_LEG_INCL; j++)
	{
		cpList[*(ncp)].inclDemandInd[j] = -1;
	}
	cpList[*(ncp)].numIncl = 0;	
	cpList[*(ncp)].crewPlaneList = NULL;
	cpList[*(ncp)].crewPUSList = NULL;
	cpList[*(ncp)].crewPUEList = NULL;
	cpList[*(ncp)].numPlaneArcs = 0;
	cpList[*(ncp)].numPUStartArcs = 0;
	cpList[*(ncp)].numPUEndArcs = 0;
	cpList[*(ncp)].getHomeCost = NULL;
	cpList[*(ncp)].nodeStartIndex = -1;

	(*ncp) ++;
	return 0;
}

/************************************************************************************************
*	Function	buildNewCrewPairList	Date last modified:  8/XX/06 BGC, updated 01/16/07 SWO	*
*	Purpose:	Creates a new crew pair list given the optimal solution to the pairing problem.	*
************************************************************************************************/

static int
buildNewCrewPairList (void)
{
	int ncp = 0, i;

	CrewPair *newCrewPairList;
	MatchingArc *current = matchingArcs;
	
	maxCPID = 1;

	for (i=0; i<numMatchingArcs; i++)
	{
		if (optMatching[i])
			ncp ++;  // Count the number of new crew pairs. Equals the number of arcs in the optimal matching solution.
	}
	for (i=0; i<numCrewPairs; i++)
	{
		if (crewPairList[i].crewPairID > maxCPID)
			maxCPID = crewPairList[i].crewPairID;
		if(optParam.pairingLevel <2){ //the number of crew pairs in the new list equals new pairings + reused pairs
			if (reuseCrewPair[i])
				ncp ++;
		}
	}
	if(optParam.pairingLevel == 2){ //the number of crew pairs in the new list will ultimately include new pairings 
		//from run with pairingLevel == 0 as above plus all (or nearly so) existing pairs plus new pairings that will be 
		//created for those not paired previously (by running with pairingLevel 1)
		ncp += numCrewPairs + (int)((numCrew - numCrewPairs*2)*0.5);
	}
	if ((newCrewPairList = (CrewPair *) calloc (ncp + 1, sizeof (CrewPair))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in buildNewCrewPairList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}
	ncp = 0;

	for (i=0; i<numMatchingArcs; i++)
	{
		if (optMatching[i])
		{
			addCrewPair (current, newCrewPairList, &ncp);
			//check if we need to duplicate the pure DQ pair - DQ - 01/28/10 ANG
		}
		current = current->next;
	}

	for (i=0; i<numCrewPairs; i++)
	{
		if (reuseCrewPair[i] == 1)
		{
			copyExistingCrewPair (i, newCrewPairList, &ncp);
		}
	}

	numOptCrewPairs = ncp;

	for (i=0; i<numCrewPairs; i++)
	{
		if (reuseCrewPair[i] == 2)
		{
			copyExistingCrewPair (i, newCrewPairList, &ncp);
		}
	}
	crewPairList = newCrewPairList;
	numCrewPairs = ncp;

	return 0;
}

/************************************************************************************************
*	Function	buildNewCrewPairListWithoutPairing			Date last modified:  03/15/07 SWO	*
*															Date last modified:  05/03/07 BGC	*
*	Purpose:	Creates a new crew pair list by removing pairs that are locked to home from		*
*				the optimization.  Used when we bypass crew pairing.							*
************************************************************************************************/

static int
buildNewCrewPairListWithoutPairing (void)
{
	int ncp = 0, i, p1, p2;
	CrewPair *newCrewPairList;

	oldCrewPairList = crewPairList;
	numOldCrewPairs = numCrewPairs;

	if ((newCrewPairList = (CrewPair *) calloc (numCrewPairs + 1, sizeof (CrewPair))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in buildNewCrewPairListWithoutPairing().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numCrewPairs; i++)
	{
		if ((crewList[crewPairList[i].crewListInd[0]].lockHome == 0) && (crewList[crewPairList[i].crewListInd[1]].lockHome == 0))
		{
			copyExistingCrewPair (i, newCrewPairList, &ncp);
		}
	}
	numOptCrewPairs = ncp;

	for (i=0; i<numCrewPairs; i++)
	{
		if ((crewList[crewPairList[i].crewListInd[0]].lockHome != 0) || (crewList[crewPairList[i].crewListInd[1]].lockHome != 0))
		{
			copyExistingCrewPair (i, newCrewPairList, &ncp);
		}
	}
	crewPairList = newCrewPairList;

	//* crewPairBonus is not needed onward - 10/15/10 ANG
	for (i=0; i<numCrewPairs; i++)
	{
		p1 = crewPairList[i].crewListInd[0];
		p2 = crewPairList[i].crewListInd[1];

		if (pairingPriority[p1][p2] > 0) //won't ever be negative if we bypass crew pairing
		{
			crewPairList[i].crewPairBonus = optParam.priorityBenefit[pairingPriority[p1][p2]-1];
		}
		else 
		{
			crewPairList[i].crewPairBonus = 0;
		}
		crewPairList[i].crewPairBonus = crewPairList[i].crewPairBonus * optParam.priorityBftWt;  //Scale up/down instead of removing it completely RLZ 10/26/2010
	}
	//crewPairBonus is not needed onward - 10/15/10 ANG*/



	qsort(oldCrewPairList, numOldCrewPairs, sizeof(CrewPair), compareCrewPairIDs);
	qsort(crewPairList, numOptCrewPairs, sizeof(CrewPair), compareCrewPairIDs);
	qsort((crewPairList+numOptCrewPairs), numCrewPairs-numOptCrewPairs, sizeof(CrewPair), compareCrewPairIDs);

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "Num opt crew pairs: %d\n", numOptCrewPairs);
		fprintf (pairCrewsLogFile, "Num crew pairs    : %d\n", numCrewPairs);
	}

	return 0;
}

/************************************************************************************************
*	Function	enumerateCrewPairs							Date last modified:  8/10/06 BGC	*
*	Purpose:	Identifies all possible pairings. At the end of this function, the				*
*				crewPairMatrix is populated with the benefit for each pairing.					*
*				Crew pairs with a negative benefit will not be paired.							*
************************************************************************************************/

static int
enumerateCrewPairs (void)
{
	int i, j, k=0;

	getPairingPriorities ();

	buildLegsFlownTogetherMatrices ();

	initializeCrewPairMatrix ();

	identifyImpossiblePairings ();

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
		fprintf (pairCrewsLogFile, "| Crew 1 | Crew 2 |  Benefit   |\n");
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
	}

	for (i=0; i<numCrew-1; i++)
	{
		for (j=i+1; j<numCrew; j++)
		{
			if (!crewPairMatrix[i][j]) 
				// So far, crewPairMatrix = 0 if pairing is allowed; < 0 otherwise.
			{
				k ++;
				crewPairMatrix[i][j] = computePairingBenefit (i,j);
				crewPairMatrix[j][i] = crewPairMatrix[i][j]; 
				// Matrix is symmetric. 
			}
		}
	}
	
	if (verbose)
	{
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
	}

	if (verbose)
	{
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
		fprintf (pairCrewsLogFile, "| Crew 1 | Crew 2 |  Benefit   |\n");
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
		for (i=0; i<numCrew-1; i++)
		{
			for (j=i+1; j<numCrew; j++)
			{
				if (crewPairMatrix[i][j] >= 0)
				{
					fprintf (pairCrewsLogFile, "| %6d | %6d | %10.3f | %d %d\n", // DQ - 12/09/2009 ANG, //XLS+ - 06/07/11 ANG
						crewList[i].crewID,
						crewList[j].crewID,
						crewPairMatrix[i][j],
						crewList[i].isDup,
						crewList[j].isDup);
				}
			}	
		}
		fprintf (pairCrewsLogFile, "+--------+--------+------------+\n");
	}

	logMsg (pairCrewsLogFile, "Num enumerated crew pairs: %d.\n", k);

	return 0;
}

static int
showCrewSwapNode (CrewSwap *swap)
{
	char opbuf1[1024], opbuf2[1024];
	DateTime swapStart, swapEnd;

	swapStart = dt_time_tToDateTime (swap->swapStart);
	swapEnd = dt_time_tToDateTime (swap->swapEnd);

	//fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d | %15s | %15s | %10.3f |\n",
	fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d | %6d | %6d | %15s | %15s | %10.3f | %d %d", //fei FA
		swap->airportID,
		swap->acID, 
		//crewPairList[swap->crewInd].crewPairID? crewList[crewPairList[swap->crewInd].crewListInd[0]].crewID : 0,
		//crewPairList[swap->crewInd].crewPairID? crewList[crewPairList[swap->crewInd].crewListInd[1]].crewID : 0,
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[0]].crewID : 0,
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[1]].crewID : 0,
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[0]].aircraftTypeID : 0,
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[1]].aircraftTypeID : 0,
		dt_DateTimeToDateTimeString(swapStart, opbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(swapEnd, opbuf2, "%Y/%m/%d %H:%M"),
		swap->benefit,
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[0]].isDup : 0, //XLS+ - 06/09/11 ANG
		(swap->crewInd > -1)? crewList[crewPairList[swap->crewInd].crewListInd[1]].isDup : 0);
	return 0;
}

/************************************************************************************************
*	Function	addCrewSwapNode								Date last modified:  5/08/07 BGC	*
*	Purpose:	Adds a "right-side" crew swap node (see struct CrewSwap at top.					*
************************************************************************************************/

static int
addCrewSwapNode (int aircraftID, int aircraftTypeID, int aptID, time_t tm, double b, int crewInd, 
				 time_t lastLegOfDay, time_t lastLegOfTour, time_t nextLegStart, int lastAptID, 
				 int isLastLeg)
{
	int p1, p2;

	crewSwaps[numCrewSwaps].acID = aircraftID;
	crewSwaps[numCrewSwaps].acTypeID = aircraftTypeID;
	crewSwaps[numCrewSwaps].airportID = aptID;
	crewSwaps[numCrewSwaps].benefit = b;
	crewSwaps[numCrewSwaps].swapStart = tm;

	crewSwaps[numCrewSwaps].crewInd = crewInd;

	if (crewInd >= 0)
	{
		p1 = crewPairList[crewInd].crewListInd[0];
		p2 = crewPairList[crewInd].crewListInd[1];

		crewSwaps[numCrewSwaps].outCrewOvertimeEnd = min (crewList[p1].tourEndTm + (time_t) (86400 * crewList[p1].stayLate), 
															crewList[p2].tourEndTm + (time_t) (86400 * crewList[p2].stayLate));
	}
	else
	{
		crewSwaps[numCrewSwaps].outCrewOvertimeEnd = 0;
	}

	crewSwaps[numCrewSwaps].swapEnd = nextLegStart;
	crewSwaps[numCrewSwaps].lastSchedInDuty = lastLegOfDay;
	crewSwaps[numCrewSwaps].lastSchedInTour = lastLegOfTour;
	crewSwaps[numCrewSwaps].lastAptID = lastAptID;
	crewSwaps[numCrewSwaps].firstPeakStart = tm - 60* minutesPastMidnight (tm, aptID) - 86400 
											+ 60 * optParam.peakStart;
	crewSwaps[numCrewSwaps].lastPeakStart = lastLegOfTour - 60* minutesPastMidnight (lastLegOfTour, lastAptID) - 86400 
						+ 60 * optParam.peakStart;
	crewSwaps[numCrewSwaps].isLastLeg = isLastLeg;

	numCrewSwaps ++;

	return 0;
}

//NOTE:  these functions are now run as part of buildOagOD.c (SO 8/16/07)
///************************************************************************************************
//*	Function	identifyAvailableLegs1						Date last modified:  8/01/06 BGC	*
//*	Purpose:	Finds all legs (excluding maintenance) at the end of which a crew swap could	*
//*				potentially occur.																*
//************************************************************************************************/
//
//static int
//identifyAvailableLegs1 (void)
//{
//	int i, j, k;
//
//	if ((legAvailable = (int *) calloc (numLegs, sizeof (int))) == NULL)
//	{
//		logMsg(logFile,"%s Line %d: Out of Memory in identifyAvailableLegs1().\n", __FILE__,__LINE__);
//		exit(1);
//	}
//
//	for (i=0; i<numLegs; i++)
//	{
//		legAvailable[i] = 0;
//		if (legList[i].planeLocked)
//		{
//			legAvailable[i] = 1; 
//			/*
//			*	If a leg is locked, its destination is a potential candidate for a crew swap
//			*	since the location of the plane is precisely known.
//			*/
//		}
//		else 
//		{
//			for (k=0; k<numCrewPairs; k++)
//			{
//				if ((crewPairList[k].optAircraftID > 0) && (legList[i].aircraftID == crewPairList[k].optAircraftID)
//					&& (legList[i].crewPairID == crewPairList[k].crewPairID))
//				{
//					legAvailable[i] = 2;
//					/*
//					*	If a crew is flying the plane at the start of the planning window, then legs along its entire
//					*	tour are crew swap candidates. This will be adjusted later to prohibit stealing planes.
//					*/
//				}
//			}
//		}
//	}
//
//	/* 
//	*	At the end of the above bit of code, we have identified all legs after which crew swaps could potentially
//	*	occur:
//	*	1.	At the destination of a locked leg.
//	*	2.	At the destination of a leg currently scheduled to be flown by a crew that is locked to a plane. 
//	*		Even though	this last case doesn't happen with certainty since the optimizer could change leg 
//	*		assignments, it is very likely to happen since the crew-plane assignment doesn't change and there is 
//	*		a penalty for re-assigning legs. So, the pilot pairer will assume that legs scheduled to be flown by
//	*		a crew that is currently flying a plane will NOT change and evaluate crews that swap out the existing
//	*		crew.
//	*/
//
//
//
//	/*
//	*	A leg may be available for (an optimized) crew swap only if there 
//	*	is no leg in the future that is locked to the same plane AND any crew.
//	*/
//	for (i=numLegs-1; i>0; i--)
//	{
//		if ((legList[i].crewLocked) && (legList[i].planeLocked))
//		{
//			for (j=i-1; j>=0; j--)
//			{
//				if (legList[i].aircraftID == legList[j].aircraftID)
//				{
//					legAvailable[j] = -1;
//				}
//			}
//		}
//	}
//
//	return 0;
//
//}
//
//
///************************************************************************************************
//*	Function	identifyAvailableLegs2						Date last modified:  05/04/07 BGC	*
//*	Purpose:	Removes legs from available list if they are before the last regular day		*
//*				of the crew's tour.																*
//************************************************************************************************/
//
//static int
//identifyAvailableLegs2 (void)
//{
//	int i, cpind;
//
//	for (i=0; i<numLegs; i++)
//	{
//		cpind = legList[i].crewPairInd;
//		if ((optParam.prohibitStealingPlanes) && (cpind >= 0))
//		{// If the crew pair exists and prohibit stealing planes
//			if((firstEndOfDay + crewPairList[cpind].endRegDay * 86400 -  legList[i].adjSchedIn) > 86400)
//			{// If the crew has more than a day left in its regular tour, leg is not available.
//				legAvailable[i] = -1;
//			}
//		}
//	}
//	return 0;
//}
//
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
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numLegs; i++)
	{
		legAvailable[i] = 0;
		//if (legList[i].planeLocked) 
 	 	if ((legList[i].planeLocked) && (legList[i].crewPairInd >= 0)) // That available leg has to have a crew pair. RLZ/AANG 01072008
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

/************************************************************************************************
*	Function	identifyAvailableAC							Date last modified:  04/30/07 SWO	*
*	Purpose:	Find all aircraft such that a crew swap can occur at							*
*				the next available location and datetime for an aircraft.						*
************************************************************************************************/

static int
identifyAvailableAC (void)
{
	int i, j;

	if ((acAvailable = (int *) calloc (numAircraft, sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in identifyAvailableAC().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numAircraft; i++)
	{
		acAvailable[i] = 1;
		//if aircraft is not available within window, it is not available for crew swap
		if (acList[i].availDT >= optParam.windowEnd)
		{
			acAvailable[i] = 0;
			continue;
		}
		//An aircraft is not available for (an optimized) crew swap if there a future leg 
		// that is locked to the plane and any crew.
		if(optParam.withFlexOS )
		{
			for (j = 0; j < acList[i].numIncl; j++)//fei Jan 2011
			{
				if (acList[i].inclInfoP->inclCrewID[j] > 0)
				{
					acAvailable[i] = 0;
				}
			}
		} else 
		{
			for (j = 0; j<= acList[i].lastIncl[optParam.planningWindowDuration -1]; j++)
			{
				if (acList[i].inclCrewID[j] > 0)
				{
					acAvailable[i] = 0;
				}
			}
		}

		if(acAvailable[i] == 0) 
		{
			continue;
		}

		//an aircraft is not available for (an optimized) crew swap if it is available earlier than the last
		//regular day of the crew's tour AND we are not allowing the stealing of planes
		/* Allow more options for possible crew pairs - commented out - 06/14/11 ANG
		if ((optParam.prohibitStealingPlanes) && (acList[i].firstCrPrInd >= 0))
		{
			if ((firstEndOfDay + crewPairList[acList[i].firstCrPrInd].endRegDay * 86400 -  acList[i].availDT) > 86400)
			{// If the crew has more than a day left in its regular tour, aircraft is not available.
				acAvailable[i] = 0;
			}
		} 
		*/
	}

	return 0;
}

/************************************************************************************************
*	Function	computeSwapBenefit				Date last modified:  8/01/06 BGC, 8/16/07 SWO	*
*	Purpose:	Computes the benefit of swapping at the end of legs identified as being			*
*				eligible for a crew swap, and at the next avail location of aircraft			*
************************************************************************************************/

static int
computeSwapBenefit (void)
{
	int i, p1, p2;
	double overtime, cost;
	time_t tempTm, departTm, arrivalTm, dutyStartTm;
	char writetodbstring1[200];

	if ((swapBenefitLeg = (double *) calloc (numLegs, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((swapBenefitAC = (double *) calloc (numAircraft, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if((swapGetHomeSavings = (double *) calloc (numCrew, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in identifyAvailableLegs1().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numLegs; i++)
	{
		swapBenefitLeg[i] = 0;
		if (legAvailable[i]>0)
		{
			p1 = crewPairList[legList[i].crewPairInd].crewListInd[0];
			p2 = crewPairList[legList[i].crewPairInd].crewListInd[1];

			if (crewList[p1].endLoc <= 0)
			{	
				/*
					If endLoc doesn't exist, the leg is still available for a swap, but with zero benefit. This statement should 
					typically not be executed --- it is only a precaution.
				*/

				logMsg(logFile,"%s Line %d: End location for crew ID %d not found.\n", __FILE__,__LINE__, crewList[p1].crewID);
				sprintf(writetodbstring1, "%s Line %d: End location for crew ID %d not found.", __FILE__,__LINE__, crewList[p1].crewID);
		        if(errorNumber==0)
		          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
			        }
		          }
	            else
		          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
	                }
	              }	
				   initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				   errorinfoList[errorNumber].crewid = crewList[p1].crewID;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=26;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				continue;
			}
          if (legList[i].inAirportID != crewList[p1].endLoc && crewList[p1].tourEndTm <= optParam.windowEnd){
			if ((getCrewTravelDataEarly(legList[i].adjSchedIn + optParam.finalPostFltTm*60, 
				(time_t)min((crewList[p1].tourEndTm + 86400*crewList[p1].stayLate), (optParam.windowEnd + 86400)), 
				legList[i].inAirportID, crewList[p1].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) == -1))
			{
				legAvailable[i] = -1;
				continue;
			}
			/* Pilot either cannot get home from the destination of the leg (no schedule in OAG) or the pilot
			*	cannot get home before the end of his tour.
			*/	
			swapBenefitLeg[i] -= cost;
			overtime = max (difftime(arrivalTm, crewList[p1].tourEndTm), 0.0);
			//we will add savings of not sending crew home at end of regularly scheduled tour later (in the enumerateCrewSwaps function)
		  } 
           else if(legList[i].inAirportID != crewList[p1].endLoc) 
				//Approximate overtime for savings calc (not worth the overhead of calculating actual get home times for these pilots. 
				// Typically, there is little or no overtime since last leg is within window and tourEndTm is outside window
				overtime = max(difftime((legList[i].adjSchedIn + optParam.finalPostFltTm*60 + 43200), crewList[p1].tourEndTm), 0.0);
		   else
				overtime = max(difftime((legList[i].adjSchedIn + optParam.finalPostFltTm*60), crewList[p1].tourEndTm), 0.0);

			if (crewList[p1].stayLate)
			{
			//  Note:  the first term is constant for all swaps, but we are trying to maximize benefit, 
		    //  so we need to keep all benefits positive (we only consider swaps with positive benefit). 
				swapBenefitLeg[i] += computeOvertimeCost (crewList[p1].stayLate) - computeOvertimeCost (overtime/86400.0);
			}

			if (crewList[p2].endLoc <= 0)
			{	
				logMsg(logFile,"%s Line %d: End location for crew ID %d not found.\n", __FILE__,__LINE__, crewList[p2].crewID);
				sprintf(writetodbstring1, "%s Line %d: End location for crew ID %d not found.", __FILE__,__LINE__, crewList[p2].crewID);
		        if(errorNumber==0)
		          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
			        }
		          }
				else
		          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
	                }
	              }	 
				   initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				   errorinfoList[errorNumber].crewid = crewList[p2].crewID;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=26;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
				continue;
			}
		   if (legList[i].inAirportID != crewList[p2].endLoc && crewList[p2].tourEndTm <= optParam.windowEnd){
			   if ((getCrewTravelDataEarly(legList[i].adjSchedIn + optParam.finalPostFltTm*60, 
					(time_t)min((crewList[p2].tourEndTm + 86400*crewList[p2].stayLate),(optParam.windowEnd + 86400)), 
					legList[i].inAirportID, crewList[p2].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) == -1))
			{
				legAvailable[i] = -1;
				continue;
			}
			swapBenefitLeg[i] -= cost;
			overtime = max ((double)(arrivalTm-crewList[p2].tourEndTm), 0);
			//we will add savings of not sending crew home at end of regularly scheduled tour later (in the enumerateCrewSwaps function)
		   }
		   else if(legList[i].inAirportID != crewList[p2].endLoc) 
				//Approximate overtime for savings calc (not worth the overhead of calculating actual get home times for these pilots. 
				// Typically, there is little or no overtime since last leg is within window and tourEndTm is outside window
				overtime = max(difftime((legList[i].adjSchedIn + optParam.finalPostFltTm*60 + 43200), crewList[p2].tourEndTm), 0.0);
			else
				overtime = max(difftime((legList[i].adjSchedIn + optParam.finalPostFltTm*60), crewList[p2].tourEndTm), 0.0);
			
			if (crewList[p2].stayLate)
			{
				
				swapBenefitLeg[i] += computeOvertimeCost (crewList[p2].stayLate) - computeOvertimeCost (overtime/86400.0);
			}
		}
	}

	for (i=0; i<numAircraft; i++)
	{
		swapBenefitAC[i] = 0;
		if (acAvailable[i]== 0)
			continue;
		if(acList[i].firstCrPrID == 0) 
			continue;  //no need to calculate swap benefit - why? ANG
		p1 = crewPairList[acList[i].firstCrPrInd].crewListInd[0];
		p2 = crewPairList[acList[i].firstCrPrInd].crewListInd[1];
		
		if(withOag==0)
		{//if plane is flying at start of planning window, current crew can head home after plane lands + post flight time
		  if(acList[i].availDT != optParam.windowStart && acList[i].maintFlag == 0 )
		   {
			tempTm = acList[i].availDT - optParam.turnTime*60 + optParam.finalPostFltTm*60;
		   }
		   else //if plane was available before window start (so avail set to window start), OR in maintenance or appointment at window start, 
			//assume crew can head home at window start
			tempTm = optParam.windowStart;
		}


		if (crewList[p1].endLoc <= 0)
		{
			logMsg(logFile,"%s Line %d: End location for crew ID %d not found.\n", __FILE__,__LINE__, crewList[p1].crewID);
            sprintf(writetodbstring1, "%s Line %d: End location for crew ID %d not found.", __FILE__,__LINE__, crewList[p1].crewID);
		        if(errorNumber==0)
		          {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
			        }
		          }
				else
		          {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		            {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
	                }
	              }	
				   initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				   errorinfoList[errorNumber].crewid = crewList[p1].crewID;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=26;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
			continue;
		}

		if (acList[i].availAirportID != crewList[p1].endLoc && crewList[p1].tourEndTm <= optParam.windowEnd){
			if ((getCrewTravelDataEarly(crewList[p1].availDT, (time_t)min((crewList[p1].tourEndTm + 86400*crewList[p1].stayLate),(optParam.windowEnd + 86400)), 
					acList[i].availAirportID, crewList[p1].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) == -1))
				{
					acAvailable[i] = -1;
					continue;
				}
		    /*  Pilot either cannot get home current location (no schedule in OAG) or the pilot
		     *	cannot get home before the end of his tour.  
	        */	
		    swapBenefitAC[i] -= cost;
		    overtime = max ((double)(arrivalTm-crewList[p1].tourEndTm), 0.0);
		   //we will add savings of not sending crew home at end of regularly scheduled tour later (in the enumerateCrewSwaps function)
		}
		else if(acList[i].availAirportID != crewList[p1].endLoc) 
			//Approximate overtime for savings calc (not worth the overhead of calculating actual get home times for these pilots. 
			// Typically, there is little or no overtime since last leg is within window and tourEndTm is outside window
			overtime = max(difftime((crewList[p1].availDT + 43200), crewList[p1].tourEndTm), 0.0);
		else
			overtime = max(difftime(crewList[p1].availDT, crewList[p1].tourEndTm), 0.0);

		if (crewList[p1].stayLate){

			//Note:  the first term is constant for all swaps, but we are trying to maximize benefit, 
			//  so we need to keep all benefits positive (we only consider swaps with positive benefit). 
			swapBenefitAC[i] += computeOvertimeCost (crewList[p1].stayLate) - computeOvertimeCost (overtime/86400.0);
		}

		if (crewList[p2].endLoc <= 0)
		{
			logMsg(logFile,"%s Line %d: End location for crew ID %d not found.\n", __FILE__,__LINE__, crewList[p2].crewID);
			sprintf(writetodbstring1, "%s Line %d: End location for crew ID %d not found.", __FILE__,__LINE__, crewList[p2].crewID);
		    if(errorNumber==0)
		       {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		          {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
			      }
		       }
		    else
		       {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		          {logMsg(logFile,"%s Line %d, Out of Memory in computeSwapBenefit().\n", __FILE__,__LINE__);
		             writeWarningData(myconn); exit(1);
	               }
	            }	
			      initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_crew");
				   errorinfoList[errorNumber].crewid= crewList[p2].crewID;
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=26;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
                errorNumber++;
			continue;
		}
		if (acList[i].availAirportID != crewList[p2].endLoc && crewList[p2].tourEndTm <= optParam.windowEnd){
			if ((getCrewTravelDataEarly(crewList[p2].availDT, (time_t)min((crewList[p2].tourEndTm + 86400*crewList[p2].stayLate),(optParam.windowEnd + 86400)), 
					acList[i].availAirportID, crewList[p2].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) == -1))
				{
					acAvailable[i] = -1;
					continue;
				}
		    swapBenefitAC[i] -= cost;
		    overtime = max ((double)(arrivalTm-crewList[p2].tourEndTm), 0);
		//we will add savings of not sending crew home at end of regularly scheduled tour later (in the enumerateCrewSwaps function)
		}
		else if(acList[i].availAirportID != crewList[p2].endLoc) 
			//Approximate overtime for savings calc (not worth the overhead of calculating actual get home times for these pilots. 
			// Typically, there is little or no overtime since last leg is within window and tourEndTm is outside window
			overtime = max(difftime((crewList[p2].availDT + 43200), crewList[p2].tourEndTm), 0.0);
		else
			overtime = max(difftime(crewList[p2].availDT, crewList[p2].tourEndTm), 0.0);

		if (crewList[p2].stayLate){
			
			swapBenefitAC[i] += computeOvertimeCost (crewList[p2].stayLate) - computeOvertimeCost (overtime/86400.0);
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	getAcType						Date last modified:  6/25/06 BGC, 04/23/07 SWO	*
*	Purpose:	Returns the aircraft type of a given aircraft ID.								*
************************************************************************************************/

static int
getACType (int acid)
{
	int i;
	for (i=0; i<numAircraft; i++)
	{
		if (acList[i].aircraftID == acid)
			return acList[i].aircraftTypeID;
	}
	return 0;
}

/************************************************************************************************
*	Function	checkIfXlsPlus					Date last modified:  XLS+ - 06/09/11 ANG		*
*	Purpose:	Check whether the aircraft is an XLS+ given aircraft ID.  Return 0 or 1			*
************************************************************************************************/

int
checkIfXlsPlus (int acid)
{
	//Ad2017 if (acid == 1980) return 1; //-- hard coded here if more XLS+ enter the fleet
	return 0;
}

/************************************************************************************************
*	Function	checkIfCj4							Date last modified:  CJ4 - 06/13/11 ANG		*
*	Purpose:	Check whether the aircraft is a CJ4 given aircraft ID.  Return 0 or 1			*
************************************************************************************************/

int
checkIfCj4 (int acid)
{
	if (acid == 2034) return 1; //-- hard coded here if CJ4 has already entered the fleet
	return 0;
}

/************************************************************************************************
*	Function	getLastLegs						Date last modified:  8/10/06 BGC, 08/16/07 SWO	*
*	Purpose:	Finds the last leg of the day and last leg of the tour for an existing crew		*
				tour.																			*
************************************************************************************************/

static int 
getLastLegs (int legInd, int acInd, time_t *lastLegOfDay, time_t *lastLegOfTour, time_t *nextLegStart, int *lastAptID,
			 int *isLastLeg)
{
	int i, j, prevLeg;
	int aircraftID, crewPairID;

	(*lastLegOfDay) = 0;
	if(withOag==1)
	  (*nextLegStart) = maxArr;
	else 
	  (*nextLegStart) = optParam.windowEnd;
	if(legInd > -1){
		(*lastLegOfTour) = legList[legInd].adjSchedIn;
		aircraftID = legList[legInd].aircraftID;
		crewPairID = legList[legInd].crewPairID;
	}
	else {//acInd > -1
		(*lastLegOfTour) = acList[acInd].availDT;
		aircraftID = acList[acInd].aircraftID;
		crewPairID = acList[acInd].firstCrPrID;
	}

	j = legInd; //j will be used to store the leg index of the last leg of the tour
	prevLeg = legInd;
	//if legInd == -1 (we are looking at next available location and time for aircraft), we set j = -1 and prevLeg = -1

	for (i=legInd+1; i<numLegs; i++)
	{
		if ((legList[i].crewPairID > 0) && (legList[i].crewPairID == crewPairID)
			&& (legList[i].aircraftID > 0) && (legList[i].aircraftID == aircraftID))
		{
			(*nextLegStart) = min((*nextLegStart), legList[i].schedOut);

			if (!(*lastLegOfDay) && (prevLeg > -1) && (legList[i].schedOut > (legList[prevLeg].schedIn + 60 * optParam.minRestTm)))
			{// Sufficient rest between two legs so duty ends here. Using schedIn instead of adjSchedIn on purpose.
				//also neglecting preFltTm and postFltTm on purpose, as sometimes these are compromised
				(*lastLegOfDay) = legList[prevLeg].adjSchedIn;
			}			
			if ((*lastLegOfTour) < legList[i].adjSchedIn){
				(*lastLegOfTour) = legList[i].adjSchedIn;
				j = i;
			}
			prevLeg = i;
		}
	}

	if (!(*lastLegOfDay))
	{//Couldn't find a gap of min rest time.
		if (prevLeg > legInd) 
			// Leg has at least one successor in the leglist
			(*lastLegOfDay) = legList[prevLeg].adjSchedIn;
		else if(legInd > -1)
			//current leg is last leg of day (and tour).
			(*lastLegOfDay) = legList[legInd].adjSchedIn;
		else //acInd > -1
			(*lastLegOfDay) = acList[acInd].availDT;
	}

//	(*lastLegOfTour) = min ((*lastLegOfTour), optParam.windowEnd);
	(*nextLegStart) = min ((*nextLegStart), optParam.windowEnd);
	if(j > -1)
		(*lastAptID) = legList[j].inAirportID;
	else
		(*lastAptID) = acList[acInd].availAirportID;
	if (j == legInd)
		(*isLastLeg) = 1;
	else
		(*isLastLeg) = 0;
	return 0;
}

/************************************************************************************************
*	Function	getRegEndDay								Date last modified:  8/09/06 SWO	*
*																				 5/04/07 BGC	*
*	Purpose:	Determines the earliest day on which the regular tour ends for the members		*
*				of an existing crew pair.														*
*************************************************************************************************/

static int
getRegEndDay (void)
{
	int c, cp, crewInd, day, windowEnd;

	windowEnd = optParam.planningWindowDuration - 1;

	for(cp = 0; cp<numCrewPairs; cp++)
	{
		for(c = 0; c<2; c++)
		{
			crewInd = crewPairList[cp].crewListInd[c];
			crewList[crewInd].endRegDay = PAST_WINDOW;
			for(day = 0; day <=windowEnd; day++)
			{
				if(crewList[crewInd].tourEndTm < firstEndOfDay + day*24*3600)
				{
					crewList[crewInd].endRegDay = day;
					break;
				}
			}
		}
		//find the earlier endRegDay of the two crew members and populate for crewPair
		if(crewList[crewPairList[cp].crewListInd[0]].endRegDay < crewList[crewPairList[cp].crewListInd[1]].endRegDay)
		{
			crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[0]].endRegDay;
		}
		else
		{
			crewPairList[cp].endRegDay = crewList[crewPairList[cp].crewListInd[1]].endRegDay;
		}
	}

	return 0;
}

/************************************************************************************************
*	Function	enumerateCrewSwaps				Date last modified:  8/XX/06 BGC, 6/11/07 SWO	*
*	Purpose:	Identifies all possible crew swap opportunities and computes the overtime cost	*
*				minus cost to get home for each potential crew swap identified.					*
*************************************************************************************************/

static int
enumerateCrewSwaps (void)
{
	int deadlines=0;
	int i, p1, p2, type, lastAptID, isLastLeg;
	time_t lastLegOfDay, lastLegOfTour, nextLegStart, tempTm;
	double cost;
	time_t departTm, arrivalTm, dutyStartTm;
	//test
	time_t test;
	//test

	getRegEndDay ();
	identifyAvailableLegs1 ();
	identifyAvailableLegs2 ();
	identifyAvailableAC ();

	computeSwapBenefit ();

	numCrewSwaps = (numLegs + numAircraft);
	/*
	*	Upper bound on the number of crew swap locations. Crew swaps can occur at the end of legs,
	*	or at the plane's next available time/place.
	*/

	if ((crewSwaps = (CrewSwap *) calloc (numCrewSwaps, sizeof (CrewSwap))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in enumerateCrewSwaps().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	
	numCrewSwaps = 0;

	for (i=0; i<numLegs; i++)
	{
		if (legAvailable[i] > 0)
		{
			type = getACType (legList[i].aircraftID);
			getLastLegs (i, -1, &lastLegOfDay, &lastLegOfTour, &nextLegStart, &lastAptID, &isLastLeg);
			//test
            test=nextLegStart;
			//test
			//add savings of sending outgoing crew home at end of last leg to the swapBenefit for the leg (overtime cost was already considered)
			p1 = crewPairList[legList[i].crewPairInd].crewListInd[0];
			p2 = crewPairList[legList[i].crewPairInd].crewListInd[1];
			if (swapGetHomeSavings[p1] == 0 && lastAptID != crewList[p1].endLoc && crewList[p1].tourEndTm <= optParam.windowEnd){
				if ((getCrewTravelDataEarly(lastLegOfTour + optParam.finalPostFltTm*60, 
					(time_t)min((crewList[p1].tourEndTm + 86400*crewList[p1].stayLate), (optParam.windowEnd + 86400)), 
					lastAptID, crewList[p1].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) != -1))
					swapGetHomeSavings[p1] = cost;
				else
					swapGetHomeSavings[p1] = INFINITY/10;  //use large "cost" since pilot can't get home (or home on time) from that airport
			}
			if (swapGetHomeSavings[p2] == 0 && lastAptID != crewList[p2].endLoc && crewList[p2].tourEndTm <= optParam.windowEnd){
				if ((getCrewTravelDataEarly(lastLegOfTour + optParam.finalPostFltTm*60, 
					(time_t)min((crewList[p2].tourEndTm + 86400*crewList[p2].stayLate),(optParam.windowEnd + 86400)), 
					lastAptID, crewList[p2].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) != -1))
					swapGetHomeSavings[p2] = cost;
				else
					swapGetHomeSavings[p2] = INFINITY/10;  //use large "cost" since pilot can't get home (or home on time) from that airport
			}
			swapBenefitLeg[i] += swapGetHomeSavings[p1] + swapGetHomeSavings[p2];

			//assume that plane can be flown by start of next leg at the latest
			if((legList[i].adjSchedIn + optParam.turnTime * 60) < nextLegStart)
				tempTm = legList[i].adjSchedIn + optParam.turnTime * 60;
			else 
				tempTm = nextLegStart - 60;  //subtract one minute tolerance
			//test
			//tempTm=60;
			//test
			addCrewSwapNode (legList[i].aircraftID, type, legList[i].inAirportID, tempTm, 
				swapBenefitLeg[i], legList[i].crewPairInd, lastLegOfDay, lastLegOfTour, nextLegStart, lastAptID, isLastLeg);	
		}
	}
	/*
	*	Swaps after maintenance legs are not explicitly considered. If a plane is in maintenance at the start of the planning
	*	window, a crew swap node will be created below when checking for availDT. 
	*	If a maintenance leg occurs within an existing tour, a crew swap is not considered at the end of the maintenance location
	*	but is considered at end of the previous leg anyway, which is equivalent. 
	*/

	for(i=0; i<numAircraft; i++){
		if(acAvailable[i] > 0){
			if(acList[i].firstCrPrID == 0){ //if there is no crew currently flying plane who will continue to fly it
				addCrewSwapNode (acList[i].aircraftID, acList[i].aircraftTypeID, acList[i].availAirportID, acList[i].availDT, 
					0, -1, acList[i].availDT, acList[i].availDT, (withOag)?maxArr:optParam.windowEnd, acList[i].availAirportID, 1);
			}
			else{ //there is a crew associated with the plane
				getLastLegs(-1, i, &lastLegOfDay, &lastLegOfTour, &nextLegStart, &lastAptID, &isLastLeg);
				//add savings of sending outgoing crew home at end of last leg to the swapBenefit for the leg (overtime cost was already considered)
				p1 = crewPairList[acList[i].firstCrPrInd].crewListInd[0];
				p2 = crewPairList[acList[i].firstCrPrInd].crewListInd[1];
				if (swapGetHomeSavings[p1] == 0 && lastAptID != crewList[p1].endLoc && crewList[p1].tourEndTm <= optParam.windowEnd){
					if ((getCrewTravelDataEarly(max(lastLegOfTour + optParam.finalPostFltTm*60,crewList[p1].availDT), 
						(time_t)min((crewList[p1].tourEndTm + 86400*crewList[p1].stayLate), (optParam.windowEnd +86400)),
						lastAptID, crewList[p1].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) != -1))
						swapGetHomeSavings[p1] = cost;
					else
						swapGetHomeSavings[p1] = INFINITY/10;  //use large "cost" since pilot can't get home (or home on time) from that airport
				}
				if (swapGetHomeSavings[p2] == 0 && lastAptID != crewList[p2].endLoc && crewList[p2].tourEndTm <= optParam.windowEnd){
					if ((getCrewTravelDataEarly(max(lastLegOfTour + optParam.finalPostFltTm*60, crewList[p2].availDT),
						(time_t)min((crewList[p2].tourEndTm + 86400*crewList[p2].stayLate), (optParam.windowEnd + 86400)),
						lastAptID, crewList[p2].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag) != -1))
						swapGetHomeSavings[p2] = cost;
					else
						swapGetHomeSavings[p2] = INFINITY/10;  //use large "cost" since pilot can't get home (or home on time) from that airport
				}
				swapBenefitAC[i] += swapGetHomeSavings[p1] + swapGetHomeSavings[p2];

				if(acList[i].availDT < nextLegStart)
					tempTm = acList[i].availDT;
				else
					tempTm = nextLegStart - 60;
				addCrewSwapNode (acList[i].aircraftID, acList[i].aircraftTypeID, acList[i].availAirportID, tempTm, 
					swapBenefitAC[i], acList[i].firstCrPrInd, lastLegOfDay, lastLegOfTour, nextLegStart, lastAptID, isLastLeg);	
			}
		}
	}

	for (i=0; i<numCrewSwaps; i++)
	{
		if (crewSwaps[i].swapEnd != optParam.windowEnd)
		{
			deadlines ++;
		}
	}

	if (verbose)
	{
		qsort (crewSwaps, numCrewSwaps, sizeof (CrewSwap), compareCrewSwaps);
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+--------+--------+------------------+------------------+------------+\n");
		fprintf (pairCrewsLogFile, "| APT ID | AC ID  | CapID  | FO ID  | acTp1  | acTp2  |    Swap Start    |     Swap End     |   Benefit  |\n"); //CJ4 - 02/16/12 ANG
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+--------+--------+------------------+------------------+------------+\n");
		for (i=0; i<numCrewSwaps; ++i)
		{
			showCrewSwapNode (&crewSwaps[i]);

			fprintf (pairCrewsLogFile, " %5d \n", i ) ; //fei FA

		}
		fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+--------+--------+------------------+------------------+------------+\n");		
	}

	fprintf (pairCrewsLogFile, "Num crew swaps: %d.\n", numCrewSwaps); 
	fprintf (pairCrewsLogFile, "Num crew swaps with end times: %d.\n", deadlines); 
	return 0;
}



/************************************************************************************************
*	Function	computeTotalBenefit		Date last modified:  5/08/07 BGC, edited 4/23/07 SWO	*
*	Purpose:	Computes benefit of a crew swap given pilots i and j, crew swap node k, and		*
*				OAG records x and y.															*
*************************************************************************************************/

static double
computeTotalBenefit (int i, int j, int k, int x, int y)
{
	time_t departTmi, dutyStartTmi, arrivalTmi, departTmj, dutyStartTmj, arrivalTmj, endTm, maxArrivalTm, latestArrivalTm;
    int remainingDutyTimei, remainingDutyTimej, remainingPairDutyTime;
	double benefit=0, costi, costj;

	maxArrivalTm = max (oag[x][0].arrivalTm, oag[y][1].arrivalTm); //max of arrival time of pilots i and j at plane
	latestArrivalTm = oag[x][0].lateArrTime;  // == oag[x][1].lateArrTime

	if (crewList[i].availAirportID == crewSwaps[k].airportID)
	{ // Pilot is already at the swap airport.
		if ((crewList[i].activityCode == 0) && (latestArrivalTm < crewList[i].availDT + 60*optParam.minRestTm))
		{ // Pilot's activity code is zero and he hasn't had enough time to rest before "arriving" at airport
			departTmi = maxArrivalTm;
			dutyStartTmi = crewList[i].availDT - 60*crewList[i].dutyTime;
			arrivalTmi = maxArrivalTm;
			costi = 0;
		}
		else
		{ // Pilot has been resting, or if activity code=0 has had time to rest since availDT.
			departTmi = maxArrivalTm;
			dutyStartTmi = maxArrivalTm;
			arrivalTmi = maxArrivalTm;
			costi = 0;
		}
	}
	else
	{// Pilot has to be flown in to swap location. Read from stored OAG query.
		departTmi = oag[x][0].departTm;
		arrivalTmi = oag[x][0].arrivalTm;
		dutyStartTmi = oag[x][0].dutyStartTm;
		costi = oag[x][0].cost;

		if (crewList[i].activityCode == 0) 
		{ // Pilot's activity code is zero
			if (dutyStartTmi < crewList[i].availDT + optParam.minRestTm)
			{ // Pilot's activity code is zero and no rest before commercial flight departure
				dutyStartTmi = crewList[i].availDT - 60*crewList[i].dutyTime;
			}
		}
	}
	if (latestArrivalTm > arrivalTmi + 60 * optParam.minRestTm)
	{ // Pilot has had enough time to rest since arrival at swap location, so reset duty clock.
		dutyStartTmi = latestArrivalTm;
	}

	// --------- Repeat above logic for pilot j --------- //

	if (crewList[j].availAirportID == crewSwaps[k].airportID)
	{ // Pilot j is already at the swap airport.
		if ((crewList[j].activityCode == 0) && (latestArrivalTm < crewList[j].availDT + 60*optParam.minRestTm))
		{ // Pilot's activity code is zero and he hasn't had enough time to rest before "arriving" at airport
			departTmj = maxArrivalTm;
			dutyStartTmj = crewList[j].availDT - 60*crewList[j].dutyTime;
			arrivalTmj = maxArrivalTm;
			costj = 0;
		}
		else
		{ // Pilot has been resting, or if activity code=0 has had time to rest since availDT.
			departTmj = maxArrivalTm;
			dutyStartTmj = maxArrivalTm;
			arrivalTmj = maxArrivalTm;
			costj = 0;
		}
	}
	else
	{// Pilot has to be flown in to swap location. Read from stored OAG query.
		departTmj = oag[y][1].departTm;
		arrivalTmj = oag[y][1].arrivalTm;
		dutyStartTmj = oag[y][1].dutyStartTm;
		costj = oag[y][1].cost;

		if (crewList[j].activityCode == 0) 
		{ // Pilot's activity code is zero
			if (dutyStartTmj < crewList[j].availDT + optParam.minRestTm)
			{ // Pilot's activity code is zero and no rest before commercial flight departure
				dutyStartTmj = crewList[j].availDT - 60*crewList[j].dutyTime;
			}
		}
	}

	if (latestArrivalTm > arrivalTmj + 60 * optParam.minRestTm)
	{ // Pilot has had enough time to rest since arrival at swap location, so reset duty clock.
		dutyStartTmj = latestArrivalTm;
	}

	remainingDutyTimei = (optParam.maxDutyTm - (int) difftime (latestArrivalTm, dutyStartTmi)/60);
	remainingDutyTimej = (optParam.maxDutyTm - (int) difftime (latestArrivalTm, dutyStartTmj)/60);
	remainingPairDutyTime = min (remainingDutyTimei, remainingDutyTimej);
		

	if (latestArrivalTm + 60*remainingPairDutyTime < (crewSwaps[k].lastSchedInDuty + optParam.postFlightTm*60)) //FATIGUE is not yet implemented in pairCrews.c - 02/05/10 ANG
	{ // Not enough duty hours left in incoming crew's day to finish today's duties scheduled for the outgoing crew.
		return -1;
	}
	if ((endTm = (time_t) min(crewList[i].tourEndTm + (time_t) crewList[i].stayLate*86400, crewList[j].tourEndTm + crewList[j].stayLate*86400))
		< (crewSwaps[k].lastSchedInTour + optParam.finalPostFltTm*60))
	{ // Not enough days left in incoming crew's tour to complete scheduled legs in the outgoing crew's tour.
		return -1;
	}

	benefit = crewPairMatrix[i][j] + crewSwaps[k].benefit;
	// Pairing benefit plus benefit of overtime savings (if any) from outgoing crew minus cost of sending outgoing crew home.

	if (crewSwaps[k].isLastLeg)
	{
		benefit += computePeakTimeOverlapBenefit (max(latestArrivalTm, crewSwaps[k].outCrewOvertimeEnd), 
			crewSwaps[k].lastPeakStart, min (endTm, optParam.windowEnd), crewSwaps[k].acID, remainingPairDutyTime);
	}
	else
	{
	/*
	 *	If the current crew pair is going to finish the existing crew's tour, it rests after the last duty in the
	 *	existing tour before peak time calculations can start. This is not always true -- this assumption is being
	 *	made to avoid calculation of number of duty hours remaining at the end of the tour.
	 */
		benefit += computePeakTimeOverlapBenefit (
			max(crewSwaps[k].outCrewOvertimeEnd, crewSwaps[k].lastSchedInTour + 60*optParam.postFlightTm + 60 * optParam.minRestTm), 
			crewSwaps[k].lastPeakStart, min (endTm, optParam.windowEnd), crewSwaps[k].acID, optParam.maxDutyTm);
	}

	benefit -= (costi + costj);
	// Cost of two airline tickets.

	if (crewList[i].activityCode == 2)
	{
		benefit -= computeOvertimeCost (max (0, (double) (crewList[i].tourStartTm - dutyStartTmi)/86400.0));
	} 
	// Overtime cost of pilot i. 

	if (crewList[j].activityCode == 2)
	{
		benefit -= computeOvertimeCost (max (0, (double) (crewList[j].tourStartTm - dutyStartTmj)/86400.0));
	}
	// Overtime cost of pilot j
	
	return benefit;
}

static time_t
getDutyStartTm (int p, time_t dutyStartTm)
{
	if ((crewList[p].activityCode == 0) && 
		(crewList[p].availDT > (dutyStartTm - 60 * optParam.minRestTm )))
	{
		return (crewList[p].availDT - 60 * crewList[p].dutyTime);
	}
	return dutyStartTm;
}

/************************************************************************************************
*	Function	getMatchingArcBenefit		Date last modified:  5/10/06 BGC, 8/17/07 by SWO	*
*	Purpose:																					*
*************************************************************************************************/
static int
getMatchingArcBenefit (int i, int j, int k, double *benefit)
{
	time_t earliestSwapTime, departTmi, dutyStartTmi, arrivalTmi, departTmj, dutyStartTmj, arrivalTmj, 
		starti, startj, latestArrivalTm, arriveByTm, endTm, arriveByPeakEnd, lastMidnight;
	double costi, costj;
	int x, checki=0, checkj=0;

	(*benefit) = 0;//-1; //XLS+ - 06/09/11 ANG
	starti = crewList[i].availDT;
	startj = crewList[j].availDT;
	

	if (crewList[i].availAirportID == crewSwaps[k].airportID)
	{ // Pilot i is already at the swap airport, so arrival time can be the start time.
		arrivalTmi = starti;
		departTmi = starti;
		dutyStartTmi = starti;
		costi = 0;
	}
	else 
	{
		if (!pilotOAGs[i][k].earlyDepTime)
		{   if(starti > (crewSwaps[k].swapEnd - optParam.firstPreFltTm*60))
				return -1;
			pilotOAGs[i][k].earlyDepTime = starti;
			pilotOAGs[i][k].lateArrTime = crewSwaps[k].swapEnd-optParam.firstPreFltTm*60;
			pilotOAGs[i][k].fromAptID = crewList[i].availAirportID;
			pilotOAGs[i][k].toAptID = crewSwaps[k].airportID;
			pilotOAGs[i][k].feasible = getCrewTravelDataEarly(starti, pilotOAGs[i][k].lateArrTime, crewList[i].availAirportID, crewSwaps[k].airportID, 
				&pilotOAGs[i][k].departTm, &pilotOAGs[i][k].dutyStartTm, &pilotOAGs[i][k].arrivalTm, 
				&pilotOAGs[i][k].cost, withOag);
		}
		
		if (pilotOAGs[i][k].feasible == -1)
			return (-1);

		arrivalTmi = pilotOAGs[i][k].arrivalTm;
		departTmi = pilotOAGs[i][k].departTm;
		dutyStartTmi = pilotOAGs[i][k].dutyStartTm;
		costi = pilotOAGs[i][k].cost;
	}


	if (crewList[j].availAirportID == crewSwaps[k].airportID)
	{ // Pilot j is already at the swap airport.
		arrivalTmj = startj;
		departTmj = startj;
		dutyStartTmj = startj;
		costj = 0;
	}
	else 
	{
		if (!pilotOAGs[j][k].earlyDepTime)
		{   if(startj > (crewSwaps[k].swapEnd - optParam.firstPreFltTm*60))
				return -1;
			pilotOAGs[j][k].earlyDepTime = startj;
			pilotOAGs[j][k].lateArrTime = crewSwaps[k].swapEnd-optParam.firstPreFltTm*60;
			pilotOAGs[j][k].fromAptID = crewList[j].availAirportID;
			pilotOAGs[j][k].toAptID = crewSwaps[k].airportID;
			pilotOAGs[j][k].feasible = getCrewTravelDataEarly(startj, pilotOAGs[j][k].lateArrTime, crewList[j].availAirportID, crewSwaps[k].airportID, 
				&pilotOAGs[j][k].departTm, &pilotOAGs[j][k].dutyStartTm, &pilotOAGs[j][k].arrivalTm, 
				&pilotOAGs[j][k].cost, withOag);
		}
		
		if (pilotOAGs[j][k].feasible == -1)
			return (-1);

		arrivalTmj = pilotOAGs[j][k].arrivalTm;
		departTmj = pilotOAGs[j][k].departTm;
		dutyStartTmj = pilotOAGs[j][k].dutyStartTm;
		costj = pilotOAGs[j][k].cost;
	}

	earliestSwapTime = max ((max (arrivalTmi, arrivalTmj)+ optParam.firstPreFltTm*60), crewSwaps[k].swapStart);
	// This is the earliest that the plane can be picked up and flown.
    if(withOag==0){
	    lastMidnight = crewSwaps[k].firstPeakStart - optParam.peakStart * 60;
	    while (lastMidnight + 86400 < earliestSwapTime)
		lastMidnight += 86400;
	// This is the last local midnight before earliestSwapTime
	}
	if ((earliestSwapTime > crewSwaps[k].swapEnd) ||
		(earliestSwapTime > min (crewList[i].tourEndTm + (time_t) 86400 * crewList[i].stayLate, 
								crewList[j].tourEndTm + (time_t) 86400 * crewList[j].stayLate)))
	{	// Swap has to happen before the next leg (if any). If earliest time swap can occur is after
		// this latest swap time, crew cannot start flying the plane in time.
		return -1;
	}
    if(withOag==1){
	//	lastMidnight = crewSwaps[k].firstPeakStart - optParam.peakStart * 60;
	//	while (lastMidnight + 86400 < earliestSwapTime)
	//		lastMidnight += 86400;
//	// This is the last local midnight before earliestSwapTime

	//set arriveByPeakEnd to the first end of peak period AFTER the earliest swap time
	arriveByPeakEnd = crewSwaps[k].firstPeakStart + 60*optParam.peakDuration;
	while(arriveByPeakEnd <= earliestSwapTime)
		arriveByPeakEnd += 86400;  
	}

	// Now step through a sequence of latest arrival times from earliestSwapTime to localPeakEndTm to find
	// the best commercial flight.

	latestArrivalTm = earliestSwapTime;
    if(withOag==1)
	    arriveByTm = (crewSwaps[k].isLastLeg == 1 ? 
			min(maxArr,arriveByPeakEnd) : crewSwaps[k].swapEnd);
	if(withOag==0)
		arriveByTm = (crewSwaps[k].isLastLeg == 1 ? 
				(lastMidnight + 60 * optParam.peakStart + 60 * optParam.peakDuration) : 
						crewSwaps[k].swapEnd);
	// If the plane has no scheduled legs after this one, peak time calculations can start right now. The pilot must
	// arrive at the swap location before the start of the next peak period. This is only to reduce
	// computation, because a crew could technically pick up this plane at any time within the 
	// planning window after the earliest swap time but investigating this would require a large number of
	// OAG calls.
	// If the plane has a scheduled leg after this, the time at which the pilots should arrive at the 
	// swap location should be within the swap window.

	numOagRecs=0;

	while (latestArrivalTm < arriveByTm )
	{
		if (arrivalTmi <= (latestArrivalTm -  60*optParam.firstPreFltTm - 60 * optParam.minRestTm))
		{ // Enough time to rest before swap time, so arrive early (see loop for numOAGRecs+x below).
			//Don't make OAG call.
			oag[numOagRecs][0].feasible = -1;
			oag[numOagRecs][0].lateArrTime = latestArrivalTm;
		}
		else
		{
			if ((numOagRecs > 0) && 
				((crewSwaps[k].isLastLeg? arriveByTm: (crewSwaps[k].lastSchedInDuty + optParam.postFlightTm*60)) < 
				(getDutyStartTm(i, oag[numOagRecs-1][0].dutyStartTm) + 60 * optParam.maxDutyTm)))
			{	/*
				*	Previous query gave us a schedule with enough duty time available to get through rest of the day.
				*	No need to query OAG any more -- getting to the swap location any later cannot help.
				*/
				checki = 1;
				oag[numOagRecs][0] = oag[numOagRecs-1][0];
			}
			else
			{
				oag[numOagRecs][0].earlyDepTime = starti;
				oag[numOagRecs][0].lateArrTime = latestArrivalTm;
				oag[numOagRecs][0].fromAptID = crewList[i].availAirportID;
				oag[numOagRecs][0].toAptID = crewSwaps[k].airportID;
				if (oag[numOagRecs][0].fromAptID == oag[numOagRecs][0].toAptID)
				{
					oag[numOagRecs][0].departTm =  oag[numOagRecs][0].lateArrTime;
					oag[numOagRecs][0].dutyStartTm = oag[numOagRecs][0].lateArrTime; 
					oag[numOagRecs][0].arrivalTm = oag[numOagRecs][0].lateArrTime; 
					oag[numOagRecs][0].cost = 0;
				}
				else
				{
					oag[numOagRecs][0].feasible = getCrewTravelDataLate(
						oag[numOagRecs][0].earlyDepTime,
						oag[numOagRecs][0].lateArrTime,
						oag[numOagRecs][0].fromAptID,
						oag[numOagRecs][0].toAptID,
						&oag[numOagRecs][0].departTm, 
						&oag[numOagRecs][0].dutyStartTm, 
						&oag[numOagRecs][0].arrivalTm, 
						&oag[numOagRecs][0].cost,
						withOag);	
				}
			}
		}

		if (arrivalTmj <= (latestArrivalTm - 60*optParam.firstPreFltTm - 60 * optParam.minRestTm))
		{// Enough time to rest before swap time, so arrive early. Don't make OAG call.
			oag[numOagRecs][1].feasible = -1;
			oag[numOagRecs][1].lateArrTime = latestArrivalTm;
		}
		else
		{
			if ((numOagRecs > 0) && 
				((crewSwaps[k].isLastLeg? arriveByTm : (crewSwaps[k].lastSchedInDuty + optParam.postFlightTm*60)) < 
				(getDutyStartTm(j, oag[numOagRecs-1][1].dutyStartTm) + 60 * optParam.maxDutyTm)))
			{// Previous query gave us a schedule with enough duty time available.
				checkj = 1;
				oag[numOagRecs][1] = oag[numOagRecs-1][1];
			}
			else
			{
				oag[numOagRecs][1].earlyDepTime = startj;
				oag[numOagRecs][1].lateArrTime = latestArrivalTm;
				oag[numOagRecs][1].fromAptID = crewList[j].availAirportID;
				oag[numOagRecs][1].toAptID = crewSwaps[k].airportID;
				if (oag[numOagRecs][1].fromAptID == oag[numOagRecs][1].toAptID)
				{
					oag[numOagRecs][1].departTm =  oag[numOagRecs][1].lateArrTime;
					oag[numOagRecs][1].dutyStartTm = oag[numOagRecs][1].lateArrTime; 
					oag[numOagRecs][1].arrivalTm = oag[numOagRecs][1].lateArrTime; 
					oag[numOagRecs][1].cost = 0;
				}
				else
				{
					oag[numOagRecs][1].feasible = getCrewTravelDataLate(		
						oag[numOagRecs][1].earlyDepTime,
						oag[numOagRecs][1].lateArrTime,
						oag[numOagRecs][1].fromAptID,
						oag[numOagRecs][1].toAptID,
						&oag[numOagRecs][1].departTm, 
						&oag[numOagRecs][1].dutyStartTm, 
						&oag[numOagRecs][1].arrivalTm, 
						&oag[numOagRecs][1].cost,
						withOag);
				}
			}
		}

		numOagRecs ++;
		latestArrivalTm += 60 * TIME_STEP;

		if ((checki) && (checkj))
		{
			latestArrivalTm = arriveByTm;
		}
	}
	//now loop through time steps a second time, this time pilot rests and starts fresh at crew swap
	for (x=0; x<numOagRecs; x++)
	{
		oag[numOagRecs+x][0].earlyDepTime = starti;
		oag[numOagRecs+x][0].lateArrTime = oag[x][0].lateArrTime - 60 * optParam.minRestTm;

		if (arrivalTmi <= oag[numOagRecs+x][0].lateArrTime - optParam.firstPreFltTm*60)
		{ // We already know pilot can get there with enough time to rest, so don't make new OAG query.
			oag[numOagRecs+x][0].fromAptID = crewList[i].availAirportID;
			oag[numOagRecs+x][0].toAptID = crewSwaps[k].airportID;
			oag[numOagRecs+x][0].feasible = 0;		
			oag[numOagRecs+x][0].departTm = departTmi;
			oag[numOagRecs+x][0].dutyStartTm = dutyStartTmi;
			oag[numOagRecs+x][0].arrivalTm = arrivalTmi;
			oag[numOagRecs+x][0].cost = costi;
		}
		else
		{ // This is not possible because arrivalTmi is the earliest possible time that pilot i can arrive.
			oag[numOagRecs+x][0].feasible = -1;
		}
	
		oag[numOagRecs+x][1].earlyDepTime = startj;
		oag[numOagRecs+x][1].lateArrTime = oag[x][1].lateArrTime - 60 * optParam.minRestTm;
		if (arrivalTmj <= oag[numOagRecs+x][1].lateArrTime - optParam.firstPreFltTm*60)
		{
			oag[numOagRecs+x][1].fromAptID = crewList[j].availAirportID;
			oag[numOagRecs+x][1].toAptID = crewSwaps[k].airportID;
			oag[numOagRecs+x][1].feasible = 0;		
			oag[numOagRecs+x][1].departTm = departTmj;
			oag[numOagRecs+x][1].dutyStartTm = dutyStartTmj;
			oag[numOagRecs+x][1].arrivalTm = arrivalTmj;
			oag[numOagRecs+x][1].cost = costj;
		}
		else
		{
			oag[numOagRecs+x][1].feasible = -1;
		}
	}

	for (x=0; x<numOagRecs; x ++)
	{
		if ((oag[x][0].feasible != -1) && (oag[x][1].feasible != -1)){
			(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, x, x));
		}		
		// Both arrive by latest arrival time.		

		if ((oag[numOagRecs+x][0].feasible != -1) && (oag[x][1].feasible != -1)){
			(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, (numOagRecs+x), x)); 
			//(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, x, x));
		}
		// i arrives, then rests. j arrives at latest possible time.

		if ((oag[x][0].feasible != -1) && (oag[numOagRecs+x][1].feasible != -1)){
			(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, x, (numOagRecs+x)));
			//(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, x, x));
		}
		// j arrives, then rests. i arrives at latest possible time.

		if ((oag[numOagRecs+x][0].feasible != -1) && (oag[numOagRecs+x][1].feasible != -1)){
			(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, (numOagRecs+x), (numOagRecs+x)));
			//(*benefit) = max ((*benefit), computeTotalBenefit (i, j, k, x, x));
		}
		// Both arrive early and rest.
	}

	endTm = min (crewList[i].tourEndTm + (time_t) (86400 * crewList[i].stayLate), crewList[j].tourEndTm + (time_t) (86400 * crewList[j].stayLate));
	// Subtract out the cost of late overtime of the incoming crew.
	(*benefit) -= computeOvertimeCost(difftime (endTm, max(optParam.windowStart, crewList[i].tourEndTm))/86400.0);
	(*benefit) -= computeOvertimeCost(difftime (endTm, max(optParam.windowStart, crewList[j].tourEndTm))/86400.0);

	//if (*benefit <= 0) XLS+ - 06/09/11 ANG
	if (*benefit < 0)
		return -1;

	return 0;
}

/************************************************************************************************
*	Function	addMatchingArc								Date last modified:  6/20/06 BGC	*
*	Purpose:	Adds arcs to the "matchingArcs" linked list.									*
************************************************************************************************/
static int 
addMatchingArc (int i, int j, int k, double benefit)
{
	MatchingArc *temp;
	if ((temp = (MatchingArc *) calloc (1, sizeof (MatchingArc))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in addMatchingArc().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	temp->benefit = benefit;
	temp->ac = crewSwaps[k].acID;
	temp->p1 = i;
	temp->p2 = j;
	temp->next = matchingArcs;
	temp->apt = crewSwaps[k].airportID;
	temp->swapTm = crewSwaps[k].swapStart;

	temp->swapInd = k ;//fei FA

	matchingArcs = temp;
	return 0;
}

/************************************************************************************************
*	Function	buildOptProblem								Date last modified:  8/01/06 BGC	*
*	Purpose:	Generates the matchingArcs linked list and a vector of variables to be used		*
*				in the optimization.															*
************************************************************************************************/

static int
buildOptProblem (void)
{
	int i, j, k;
	double benefit;
	//double maxbenefit; // 12/11/2009 HWG
	int arcFlag = 0; // 12/11/2009 HWG

	if ((oag = (OAG **) calloc ((2 * 24 * 60 * (MAX_WINDOW_DURATION + 10))/TIME_STEP+1, sizeof (OAG *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in buildOptProblem().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<(2* 24 * 60 * (MAX_WINDOW_DURATION + 10))/TIME_STEP+1; i++)
	{
		if ((oag[i] = (OAG *) calloc (2, sizeof (OAG))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in buildOptProblem().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	if ((pilotOAGs = (OAG **) calloc (numCrew, sizeof (OAG *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in buildOptProblem().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<numCrew; i++)
	{
		if ((pilotOAGs[i] = (OAG *) calloc (numCrewSwaps, sizeof (OAG))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in buildOptProblem().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	
	for (i=0; i<numCrew; i++)
	{
		for (j=0; j<numCrewSwaps; j++)
		{
			pilotOAGs[i][j].earlyDepTime = 0;
		}
	}
	

	matchingArcs = NULL;
	numMatchingArcs = 0;

	
	for (i=0; i<numCrew-1; i++)
	{
		for (j=i+1; j<numCrew; j++)
		{
			if (crewPairMatrix[i][j] >= 0)
			{
				for (k=0; k<numCrewSwaps; k++)
				{
					if ((acTypeList[crewList[i].acTypeIndex].aircraftTypeID == crewSwaps[k].acTypeID)
						&& (getMatchingArcBenefit (i, j, k, & benefit) != -1))
					{
						if(checkIfXlsPlus(crewSwaps[k].acID) && (crewList[i].isDup != 1 || crewList[i].isDup != 1)) continue; //XLS+ - 06/09/11 ANG
						else if (!checkIfXlsPlus(crewSwaps[k].acID) && (crewList[i].isDup == 1 || crewList[i].isDup == 1)) continue; //XLS+ - 06/09/11 ANG
						else if (checkIfXlsPlus(crewSwaps[k].acID) && crewList[i].isDup == 1 && crewList[i].isDup == 1 && benefit == 0) benefit = 2.0; //XLS+ - 06/09/11 ANG

						//No need of this since CJ4 is a separate aircraft type
						//if(checkIfCj4(crewSwaps[k].acID) && (crewList[i].isDup != 2 || crewList[i].isDup != 2)) continue; //CJ4 - 06/13/11 ANG
						//else if (!checkIfCj4(crewSwaps[k].acID) && (crewList[i].isDup == 2 || crewList[i].isDup == 2)) continue; //CJ4 - 06/13/11 ANG
						//else if (checkIfCj4(crewSwaps[k].acID) && crewList[i].isDup == 2 && crewList[i].isDup == 2 && benefit == 0) benefit = 2.0; //CJ4 - 06/13/11 ANG

						//Adding benefit to create pair - CJ4 - 05/07/2012 ANG
						if (checkIfCj4(crewSwaps[k].acID) && benefit == 0) benefit = 2.0; //Note: If benefit = 0, no pair will be created. 

						addMatchingArc (i, j, k, benefit);
						numMatchingArcs ++;
					}
				}
			}
		}
	}
	

	//Above code is replaced by the following - 12/11/2009 HWG
	/*for (i=0; i<numCrew-1; i++)
	{
		for (j=i+1; j<numCrew; j++)
		{
			if (crewPairMatrix[i][j] >= 0)
			{
			    arcFlag = 0;
				maxbenefit = -1000;
				for (k=0; k<numCrewSwaps; k++)
				{
					
					if ((acTypeList[crewList[i].acTypeIndex].aircraftTypeID == crewSwaps[k].acTypeID)
						&& (getMatchingArcBenefit (i, j, k, & benefit) != -1)){
						arcFlag = 1;
						maxbenefit = max(maxbenefit,benefit);
					}	

   				}

				if (arcFlag)
				{
					addMatchingArc (i, j, 0, maxbenefit);
					numMatchingArcs ++;
				}

			}
		}
	}*/   //HWG

	fprintf (logFile, "Num matching arcs: %d.\n", numMatchingArcs);
	if (verbose)
	{
		MatchingArc *temp = matchingArcs;
		DateTime dt;
		char opbuf[1024];

		fprintf(pairCrewsLogFile, "+--------+--------+----------+---------+------------------+------------+\n"); 
		fprintf(pairCrewsLogFile, "| Crew1  | Crew2  | Aircraft | Airport |     Date Time    |   Benefit  |\n"); 
		fprintf(pairCrewsLogFile, "+--------+--------+----------+---------+------------------+------------+\n"); 

		while (temp) {

			dt = dt_time_tToDateTime (temp->swapTm);
			fprintf(pairCrewsLogFile, "| %6d | %6d |  %6d  | %6d  | %15s | %10.3f | %d %d %5d \n", 
				crewList[temp->p1].crewID, crewList[temp->p2].crewID, temp->ac, temp->apt, 
				dt_DateTimeToDateTimeString(dt, opbuf, "%Y/%m/%d %H:%M"), temp->benefit, crewList[temp->p1].isDup, crewList[temp->p2].isDup, temp->swapInd ); //fei FA //XLS+ - 06/09/11 ANG
			temp = temp->next;
		}
		fprintf (pairCrewsLogFile, "+--------+--------+----------+---------+------------------+------------+\n"); 
	}

	if ((optMatching = (int *) calloc (numMatchingArcs, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in buildOptProblem().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	return 0;
}

/************************************************************************************************
*	Function	repopulateCrewpairInd			Date last modified:  8/08/06 BGC, 04/30/07 SWO	*
*	Purpose:	Re-populates all crew pair indices that were disrupted by 						*
*				creating the new crewPairList.													*
************************************************************************************************/

static int 
repopulateCrewpairInd (void)
{
	int i, j, k;
	for (i=0; i<numLegs; i++)
	{
		for (j=0; j<numCrewPairs; j++)
		{
			if (legList[i].crewPairID == crewPairList[j].crewPairID)
			{
				legList[i].crewPairInd = j;
				break;
			}

		}
		if (j == numCrewPairs)
		{
			legList[i].crewPairInd = -1;
		}
	}

	for (i= 0; i<numAircraft; i++)
	{
		for (j=0; j<numCrewPairs; j++)
		{
			if (acList[i].firstCrPrID == crewPairList[j].crewPairID)
			{
				acList[i].firstCrPrInd = j;
				break;
			}

		}
		if (j == numCrewPairs)
		{ 
			//If the index was not found, then this crew pair is no longer being considered by the optimization
			//(perhaps because of hard pairing constraints).  Clear acList[p].firstCrPrID.
			acList[i].firstCrPrID = 0;
			acList[i].firstCrPrInd = -1;
		}

		//RLZ convert crewPairID to crewPairInd

		for (k = 0; k < acList[i].numCrPairs; k++){
			for (j=0; j<numCrewPairs; j++){
				if (acList[i].cprInd[k] == crewPairList[j].crewPairID){
					acList[i].cprInd[k] = j;
					break;
				}
			}
			if (j == numCrewPairs){
				acList[i].cprInd[k] = -1;
			}
		}

		//update the crew pair indices in acList
		for (k = 0; k < MAX_LEGS; k++)
		{
			if( acList[i].schedLegIndList[k] < 0 )
				break;
			acList[i].schedCrPrIndList[k] = legList[acList[i].schedLegIndList[k]].crewPairInd ;
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	compareCrewLists							Date last modified:  6/15/06 BGC	*
*	Purpose:	Prints the existing crew pair list and the new crew pair list.					*
************************************************************************************************/

int
compareCrewLists ()
{
	int i;

	FILE *compareFile;

	//compareFile = fopen ("./Logfiles/CrewPairs.txt", "w");
	if ((compareFile = fopen("./Logfiles/CrewPairs.txt", "w")) == NULL) // 07/13/2017 ANG
	{
		logMsg (logFile, "Couldn't create CrewPairs log file.\n");
		writeWarningData(myconn); exit(1);
	}

	logMsg (logFile, "numOldCrewPairs = %d, numOptCrewPairs = %d.\n", numOldCrewPairs, numOptCrewPairs); // 07/12/2017
	logMsg (compareFile, "numOldCrewPairs = %d, numOptCrewPairs = %d.\n", numOldCrewPairs, numOptCrewPairs); // 07/12/2017

	fprintf (compareFile, "Num old crew pairs: %d.\n", numOldCrewPairs);
	fprintf (compareFile, "Old crew pair list:\n");
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	fprintf (compareFile, "| Crew Pair ID |   Captain ID   |  Flight Off ID |    acTypeID    | C1 qual | C2 qual |\n"); 
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	for (i=0; i<numOldCrewPairs; i++)
	{
		fprintf (compareFile, "|    %6d    |     %6d     |     %6d     |     %6d     | %7d | %7d | %d %d\n", oldCrewPairList[i].crewPairID, oldCrewPairList[i].captainID, //XLS+ - 06/07/11 ANG
			oldCrewPairList[i].flightOffID, acTypeList[oldCrewPairList[i].acTypeIndex].aircraftTypeID, 
			crewList[crewPairList[i].crewListInd[0]].qualification, crewList[crewPairList[i].crewListInd[1]].qualification, //AD20171019
			crewList[oldCrewPairList[i].crewListInd[0]].isDup, crewList[oldCrewPairList[i].crewListInd[1]].isDup); //XLS+ - 06/07/11 ANG
	}
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	fprintf (compareFile, "Num opt crew pairs: %d.\n", numOptCrewPairs);
	fprintf (compareFile, "Opt crew pair list:\n");
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	fprintf (compareFile, "| Crew Pair ID |   Captain ID   |  Flight Off ID |    acTypeID    | C1 qual | C2 qual |\n"); 
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	for (i=0; i<numOptCrewPairs; i++)
	{
		fprintf (compareFile, "|    %6d    |     %6d     |     %6d     |     %6d     | %7d | %7d | %d %d\n", crewPairList[i].crewPairID, crewPairList[i].captainID, //XLS+ - 06/07/11 ANG
			crewPairList[i].flightOffID, acTypeList[crewPairList[i].acTypeIndex].aircraftTypeID, 
			crewList[crewPairList[i].crewListInd[0]].qualification, crewList[crewPairList[i].crewListInd[1]].qualification, //AD20171019
			crewList[crewPairList[i].crewListInd[0]].isDup, crewList[crewPairList[i].crewListInd[1]].isDup); //XLS+ - 06/07/11 ANG
	}
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");

	fprintf (compareFile, "Num new crew pairs (including opt crew pairs): %d.\n", numCrewPairs);
	fprintf (compareFile, "New crew pairs not in opt crew pairs:\n");

	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");
	fprintf (compareFile, "| Crew Pair ID |   Captain ID   |  Flight Off ID |    acTypeID    | C1 qual | C2 qual |\n"); 
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");

	for (i=numOptCrewPairs; i<numCrewPairs; i++)
	{
		fprintf (compareFile, "|    %6d    |     %6d     |     %6d     |     %6d     | %7d | %7d | %d %d\n", crewPairList[i].crewPairID, crewPairList[i].captainID, //XLS+ - 06/07/11 ANG
			crewPairList[i].flightOffID, acTypeList[crewPairList[i].acTypeIndex].aircraftTypeID, 
			crewList[crewPairList[i].crewListInd[0]].qualification, crewList[crewPairList[i].crewListInd[1]].qualification, //AD20171019
			crewList[crewPairList[i].crewListInd[0]].isDup, crewList[crewPairList[i].crewListInd[1]].isDup
			); //XLS+ - 06/07/11 ANG
	}
	fprintf (compareFile, "+--------------+----------------+----------------+----------------+---------+---------+\n");

	fclose (compareFile);

	return 0;
}


/************************************************************************************************
*	Function	getPairingBonusesAndSort					Date last modified:  4/24/07 SWO	*
*	Purpose:																					*
************************************************************************************************/

int
getPairingBonusesAndSort (){
	int p1, p2, i;

	///* crewPairBonus is not needed onward - 10/15/10 ANG
	for (i=0; i<numCrewPairs; i++)
	{
		p1 = crewPairList[i].crewListInd[0];
		p2 = crewPairList[i].crewListInd[1];
		if (pairingPriority[p1][p2] > 0)
			crewPairList[i].crewPairBonus = optParam.priorityBenefit[pairingPriority[p1][p2]-1];
		else if (pairingPriority[p1][p2] == -1) //we set pairingPriority == -1 for crewPair in function identifyImpossiblePairings or in pairCrews
			crewPairList[i].crewPairBonus = optParam.priorityBenefit[0]; 
		else //pairingPriority[p1][p2] == 0
			crewPairList[i].crewPairBonus = 0;
		crewPairList[i].crewPairBonus = crewPairList[i].crewPairBonus * optParam.priorityBftWt; //Scale up/down instead of removing it completely RLZ 10/26/2010
	}
	//crewPairBonus is not needed onward - 10/15/10 ANG*/

	qsort((void *) oldCrewPairList, numOldCrewPairs, sizeof(CrewPair), compareCrewPairIDs);
	qsort((void *) crewPairList, numOptCrewPairs, sizeof(CrewPair), compareCrewPairIDs);
	qsort((void *) (crewPairList+numOptCrewPairs), numCrewPairs-numOptCrewPairs, sizeof(CrewPair), compareCrewPairIDs);
	
	return 0;
}


/************************************************************************************************
*	Function	showOptimalMatching							Date last modified:  5/15/07 BGC	*
*	Purpose:	Displays arcs in the optimal matching											*
************************************************************************************************/

int
showOptimalMatching (void)
{
	int i;
	char opbuf[1024];
	DateTime swapTime;
	MatchingArc *current = matchingArcs;
	int countL1Matching = 0; //AD20171213

	fprintf (pairCrewsLogFile, "\n\n Optimal matching:\n\n");
	fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+------------------+----------------------+--------+--------+\n");
	fprintf (pairCrewsLogFile, "|Aircraft|Airport | Pilot1 | Pilot2 |     Swap time    |        Benefit       | P1Qual | P2Qual |\n");
	fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+------------------+----------------------+--------+--------+\n");
	for (i=0; i<numMatchingArcs; i++)
	{
		if (optMatching[i])
		{
			countL1Matching++; //AD20171213
			swapTime = dt_time_tToDateTime (current->swapTm);
			fprintf (pairCrewsLogFile, "| %6d | %6d | %6d | %6d | %15s | %20.3f | %6d | %6d | %d %d %5d \n",
				current->ac,
				current->apt,
				crewList[current->p1].crewID,
				crewList[current->p2].crewID,
				dt_DateTimeToDateTimeString(swapTime, opbuf, "%Y/%m/%d %H:%M"),
				current->benefit, 
				crewList[current->p1].qualification,
				crewList[current->p2].qualification,
				crewList[current->p1].isDup, crewList[current->p2].isDup, //XLS+ - 06/09/11 ANG
				current->swapInd ); //fei FA	
		}
		current = current->next;
	}
	fprintf (pairCrewsLogFile, "+--------+--------+--------+--------+------------------+----------------------+--------+--------+\n");
	fprintf (logFile,"Found %d matching arcs in Pairing Level 1 process.\n", countL1Matching); //AD20171213
	return 0;
}

/************************************************************************************************
*	Function	pairCrews									Date last modified:  8/10/06 BGC	*
*	Purpose:	Pairs available pilots into crews.					Updated 08/16/07 SWO		*
************************************************************************************************/

int
pairCrews (void)
{
	int i, cp, foundFirst, capInd, foInd;
	MatchingArc *current;
	MatchingArc *temp;
	MatchingArc *last;
	int countL2Dup, countL2Matching, countExgPairReuse; //AD20171213 - count Pairing Level 2 duplicates, count Exg Pair reuse

	if ((pairCrewsLogFile = fopen("./Logfiles/pairCrewsLog.txt", "w")) == NULL)
	{
		logMsg (logFile, "Couldn't create pair crews log file.\n");
		writeWarningData(myconn); exit(1);
	}

	//////////////////////////// Populates adjSchedIn using flight time calculator. /////////////////////////
	///////////////////  This could be moved into process input at some point. //////////////////////////////
	/*for (i=0; i<numLegs; i++)
	{
		if (legList[i].acInd >= 0)
		{
			if (legList[i].demandInd > -1)
			{// Demand leg
				numPax = demandList[legList[i].demandInd].numPax;
			}
			else
			{// Positioning leg
				numPax = 0;
			}
			getFlightTime(legList[i].outAirportID, legList[i].inAirportID, acList[legList[i].acInd].aircraftTypeID, 
			month, numPax, &fltTm, &blkTm, &elapsedTm, &stops);
			legList[i].adjSchedIn = legList[i].schedOut + 60 * elapsedTm;
		}
		else
		{
			legList[i].adjSchedIn = legList[i].schedIn;
		}
	}*/

	if (optParam.pairingLevel == 3) // BYPASS CREW PAIRING ALTOGETHER
	{
		getPairingPriorities ();
		buildNewCrewPairListWithoutPairing ();
		repopulateCrewpairInd ();
		return (0);
	}

	logMsg (pairCrewsLogFile, "** Started enumerating crew pairs.\n");
	enumerateCrewPairs ();
	logMsg (pairCrewsLogFile, "** Enumerated crew pairs.... started enumerating crew swaps.\n");
	enumerateCrewSwaps ();
	logMsg (pairCrewsLogFile, "** Enumerated crew swaps... started generating arcs and costs.\n");
	buildOptProblem ();
	logMsg (pairCrewsLogFile, "** Built optimization problem.\n");

	if (getOptimalMatching (matchingArcs, numMatchingArcs, optMatching))
	{
		logMsg (logFile, "** Crew pairing optimizer did not execute. Continuing with existing crew pair list.\n");
		numOptCrewPairs = numCrewPairs;
		oldCrewPairList = NULL;
		numOldCrewPairs = 0;
		freeAndNullMemory (); //07/13/2017 ANG

		fclose (pairCrewsLogFile);
		pairCrewsLogFile = NULL;
		return 0;
	}

	if (verbose)
	{
		showOptimalMatching ();
	}

	oldCrewPairList = crewPairList;
	numOldCrewPairs = numCrewPairs;
	buildNewCrewPairList ();  // note that crewPairList and numCrewPairs are updated in this function

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//IF PAIRING LEVEL == 2, we have already considered (above) all new pairings as per pairingLevel 0, 
	//but we will now also consider existing pairs plus new pairings of those not yet paired as per pairingLevel 1
	if(optParam.pairingLevel == 2)
	{
		countExgPairReuse = 0; //AD20171213
		//copy existing (old) crewPairs to new crewPairList if they have optAircraftID == 0 and they are 
		//not duplicates of pairs already in new list. Set crewPairMatrix[][] = -2 for these.
		//(Those with optAircraftID > 0 were re-used and included in new list already, and already have crewPairMatrix[][]== -2)  
		for (cp=0; cp<numOldCrewPairs; cp++)
		{
			if (oldCrewPairList[cp].optAircraftID == 0)
			{
				capInd = oldCrewPairList[cp].crewListInd[0];
				foInd = oldCrewPairList[cp].crewListInd[1];
				//EXCEPTION 1:  if the existing crew pair is NOT locked to a leg, and the crew pair violates
				//hard pairing constraints where one of the pilots is crewID1, then we will NOT keep the existing pair
				// - the hard constraint governs (see above)
				//if((hasHardPairing[capInd]== 1 || hasHardPairing[foInd]== 1) && pairingPriority[capInd][foInd] != 1)
				//	continue;
				if((hasHardPairing[capInd]== 1 || hasHardPairing[foInd]== 1) && pairingPriority[capInd][foInd] != 1) //09/14/11 ANG
					logMsg (logFile, "Exg Pair %d and %d violates pairing constraints.\n", crewList[capInd], crewList[foInd]);

				//else if((!crewList[foInd].lockHome) && (!crewList[capInd].lockHome)){ //09/14/11 ANG
				if((!crewList[foInd].lockHome) && (!crewList[capInd].lockHome)){
					//EXCEPTION 2:  if the existing crew pair (pilotA, pilotB) is NOT locked to a leg, and pilotA is crewID2 in a 
					//hard pairing constraint not satisfied by the existing crew pair, then we will keep the existing
					//pair, but we will also consider the hard pairing constraint for pilotA, and other (general) pairings for pilot B.
					if((hasHardPairing[capInd]== 2 || hasHardPairing[foInd]== 2) && pairingPriority[capInd][foInd] != 1)
					{
						if(hasHardPairing[capInd]== 2)
						{
							for(i=0; i<numCrew; i++)
							{
								if(pairingPriority[capInd][i]!=1 && i!=foInd)
								{
									crewPairMatrix[capInd][i] = -2;
									crewPairMatrix[i][capInd] = -2;
								}
								if(pairingPriority[capInd][i] == 1)
								{
									//set pairingPriority to -1 to mark this pair - we will use a slightly smaller priorityBenefit bonus
									//when generating pairings later in the code
									pairingPriority[capInd][i] = -1;
									pairingPriority[i][capInd] = -1;
								}	
							}
						}
						if(hasHardPairing[foInd]== 2)
						{
							for(i=0; i<numCrew; i++)
							{
								if(pairingPriority[foInd][i]!=1 && i!=capInd)
								{
									crewPairMatrix[foInd][i] = -2;
									crewPairMatrix[i][foInd] = -2;
								}
								if(pairingPriority[foInd][i] == 1)
								{
									//set pairingPriority to -1 to mark this pair - we will use a slightly smaller priorityBenefit bonus
									//when generating pairings later in the code
									pairingPriority[foInd][i] = -1;
									pairingPriority[i][foInd] = -1;
								}
							}
						}
					}   //end if((hasHardPairing[capInd]== 2 || hasHardPairing[foInd]== 2) && pairingPriority[capInd][foInd] != 1){
					//ELSE (NOT ONE OF THE EXCEPTIONS)
					else{
						for(i = 0; i<numCrew; i++)
						{
							crewPairMatrix[capInd][i] = -2;
							crewPairMatrix[foInd][i] = -2;
							crewPairMatrix[i][capInd] = -2;
							crewPairMatrix[i][foInd] = -2;
						}
					}

					for(i = 0; i<numCrewPairs; i++)
					{
						if(oldCrewPairList[cp].crewPairID == crewPairList[i].crewPairID) //we reused crewPairIDs if a new crewPair was the same as the old
							i = numCrewPairs+1; //this is a duplicate
					}
					if(i == numCrewPairs)
					{ //if no duplicate found, insert crewPair into new crewPairList
						//note that if either pilot was locked to home, then crewPair was already inserted at end of list (between numOptCrewPairs and numCrewPairs)
						if(numCrewPairs > numOptCrewPairs)//if there are crewPairs that are currently NOT to be included in the optimization
							crewPairList[numCrewPairs] = crewPairList[numOptCrewPairs]; //move first non-opt crew pair to end of list
						numCrewPairs++;
						countExgPairReuse++; //AD20171213
						copyExistingCrewPair (cp, crewPairList, &numOptCrewPairs); //insert new crew pair (which should be included in opt.) into list
					}
				} // end else if((!crewList[foInd].lockHome) && (!crewList[capInd].lockHome))
			}  //end if (oldCrewPairList[cp].optAircraftID == 0)
		} //end for (cp = 0....

		fprintf(logFile,"Added %d non-duplicate existing crewpairs to CrewPairList out of %d existing crewpairs.\n", countExgPairReuse, numOldCrewPairs); //AD20171213

		//remove matchingArcs from list if crewPair will be reused (rather than repaired) per newly edited crewPairMatrix. 
		//use smaller bonus for hard pairing constraints that conflict with existing pair (for crewID2)
		current = matchingArcs;
		foundFirst = 0;
		while(current && !foundFirst){
			if(crewPairMatrix[current->p1][current->p2]== -2){
				temp = current->next;
				free (current);
				current = temp;
				numMatchingArcs--;
			}
			else{
				if(pairingPriority[current->p1][current->p2] == -1){
					//this crew pair is getting reduced bonus since crewID2 for hard pairing constraint was part of an existing pair
					current->benefit += (optParam.priorityBenefit[1] - optParam.priorityBenefit[0]*0.5);
				}	
				foundFirst = 1;
				matchingArcs = current;
				last = current;
				current = current->next;
			}
		}
		while(current){
			if(crewPairMatrix[current->p1][current->p2]== -2){
				last->next = current->next;
				free (current);
				current = last->next;
				numMatchingArcs--;
			}
			else{
				if(pairingPriority[current->p1][current->p2] == -1){
					//this crew pair is getting reduced bonus since crewID2 for hard pairing constraint was part of an existing pair
					current->benefit += (optParam.priorityBenefit[1] - optParam.priorityBenefit[0]*0.5);
				}
				last = current;
				current = current->next;
			}
		}
		if(numMatchingArcs > 0 && !getOptimalMatching(matchingArcs, numMatchingArcs, optMatching))
		{
			countL2Dup = 0; countL2Matching = 0;//AD20171213
			//add newly created crewPairs to list if they are not duplicates of pairs in the new list already
			current = matchingArcs;
			for(i = 0; i < numMatchingArcs; i++){
				if (optMatching[i])
				{
					countL2Matching++;
					for(cp = 0; cp < numCrewPairs; cp++){
						if(((crewList[current->p1].crewID == crewPairList[cp].captainID) && 
							(crewList[current->p2].crewID == crewPairList[cp].flightOffID)) ||
							((crewList[current->p1].crewID == crewPairList[cp].flightOffID) && 
							(crewList[current->p2].crewID == crewPairList[cp].captainID))){
								cp = numCrewPairs+1; //this is a duplicate
								countL2Dup++; //AD20171213
						}
					}
					if(cp == numCrewPairs)
					{ //if no duplicate found
						if(numCrewPairs > numOptCrewPairs)//if there are crewPairs that are currently NOT to be included in the optimization
							crewPairList[numCrewPairs] = crewPairList[numOptCrewPairs]; //move first non-opt crew pair to end of list
						numCrewPairs++;
						addCrewPair (current, crewPairList, &numOptCrewPairs);//insert new crew pair (which should be included in opt.) into list
						//check if we need to duplicate the pure DQ pair - DQ - 01/28/10 ANG
					}
				}
				current = current->next;				
			}
			fprintf(logFile,"Found %d additional matching arcs in Pairing Level 2 process, %d of which are NOT duplicates.\n", countL2Matching, countL2Matching-countL2Dup); //AD20171213
		}
	}  //END IF PAIRING LEVEL == 2
//////////////////////////////////////////////////////////////////////////////////////////////
	getPairingBonusesAndSort ();
	if (optParam.withVac == 1){ //VAC - 12/09/10 ANG
		addVacPairToCrewPairList();
	}

	repopulateCrewpairInd ();

	if (verbose)
	{	
		compareCrewLists ();
	}

	fclose (pairCrewsLogFile);

	pairCrewsLogFile = NULL;

	freeAndNullMemory (); //07/13/2017 ANG

	reprintFinalCrewPairList();

	return 0;
}


/************************************************************************************************
*	Function	addVacPairToCrewPairList					  Date created:  VAC - 12/09/10 ANG *
*	Purpose:	Add vendor fake crew pairs to crew pair list									*
************************************************************************************************/

static int
addVacPairToCrewPairList (void)
{
	int ncp = 0, i, j, k, b;

	CrewPair *addVacCrewPairList;
	
	maxCPID = 1;

	
	for (i=0; i<numCrewPairs; i++)
	{
		if (crewPairList[i].crewPairID > maxCPID)
			maxCPID = crewPairList[i].crewPairID;
	}

	ncp = numCrewPairs + numVac;

	if ((addVacCrewPairList = (CrewPair *) calloc (ncp + 1, sizeof (CrewPair))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in addVacPairToCrewPairList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}

	ncp = 0;

	for (i=0; i<numCrewPairs; i++)
	{
		//copyExistingCrewPair (i, addVacCrewPairList, &ncp);
		addVacCrewPairList[ncp].crewPairID = crewPairList[i].crewPairID;
		addVacCrewPairList[ncp].captainID = crewPairList[i].captainID;
		addVacCrewPairList[ncp].flightOffID = crewPairList[i].flightOffID;
		addVacCrewPairList[ncp].crewListInd[0] = crewPairList[i].crewListInd[0];
		addVacCrewPairList[ncp].crewListInd[1] = crewPairList[i].crewListInd[1];

		for (k=0; k<numAircraft; k++)
		{
			if (crewPairList[i].aircraftID[k] == 0)
				break;
		}
		k ++;

		if ((addVacCrewPairList[ncp].aircraftID = (int *) calloc (k+1, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in copyExistingCrewPair().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		for (j=0; j<k+1; j++)
		{
			addVacCrewPairList[ncp].aircraftID[j] = crewPairList[i].aircraftID[j];
		}

		if ((addVacCrewPairList[ncp].lockTour = (int *) calloc (k+1, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in copyExistingCrewPair().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		for (j=0; j<k+1; j++)
		{
			addVacCrewPairList[ncp].lockTour[j] = crewPairList[i].lockTour[j];
		}

		addVacCrewPairList[ncp].hasFlownFirst = crewPairList[i].hasFlownFirst;
		addVacCrewPairList[ncp].optAircraftID = crewPairList[i].optAircraftID;
		addVacCrewPairList[ncp].acInd = crewPairList[i].acInd;
		addVacCrewPairList[ncp].acTypeIndex = crewPairList[i].acTypeIndex;
		addVacCrewPairList[ncp].pairStartTm = crewPairList[i].pairStartTm;
		addVacCrewPairList[ncp].pairEndTm = crewPairList[i].pairEndTm;
		//addVacCrewPairList[ncp].crewPairBonus = crewPairList[i].crewPairBonus;  // NOT NEEDED - crewPairBonuses are set in getPairingBonusesAndSort at end
		addVacCrewPairList[ncp].availAptID = crewPairList[i].availAptID;
		addVacCrewPairList[ncp].availDT = crewPairList[i].availDT;
		addVacCrewPairList[ncp].activityCode = crewPairList[i].activityCode;
		addVacCrewPairList[ncp].dutyTime = crewPairList[i].dutyTime;
		addVacCrewPairList[ncp].blockTm = crewPairList[i].blockTm;
		for (j=0; j<MAX_LEGS; j++)
		{
			addVacCrewPairList[ncp].schedLegIndList[j] = crewPairList[i].schedLegIndList[j];
			addVacCrewPairList[ncp].schedLegACIndList[j] = crewPairList[i].schedLegACIndList[j];
		}
		for (j=0; j<MAX_LEG_INCL; j++)
		{
			addVacCrewPairList[ncp].inclDemandInd[j] = crewPairList[i].inclDemandInd[j];
		}
		addVacCrewPairList[ncp].numIncl = crewPairList[i].numIncl;	
		addVacCrewPairList[ncp].crewPlaneList = crewPairList[i].crewPlaneList;
		addVacCrewPairList[ncp].crewPUSList = crewPairList[i].crewPUSList;
		addVacCrewPairList[ncp].crewPUEList = crewPairList[i].crewPUEList;
		addVacCrewPairList[ncp].numPlaneArcs = crewPairList[i].numPlaneArcs;
		addVacCrewPairList[ncp].numPUStartArcs = crewPairList[i].numPUStartArcs;
		addVacCrewPairList[ncp].numPUEndArcs = crewPairList[i].numPUEndArcs;
		addVacCrewPairList[ncp].getHomeCost = crewPairList[i].getHomeCost;
		addVacCrewPairList[ncp].nodeStartIndex = crewPairList[i].nodeStartIndex;
		addVacCrewPairList[ncp].startDay= crewPairList[i].startDay;
		addVacCrewPairList[ncp].endDay = crewPairList[i].endDay;
		addVacCrewPairList[ncp].endRegDay = crewPairList[i].endRegDay;

		ncp++;
	}

	//for(ncp = 0, tPtr = crewPairList; ncp < numCrewPairs; ncp++, tPtr++) {
	//	if(tPtr->crewPairID) {
	//		memcpy(addVacCrewPairList, tPtr, sizeof(CrewPair));
	//		addVacCrewPairList++;
	//	}
	//}

	
	for (b=0; b<numVac; b++)
	{
		maxCPID ++; // The new crew pair id is the maximum of those so far + 1.
		addVacCrewPairList[ncp].crewPairID = maxCPID;
		addVacCrewPairList[ncp].vacPair = 1;

		for (j = numCrew-numVac*2; j < numCrew; j++){
			if(crewList[j].position == 1 && crewList[j].vacIndex == b){
				addVacCrewPairList[ncp].captainID = crewList[j].crewID;
				addVacCrewPairList[ncp].crewListInd[0] = j;
				addVacCrewPairList[ncp].pairStartTm = crewList[j].tourStartTm;
				addVacCrewPairList[ncp].pairEndTm = crewList[j].tourEndTm;
				addVacCrewPairList[ncp].availAptID = crewList[j].availAirportID;
				addVacCrewPairList[ncp].availDT = crewList[j].availDT;
				addVacCrewPairList[ncp].blockTm =  crewList[j].blockTm;
				addVacCrewPairList[ncp].dutyTime = crewList[j].dutyTime;
				addVacCrewPairList[ncp].activityCode = crewList[j].activityCode;
			}
			else if (crewList[j].position == 2 && crewList[j].vacIndex == b){
				addVacCrewPairList[ncp].flightOffID = crewList[j].crewID;
				addVacCrewPairList[ncp].crewListInd[1] = j;
			}
		}

		if ((addVacCrewPairList[ncp].aircraftID = (int *) calloc (3, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in addVacPairToCrewPairList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		addVacCrewPairList[ncp].aircraftID[0] = vacList[b].aircraftID;
		addVacCrewPairList[ncp].aircraftID[1] = 0;
		addVacCrewPairList[ncp].hasFlownFirst = 1;

		if ((addVacCrewPairList[ncp].lockTour = (int *) calloc (1, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in addVacPairToCrewPairList().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		addVacCrewPairList[ncp].optAircraftID = vacList[b].aircraftID;
	
		addVacCrewPairList[ncp].lockTour[0] = 0;

		for (j = numAircraft-numVac ; j < numAircraft; j++){
			if(acList[j].aircraftID == vacList[b].aircraftID){
				addVacCrewPairList[ncp].acInd = j;
				addVacCrewPairList[ncp].acTypeIndex = acList[j].acTypeIndex;
				//populate information for aircraft
				//acList[j].legCrewPairFlag = maxCPID;
				acList[j].firstCrPrID = maxCPID;
				acList[j].firstCrPrInd = ncp;
				acList[j].cprInd[0] = maxCPID; //ncp; intentionally changed, later the ID is repopulated as Index in repopulateCrewpairIndex()
				acList[j].numCrPairs = 1;
			}
		}

		for (j=0; j<MAX_LEGS; j++)
		{
			addVacCrewPairList[ncp].schedLegIndList[j] = -1;
			addVacCrewPairList[ncp].schedLegACIndList[j] = -1;
		}
		for (j=0; j<MAX_LEG_INCL; j++)
		{
			addVacCrewPairList[ncp].inclDemandInd[j] = -1;
		}
		addVacCrewPairList[ncp].numIncl = 0;	
		addVacCrewPairList[ncp].crewPlaneList = NULL;
		addVacCrewPairList[ncp].crewPUSList = NULL;
		addVacCrewPairList[ncp].crewPUEList = NULL;
		addVacCrewPairList[ncp].numPlaneArcs = 0;
		addVacCrewPairList[ncp].numPUStartArcs = 0;
		addVacCrewPairList[ncp].numPUEndArcs = 0;
		addVacCrewPairList[ncp].getHomeCost = NULL;
		addVacCrewPairList[ncp].nodeStartIndex = -1;
		addVacCrewPairList[ncp].startDay = 0;
		addVacCrewPairList[ncp].endDay = PAST_WINDOW;
		addVacCrewPairList[ncp].endRegDay = PAST_WINDOW;

		ncp++;
	}

	crewPairList = addVacCrewPairList;
	numOptCrewPairs = ncp;
	numCrewPairs = ncp;
	

	return 0;
}


/************************************************************************************************
*	Function	reprintFinalCrewPairList					Date last modified:  03/11/09 ANG	*
*	Purpose:	Reprint Final Crew Pair List.													*		
************************************************************************************************/
static int 
reprintFinalCrewPairList (void)
{
	extern CrewPair *crewPairList;
	//int errNbr1, errNbr2, 
	int cp;
	char tbuf1[32], tbuf2[32];
	int *acPtr;

	fprintf(logFile,"\n\n\n\n Reprint Final Crew Pair List:\n");
	fprintf(logFile,"+------+------+--------+---------+---------+------------------+------------------+-----+\n");
	fprintf(logFile,"| crew | crew |        |  cap    |  flight | pair             | pair             |     |\n");
	fprintf(logFile,"| pair | pair | acType |  tain   |  off    | start            | end              | has |\n");
	fprintf(logFile,"| in   | id   |        |  id     |  id     | time             | time             | fln | aircraft id list\n");
	fprintf(logFile,"+------+------+--------+---------+---------+------------------+------------------+-----+\n");
	for(cp = 0; cp < numOptCrewPairs; cp++){ 
			fprintf(logFile,"| %4d | %4d | %6d | %7d | %7d | %s | %s | %3d | %d %d ", 
				cp, 
				crewPairList[cp].crewPairID,
				acTypeList[crewPairList[cp].acTypeIndex].aircraftTypeID,
				crewPairList[cp].captainID, 
				crewPairList[cp].flightOffID, 
				(crewPairList[cp].pairStartTm) ? dt_DateTimeToDateTimeString(dt_time_tToDateTime(crewPairList[cp].pairStartTm), tbuf1,"%Y/%m/%d %H:%M") : "",
				(crewPairList[cp].pairEndTm) ? dt_DateTimeToDateTimeString(dt_time_tToDateTime(crewPairList[cp].pairEndTm), tbuf2,"%Y/%m/%d %H:%M") : "",
				crewPairList[cp].hasFlownFirst,
				crewList[crewPairList[cp].crewListInd[0]].isDup,
				crewList[crewPairList[cp].crewListInd[1]].isDup); //XLS+ - 06/07/11 ANG
				acPtr = crewPairList[cp].aircraftID;
				while(*acPtr){
					fprintf(logFile, " %d ", *acPtr);
					++acPtr;
				}
			fprintf(logFile,"\n");
	}
	fprintf(logFile,"+------+------+--------+---------+---------+------------------+------------------+-----+\n");
	return 0;
}


/************************************************************************************************
*	Function	freeAndNullMemory							Date last modified:  8/XX/06 BGC	*
*	Purpose:	Frees all memory allocated.														*		
************************************************************************************************/

static int 
freeAndNullMemory (void)
{
	int i, limit;
	MatchingArc *current, *temp;

	for (i=0; i<numCrew; i++)
	{
		free(pairingPriority[i]);
		pairingPriority[i] = NULL;

		free (crewPairMatrix[i]);
		crewPairMatrix[i] = NULL;

		free (legsFlownTogetherToday[i]);
		legsFlownTogetherToday[i] = NULL;

		free (legsFlownTogetherTomorrow[i]);
		legsFlownTogetherTomorrow[i] = NULL;
	}

	free (crewPairMatrix);
	crewPairMatrix = NULL;

	free (pairingPriority);
	pairingPriority = NULL;

	free (legsFlownTogetherToday);
	legsFlownTogetherToday = NULL;

	free (legsFlownTogetherTomorrow);
	legsFlownTogetherTomorrow = NULL;

	free (crewSwaps);
	crewSwaps = NULL;
	
	if (numMatchingArcs){ //RLZ: 
		current = matchingArcs;
		while (current)
		{
			temp = current->next;
			free (current);
			current = temp;
		}
		current = NULL;
		temp =NULL;
		matchingArcs = NULL;
	}

	free (legAvailable);
	legAvailable = NULL;

	free(acAvailable);
	acAvailable = NULL;
	
	free (reuseCrewPair);
	reuseCrewPair = NULL;

	free(optMatching);
	optMatching = NULL;

	free(swapBenefitLeg);
	swapBenefitLeg = NULL;

	free(swapBenefitAC);
	swapBenefitAC = NULL;

	free(swapGetHomeSavings);
	swapGetHomeSavings = NULL;

	limit = (2 * 24 * 60 * MAX_WINDOW_DURATION)/TIME_STEP + 1;
	for (i=0; i<limit; i++)
	{
		free(oag[i]);
		oag[i] = NULL;
	}

	for (i=0; i<numCrew; i++)
	{
		free(pilotOAGs[i]);
		pilotOAGs[i] = NULL;
	}

	free(oag);
	oag = NULL;

	free(pilotOAGs);
	pilotOAGs = NULL;

	return 0;
}
