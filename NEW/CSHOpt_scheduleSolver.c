#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "logMsg.h"
#include <string.h>
#include "datetime.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_processInput.h"
#include "CSHOpt_dutyNodes.h"
#include "CSHOpt_tours.h"
#include "CSHOpt_scheduleSolver.h"
#include "CSHOpt_output.h"
#include "my_mysql.h"
#include "CSHOpt_readInput.h"

#include <ilcplex/cplex.h>

#define bounds(idx,count) ((idx >= 0 && idx < count) ? idx : stoppit(__FILE__,__LINE__,idx, count))

extern int month;
extern int actualMaxDemandID;
extern int  local_scenarioid;
extern Warning_error_Entry *errorinfoList;
extern int errorNumber;
extern MY_CONNECTION *myconn;
extern CrewEndTourRecord *crewEndTourList; // 11/14/07 ANG
extern int crewEndTourCount; // 11/14/07 ANG
extern int maxTripsPerDuty; // 04/30/08 ANG
extern Airport2 *aptList;
extern DateTime dt_run_time_GMT; // run time in dt_ format in GMT

crewAcTour *crewAcTourList;

//---- Lists that will contain the proposed solution ----//
ProposedCrewAssg *propCrewAssignment = NULL;
int numPropCrewDays = 0;
ProposedMgdLeg *propMgdLegs = NULL;
int numPropMgdLegs = 0;
ProposedUnmgdLeg *propUnmgdLegs = NULL;
int numPropUnmgdLegs = 0;

//----travel request list
BINTREENODE *travelRqstRoot = NULL;
int travelRqsts = 0;
//-------------------------------------------------------//
//--- Parameters below are used to perturb the problem when LP seems to be getting "stuck" due to degeneracy. ---//
static const double EPSILON = 1e-05;
static const double MIN_RHS = 0.999999;
static const double MAX_RHS = 1.000001;
static const int MAX_STALLS = 10;
static const int MIN_DEMANDS_SWITCH_LPSOLVER = 180;
static const int LP_OBJ_DECREMENT = 10;
static const int MAX_STALLS_EXIT = 35;

extern int verbose;
extern FILE *logFile;
extern int firstEndOfDay;


static char **constraintName = NULL;

static int buildTourColumn (int tourInd, int *nz);
static int showTour (int tourInd);
static int showExgTour (int exgTourInd);
static int travelRequestCmp(void *a1, void *b1);

static int
stoppit(char *file, int line, int idx, int max)
{
 fprintf(logFile,"%s Line %d: array subscript=%d max=%d\n", file, line, idx, max);
 writeWarningData(myconn); exit(0);
 return(-1);
}

/************************************************************************************************
*	Function	round										Date last modified:  6/21/06 BGC	*
*	Purpose:	Round a double to the nearest integer.											*
************************************************************************************************/

static double round (double x)
{
	if (x > 0)
	{
		if ((x - floor(x)) < 0.5)
		{
			return floor(x);
		}
		else 
			return (ceil(x));
	}
	else
	{
		if ((x - ceil(x)) < 0.5)
			return ceil(x);
		else
			return floor(x);
	}
}

/************************************************************************************************
*	Function	compareMgdLegsSchedOut						Date last modified:  8/01/06 BGC	*
*	Purpose:	Used in qsort to sort managed legs by sched out.								*
************************************************************************************************/

static int compareMgdLegsSchedOut (const ProposedMgdLeg *a, const ProposedMgdLeg *b)
{
	return (int) (a->schedOut - b->schedOut);
}


/************************************************************************************************
*	Function	compareMgdLegs								Date last modified:  8/01/06 BGC	*
*	Purpose:	Used in qsort to sort managed legs.												*
************************************************************************************************/

static int compareMgdLegs (const ProposedMgdLeg *a, const ProposedMgdLeg *b)
{
	int cond;
	if ((cond = (a->aircraftID - b->aircraftID)) != 0)
		return cond;
	if ((cond = (int) difftime (a->schedOut, b->schedOut)) != 0)
		return cond;
	if ((cond = (a->captainID - b->captainID)) != 0)
		return cond;
	return (a->FOID - b->FOID);
}


/************************************************************************************************
*	Function	comparePropCrewDays		Date last modified:  8/01/06 BGC, Updated 02/07/07 SWO	*
*	Purpose:	Used in qsort to sort proposed crew assignments.								*
************************************************************************************************/

static int comparePropCrewDays (const ProposedCrewAssg *a, const ProposedCrewAssg *b)
{
	int cond;
	if ((cond = (a->crewID - b->crewID)) != 0)
		return cond;
	return (int) difftime (a->startTm, b->startTm);
//	return (int) difftime (a->startTm, b->endTm);	
}

/************************************************************************************************
*	Function	compareUnmgdLegsID							Date last modified:  8/01/06 BGC	*
*	Purpose:	Used in qsort to sort proposed unmanaged legs.									*
************************************************************************************************/

static int compareUnmgdLegsID (const ProposedUnmgdLeg *a, const ProposedUnmgdLeg *b)
{
	return (int) (a->demandID - b->demandID);
}

/************************************************************************************************
*	Function	compareOwners							Date last modified:  5/15/06 BGC	*
*	Purpose:	Used in qsort to sort owner list.											*
************************************************************************************************/

static int compareOwners (const Owner *a, const Owner *b)
{
	return (int) (a->ownerID - b->ownerID);
}

extern Owner *ownerList;
extern int numOwners;

extern int numLegs;

extern Crew *crewList;
extern int numCrew;

extern int month;

extern Demand *demandList;
extern int numDemand;
extern int numOptDemand;

extern CrewPair *crewPairList;
extern int numOptCrewPairs;
extern int numCrewPairs;

extern Aircraft *acList;
//extern McpAircraft *mcpAircraftList; //03/12/08 ANG
extern int numAircraft;

extern Leg *legList;
extern int numLegs;

extern OptParameters optParam;

extern AircraftType *acTypeList;
extern int numAcTypes;

extern int numSetsSpecConnConstr;
extern int **pickupTripList; 
extern int **puTripListInfo;
extern AcGroup *acGroupList;

extern int numExgTours;
extern ExgTour *exgTourList;

extern int *tourCount;
static int oldTourCount = 0;
extern Tour *tourList;
extern int maxTripsPerDuty;
extern Duty **dutyList;
extern struct listMarker dutyTally[MAX_AC_TYPES][MAX_WINDOW_DURATION][9];


extern int repoConnxnList[maxRepoConnxns];
extern int numRepoConnxns;

static int numRows = 0; 
static int numCols = 0;
static int numInitialCols = 0;
static int numCharterCols = 0;
static int numExgTourCols = 0;
static int numApptCols = 0;
static int numDemandRows = 0; 
static int numBeforeRows = 0; 
static int numAfterRows = 0; 
static int numCrewPairRows = 0; 
static int numPlaneRows = 0; 
static int numRepoConnxnRows = 0;
static int numAfterAcTypeRows = 0;
static int numBeforeAcTypeRows = 0;
static int numCrewRows = 0;

////////////////////////////////////////////////////////////////////////////////////////
// This set of structures map the constraint back to the demand, plane, crew pair, etc.
////////////////////////////////////////////////////////////////////////////////////////
static int *demandRowIndex = NULL; 
// Array of size number of demand constraints; Index of demand corresponding to the demand constraint.
static int *crewPairRowIndex = NULL; 
// Array of size number of crew constraints; Index of crew pair corresponding to the crew pair constraint.
static int **beforeRowIndex = NULL;	
// Array of size [numBeforeConstraints][2]; First index -- index of plane (-1 if actype) corresponding to the row, second -- index of demand.
static int **afterRowIndex = NULL;	
// Array of size [numAfterConstraints][2]; First index -- index of plane (-1 if actype) corresponding to the row, second -- index of demand.
static int *planeRowIndex = NULL; 
// Array of size num Aircraft constraints; Index of plane corresponding to the row.
static int *crewRowIndex = NULL;
// Array of size num crew constraints; Index of crew corresponding to the row.


////////////////////////////////////////////////////////////////////////////////////////////
// This set of structures map the demand, plane, crew pair, etc to the constraint row index
////////////////////////////////////////////////////////////////////////////////////////////
static int *demandIndexToRow = NULL; // Row (constraint) number of a demand.
static int *crewPairIndexToRow = NULL; // Row number of a crew pair.
//static int **beforeAcTypeToRow = NULL; // Row number of a  before actype and demand.
//static int **beforeAircraftToRow = NULL; // Row number of a  before aircraft and demand.
//static int **afterAcTypeToRow = NULL; // Row number of a  after actype and demand.
//static int **afterAircraftToRow = NULL; // Row number of a  after aircraft and demand.
static int **beforePUTripListToRow = NULL;  //Row number of a "before" pickup trip list index and demand
static int **afterPUTripListToRow = NULL;  //Row number of an "after" pickup trip list index and demand
static int *planeIndexToRow = NULL;	// Row number of a plane.
static int *crewIndexToRow = NULL; // Row number of a crew.


////////////////Used for generating columns in CPLEX////////////////////
static const int oneInt = 1;
static const int zeroInt = 0;
static const double oneDbl = 1.0;
static const double zeroDbl = 0.0;

static char *charBuf = NULL;
static int *optSolution = NULL;

static int iteration = 0;
static int *rowInd = NULL;
static double *rowCoeff = NULL;

////////////////////////////////////////////////////////////////////////////////////////////////
/// These structures temporarily store the optimal solution while generating final output
////////////////////////////////////////////////////////////////////////////////////////////////
static Tour *optTours = NULL; 
static ExgTour *optExgTours = NULL;
static int numOptTours = 0;
static int numOptExgTours = 0;
static int *optAircraftID = NULL;
static int *availStart = NULL;
static int *availEnd = NULL;
static int *availRepoConnxn = NULL;
static int *isManaged = NULL;
static int **scanned = NULL;
static int *scannedFirst = NULL;
static time_t *outTimes = NULL;
static time_t *inTimes = NULL;


/************************************************************************************************
*	Function	showSolution								Date last modified:  8/01/06 BGC	*
*	Purpose:	Displays the LP solution (variable name and value) for non-zero variables.		*
*				Used only for debugging.  Not currently called.									*
************************************************************************************************/

static int 
showSolution (CPXENVptr env, CPXLPptr lp)
{
	int i, cur_colnamespace, cur_numcols, surplus, status;
	double *cur_solution;
	char **cur_colname, *cur_colnamestore;
	char writetodbstring1[200];
	cur_numcols = CPXgetnumcols (env, lp);
	
	status = CPXgetcolname (env, lp, NULL, NULL, 0, &surplus, 0, cur_numcols-1);
	if (( status != CPXERR_NEGATIVE_SURPLUS ) &&
		( status != 0 ))  
	{
		logMsg (logFile, "Could not determine amount of space for column names.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	cur_colnamespace = - surplus;

	if ( cur_colnamespace > 0 ) 
	{
		if ((cur_colname = (char **) calloc (cur_numcols, sizeof (char *))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in showSolution().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		if ((cur_solution = (double *) calloc (cur_numcols, sizeof (double))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in showSolution().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		if ((cur_colnamestore = (char *)  malloc (cur_colnamespace)) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in showSolution().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		status = CPXgetcolname (env, lp, cur_colname, cur_colnamestore, cur_colnamespace, &surplus, 0, cur_numcols-1);
		if ( status ) 
		{
			logMsg (logFile, "CPXgetcolname failed.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}

		status = CPXgetx (env, lp, cur_solution, 0, cur_numcols-1); 
		if (status) {
			logMsg(logFile, "Failed to get Optimal Solution.\n", __FILE__, __LINE__);
			sprintf(writetodbstring1, "Failed to get Optimal Solution.", __FILE__, __LINE__);
			if(errorNumber==0)
			  {if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1, sizeof(Warning_error_Entry)))) 
		           {logMsg(logFile,"%s Line %d, Out of Memory in showSolution().\n", __FILE__,__LINE__);
		            writeWarningData(myconn); exit(1);
				   }
		      }
			else
			  {if((errorinfoList = (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL) 
		          {logMsg(logFile,"%s Line %d, Out of Memory in showSolution().\n", __FILE__,__LINE__);
		            writeWarningData(myconn); exit(1);
	              }
		       }
			   initializeWarningInfo(&errorinfoList[errorNumber]);
				   errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
                   strcpy(errorinfoList[errorNumber].group_name,"group_debugging"); 
				   sprintf(errorinfoList[errorNumber].filename,"%s", __FILE__);
				   sprintf(errorinfoList[errorNumber].line_number,"%d", __LINE__);
			       errorinfoList[errorNumber].format_number=35;
                   strcpy(errorinfoList[errorNumber].notes, writetodbstring1);
            errorNumber++;
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
	}

	fprintf (logFile, "-----------Optimal Solution-----------\n");
	for (i=0; i<cur_numcols; i++)
	{
		if (round(cur_solution[i]))
			fprintf (logFile, "%-50s: %6.3f\n", cur_colname[i], cur_solution[i]);
	}
	fprintf (logFile, "--------------------------------------\n");
	fflush (logFile);

	free(cur_colname);
	cur_colname = NULL;

	free (cur_colnamestore);
	cur_colnamestore = NULL;

	free (cur_solution);
	cur_solution = NULL;

	return 0;
}



/************************************************************************************************
*	Function	getReducedCost								Date last modified:  8/XX/06 BGC	*
*	Purpose:	Used only for debugging.  Not currently called.									*
************************************************************************************************/
static double
getReducedCost (int tourInd, int *rowInd, double *rowCoeff, int nz, double *duals)
{
	int i;
	double redCost = tourList[tourInd].cost;
	for (i=0; i<nz; i++)
	{
		redCost -= (duals[rowInd[i]] * rowCoeff[i]);
	}
	return redCost;
}


/************************************************************************************************
*	Function	checkNegativeRC								Date last modified:  8/XX/06 BGC	*
*	Purpose:	Used only for debugging.  Not currently called.									*
************************************************************************************************/
static int
checkNegativeRC (CPXENVptr env, CPXLPptr lp)
{
	int i, status, nc;
	double *rc, quality;


	status = CPXgetdblquality(env, lp, &quality, CPX_MAX_RED_COST);
//	fprintf (logFile, "Max red cost: %f\n");


	nc = CPXgetnumcols(env,lp);

	if ((rc = (double *) calloc (nc, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in checkDuals().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	status = CPXgetdj (env, lp, rc, 0, nc-1); 
	if ( status ) {
		logMsg (stderr, "Failure to get reduced costs, error %d.\n", status);
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<nc; i++)
	{
		if (rc[i] < 0)
		{
			fprintf (logFile, "Column %d negative CPLEX reduced cost: %f\n", 
				i,
				rc[i]);
		}
	}

	free(rc);
	rc = NULL;
	
}


/************************************************************************************************
*	Function	checkRC										Date last modified:  8/XX/06 BGC	*
*	Purpose:	Used only for debugging.  Not currently called.									*
************************************************************************************************/
static int
checkRC (CPXENVptr env, CPXLPptr lp, double *duals)
{
	int i, status, nz, nc;
	double *rc, redCost;

	nc = CPXgetnumcols(env,lp);

	if ((rc = (double *) calloc (nc, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in checkDuals().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	status = CPXgetdj (env, lp, rc, 0, nc-1); 
	if ( status ) {
		logMsg (stderr, "Failure to get reduced costs, error %d.\n", status);
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<(*tourCount); i++)
	{
		buildTourColumn (i, &nz);	
		redCost = getReducedCost (i, rowInd, rowCoeff, nz, duals);
		fprintf (logFile, "Tour index %d: CPLEX red cost: %f, computed red cost: %f\n", 
			i,
			rc[numInitialCols+i], 
			redCost);
	}

	free(rc);
	rc = NULL;
	
}


/************************************************************************************************
*	Function	showDuals									Date last modified:  8/01/06 BGC	*
*	Purpose:	Displays dual values (constraint name and value) for non-zero duals.			*
*				Used only for debugging.  Not currently called.									*
************************************************************************************************/

static int showDuals (CPXENVptr env, CPXLPptr lp)
{
	int i, status, surplus, cur_rownamespace, cur_numrows;
	char **cur_rowname, *cur_rownamestore;
	double *duals;
	cur_numrows = CPXgetnumrows (env, lp);

	if ((duals = (double *) calloc (cur_numrows, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in showDuals().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	status = CPXgetpi (env, lp, duals, 0, cur_numrows-1);  // Get duals
	if (status) {
		logMsg(logFile, "Failed to get dual prices.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	
	status = CPXgetrowname (env, lp, NULL, NULL, 0, &surplus, 0, cur_numrows-1);
	if (( status != CPXERR_NEGATIVE_SURPLUS ) &&
		( status != 0 ))  
	{
		logMsg (logFile, "Could not determine amount of space for row names.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	cur_rownamespace = - surplus;

	if ( cur_rownamespace > 0 ) 
	{
		if ((cur_rowname = (char **) calloc (cur_numrows, sizeof (char *))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in showDuals().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		if ((cur_rownamestore = (char *)  malloc (cur_rownamespace)) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in showDuals().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}

		status = CPXgetrowname (env, lp, cur_rowname, cur_rownamestore, cur_rownamespace, &surplus, 0, cur_numrows-1);
		if ( status ) 
		{
			logMsg (logFile, "CPXgetrowname failed.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}

		fprintf (logFile, "----------------Duals----------------\n");
		for (i=0; i<numRows; i++)
		{
			if (duals[i])
			{
				fprintf (logFile, "%50s: %6.3f\n", cur_rowname[i], duals[i]);
			}
		}
		fprintf (logFile, "--------------------------------------\n");
		fflush (logFile);
	
		free(cur_rowname);
		cur_rowname = NULL;

		free(cur_rownamestore);
		cur_rownamestore = NULL;
	}

	free (duals);
	duals = NULL;

	return 0;
}


/************************************************************************************************
*	Function	getRowData						Date last modified:  8/01/06 BGC, 03/13/07 SWO	*
*	Purpose:	Builds all constraints and arrays that store constraint indices for demands,	*
*				pickups, planes, and crewPairs, AND arrays that store demandindex,	*
*				planeindex, etc for each row.													*
************************************************************************************************/

static int
getRowData ()
{
	int i, j;

	// Callocing memory for puSDual and puEdual.
	for (i=0; i<numDemand; i++)
	{
		if ((demandList[i].puSDual = (double *) calloc (numAcTypes+numSetsSpecConnConstr, sizeof (double))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		if ((demandList[i].puEDual = (double *) calloc (numAcTypes+numSetsSpecConnConstr, sizeof (double))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		for (j=0; j<numAcTypes+numSetsSpecConnConstr; j++)
		{
			demandList[i].puSDual[j] = 0;
			demandList[i].puEDual[j] = 0;
		}
	}

	// Demand index to row stores the constraint index for each demand.
	if ((demandIndexToRow = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	// Add one row for each demand that is not in a locked tour.
	/*
	*	Could have used numOptDemand instead, but I had already started writing the code and didn't want to change
	*	something that was working.
	*/
	for (i=0; i<numDemand; i++)
	{
		demandIndexToRow[i] = -1;
		if (!demandList[i].inLockedTour)
		{
			numDemandRows ++;
		}
	}

	// Demand row index stores the demand index corresponding to the demand constraints.
	if ((demandRowIndex = (int *) calloc (numDemandRows, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	


	numDemandRows = 0;
	for (i=0; i<numDemand; i++)
	{
		if (!demandList[i].inLockedTour)
		{
			demandRowIndex[numDemandRows] = i;
			demandIndexToRow[i] = numRows;
			numRows ++;
			numDemandRows ++;
		}
	}

	// -- Finished demand.


	if ((afterPUTripListToRow = (int **) calloc ((numAcTypes+numSetsSpecConnConstr), sizeof (int*))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		if ((afterPUTripListToRow[i] = (int *) calloc (numDemand, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}

	// afterPUTripListToRow[i][j] stores the "after" constraint index corresponding to puTripList index i and demand index j
	//("pickup after trip" constraints)
	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		for (j=0; j<numDemand; j++)
		{
			afterPUTripListToRow[i][j] = -1;
		}
	}

	numAfterRows=0;
	for (j=0; j<numDemand; j++)
	{
		if (!demandList[j].inLockedTour)
		{
			numAfterRows += (numAcTypes + numSetsSpecConnConstr);
		}
	}
	if ((afterRowIndex = (int **) calloc (numAfterRows, sizeof (int *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	for (i=0; i<numAfterRows; i++)
	{
		if ((afterRowIndex[i] = (int *) calloc (2, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}	
	}

	numAfterRows=0;
	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		for (j=0; j<numDemand; j++)
		{
			if (!demandList[j].inLockedTour)
			{
				afterRowIndex[numAfterRows][0] = i;
				afterRowIndex[numAfterRows][1] = j;
				afterPUTripListToRow[i][j] = numRows;
				numRows ++;
				numAfterRows ++;
			}
		}
	}
	// -- Finished  after

	if ((beforePUTripListToRow = (int **) calloc ((numAcTypes+numSetsSpecConnConstr), sizeof (int*))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		if ((beforePUTripListToRow[i] = (int *) calloc (numDemand, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	// beforePUTripListToRow[i][j] stores the "before" constraint index corresponding to puTripList index i and demand index j
	//("pickup before trip" constraints)

	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		for (j=0; j<numDemand; j++)
		{
			beforePUTripListToRow[i][j] = -1;
		}
	}

	numBeforeRows=0;
	for (j=0; j<numDemand; j++)
	{
		if (!demandList[j].inLockedTour)
		{
			numBeforeRows += (numAcTypes + numSetsSpecConnConstr);
		}
	}

	if ((beforeRowIndex = (int **) calloc (numBeforeRows, sizeof (int *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	for (i=0; i<numBeforeRows; i++)
	{
		if ((beforeRowIndex[i] = (int *) calloc (2, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}	
	}

	numBeforeRows=0;
	for (i=0; i<(numAcTypes+numSetsSpecConnConstr); i++)
	{
		for (j=0; j<numDemand; j++)
		{
			if (!demandList[j].inLockedTour)
			{
				beforeRowIndex[numBeforeRows][0] = i;
				beforeRowIndex[numBeforeRows][1] = j;
				beforePUTripListToRow[i][j] = numRows;
				numRows ++;
				numBeforeRows ++;
			}
		}
	}
		// Finished  before

	if ((planeIndexToRow = (int *) calloc (numAircraft, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	numPlaneRows=0;
	for (i=0; i<numAircraft; i++)
	{
		planeIndexToRow[i] = -1;
		if (acList[i].availDT < optParam.windowEnd)
		{
			numPlaneRows ++;
		}
	}
	if ((planeRowIndex = (int *) calloc (numPlaneRows, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	numPlaneRows = 0;
	for (i=0; i<numAircraft; i++)
	{
		if (acList[i].availDT < optParam.windowEnd)
		{
			planeRowIndex[numPlaneRows] = i;
			planeIndexToRow[i] = numRows;
			numRows ++;
			numPlaneRows ++;
		}
	}

	// -- Finished planes

	if ((crewPairIndexToRow = (int *) calloc (numOptCrewPairs, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	numCrewPairRows = numOptCrewPairs;
	if ((crewPairRowIndex = (int *) calloc (numCrewPairRows, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	for (i=0; i<numCrewPairRows; i++)
	{
		crewPairRowIndex[i] = i;
		crewPairIndexToRow[i] = numRows;
		numRows ++;
	}

	// Finished crew pairs

	if ((crewIndexToRow = (int *) calloc (numCrew, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	numCrewRows = numCrew;
	if ((crewRowIndex = (int *) calloc (numCrewRows, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getRowData().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	for (i=0; i<numCrewRows; i++)
	{
		crewRowIndex[i] = i;
		crewIndexToRow[i] = numRows;
		numRows ++;
	}

	numRepoConnxnRows = numRepoConnxns;
	return 0;
}

/************************************************************************************************
*	Function	setRHSSenseAndNames				Date last modified:  8/01/06 BGC, 03/13/07 SWO	*
*	Purpose:	Creates constraint names and sense, i.e., <=, =, or >=.							*
************************************************************************************************/

static int
setRHSSenseAndNames (double *rhs, char *sense, char **name)
{
	int i, rowsSoFar=0;

	for (i=0; i<numDemandRows; i++)
	{
		rhs[rowsSoFar] = 1;
		sense[rowsSoFar] = 'E';
		sprintf(name[rowsSoFar], "Row %d; Demand Constraint; Demand ID %d", rowsSoFar, 
		demandList[demandRowIndex[i]].demandID);
		rowsSoFar ++;
	}
	
	for (i=0; i<numAfterRows; i++)
	{
		rhs[rowsSoFar] = 0;
		sense[rowsSoFar] = 'L';
		sprintf(name[rowsSoFar], "Row %d; After constraint; puTripList index %d, Demand ID %d", 
						rowsSoFar, afterRowIndex[i][0], demandList[afterRowIndex[i][1]].demandID);
		rowsSoFar ++;
	}

	for (i=0; i<numBeforeRows; i++)
	{
		rhs[rowsSoFar] = 0;
		sense[rowsSoFar] = 'L';
		sprintf(name[rowsSoFar], "Row %d; Before constraint; puTripList index %d, Demand ID %d", 
						rowsSoFar, beforeRowIndex[i][0], demandList[beforeRowIndex[i][1]].demandID);
		rowsSoFar ++;
	}


	for (i=0; i<numPlaneRows; i++)
	{
		rhs[rowsSoFar] = 1;
		sense[rowsSoFar] = 'L';
		sprintf(name[rowsSoFar], "Row %d; Plane constraint; Aircraft ID %d.", 
							rowsSoFar, acList[planeRowIndex[i]].aircraftID);
		rowsSoFar ++;
	}

	for (i=0; i<numCrewPairRows; i++)
	{
		rhs[rowsSoFar] = 1;
		sense[rowsSoFar] = 'L';
		sprintf(name[rowsSoFar], "Row %d; Crew pair constraint; Crew pair ID %d.", 
					rowsSoFar, crewPairList[crewPairRowIndex[i]].crewPairID);
		rowsSoFar ++;
	}

	for (i=0; i<numCrewRows; i++)
	{
		rhs[rowsSoFar] = 1;
		sense[rowsSoFar] = 'L';
		sprintf(name[rowsSoFar], "Row %d; Crew constraint; Crew ID %d.", 
					rowsSoFar, crewList[crewRowIndex[i]].crewID);
		rowsSoFar ++;
	}

	return 0;
}

/************************************************************************************************
*	Function	getCharterCost					Date last modified:  8/01/06 BGC, 05/15/07 SWO	*
*	Purpose:	Gets the charter cost for a particular demand index. The cost includes			*
*				charter cost for that acType plus xscharter cost.								*
************************************************************************************************/

static double
getCharterCost (int demandInd)
{
	int i, low, high, mid, cond;
	double cost=0;
	int flightTm, blockTm, elapsedTm, numStops;
	double chartercost_tune;
    
	chartercost_tune = 1;

	//if (demandList[demandInd].demandID > 250000)
	//	return 200000;

	if(demandList[demandInd].contingecnyfkdmdflag == 1){ 
		flightTm = (int) abs(difftime(demandList[demandInd].reqIn, demandList[demandInd].reqOut)/60);
	    numStops = 0;
	    elapsedTm = optParam.taxiOutTm + flightTm + ((optParam.taxiInTm +optParam.taxiOutTm) * numStops) + optParam.taxiInTm;
	    blockTm = optParam.taxiOutTm + flightTm + ((optParam.taxiInTm + optParam.taxiOutTm) * numStops) + optParam.taxiInTm;
	}
	else{ 
		if (demandList[demandInd].outAirportID != demandList[demandInd].inAirportID) 
          getFlightTime (demandList[demandInd].outAirportID, demandList[demandInd].inAirportID, demandList[demandInd].aircraftTypeID,
			 month, demandList[demandInd].numPax, &flightTm, &blockTm, &elapsedTm, &numStops);
	    else
          elapsedTm = (demandList[demandInd].reqIn - demandList[demandInd].reqOut)/60;
	}

	for (i=0; i<numAcTypes; i++)
	{
		if (acTypeList[i].aircraftTypeID == demandList[demandInd].aircraftTypeID)
		{
//			cost = acTypeList[i].charterCost*(max(optParam.minCharterTime, demandList[demandInd].elapsedTm[i]))/60.0;
			if(demandList[demandInd].contingecnyfkdmdflag == 1)
			   cost = (acTypeList[i].charterCost * chartercost_tune)*(max(optParam.minCharterTime, elapsedTm))/60.0;
			else 
				if (demandList[demandInd].ownerID == 87359){ //fake contengency demand, lower the charter cost, but still higher enough to put on mac
					cost = (acTypeList[i].macOprCost + acTypeList[i].operatingCost/2)*(max(optParam.minCharterTime, elapsedTm))/60.0;
					return cost;
				}
				else 
					cost = acTypeList[i].charterCost*(max(optParam.minCharterTime, elapsedTm))/60.0;
			break;
		}
	}
	//for sales demos, contractID == -1 (and no ownerID)
	if(demandList[demandInd].contractID < 0)
		return optParam.uncovDemoPenalty;

	//for demand with runways < 5000, RZL 02/28/08
	if(demandList[demandInd].noCharterFlag)
		return 5 * cost;

	//MAC - big charter penalty for mac owner's trips - 09/23/08 ANG
	if(optParam.withMac == 1){
		if(demandList[demandInd].isMacDemand)
			return 3 * cost;
	}

	// Binary search in owner list.
	if(demandList[demandInd].contingecnyfkdmdflag != 1){
		low = 0;
		high = numOwners - 1;
		while(low <= high) {
			mid = low + (high - low) / 2;
			if ((cond = (demandList[demandInd].ownerID - ownerList[mid].ownerID)) < 0)
				high = mid -1;
			else if(cond > 0)
				low = mid + 1;
			else {
				return (cost + optParam.xsCharterPenalty[ownerList[mid].charterLevel-1]);
			}
		}
	}
	if (demandList[demandInd].contingecnyfkdmdflag)
		return cost;

	return (cost + optParam.xsCharterPenalty[2]); //Can't find the owner, label as level 3. RLZ 03/31/08
	//return cost;
}

/************************************************************************************************
*	Function	addCharterCols			Date last modified:  8/01/06 BGC, updated 02/15/07 SWO	*
*	Purpose:	Adds charter variables for all demand rows.										*
************************************************************************************************/

static int
addCharterCols (const CPXENVptr env, const CPXLPptr lp)
{
	int i, status;
	double cost;
	double flyHomeCost = 0;

	//START - 04/09/08 ANG
	//if(optParam.autoFlyHome == 1) {
		int x, y, pos;
		CrewEndTourRecord *fmPtr;
		Crew *cPtr;

		fmPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));
		if(! fmPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in addCharterCols().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		cPtr = (Crew *) calloc((size_t) 1, (size_t) sizeof(Crew));
		if(! cPtr) {
			logMsg(logFile,"%s Line %d, Out of Memory in addCharterCols().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	//}
	//END - 04/09/08 ANG

	for (i=0; i<numDemandRows; i++) // Add one column for each demand row.
	{
		// Creating variable name.
		sprintf (charBuf, "Charter variable; Demand ID %d", demandList[demandRowIndex[i]].demandID);

		if (demandList[demandRowIndex[i]].isAppoint)
		{// If maintenance/appointment, charter cost is uncovMaintPenalty.

			#ifdef DEBUGGING // Check will be executed only if run in debug mode.
			status = CPXcheckaddcols (env, lp, 1, 1, &optParam.uncovMaintPenalty, &zeroInt, &i, &oneDbl,  &zeroDbl, &oneDbl, &charBuf);
			if (status)
			{
				logMsg(logFile, "Column error.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
			#endif

			//START - Get fly home cost - preference to PIC, second preference to SIC, more bonus if both are sent home - 04/09/08 ANG
			if (optParam.autoFlyHome == 1 && demandList[demandRowIndex[i]].isAppoint == 4 && demandList[demandRowIndex[i]].maintenanceRecordID > 0){
				for(x = 0, fmPtr = crewEndTourList; x < crewEndTourCount; ++x, ++fmPtr) {
					if (fmPtr->recordID == demandList[demandRowIndex[i]].maintenanceRecordID){
						switch(fmPtr->recordType) {
							case 1: //only one crew is sent home.  Need to check whether the crew is PIC or SIC.  PIC gets more preference.
								pos = 0;
								for(y = 0, cPtr = crewList; y < numCrew; ++y, ++cPtr){
									if(cPtr->crewID == fmPtr->crewID1){
										if (cPtr->position == 1){
											flyHomeCost = optParam.uncovFlyHomePenalty;
											pos = 1;
										} 
										else if (cPtr->position == 2){
											flyHomeCost = optParam.uncovFlyHomePenalty2;
											pos = 2;
										}
										break;
									}
								}
								if (pos == 0){ //should not happen
									fprintf(logFile, "Could not find crew's position for fake maint record %d.\n", fmPtr->recordID);
									flyHomeCost = optParam.uncovMaintPenalty;
								}
								break;
							case 2: //means that 2 crews are sent home 
								flyHomeCost = optParam.uncovFlyHomePenalty + optParam.uncovFlyHomePenalty2; 
								break;
							default: 
								flyHomeCost = optParam.uncovMaintPenalty;  // is not supposed to happen, to come here, recordType is not in (0,3)
						}
					}
				}
			}
			//END - Get fly home cost - 04/09/08 ANG

			//cost = optParam.uncovMaintPenalty;
			//cost = (demandList[demandRowIndex[i]].isAppoint == 4) ? optParam.uncovFlyHomePenalty : optParam.uncovMaintPenalty; // 11/05/07 ANG
			cost = (demandList[demandRowIndex[i]].isAppoint == 4) ? flyHomeCost : optParam.uncovMaintPenalty; // 11/05/07 ANG

			//status = CPXaddcols (env, lp, 1, 1, &cost, &zeroInt, &i, &oneDbl,  &zeroDbl, &oneDbl, &charBuf);
			status = CPXaddcols (env, lp, 1, 1, &cost, &zeroInt, &i, &oneDbl,  &zeroDbl, NULL, &charBuf);
			if (status)
			{
				logMsg(logFile, "Could not add charter column.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
		}
		else // Not an appointment/maintenance demand
		{
			cost = getCharterCost (demandRowIndex[i]);
			#ifdef DEBUGGING
			status = CPXcheckaddcols (env, lp, 1, 1, &cost, &zeroInt, &i, &oneDbl,  &zeroDbl, &oneDbl, &charBuf);
			if (status)
			{
				logMsg(logFile, "Column error.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
			#endif

			//status = CPXaddcols (env, lp, 1, 1, &cost, &zeroInt, &i, &oneDbl,  &zeroDbl, &oneDbl, &charBuf);
			status = CPXaddcols (env, lp, 1, 1, &cost, &zeroInt, &i, &oneDbl,  &zeroDbl, NULL, &charBuf);
			if (status)
			{
				logMsg(logFile, "Could not add charter column.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
		}
////TEMP FOR DEBUGGING
//		if(demandList[i].isAppoint)
//			logMsg(logFile, "Appoint for Aircraft: %d, Cost: %f.\n", acList[demandList[i].acInd].aircraftID, cost);
//		else
//			logMsg(logFile, "Demand: %d, Cost: %f.\n", demandList[i].demandID, cost);
////END TEMP
	}

	return 0;
}


/************************************************************************************************
*	Function	buildTourColumn				Date last modified:  8/01/06 BGC, 03/14/07 SWO		*
*	Purpose:	Builds column for tour with index tourInd in tourList.							*
************************************************************************************************/

static int 
buildTourColumn (int tourInd, int *nz)
{
	int i, j, special=0, k, acInd, demInd, acTypeInd, lastDemandInd, m;

	(*nz)=0;

	// rowInd and rowCoeff store the non-zero elements of each column as required by CPLEX. 
	rowInd[*nz] = crewPairIndexToRow[tourList[tourInd].crewPairInd];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	rowInd[*nz] = crewIndexToRow[crewPairList[tourList[tourInd].crewPairInd].crewListInd[0]];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	rowInd[*nz] = crewIndexToRow[crewPairList[tourList[tourInd].crewPairInd].crewListInd[1]];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	//START - DQ - 12/09/2009 ANG
	if(optParam.withDQ == 1){
		if(crewList[crewPairList[tourList[tourInd].crewPairInd].crewListInd[0]].dqOtherCrewPos != 0){
			rowInd[*nz] = crewIndexToRow[crewPairList[tourList[tourInd].crewPairInd].crewListInd[0]+crewList[crewPairList[tourList[tourInd].crewPairInd].crewListInd[0]].dqOtherCrewPos];
			rowCoeff[*nz] = 1;
			(*nz) ++;
		}
		if(crewList[crewPairList[tourList[tourInd].crewPairInd].crewListInd[1]].dqOtherCrewPos != 0){
			rowInd[*nz] = crewIndexToRow[crewPairList[tourList[tourInd].crewPairInd].crewListInd[1]+crewList[crewPairList[tourList[tourInd].crewPairInd].crewListInd[1]].dqOtherCrewPos];
			rowCoeff[*nz] = 1;
			(*nz) ++;
		}
	}
	//END - DQ - 12/09/2009 ANG

	acTypeInd = crewPairList[tourList[tourInd].crewPairInd].acTypeIndex;

	for (i=0; i<MAX_WINDOW_DURATION; i++)
	{
		if (tourList[tourInd].duties[i] < 0)
			continue;
		for (j=0; j<maxTripsPerDuty; j++)
		{
			if ((k = dutyList[acTypeInd][tourList[tourInd].duties[i]].demandInd[j]) < 0)
				break;
			lastDemandInd = k;
			
			rowInd[*nz] = demandIndexToRow[k];
			rowCoeff[*nz] = 1;
			(*nz) ++;
		}
		if ((m = dutyList[acTypeInd][tourList[tourInd].duties[i]].repoDemandInd) >= 0)
		{
			lastDemandInd = -1*(m+1);
		}
		/*
		*	lastDemandInd keeps track of last demand index in the tour.
		*	Repo gets special treatment because it signals a drop-off at the begining of a trip, while all
		*	other demands are dropped off at the end of a trip.
		*/
	}

	if (tourList[tourInd].finalApptDemInd >= 0)
	{// Maintenance/appointment demand is covered.
		rowInd[*nz] = demandIndexToRow[tourList[tourInd].finalApptDemInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
	}

	switch (tourList[tourInd].crewArcType)
	{
	case 1: // Pick up when plane next available.
		acInd = crewPairList[tourList[tourInd].crewPairInd].crewPlaneList[tourList[tourInd].crewArcInd].acInd;
		rowInd[*nz] = planeIndexToRow[acInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	case 2: // Pick up at start of demand.
		acInd = crewPairList[tourList[tourInd].crewPairInd].crewPUSList[tourList[tourInd].crewArcInd]->acInd;
		demInd = crewPairList[tourList[tourInd].crewPairInd].crewPUSList[tourList[tourInd].crewArcInd]->demandInd;
		if(acInd > -1)
			rowInd[*nz] = beforePUTripListToRow[acList[acInd].puTripListIndex][demInd];
		else if(acInd == -1)
			rowInd[*nz] = beforePUTripListToRow[acTypeInd][demInd];
		else //acInd < -1 and we are considering a group of planes
			rowInd[*nz] = beforePUTripListToRow[acList[acGroupList[-acInd].acInd[0]].puTripListIndex][demInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	case 3: // Pick up at end of demand leg.
		acInd = crewPairList[tourList[tourInd].crewPairInd].crewPUEList[tourList[tourInd].crewArcInd]->acInd;
		demInd = crewPairList[tourList[tourInd].crewPairInd].crewPUEList[tourList[tourInd].crewArcInd]->demandInd;
		if(acInd > -1)
			rowInd[*nz] = afterPUTripListToRow[acList[acInd].puTripListIndex][demInd];
		else if(acInd == -1)
			rowInd[*nz] = afterPUTripListToRow[acTypeInd][demInd];
		else //acInd < -1 and we are considering a group of planes
			rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-acInd].acInd[0]].puTripListIndex][demInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	}

	if (tourList[tourInd].dropPlane){
		if (tourList[tourInd].finalApptDemInd >= 0){ // Final maintenance/appointment demand is covered.
			if(acInd > -1)
				rowInd[*nz] = afterPUTripListToRow[acList[acInd].puTripListIndex][tourList[tourInd].finalApptDemInd];
			else if(acInd == -1)
				rowInd[*nz] = afterPUTripListToRow[acTypeInd][tourList[tourInd].finalApptDemInd];
			else //acInd < -1 and we are considering a group of planes
				rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-acInd].acInd[0]].puTripListIndex][tourList[tourInd].finalApptDemInd];
		}
		else if (lastDemandInd < 0){ // Repo so drop off before
			lastDemandInd = lastDemandInd*(-1) - 1;
			if(acInd > -1)
				rowInd[*nz] = beforePUTripListToRow[acList[acInd].puTripListIndex][lastDemandInd];
			else if(acInd == -1)
				rowInd[*nz] = beforePUTripListToRow[acTypeInd][lastDemandInd];
			else //acInd < -1 and we are considering a group of planes
				rowInd[*nz] = beforePUTripListToRow[acList[acGroupList[-acInd].acInd[0]].puTripListIndex][lastDemandInd];
		}
		else{ // Regular demand -- drop off after
			if(acInd > -1)
				rowInd[*nz] = afterPUTripListToRow[acList[acInd].puTripListIndex][lastDemandInd];
			else if(acInd == -1)
				rowInd[*nz] = afterPUTripListToRow[acTypeInd][lastDemandInd];
			else //acInd < -1 and we are considering a group of planes
				rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-acInd].acInd[0]].puTripListIndex][lastDemandInd];
		}
		rowCoeff[*nz] = -1;
		(*nz) ++;	
	}

	return 0;
}


/************************************************************************************************
*	Function	buildExgTourColumn				Date last modified:  8/01/06 BGC, 03/19/07 SWO	*
*	Purpose:	Builds column for existing tour with index exgTourInd in exgTourList.			*
************************************************************************************************/

static int 
buildExgTourColumn (int exgTourInd, int *nz)
{
	int i, j, special=0;
	
	(*nz)=0;

	rowInd[*nz] = crewPairIndexToRow[exgTourList[exgTourInd].crewPairInd];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0]];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]];
	rowCoeff[*nz] = 1;
	(*nz) ++;

	//START - DQ - 12/09/2009 ANG
	if(optParam.withDQ == 1){
		if(crewList[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0]].dqOtherCrewPos != 0){
			rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0]+crewList[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0]].dqOtherCrewPos];
			rowCoeff[*nz] = 1;
			(*nz) ++;
		}
		if(crewList[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]].dqOtherCrewPos != 0){
			rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]+crewList[crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]].dqOtherCrewPos];
			rowCoeff[*nz] = 1;
			(*nz) ++;
		}
	}
	//END - DQ - 12/09/2009 ANG

	//START - Build additional columns related to existing solution - same aircraft, 2 overlapping crew - 03/17/08 ANG
	if(exgTourList[exgTourInd].crewPairInd2 > -1){
		rowInd[*nz] = crewPairIndexToRow[exgTourList[exgTourInd].crewPairInd2];
		rowCoeff[*nz] = 1;
		(*nz) ++;

		//Do not re-add crew used in both crewpair - 05/01/08 ANG
		if(crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0] != crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0] &&
			crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0] != crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]) 
		{
			rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0]];
			rowCoeff[*nz] = 1;
			(*nz) ++;

			//START - DQ - 12/09/2009 ANG
			if(optParam.withDQ == 1){
				if(crewList[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0]].dqOtherCrewPos != 0){
					rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0]+crewList[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[0]].dqOtherCrewPos];
					rowCoeff[*nz] = 1;
					(*nz) ++;
				}
			}
			//END - DQ - 12/09/2009 ANG
		}

		if(crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1] != crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[0] &&
			crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1] != crewPairList[exgTourList[exgTourInd].crewPairInd].crewListInd[1]) 
		{
			rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1]];
			rowCoeff[*nz] = 1;
			(*nz) ++;

			//START - DQ - 12/09/2009 ANG
			if(optParam.withDQ == 1){
				if(crewList[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1]].dqOtherCrewPos != 0){
					rowInd[*nz] = crewIndexToRow[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1]+crewList[crewPairList[exgTourList[exgTourInd].crewPairInd2].crewListInd[1]].dqOtherCrewPos];
					rowCoeff[*nz] = 1;
					(*nz) ++;
				}
			}
			//END - DQ - 12/09/2009 ANG
		}
	}
	//END - Build additional columns related to existing solution - same aircraft, 2 overlapping crew - 03/17/08 ANG

	for (i=0; i<MAX_LEGS; i++)
	{
		if ((j = exgTourList[exgTourInd].demandInd[i]) < 0)
			break;
		rowInd[*nz] = demandIndexToRow[j];
		rowCoeff[*nz] = 1;
		(*nz) ++;
	}

	//START - Additional nonzero from second aircraft - 03/27/08 ANG
	for (i=0; i<MAX_LEGS; i++)
	{
		if ((exgTourList[exgTourInd].acInd2 < 0) || ((j = exgTourList[exgTourInd].demandInd2[i]) < 0))
			break;
		rowInd[*nz] = demandIndexToRow[j];
		rowCoeff[*nz] = 1;
		(*nz) ++;
	}
	//END - Additional nonzero from second aircraft - 03/27/08 ANG*/
	switch (exgTourList[exgTourInd].pickupType)
	{
	case 1: // pickup when available or flying at the beginning of the planning window.
		rowInd[*nz] = planeIndexToRow[exgTourList[exgTourInd].acInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	case 2: //  before demand leg
		if(exgTourList[exgTourInd].acInd > -1)
			rowInd[*nz] = beforePUTripListToRow[acList[exgTourList[exgTourInd].acInd].puTripListIndex][exgTourList[exgTourInd].pickupInd];
		else if(exgTourList[exgTourInd].acInd == -1)
			rowInd[*nz] = beforePUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].pickupInd];
		else //exgTourList[exgTourInd].acInd < -1 and we are considering a group of planes
			rowInd[*nz] = beforePUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd].acInd[0]].puTripListIndex][exgTourList[exgTourInd].pickupInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	case 3: //  after demand leg
		if(exgTourList[exgTourInd].acInd > -1)
			rowInd[*nz] = afterPUTripListToRow[acList[exgTourList[exgTourInd].acInd].puTripListIndex][exgTourList[exgTourInd].pickupInd];
		else if(exgTourList[exgTourInd].acInd == -1)
			rowInd[*nz] = afterPUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].pickupInd];
		else //exgTourList[exgTourInd].acInd < -1 and we are considering a group of planes
			rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd].acInd[0]].puTripListIndex][exgTourList[exgTourInd].pickupInd];
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	case 4: //  after repo leg
		for (j=0; j<numRepoConnxnRows; j++)
		{
			if (repoConnxnList[j] == exgTourList[exgTourInd].pickupInd)
				break; /*	Not checking to make sure that the index is in the connection list.
							Assume the structure is clean.*/
		}
		rowInd[*nz] = numRows+j;
		rowCoeff[*nz] = 1;
		(*nz) ++;
		break;
	}


	//RLZ 07/31/08 One pair with 2nd aircraft
	//if (exgTourList[exgTourInd].acInd2 > -1){
	//	rowInd[*nz] = planeIndexToRow[exgTourList[exgTourInd].acInd2];
	//	rowCoeff[*nz] = 1;
	//	(*nz) ++;
	//}


	switch (exgTourList[exgTourInd].dropoffType)
	{
	case 2: //  before demand leg
		if(exgTourList[exgTourInd].acInd > -1)
			rowInd[*nz] = beforePUTripListToRow[acList[exgTourList[exgTourInd].acInd].puTripListIndex][exgTourList[exgTourInd].dropoffInd];
		else if(exgTourList[exgTourInd].acInd == -1)
			rowInd[*nz] = beforePUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].dropoffInd];
		else //exgTourList[exgTourInd].acInd < -1 and we are considering a group of planes
			rowInd[*nz] = beforePUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd].acInd[0]].puTripListIndex][exgTourList[exgTourInd].dropoffInd];
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	case 3: //  after demand leg
		if(exgTourList[exgTourInd].acInd > -1)
			rowInd[*nz] = afterPUTripListToRow[acList[exgTourList[exgTourInd].acInd].puTripListIndex][exgTourList[exgTourInd].dropoffInd];
		else if(exgTourList[exgTourInd].acInd == -1)
			rowInd[*nz] = afterPUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].dropoffInd];
		else //exgTourList[exgTourInd].acInd < -1 and we are considering a group of planes
			rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd].acInd[0]].puTripListIndex][exgTourList[exgTourInd].dropoffInd];
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	case 4: //  after repo leg
		for (j=0; j<numRepoConnxnRows; j++)
		{
			if (repoConnxnList[j] == exgTourList[exgTourInd].dropoffInd)
				break; /*	Not checking to make sure that the index is in the connection list.
							Assume the structure is clean.*/
		}
		rowInd[*nz] = numRows+j;
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	}

	
	//START - insert pickup information for 2nd aircraft - 08/15/08 ANG
	if(exgTourList[exgTourInd].acInd2 > -1){
		switch (exgTourList[exgTourInd].pickupType2)
		{
		case 1: // pickup when available or flying at the beginning of the planning window.
			rowInd[*nz] = planeIndexToRow[exgTourList[exgTourInd].acInd2];
			rowCoeff[*nz] = 1;
			(*nz) ++;
			break;
		case 2: //  before demand leg
			rowInd[*nz] = beforePUTripListToRow[acList[exgTourList[exgTourInd].acInd2].puTripListIndex][exgTourList[exgTourInd].pickupInd2];
			rowCoeff[*nz] = 1;
			(*nz) ++;
			break;
		case 3: //  after demand leg
			rowInd[*nz] = afterPUTripListToRow[acList[exgTourList[exgTourInd].acInd2].puTripListIndex][exgTourList[exgTourInd].pickupInd2];
			rowCoeff[*nz] = 1;
			(*nz) ++;
			break;
		case 4: //  after repo leg
			for (j=0; j<numRepoConnxnRows; j++)
			{
				if (repoConnxnList[j] == exgTourList[exgTourInd].pickupInd2)
					break; /*	Not checking to make sure that the index is in the connection list.
								Assume the structure is clean.*/
			}
			rowInd[*nz] = numRows+j;
			rowCoeff[*nz] = 1;
			(*nz) ++;
			break;
		}
	}//end if
	//END - 08/15/08 ANG
	
	//START - 03/27/08 ANG, 
   	switch (exgTourList[exgTourInd].dropoffType2)
	{
	case 2: //  before demand leg
		if(exgTourList[exgTourInd].acInd2 > -1)
			rowInd[*nz] = beforePUTripListToRow[acList[exgTourList[exgTourInd].acInd2].puTripListIndex][exgTourList[exgTourInd].dropoffInd2];
		else if(exgTourList[exgTourInd].acInd2 == -1)
			rowInd[*nz] = beforePUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].dropoffInd2];
		else //exgTourList[exgTourInd].acInd2 < -1 and we are considering a group of planes
			rowInd[*nz] = beforePUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd2].acInd[0]].puTripListIndex][exgTourList[exgTourInd].dropoffInd2];
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	case 3: //  after demand leg
		if(exgTourList[exgTourInd].acInd2 > -1)
			rowInd[*nz] = afterPUTripListToRow[acList[exgTourList[exgTourInd].acInd2].puTripListIndex][exgTourList[exgTourInd].dropoffInd2];
		else if(exgTourList[exgTourInd].acInd2 == -1)
			rowInd[*nz] = afterPUTripListToRow[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][exgTourList[exgTourInd].dropoffInd2];
		else //exgTourList[exgTourInd].acInd2 < -1 and we are considering a group of planes
			rowInd[*nz] = afterPUTripListToRow[acList[acGroupList[-exgTourList[exgTourInd].acInd2].acInd[0]].puTripListIndex][exgTourList[exgTourInd].dropoffInd2];
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	case 4: //  after repo leg
		for (j=0; j<numRepoConnxnRows; j++)
		{
			if (repoConnxnList[j] == exgTourList[exgTourInd].dropoffInd2)
				break; 
		}
		rowInd[*nz] = numRows+j;
		rowCoeff[*nz] = -1;
		(*nz) ++;
		break;
	}

	//END - 03/27/08 ANG

	return 0;
}


/************************************************************************************************
*	Function	addExgSolnCols								Date last modified:  8/01/06 BGC	*
*	Purpose:	Add columns for existing solution.												*
************************************************************************************************/

static int
addExgSolnCols (const CPXENVptr env, const CPXLPptr lp)
{
	int i, nz, status;
	double cost;
	
	for (i=0; i<numExgTours; i++) // Build column for each existing tour.
	{
		sprintf (charBuf, "Exg tour variable; Index %d", i);	
		buildExgTourColumn (i, &nz);
		cost = exgTourList[i].cost;

		//add cost of 2nd aircraft for related crewpair (additional existing columns) - 05/06/08 ANG
		if(exgTourList[i].acInd2 > -1)
			cost += exgTourList[i].cost2;

		#ifdef DEBUGGING
		status = CPXcheckaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
		if (status)
		{
			logMsg(logFile, "Column error.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
		#endif

		//status = CPXaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
		status = CPXaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, NULL, &charBuf);
		if (status)
		{
			logMsg(logFile, "CPLEX failed to create column.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	addApptCols						Date last modified:  8/01/06 BGC, 03/14/07 SWO	*
*	Purpose:	Add columns for pickups at end of maintenance/appointment leg.					*
*   Update: this funcion is commented out on 04/24/08. We can rely on dutyNode to cover these maintenance/appointment leg.
*    RLZ 04/24/08, But still good to have it.
************************************************************************************************/

static int
addApptCols (const CPXENVptr env, const CPXLPptr lp)
{
	int i, j, nz, status, firstCrPrInd;
//Moved from getOptPlanes to here
	if ((availEnd = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numDemand; i++)
	{
		availEnd[i] = 0; // ID of plane that is available at the end.
	}

	for (i=0; i<numOptDemand; i++)
	{
		if (demandList[i].isAppoint)
		{ // If demand is a maintenance or airport appointment leg
			//if ((difftime(acList[demandList[i].acInd].availDT, demandList[i].reqOut) <= 0) && 
			//	(optParam.windowStart < demandList[i].reqOut) &&
			//	(acList[demandList[i].acInd].availAirportID == demandList[i].inAirportID))

			//Relax the above condition - 12/31/08 ANG
			if (//(difftime(acList[demandList[i].acInd].availDT, demandList[i].reqOut) <= 0) && 
				(optParam.windowStart <= demandList[i].reqOut) &&
				(acList[demandList[i].acInd].availAirportID == demandList[i].inAirportID))
			{// If aircraft's availDT is before beginning of appointment AND is available at the same airport as appointment
				for (j=0; j<i; j++)
				{
					if (demandList[j].aircraftID == demandList[i].aircraftID)
					{
						j = i+1;
					}
				}
				if (j == i) // If the appointment demand is the first (smallest schedOut) for that plane after availDT.
				{ // Create a column that picks up the plane at availDT and covers the maintenance/appointment.
					rowInd[0] = planeIndexToRow[demandList[i].acInd];
					rowCoeff[0] = 1;
					rowInd[1] = demandIndexToRow[i];
					rowCoeff[1] = 1;
					nz = 2;

					
					//if (difftime(optParam.windowEnd, demandList[i].reqIn) < 0)
					if (difftime(optParam.windowEnd, demandList[i].reqIn) >= 0) // 02/29/08 ANG
					{// If the appointment ends before the end of the planning window, drop it off -- Note ANG: only if there is crewpair
						firstCrPrInd = acList[demandList[i].acInd].firstCrPrInd;
						if (firstCrPrInd < 0){ //another way to get crewPairInd, if hasflownfirst is not 1
							firstCrPrInd = acList[demandList[i].acInd].cprInd[0];
						}

						//do not drop planes if appt is not on the last day.
						if (firstCrPrInd > -1){
							if((firstEndOfDay + crewPairList[firstCrPrInd].endRegDay * 86400 - demandList[i].reqOut) < 86400){
								rowInd[2] = afterPUTripListToRow[acList[demandList[i].acInd].puTripListIndex][i];
								rowCoeff[2] = -1;
								nz = 3;
								availEnd[i] = demandList[i].aircraftID;
							}
						}
						else{
							rowInd[2] = afterPUTripListToRow[acList[demandList[i].acInd].puTripListIndex][i];
							rowCoeff[2] = -1;
							nz = 3;
							availEnd[i] = demandList[i].aircraftID;
						}
					}
					

					sprintf (charBuf, "Appt variable; Demand index %d", i);



					#ifdef DEBUGGING
					status = CPXcheckaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
					if (status)
					{
						logMsg(logFile, "Column error.\n");
						CPXgeterrorstring (env, status, charBuf);
						logMsg(logFile,"%s", charBuf);
						writeWarningData(myconn); exit(1);
					}
					#endif

					//status = CPXaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
					status = CPXaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, NULL, &charBuf);
					
					logMsg(logFile, "Appt variable; Demand index %d, col %d \n", demandList[i].demandID, CPXgetnumcols(env,lp)-1);
					
					
					if (status)
					{
						logMsg(logFile, "CPLEX failed to create column.\n");
						CPXgeterrorstring (env, status, charBuf);
						logMsg(logFile,"%s", charBuf);
						writeWarningData(myconn); exit(1);
					}
				}
			}
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	addApptCols2						Date last modified:  12/30/08 ANG	*
*	Purpose:	Add columns for pickups at end of maintenance/appointment leg.					*
*   Update:  Additional appointment columns for covering ALL maintenance/ownersigning with
*			 no other activity legs (just pure appointments with NO CREW)
************************************************************************************************/

static int
addApptCols2 (const CPXENVptr env, const CPXLPptr lp)
{
	int i, j, nz, status, firstCrPrInd;
	int addColumn;

	//Moved from getOptPlanes to here
	if ((availEnd = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numDemand; i++)
	{
		availEnd[i] = 0; // ID of plane that is available at the end.
	}

	for (i=0; i<numOptDemand; i++)
	{
		if (demandList[i].isAppoint)
		{ // If demand is a maintenance or airport appointment leg
			if ((difftime(acList[demandList[i].acInd].availDT, demandList[i].reqOut) <= 0) && 
				(optParam.windowStart < demandList[i].reqOut) &&
				(acList[demandList[i].acInd].availAirportID == demandList[i].inAirportID))
			{// If aircraft's availDT is before beginning of appointment AND is available at the same airport as appointment
				for (j=0; j<i; j++)
				{
					if (demandList[j].aircraftID == demandList[i].aircraftID)
					{
						j = i+1;
					}
				}
				if (j == i) // If the appointment demand is the first (smallest schedOut) for that plane after availDT.
				{ // Create a column that picks up the plane at availDT and covers the maintenance/appointment.
					addColumn = 0;

					rowInd[0] = planeIndexToRow[demandList[i].acInd];
					rowCoeff[0] = 1;
					rowInd[1] = demandIndexToRow[i];
					rowCoeff[1] = 1;
					nz = 2;

					//First, check if there is another appointment for this aircraft in the planning horizon
					for(j=i+1; j<numOptDemand; j++){
						if(demandList[j].isAppoint && demandList[j].reqOut < optParam.windowEnd && demandList[j].aircraftID == demandList[i].aircraftID){
							if (demandList[i].outAirportID == demandList[j].inAirportID){ //Make sure they are at the same airport, WHALL's fix.
								addColumn++;
								rowInd[1+addColumn] = demandIndexToRow[j];
								rowCoeff[1+addColumn] = 1;
								nz++;
							}
							else
								break;
						}
					}
					availEnd[i] = demandList[i].aircraftID;

					////if (difftime(optParam.windowEnd, demandList[i].reqIn) < 0)
					//if (difftime(optParam.windowEnd, demandList[i].reqIn) >= 0) // 02/29/08 ANG
					//{// If the appointment ends before the end of the planning window, drop it off -- Note ANG: only if there is crewpair
					//	firstCrPrInd = acList[demandList[i].acInd].firstCrPrInd;
					//	if (firstCrPrInd < 0){ //another way to get crewPairInd, if hasflownfirst is not 1
					//		firstCrPrInd = acList[demandList[i].acInd].cprInd[0];
					//	}

					//	//do not drop planes if appt is not on the last day.
					//	if (firstCrPrInd > -1){
					//		if((firstEndOfDay + crewPairList[firstCrPrInd].endRegDay * 86400 - demandList[i].reqOut) < 86400){
					//			rowInd[2] = afterPUTripListToRow[acList[demandList[i].acInd].puTripListIndex][i];
					//			rowCoeff[2] = -1;
					//			nz = 3;
					//			availEnd[i] = demandList[i].aircraftID;
					//		}
					//	}
					//	else{
					//		rowInd[2] = afterPUTripListToRow[acList[demandList[i].acInd].puTripListIndex][i];
					//		rowCoeff[2] = -1;
					//		nz = 3;
					//		availEnd[i] = demandList[i].aircraftID;
					//	}
					//}
					//

					if(addColumn == 0)
						continue; //Don't add column

					sprintf (charBuf, "Appt variable2; Aircraft index %d", demandList[i].acInd);

					#ifdef DEBUGGING
					status = CPXcheckaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
					if (status)
					{
						logMsg(logFile, "Column error.\n");
						CPXgeterrorstring (env, status, charBuf);
						logMsg(logFile,"%s", charBuf);
						writeWarningData(myconn); exit(1);
					}
					#endif

					//status = CPXaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
					status = CPXaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, NULL, &charBuf);
					
					logMsg(logFile, "Appt variable2; Aircraft index %d, col %d \n", demandList[i].aircraftID, CPXgetnumcols(env,lp)-1);
					
					
					if (status)
					{
						logMsg(logFile, "CPLEX failed to create column.\n");
						CPXgeterrorstring (env, status, charBuf);
						logMsg(logFile,"%s", charBuf);
						writeWarningData(myconn); exit(1);
					}
				}
			}
		}
	}
	return 0;
}


/************************************************************************************************
*	Function	addPureCrewACCols						Date last modified:  04/16/2009 RLZ	*
*	Purpose:	Add columns for pure CREW-AC columns .					*

************************************************************************************************/

static int
addPureCrewACCols (const CPXENVptr env, const CPXLPptr lp)
{
	int x, cp, nz, status, p, numCrewAcTour;
	CrewArc *crewArc;
	double bonus;
	double bonus_adj = 1000.0;


	if((crewAcTourList = (crewAcTour *)calloc((numOptCrewPairs * 40), sizeof(crewAcTour))) == NULL) {  
		logMsg(logFile,"%s Line %d, Out of Memory in addPureCrewACCols().\n", __FILE__, __LINE__);
		writeWarningData(myconn); exit(1);
	}

	numCrewAcTour = 0;


    for(cp = 0; cp < numOptCrewPairs; cp++){ 
		for(x = 0; x<crewPairList[cp].numPlaneArcs; x++){
			//initialize arc count
			crewArc = &crewPairList[cp].crewPlaneList[x];
			p = crewArc->acInd;
			nz = 0;

			rowInd[nz] = crewPairIndexToRow[cp];
			rowCoeff[nz] = 1;
			(nz) ++;

			rowInd[nz] = crewIndexToRow[crewPairList[cp].crewListInd[0]];
			rowCoeff[nz] = 1;
			(nz) ++;

			rowInd[nz] = crewIndexToRow[crewPairList[cp].crewListInd[1]];
			rowCoeff[nz] = 1;
			(nz) ++;

			//START - DQ - 12/09/2009 ANG
			if(optParam.withDQ == 1){
				if(crewList[crewPairList[cp].crewListInd[0]].dqOtherCrewPos != 0){
					rowInd[nz] = crewIndexToRow[crewPairList[cp].crewListInd[0]+crewList[crewPairList[cp].crewListInd[0]].dqOtherCrewPos];
					rowCoeff[nz] = 1;
					(nz) ++;
				}
				if(crewList[crewPairList[cp].crewListInd[1]].dqOtherCrewPos != 0){
					rowInd[nz] = crewIndexToRow[crewPairList[cp].crewListInd[1]+crewList[crewPairList[cp].crewListInd[1]].dqOtherCrewPos];
					rowCoeff[nz] = 1;
					(nz) ++;
				}
			}
			//END - DQ - 12/09/2009 ANG

			rowInd[nz] = planeIndexToRow[p];
			rowCoeff[nz] = 1;
			(nz) ++;

			bonus = (crewArc->cost - bonus_adj)/100;

			
			sprintf (charBuf, "Pure AC-Crew variable; cp: %d, acInd: %d", cp, p);

			#ifdef DEBUGGING
			status = CPXcheckaddcols (env, lp, oneInt, nz, &zeroDbl, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
			if (status)
			{
				logMsg(logFile, "Column error.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
			#endif	

			status = CPXaddcols (env, lp, oneInt, nz, &bonus, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
			
			//logMsg(logFile, "Appt variable; Demand index %d, col %d \n", demandList[i].demandID, CPXgetnumcols(env,lp)-1);

			if (status)
			{
				logMsg(logFile, "CPLEX failed to create column.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}

			crewAcTourList[numCrewAcTour].crewPairInd = cp;
			crewAcTourList[numCrewAcTour].acInd = p;
			crewAcTourList[numCrewAcTour].cost = crewArc->cost;
			numCrewAcTour ++ ;

		}
	}
	return 0;
}

/************************************************************************************************
*	Function	addInitialColumns							Date last modified:  8/01/06 BGC	*
*	Purpose:	Adds charter columns, exg tour columns and appointment cols.					*
************************************************************************************************/

static int
addInitialColumns (CPXENVptr env, CPXLPptr lp)
{
	int status; //for debug - 05/03/08 ANG

	addCharterCols (env, lp);
	logMsg (logFile, "** Added charter cols\n");

	//START - 05/03/08 ANG
	//status = CPXwriteprob(env, lp, "./Logfiles/firstLP.lp", NULL);
	//if(status){
	//	logMsg(logFile, "Failed to write first LP - addCharterCols file.\n");
	//}
	//END - 05/03/08 ANG

	numCharterCols = CPXgetnumcols(env,lp);
	fprintf (logFile, "Last charter col: %d\n", CPXgetnumcols(env,lp)-1);

	addExgSolnCols (env, lp);
	logMsg (logFile, "** Added exg tour cols\n");
	fprintf (logFile, "Last exg tour col: %d\n", CPXgetnumcols(env,lp)-1);

	numExgTourCols = CPXgetnumcols(env,lp) - numCharterCols;

	//START - 05/03/08 ANG
	//status = CPXwriteprob(env, lp, "./Logfiles/firstLP.lp", NULL);
	//if(status){
	//	logMsg(logFile, "Failed to write first LP - addExgSolnCols file.\n");
	//}
	//END - 05/03/08 ANG

	addApptCols (env, lp);  
	logMsg (logFile, "** Added appt cols\n");
	fprintf (logFile, "Last appt col: %d\n", CPXgetnumcols(env,lp)-1);

	addApptCols2 (env, lp);  
	logMsg (logFile, "** Added appt cols for aircraft - type 2\n");
	fprintf (logFile, "Last appt col: %d\n", CPXgetnumcols(env,lp)-1);

    numApptCols = CPXgetnumcols(env,lp) - numExgTourCols - numCharterCols;


	//RLZ: pure crew-ac columns test test
	if (PURE_CREW_AC_FLAG) addPureCrewACCols (env, lp);  

	fprintf (logFile, "Last pure crew-ac col: %d\n", CPXgetnumcols(env,lp)-1);



	numInitialCols = CPXgetnumcols(env,lp);

	//START - 05/03/08 ANG
	//status = CPXwriteprob(env, lp, "./Logfiles/firstLP.lp", NULL);
	//if(status){
	//	logMsg(logFile, "Failed to write first LP - addApptCols file.\n");
	//}
	//END - 05/03/08 ANG

	return 0;
}

/************************************************************************************************
*	Function	updateDuals									Date last modified:  8/01/06 BGC	*
*	Purpose:	Updates the values of duals	in all structs.						11/02/06 HB		*
************************************************************************************************/

static int 
updateDuals (double *duals)
{
	int i, rowCount=0, j, k, day, demInd;

	for (i=0; i<numDemandRows; i++) // For all demand rows
	{
		demandList[demandRowIndex[i]].dual = duals[rowCount+i];
	}
	rowCount += numDemandRows;

	for (i=0; i<numAfterRows; i++) // For all "after" rows
	{
		demandList[afterRowIndex[i][1]].puEDual[afterRowIndex[i][0]] = duals[rowCount+i];
	}
	rowCount += numAfterRows;

	for (i=0; i<numBeforeRows; i++) // For all "before" rows
	{
		demandList[beforeRowIndex[i][1]].puSDual[beforeRowIndex[i][0]] = duals[rowCount+i];
	}
	rowCount += numBeforeRows;

	for (i=0; i<numPlaneRows; i++) // For all plane rows
	{
		acList[planeRowIndex[i]].dual = duals[rowCount+i];
	}
	rowCount += numPlaneRows;

	for (i=0; i<numCrewPairRows; i++) // For all crew pair rows
	{
		crewPairList[crewPairRowIndex[i]].dual = duals[rowCount+i];
		crewPairList[crewPairRowIndex[i]].dual += duals[crewIndexToRow[crewPairList[i].crewListInd[0]]];
		crewPairList[crewPairRowIndex[i]].dual += duals[crewIndexToRow[crewPairList[i].crewListInd[1]]];
		//Do we need to update duals for DQ? 12/09/2009 ANG
	}
	rowCount += numCrewPairRows;

	for(day = 0; day < optParam.planningWindowDuration; day++) // Computing sumDuals for each duty.
	{
		for(j = 0; j< numAcTypes; j++)
		{
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++)
			{
				i=0;
				dutyList[j][k].sumDuals = 0;
				while ( i < maxTripsPerDuty && ((demInd = dutyList[j][k].demandInd[i]) >= 0))
				{
					dutyList[j][k].sumDuals += duals[demandIndexToRow[demInd]];
					i ++;
				}
			}
		}
	}	

	return 0;
}

/************************************************************************************************
*	Function	isSameTour						Date last modified:  8/15/06 BGC, 8/21/07 SWO	*
*	Purpose:	Returns 1 if tours corresponding to tour index t1 and t2 in the tour list are	*
*				the same; 0 otherwise.															*
************************************************************************************************/

static int 
isSameTour (int t1, int t2)
{
	int i;

	if (tourList[t1].cost != tourList[t2].cost)
		return 0;

	if (tourList[t1].crewPairInd != tourList[t2].crewPairInd)
		return 0;

	for (i=0; i<MAX_WINDOW_DURATION; i++) // If crew pair index and duty index are the same, it is the same duty.
	{
		if (tourList[t1].duties[i] != tourList[t2].duties[i])
			return 0;
	}

	if (tourList[t1].crewArcInd != tourList[t2].crewArcInd)
		return 0;

	if (tourList[t1].crewArcType != tourList[t2].crewArcType)
		return 0;

	if (tourList[t1].dropPlane != tourList[t2].dropPlane)
		return 0;

	if (tourList[t1].finalApptDemInd != tourList[t2].finalApptDemInd)
		return 0;

	return 1;
}

/************************************************************************************************
*	Function	generateNewCols								Date last modified:  8/01/06 BGC	*
*	Purpose:	Updates duals and calls for new columns from tours.c.							*
************************************************************************************************/

static int
generateNewCols (CPXENVptr env, CPXLPptr lp, double *duals)
{
	int start=0, end=0, i, nz, status;
	double cost;
	int acInd; //MAC - 08/21/08 ANG

	if (iteration > MAX_CPLEX_ITERATIONS) 
	{// In case it gets stuck in an infinite loop for whatever reason.
		return 0;
	}

	iteration ++;
	updateDuals (duals);

	oldTourCount = (*tourCount);

	getNewColumns (iteration, &start, &end); // Get new columns from CSHOpt_tours.c

	fprintf (logFile, "%d new columns found, %d columns total.\n", end-start+1, end+1+numInitialCols);

	if (end < start) // no new columns found, so optimal.
	{
		return 0;
	}

	for (i=start; i<=end; i++) //For each new column found
	{
		sprintf (charBuf, "New tour variable; Index %d", i);
		buildTourColumn (i, &nz);

		cost = tourList[i].cost;

		#ifdef DEBUGGING
		status = CPXcheckaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
		if (status)
		{
			logMsg(logFile, "Column error.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
		#endif

		//status = CPXaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, &oneDbl, &charBuf);
		status = CPXaddcols (env, lp, oneInt, nz, &cost, &zeroInt, rowInd, rowCoeff,  &zeroDbl, NULL, &charBuf);
		if (status)
		{
			logMsg(logFile, "CPLEX failed to create column.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
	}

	return 1;
}



/************************************************************************************************
*	Function	showObjCoeffs								Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showObjCoeffs (CPXENVptr env, CPXLPptr lp)
{
	int status, i;
	double *obj;

	if ((obj = (double *) calloc (CPXgetnumcols (env, lp), sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in roundObjCoeffs().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	status =  CPXgetobj (env, lp, obj, 0, CPXgetnumcols (env, lp)-1);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to get obj coefficients.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<CPXgetnumcols (env, lp); i++)
	{
		fprintf (logFile, "obj[%d]: %f\n", i, obj[i]);
	}

	free (obj);
	obj = NULL;

	return 0;

}


int
roundObjCoeffs (CPXENVptr env, CPXLPptr lp) //NOT CURRENTLY BEING CALLED
{
	int *ind, status, i;
	double *obj;

	if ((obj = (double *) calloc (numCols, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in roundObjCoeffs().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	if ((ind = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in roundObjCoeffs().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	status =  CPXgetobj (env, lp, obj, 0, numCols-1);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to get obj coefficients.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	//for (i=0; i<numCols; i++)
	//{
	//	fprintf (logFile, "obj[%d]: %f\n", i, obj[i]);
	//}
	for (i=0; i<numCols; i++)
	{
		ind[i] = i;
		obj[i] = round(obj[i]);
	}


	status = CPXchgobj (env, lp, numCols, ind, obj);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to change obj coefficients.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	free(ind);
	ind = NULL;
	free (obj);
	obj = NULL;

	return 0;

}

static int
getPriorityOrder (CPXENVptr env, CPXLPptr lp)  //NOT CURRENTLY BEING CALLED
{
	int *index, *priority, i, *direction, status;
	
	if ((index = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getPriorityOrder().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	if ((priority = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getPriorityOrder().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	if ((direction = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getPriorityOrder().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	for (i=0; i<numCharterCols; i++)
	{
		priority[i] = 1;
		index[i] = i;
		direction[i] = CPX_BRANCH_UP;
	}
	for (i=numCharterCols; i<numCharterCols+numExgTourCols; i++)
	{
		index[i] = i;
		direction[i] = CPX_BRANCH_UP;
		priority[i] = 2;
	}
	for (i=numCharterCols+numExgTourCols; i<numInitialCols; i++)
	{
		index[i] = i;
		direction[i] = CPX_BRANCH_GLOBAL;
		priority[i] = 2;
	}
	for (i=numInitialCols; i<numCols; i++)
	{
		index[i] = i;
		direction[i] = CPX_BRANCH_GLOBAL;
		priority[i] = 1;
	}

	status =  CPXcopyorder (env, lp, numCols, index, priority, direction);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to copy priority order.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	free(direction);
	free(priority);
	free(index);
	index=NULL;
	priority=NULL;
	direction=NULL;
	return 0;
}

static int
generateInitialSolution (CPXENVptr env, CPXLPptr lp) //NOT CURRENTLY BEING CALLED
{
	double *initSolution, cost=0, *obj;
	int *index, i, j, status, *initSolDem, nz=0;

	if ((index = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in generateInitialSolution().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	if ((initSolution = (double *) calloc (numCols, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in generateInitialSolution().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	if ((obj = (double *) calloc (numCols, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in generateInitialSolution().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	if ((initSolDem = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in generateInitialSolution().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	status =  CPXgetobj (env, lp, obj, 0, numCols-1);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to get obj coefficients.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numDemand; i++)
	{
		initSolDem[i] = 0;
	}

	for (i=0; i<numExgTours; i++)
	{
		j = 0;
		while (exgTourList[i].demandInd[j] >= 0)
		{
			initSolDem[exgTourList[i].demandInd[j]] = 1;
			j ++;
		}
	}

	for (i=0; i<numExgTourCols; i++)
	{
		index[i] = numCharterCols+i;
		initSolution[i] = 1.0;
		cost += obj[index[i]];
	}
	nz = numExgTourCols;

	for (i=0; i<numDemand; i++)
	{
		if ((!initSolDem[i]) && (demandIndexToRow[i] >= 0))
		{
			index[nz] = demandIndexToRow[i];
			cost += obj[index[i]];
			initSolution[nz] = 1.0;
			nz ++;
		}
	}

	status = CPXcopymipstart (env, lp, nz, index, initSolution);
	if (status)
	{
		logMsg(logFile, "CPLEX failed to generate initial solution.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}	

	fprintf (logFile, "Cost of initial solution: %f\n", cost);

	free(obj);
	free(index);
	free(initSolution);
	free(initSolDem);
	initSolution = NULL;
	index = NULL;
	initSolDem = NULL;
	obj = NULL;
	return 0;
}


/************************************************************************************************
*	Function	checkIfDualsChanged							Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
checkIfDualsChanged (double *duals)
{
	int i, rowCount=0, j, k, day, demInd;

	for (i=0; i<numDemandRows; i++) // For all demand rows
	{
		if (demandList[demandRowIndex[i]].dual != duals[rowCount+i])
		{
			fprintf (logFile, "DemandList[%d].dual has changed from %f to %f\n",
			demandRowIndex[i],
			duals[rowCount+i],
			demandList[demandRowIndex[i]].dual);
		}	
	}
	rowCount += numDemandRows;

	for (i=0; i<numAfterRows; i++) // For all "after" rows
	{
		if (demandList[afterRowIndex[i][1]].puEDual[afterRowIndex[i][0]] != duals[rowCount+i])
		{
			fprintf (logFile, "demandList[%d].puEDual[%d] has changed from %f to %f\n",
			afterRowIndex[i][1], 
			afterRowIndex[i][0], 
			duals[rowCount+i], 
			demandList[afterRowIndex[i][1]].puEDual[afterRowIndex[i][0]]);
		}
	}
	rowCount += numAfterRows;

	for (i=0; i<numBeforeRows; i++) // For all "before" rows
	{
		if(demandList[beforeRowIndex[i][1]].puSDual[beforeRowIndex[i][0]] != duals[rowCount+i])
		{
			fprintf (logFile, "demandList[%d].puSDual[%d] has changed from %f to %f\n",
				beforeRowIndex[i][1], 
				beforeRowIndex[i][0], 
				duals[rowCount+i], 
				demandList[beforeRowIndex[i][1]].puSDual[beforeRowIndex[i][0]]);
		}
	}
	rowCount += numBeforeRows;

	for (i=0; i<numPlaneRows; i++) // For all plane rows
	{
		if (acList[planeRowIndex[i]].dual != duals[rowCount+i])
		{
			fprintf (logFile, "acList[%d].dual has changed from %f to %f\n",
				planeRowIndex[i],
				duals[rowCount+i],
				acList[planeRowIndex[i]].dual);
		}
	}
	rowCount += numPlaneRows;

	for (i=0; i<numCrewPairRows; i++) // For all crew pair rows
	{
		if(crewPairList[crewPairRowIndex[i]].dual != duals[rowCount+i])
		{
			fprintf (logFile, "crewPairList[%d].dual has changed from %f to %f\n",
				crewPairRowIndex[i],
				duals[rowCount+i],
				crewPairList[crewPairRowIndex[i]].dual);
		}
	}
	rowCount += numCrewPairRows;

	for(day = 0; day < optParam.planningWindowDuration; day++) // Computing sumDuals for each duty.
	{
		for(j = 0; j< numAcTypes; j++)
		{
			for(k = dutyTally[j][day][0].startInd; k <= dutyTally[j][day][8].endInd; k++)
			{
				i=0;
				dutyList[j][k].sumDuals = 0;
				while ((demInd = dutyList[j][k].demandInd[i]) >= 0)
				{
					dutyList[j][k].sumDuals += duals[demandIndexToRow[demInd]];
					i ++;
				}
			}
		}
	}	
	return 0;
}


/************************************************************************************************
*	Function	checkIfNewColsAdded					Date last modified:  8/15/06 BGC,			*
*																		1/03/07 SWO				*
*	Purpose:	Checks if the last pricing iteration found new columns. Returns 1 if yes, 0		*
*				if no.																			*
************************************************************************************************/

static int
checkIfNewColsAdded ()
{
	int i, j;

	if(oldTourCount == 0)
		return 1;
	for (i=oldTourCount; i< (*tourCount); i++)
	{

		for (j=0; j<oldTourCount; j++)
		{
			if (!isSameTour (i, j))
			{
				return 1;
			}
		}
	}

	return 0;
}


/************************************************************************************************
*	Function	sameExgAndNewTour							Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
sameExgAndNewTour (int exgTourInd, int tourInd)
{
	int lastDemandInd, i, j, k, m, n, exists, numExgDemands=0, numNewDemands=0;

	// Check if repoConnxn
	if (exgTourList[exgTourInd].dropoffType == 4)
		return 0;

	// Check if same crew pair
	if (exgTourList[exgTourInd].crewPairInd != tourList[tourInd].crewPairInd)
		return 0;


	for (n=0; n<MAX_LEGS; n++)
	{
		if (exgTourList[exgTourInd].demandInd[n] < 0)
			break;
		numExgDemands ++;
	}

	// Check if same demands
	for (i=0; i<MAX_WINDOW_DURATION; i++)
	{
		if (tourList[tourInd].duties[i] < 0)
			continue;

		for (j=0; j<maxTripsPerDuty; j++)
		{
			if ((k = dutyList[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][tourList[tourInd].duties[i]].demandInd[j]) < 0)
				break;
			lastDemandInd = k;
			numNewDemands ++;
			exists = 0;
			for (n=0; n<MAX_LEGS; n++)
			{
				if (exgTourList[exgTourInd].demandInd[n] < 0)
					break;

				if (lastDemandInd == exgTourList[exgTourInd].demandInd[n])
				{
					exists = 1;
					break;
				}
			}

			if (!exists) // Tour in new tour list not found in exg tour list so different.
				return 0;

		}
		if ((m = dutyList[crewPairList[exgTourList[exgTourInd].crewPairInd].acTypeIndex][tourList[tourInd].duties[i]].repoDemandInd) >= 0)
		{
			lastDemandInd = -1*(m+1);
		}
	}

	if (numNewDemands != numExgDemands)
		return 0;

	// Check if same pickup type
	if (exgTourList[exgTourInd].pickupType != tourList[tourInd].crewArcType)
		return 0;

	if ((exgTourList[exgTourInd].pickupType == 1) && (tourList[tourInd].crewArcType == 1))
	{// If pickup plane when next available, check if same aircraft
		if (exgTourList[exgTourInd].acInd !=
			crewPairList[tourList[tourInd].crewPairInd].crewPlaneList[tourList[tourInd].crewArcInd].acInd)
			return 0;
	}

	if (!tourList[tourInd].dropPlane)
	{// Plane not dropped off
		if (exgTourList[exgTourInd].dropoffType > 1)
			return 0;
	}
	else
	{ 
		if (exgTourList[exgTourInd].dropoffType == 1)
			return 0;

		else if (exgTourList[exgTourInd].dropoffType == 3)
		{// Dropoff at demand end
			if (lastDemandInd < 0) // Exg tour drops off at demand end, new tour drops off at beginning.
				return 0;
		}
		else if (exgTourList[exgTourInd].dropoffType == 2)
		{
			if (lastDemandInd >= 0)
				return 0; // Exg tour drops off before, but new tour drops off after.

			if (lastDemandInd != -1*(exgTourList[exgTourInd].dropoffInd+1))
				return -1;
				// Being dropped off at start of different demand.
		}
	}

	return 1;
}


static int
changeVariableType (CPXENVptr env, CPXLPptr lp)
{
	char *ctype, *lu;
	double *bd;
	int *indices, i, status;

	numCols = CPXgetnumcols (env, lp);

	if ((ctype = (char *) calloc (numCols, sizeof (char))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in changeVariableType().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		

	if ((indices = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in changeVariableType().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		

	if ((lu = (char *) calloc (numCols, sizeof (char))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in changeVariableType().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		

	if ((bd = (double *) calloc (numCols, sizeof (double))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in changeVariableType().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}			

	for (i=0; i<numCols; i++)
	{// Define all variables to be binary
		ctype[i] = 'B';
		indices[i] = i;
		lu[i] = 'U';
		bd[i] = 1.0;
	}

	status = CPXcopyctype (env, lp, ctype);
	if ( status ) {
		fprintf (stderr, "Failed to copy ctype\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	status = CPXchgbds (env, lp, numCols, indices, lu, bd);
	if ( status ) {
		fprintf (stderr, "Failed to change variable bounds\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	free (ctype);
	free(indices);
	free(lu);
	free(bd);
	ctype = NULL;
	lu = NULL;
	bd = NULL;
	indices = NULL;

	return 0;
}

/************************************************************************************************
*	Function	compareTourLists							Date last modified:  8/01/06 BGC	*
*	Purpose:	Compares tours in the exgTourList to those in the tourList. Outputs the exg		*
*				that are not found in the tourList, and also outputs the tours if a new			*
*				tour is the same as an exg tour (to compare costs).								*
*				For debugging.  Not currently called.											*
************************************************************************************************/

static int
compareTourLists (void)
{

	int i, j, check;

	for (i=0; i<numExgTours; i++)
	{
		check = 0;
		for (j=0; j<(*tourCount); j++)
		{
			if (sameExgAndNewTour (i, j))
			{
				check = 1;
				fprintf (logFile, "\n_____________\n");
				fprintf (logFile, "SAME TOURS:\n");
				showExgTour (i);
				showTour (j);
				fprintf (logFile, "_____________\n");
			}
		}
		if (!check)
		{
			fprintf (logFile, "\n_____________\n");
			fprintf (logFile, "EQUIVALENT NEW TOUR NOT FOUND FOR EXG TOUR:\n");
			showExgTour (i);
			fprintf (logFile, "_____________\n");
		}
	}
	return 0;
}

/************************************************************************************************
*	Function	optimizeIt									Date last modified:  8/01/06 BGC,  	*
*	Purpose:	Creates and optimizes the problem.		added charter log output 04/03/07 SWO	*							*
************************************************************************************************/

static int
optimizeIt ()
{
	CPXENVptr env = NULL;
	CPXLPptr lp = NULL;
//	CPXCHANNELptr logChannel;
	int status, i, numStalled=0, perturbed=0;
	double *rhs, *duals, *solution, curObjValue, newObjValue;
	char *sense, sens;
	char writetodbstring1[200];
	CrewEndTourRecord *xPtr; // 11/14/07 ANG
	int ty; // 11/14/07 ANG

	//For writing summaryFile - 12/23/08 ANG
	extern time_t run_time_t;
	extern FILE *summaryFile;
	extern char logFileName[512];
	extern char *username;
	int errNbr1, errNbr2, errNbr3;
	char tbuf1[32], tbuf2[32], tbuf3[32];

	//For getting column name - 11/13/08 ANG
	int           cur_numrows, cur_numcols;
	char          **cur_colname = NULL;
	char          *cur_colnamestore = NULL;
	int           cur_colnamespace;
	int           surplus;

	CPXFILEptr cplexLogFile;

	xPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord));// 11/14/07 ANG
	if(! xPtr) {// 11/14/07 ANG
		logMsg(logFile,"%s Line %d, Out of Memory while creating crewEndTourRecord pointer in optimizeIt().\n", __FILE__,__LINE__);
		exit(1);
	}

	srand((int)time(0)); // Initializes random number generator for degeneracy perturbation.

	if ((charBuf = (char *) calloc (1024, sizeof (char))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	env = CPXopenCPLEX (&status); // Creates cplex instance
	if ( env == NULL ) {
		logMsg(logFile, "Could not open CPLEX environment.\n");
		writeWarningData(myconn); exit(1);
   }

	cplexLogFile = CPXfopen ("./Logfiles/ScheduleCPLEXLog.txt", "w");
	status = CPXsetlogfile (env, cplexLogFile); // Create CPLEX log file
	if ( status ) {
		logMsg (stderr, "Failure to set log file, error %d.\n", status);
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	lp = CPXcreateprob (env, &status, "Scheduler"); // Create empty instance of problem.
	if ( lp == NULL ) {
		logMsg(logFile, "Failed to create LP.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	//status = CPXsetintparam (env, CPX_PARAM_SCRIND, 1); // Output to screen turned on
	//status = CPXsetintparam (env, CPX_PARAM_PRICELIM, 9999999); 
	////status = CPXsetintparam (env, CPX_PARAM_REDUCE, 0); // Only dual preprocessor reductions
	//if (numDemand > MIN_DEMANDS_SWITCH_LPSOLVER && optParam.planningWindowDuration == MAX_WINDOW_DURATION) 
	//Use Barrier LP sovler completely
	    status = CPXsetintparam (env, CPX_PARAM_LPMETHOD, CPX_ALG_BARRIER); // Barrier
	//status = CPXsetintparam (env, CPX_PARAM_LPMETHOD, CPX_ALG_SIFTING); // 
	//status = CPXsetintparam (env, CPX_PARAM_PREIND, 0); // Primal simplex
	//status = CPXsetintparam (env, CPX_PARAM_BARCROSSALG, -1); // Primal simplex
	//status = CPXsetintparam (env, CPX_PARAM_EPOPT, 1e-09); // Primal simplex
	//status = CPXsetintparam (env, CPX_PARAM_SCAIND, 1); // Primal simplex
	//status = CPXsetintparam (env, CPX_PARAM_PPRIIND, 4); // Primal simplex
	//status = CPXsetintparam (env, CPX_PARAM_EPPER, 1e-08); // Primal simplex

	getRowData ();

	//numRows = (numDemandRows + numAfterRows + numBeforeRows + numPlaneRows + numCrewPairRows);
	// Note that the order of rows is the same as that of the formulation in the optimizer design document.

	if ((sense = (char *) calloc (numRows, sizeof (char))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	
	if ((rhs = (double *) calloc (numRows, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((constraintName = (char **) calloc (numRows, sizeof (char *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<numRows; i++)
	{
		if ((constraintName[i] = (char *) calloc (1024, sizeof (char))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
	}
	setRHSSenseAndNames (rhs, sense, constraintName);

	status = CPXnewrows (env, lp, numRows, rhs, sense, NULL, constraintName); // Created rows.
	if (status)
	{
		logMsg(logFile, "CPLEX failed to create rows.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numRepoConnxnRows; i++) // Creating repo connxn rows
	{
		sprintf (charBuf, "Repo connection constraint; Row number %d Leg index %d.", 
					numRows+i, repoConnxnList[i]);
		sens = 'L';

		status = CPXnewrows (env, lp, oneInt, &zeroDbl, &sens, NULL, &charBuf);
		if (status)
		{
			logMsg(logFile, "CPLEX failed to create rows.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
	}

	if ((rowInd = (int *) calloc (numRows+numRepoConnxnRows, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	if ((rowCoeff = (double *) calloc (numRows+numRepoConnxnRows, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	addInitialColumns (env, lp);

	logMsg (logFile, "** Added initial columns\n");

	CPXchgobjsen (env, lp, CPX_MIN); // This is the default (minimization), but adding this just to make sure.

	//START - 05/06/08 ANG
	//status = CPXwriteprob(env, lp, "./Logfiles/firstLP.lp", NULL);
	//if(status){
	//	logMsg(logFile, "Failed to write first LP file.\n");
	//}
	//END - 05/06/08 ANG

	status = CPXlpopt (env, lp); // Solve first LP
	if (status) {
		logMsg(logFile, "Failed to optimize initial LP.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	logMsg (logFile, "** Solved first LP\n");

	if ((duals = (double *) calloc (numRows, sizeof (double))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	status = CPXgetpi (env, lp, duals, 0, numRows-1);  // Get duals
	if (status) 
	{
		logMsg(logFile, "Failed to get dual prices.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	status = CPXgetobjval (env, lp, &curObjValue); // Get objective function value
	if (status) 
	{
		logMsg(logFile, "Failed to get objective function value.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}
//TEMP FOR TESTING OF UI / OPT:  CREATE ONLY EXISTING TOURS, LOCKING TOURS AS REQ'D TO REPLICATE MASTER
//debug only
//	while (status)
//END TEMP (UNCOMMENT NEXT LINE BACK IN)
	while (generateNewCols (env, lp, duals)) 
	{// While negative reduced columns exist or CPLEX iteration limit hasn't been reached

		fprintf (logFile, "\nIteration %d...\n", iteration);

		//START - 04/30/08 ANG
		//status = CPXwriteprob(env, lp, "./Logfiles/latestIterationLP.lp", NULL);
		//if(status){
		//	logMsg(logFile, "Failed to write latestIterationLP LP file.\n");
		//}
		//END - 04/30/08 ANG

		status = CPXlpopt (env, lp); // Solve LP with new columns
		if (status) 
		{
			logMsg(logFile, "Failed to optimize LP.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}

		status = CPXgetpi (env, lp, duals, 0, numRows-1); // Get duals
		if (status) {
			logMsg(logFile, "Failed to get dual prices.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}

		#ifdef DEBUGGING
		fprintf (logFile, "\n-------------------------------------------------------------\n");
		for (j=0; j<numRows; j++)
		{
			if (duals[j])
			{
				fprintf (logFile, "%75s: %8.5f\n", constraintName[j], duals[j]);
			}
		}
		fprintf (logFile, "-------------------------------------------------------------\n\n");	
		#endif

		status = CPXgetobjval (env, lp, &newObjValue); // Get objective function value
		if (status) {
			logMsg(logFile, "Failed to get objective function value.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}

		if (verbose)
		{
			fprintf (logFile, "Objective = %f.\n", newObjValue);
		}

		if (perturbed) // If problem was perturbed in last iteration, reset RHS to original value
		{
			fprintf(logFile, "Removing perturbation...\n");
			for (i=0; i<numDemandRows; i++)
			{
				rowInd[i] = i;
				rowCoeff[i] = 1;
			}
			status = CPXchgrhs(env, lp, numDemandRows, rowInd, rowCoeff);
			if (status) {
				logMsg(logFile, "Failed to change RHS.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(logFile,"%s", charBuf);
				writeWarningData(myconn); exit(1);
			}
			perturbed = 0;
		}
		//else if (fabs(newObjValue-curObjValue) < 100)
		else if (fabs((newObjValue-curObjValue)/curObjValue) <  EPSILON)
		{ // Else if improvement in objective function value is less than EPSILON (defined at top)
			if (!checkIfNewColsAdded ())
			{
				break;
			}
			numStalled ++; // Number of consecutive iterations where there has been little improvement in objective
			if (numStalled > MAX_STALLS)
			{
				fprintf(logFile, "Perturbing problem...\n");
				for (i=0; i<numDemandRows; i++)
				{// For each demand row
					rowInd[i] = i;
					rowCoeff[i] = MIN_RHS + ((double) rand()/ (double) RAND_MAX) * (MAX_RHS - MIN_RHS);
					// RHS of 1 is changed to a random number in [MIN_RHS, MAX_RHS]
				}
				status = CPXchgrhs(env, lp, numDemandRows, rowInd, rowCoeff);
				if (status) {
					logMsg(logFile, "Failed to change RHS.\n");
					CPXgeterrorstring (env, status, charBuf);
					logMsg(logFile,"%s", charBuf);
					writeWarningData(myconn); exit(1);
				}
				numStalled = 0;
				perturbed = 1;
			}
		}
		
		
		//Stop LP iteration if the problem has already taken a while and no major objective improvement
		//Only applied with MAX planning windows and high demands.
		if ( iteration > MAX_STALLS_EXIT && numDemand > MIN_DEMANDS_SWITCH_LPSOLVER && optParam.planningWindowDuration == MAX_WINDOW_DURATION
			 &&  fabs(newObjValue-curObjValue) < LP_OBJ_DECREMENT)
			 break;
		
		curObjValue = newObjValue;
	}

	//showDuals (env, lp);

	// Finished solving sequence of LPs
	if (perturbed) // If the last LP was perturbed, remove perturbation
	{
		fprintf(logFile, "Removing perturbation...\n");
		for (i=0; i<numDemandRows; i++)
		{
			rowInd[i] = i;
			rowCoeff[i] = 1;
		}
		status = CPXchgrhs(env, lp, numDemandRows, rowInd, rowCoeff);
		if (status) {
			logMsg(logFile, "Failed to change RHS.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(logFile,"%s", charBuf);
			writeWarningData(myconn); exit(1);
		}
		perturbed = 0;
	}

	changeVariableType (env, lp);

	//roundObjCoeffs  (env, lp);
	//generateInitialSolution (env, lp);

	//getPriorityOrder (env, lp);
	//status = CPXsetintparam (env, CPX_PARAM_MIPORDIND, 1);
	//status = CPXsetintparam (env, CPX_PARAM_BRDIR, 1);
	//status = CPXsetintparam (env, CPX_PARAM_FRACCUTS, 2);
	//status = CPXsetintparam (env, CPX_PARAM_CLIQUES, 2);

	//status = CPXsetintparam (env, CPX_PARAM_STARTALG, CPX_ALG_BARRIER);
	//status = CPXsetintparam (env, CPX_PARAM_MIPEMPHASIS, CPX_MIPEMPHASIS_FEASIBILITY);
	//status = CPXsetintparam (env, CPX_PARAM_PROBE, 2);
	//status = CPXsetintparam (env, CPX_PARAM_HEURFREQ, 10);
	//status = CPXsetintparam (env, CPX_PARAM_BBINTERVAL, 1);
	//status = CPXsetintparam (env, CPX_PARAM_ADVIND, 1);
	//status = CPXsetintparam (env, CPX_PARAM_REPAIRTRIES, 10);
	//status = CPXsetintparam (env, CPX_PARAM_LBHEUR, 1);
	//status = CPXsetintparam (env, CPX_PARAM_DIVETYPE, 2);
	//status = CPXsetintparam (env, CPX_PARAM_COEREDIND, 0);

	status = CPXsetdblparam (env, CPX_PARAM_TILIM, CPLEX_TIME_LIMIT);	// Time limit defined in define.h
	status = CPXsetdblparam (env, CPX_PARAM_EPGAP, 0.001); // Stops when 0.1% within optimal.
	status = CPXsetintparam (env, CPX_PARAM_MIPINTERVAL, 10); // Display of MIP progress	
	//status = CPXsetintparam (env, CPX_PARAM_RELOBJDIF, 0.001);
	logMsg (logFile, "** Starting MIP\n");

	//START - 03/18/08 ANG
	//debug only
	//status = CPXwriteprob(env, lp, "./Logfiles/scheduleModel.lp", NULL);
	//if(status){
	//	logMsg(logFile, "Failed to write Schedule LP file.\n");
	//}
	//END - 03/18/08 ANG

	status = CPXmipopt (env, lp); // Optimize MIP
	if (status) {
		logMsg(logFile, "Failed to optimize MIP.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}	

	status = CPXgetmipobjval (env, lp, &curObjValue); // Get objective function value (only for display)
	
	if (status) {
		logMsg(logFile, "Failed to get objective function value.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}
	fprintf (logFile, "MIP objective function: %f\n", curObjValue);
	logMsg (logFile, "** Solved MIP.\n");

	//Write run summary - 12/23/08 ANG
	fprintf(summaryFile, "| %16s | %5d | %s | %s | %s | %10.2f | %s |\n",
				username,
				local_scenarioid,
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(run_time_t))), NULL, &errNbr1),tbuf1,"%Y/%m/%d %H:%M"),
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(optParam.windowStart))), NULL, &errNbr2),tbuf2,"%Y/%m/%d %H:%M"),
		    		dt_DateTimeToDateTimeString(dt_StringToDateTime("sDHmY", "%*s %s %d %d:%d:%*d %d\n",
					asctime(gmtime(&(optParam.windowEnd))), NULL, &errNbr2),tbuf3,"%Y/%m/%d %H:%M"),
				curObjValue,
				logFileName);
	fclose(summaryFile);

	if ((solution = (double *) calloc (numCols, sizeof (double))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}	

	status = CPXgetmipx (env, lp, solution, 0, numCols-1); // get optimal solution
	if (status) {
		logMsg(logFile, "Failed to recover optimal solution.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(logFile,"%s", charBuf);
		writeWarningData(myconn); exit(1);
	}

	if ((optSolution = (int *) calloc (numCols, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}	

	//START - Get column names - 11/13/08 ANG
	cur_numcols = CPXgetnumcols (env, lp);
	cur_numrows = CPXgetnumrows (env, lp);
	status = CPXgetcolname (env, lp, NULL, NULL, 0, &surplus, 0, cur_numcols-1);

	cur_colnamespace = - surplus;
	if ( cur_colnamespace > 0 ) {
		cur_colname      = (char **) malloc (sizeof(char *)*cur_numcols);
		cur_colnamestore = (char *)  malloc (cur_colnamespace);
		if ( cur_colname == NULL || cur_colnamestore == NULL){
			logMsg(logFile, "Failed to get memory for column names.\n"); exit(1);
		}
		status = CPXgetcolname (env, lp, cur_colname, cur_colnamestore, cur_colnamespace, &surplus, 0, cur_numcols-1);
		if (status){
			logMsg(logFile, "Failed to get column names.\n");
		}
	}
	else {
		logMsg(logFile, "No names associated with columns.\n");
	}
   //END - 11/13/08 ANG

	printMaintList();//for debug - 04/22/08 ANG

	fprintf(logFile,"\n\n");
	for (i=0; i<numCols; i++)
	{
		if (solution[i] > 0.5)
		{
			optSolution[i] = 1;

			//Write solution - 11/13/08 ANG
			if ( cur_colnamespace > 0 ) {
				fprintf (logFile, "%s = 1 \n", cur_colname[i]);
			}

			//fprintf(logFile, "i: %d \n", i - numInitialCols );
			if(i<numDemandRows){
				if(demandList[i].isAppoint){ 
					//START - 11/14/07 ANG 
					if(optParam.autoFlyHome == 1){
						if(demandList[i].isAppoint == 4){ 
							for(ty = 0, xPtr = crewEndTourList;  ty < crewEndTourCount; ++ty, ++xPtr){
								if(	demandList[i].demandID == xPtr->assignedDemandID && xPtr->recordType > 0){
									xPtr->covered = 0;
									xPtr->wrongCrew = 0;
									//fprintf(logFile, "1 - demandID = %d \n", demandList[i].demandID);
									break;
								}//end if
							}//end for
							fprintf(logFile, "Crew is not flown home by aircraftID	%d.\n", acList[demandList[i].acInd].aircraftID);
							sprintf(writetodbstring1, "Crew	is not flown home by aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
						}//end if
						//sprintf(writetodbstring1, "Crew	is not flown home by aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
						else{ //04/17/08 ANG
							fprintf(logFile, "Uncov	Appoint	for	aircraftID %d.\n", acList[demandList[i].acInd].aircraftID);
							sprintf(writetodbstring1, "Uncov Appoint for aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
						}
					}//end if(optParam.autoFlyHome == 1)
					else {
						if(optParam.autoFlyHome == 1){
							for(ty = 0, xPtr = crewEndTourList;  ty < crewEndTourCount; ++ty, ++xPtr){
								if(	demandList[i].demandID == xPtr->recordID){
									xPtr->covered = 0;
									xPtr->wrongCrew = 0;
									//fprintf(logFile, "2 - demandID = %d \n", demandList[i].demandID);
									break;
								}//end if
							}//end for
						}//end if(optParam.autoFlyHome == 1)
						fprintf(logFile, "Uncov	Appoint	for	aircraftID %d.\n", acList[demandList[i].acInd].aircraftID);
						sprintf(writetodbstring1, "Uncov Appoint for aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
					}
					//END - 11/23/07 ANG

					//These codes are replaced
					/*if(demandList[i].isAppoint == 4	&& demandList[i].demandID){// 11/06/07 ANG
						fprintf(logFile, "Crew is not flown	home by	aircraftID %d.\n", acList[demandList[i].acInd].aircraftID);	// 11/06/07	ANG
						sprintf(writetodbstring1, "Crew	is not flown home by aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
					} else {
						fprintf(logFile, "Uncov	Appoint	for	aircraftID %d.\n", acList[demandList[i].acInd].aircraftID);
						sprintf(writetodbstring1, "Uncov Appoint for aircraftID	%d.", acList[demandList[i].acInd].aircraftID);
					}
					//end of replace*/

					if(errorNumber==0){
						if(!( errorinfoList= (Warning_error_Entry *) calloc((size_t) 1,	sizeof(Warning_error_Entry)))) {
							logMsg(logFile,"%s	Line %d, Out of	Memory in optimizeIt().\n",	__FILE__,__LINE__);
							writeWarningData(myconn); exit(1);
						}
					} 
					else {
						if((errorinfoList	= (Warning_error_Entry *)realloc((errorinfoList),(errorNumber+1)*sizeof(Warning_error_Entry))) == NULL)	{
							logMsg(logFile,"%s	Line %d, Out of	Memory in optimizeIt().\n",	__FILE__,__LINE__);
							writeWarningData(myconn); exit(1);
						}
					}
					initializeWarningInfo(&errorinfoList[errorNumber]);
					errorinfoList[errorNumber].local_scenarioid=local_scenarioid;
					strcpy(errorinfoList[errorNumber].group_name,"group_aircraft");
					errorinfoList[errorNumber].aircraftid=acList[demandList[i].acInd].aircraftID;
					errorinfoList[errorNumber].format_number=11;
					strcpy(errorinfoList[errorNumber].notes,writetodbstring1);
					errorNumber++;
				}	
				else
					fprintf(logFile, "Charter demandID %d.\n", demandList[i].demandID);
			}
		}
		else {
			optSolution[i] = 0;
			//START - 11/23/07 ANG 
			if(i<numDemandRows){
				if(optParam.autoFlyHome == 1){
					if(demandList[i].isAppoint >= 2){ 
						for(ty = 0, xPtr = crewEndTourList;  ty < crewEndTourCount; ++ty, ++xPtr){
							if(	demandList[i].demandID == xPtr->assignedDemandID){
								xPtr->covered = 1;
								xPtr->wrongCrew = 0;
								//fprintf(logFile, "3 - demandID = %d \n", demandList[i].demandID);
								break;
							}//end if
						}//end for
					}//end if
				}//end if(optParam.autoFlyHome == 1)
			}
			//END - 11/23/07 ANG
		}
	}
	fprintf(logFile,"\n\n");

/*
	CPXgetchannels(env, NULL, NULL, NULL, &logChannel);
	CPXmsg(logChannel, "\n\n");
	CPXmsg(logChannel, "+---------------------------------------+\n");
	CPXmsg(logChannel, "|  Number of constraints       : %6d |\n", CPXgetnumrows(env,lp));
	CPXmsg(logChannel, "|  Number of variables         : %6d |\n", CPXgetnumcols(env,lp));
	CPXmsg(logChannel, "|  Number of charter variables : %6d |\n", numCharterCols);
	CPXmsg(logChannel, "|  Number of exg variables     : %6d |\n", numInitialCols-numCharterCols);
	CPXmsg(logChannel, "+---------------------------------------+\n");
	CPXmsg(logChannel, "+--------------------------+\n");
	CPXmsg(logChannel, "| CPLEX parameters:        |\n");		
	CPXmsg(logChannel, "+--------------------------+\n");
	CPXmsg(logChannel, "| Time limit = 4d% sec.   |\n", CPLEX_TIME_LIMIT);
	CPXmsg(logChannel, "| CPX_PARAM_REDUCE = 2      |\n");
	CPXmsg(logChannel, "| MIP gap    = 0.1%%        |\n");
	CPXmsg(logChannel, "+--------------------------+\n");
*/


	// Free CPLEX lp and environment.
	if ( lp != NULL ) {
		status = CPXfreeprob (env, &lp);
		if ( status ) {
			logMsg (logFile, "CPXfreeprob failed, error code %d.\n", status);
		}
	}
	
	if ( env != NULL ) {
		status = CPXcloseCPLEX (&env);
		
		if ( status ) {
			logMsg (logFile, "Could not close CPLEX environment.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg (logFile, "%s", charBuf);
		}
	}

	CPXfclose (cplexLogFile);

	//compareTourLists ();


	free (sense);
	sense = NULL;
	free (rhs);
	rhs = NULL;
	for (i=0; i<numRows; i++)
	{
		free (constraintName[i]);
		constraintName[i] = NULL;
	}
	free (constraintName);
	constraintName = NULL;
	free (solution);
	solution = NULL;
	free(tourCount);
	tourCount = NULL;
	return 0;
}

/************************************************************************************************
*	Function	getAircraftID								Date last modified:  8/01/06 BGC	*
*	Purpose:	Returns the aircraft ID (could be 0) corresponding to a tour					*
************************************************************************************************/

static int 
getAircraftID (int i)
{
	int j, cpInd = optTours[i].crewPairInd;
	if (optTours[i].crewArcType == 1)
	{
		if ((j = crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].acInd) >= 0)
			return acList[j].aircraftID;
	}
	else if (optTours[i].crewArcType == 2)
	{
		if ((j = crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->acInd) >= 0)
			return acList[j].aircraftID;		
	}
	else if (optTours[i].crewArcType == 3)
	{
		if ((j = crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->acInd) >= 0)
			return acList[j].aircraftID;		
	}
	return 0;
}

/************************************************************************************************
*	Function	updatePlaneAvailability						Date last modified:  8/01/06 BGC	*
*	Purpose:	While constructing optimal solution, moves the next available location for		*
*				a plane to the last demand index/ repo location of the tour.					*
************************************************************************************************/

static int
updatePlaneAvailability (int i)
{
	int j, n, acTypeInd;
	
	acTypeInd = crewPairList[optTours[i].crewPairInd].acTypeIndex;
	
	j = optParam.planningWindowDuration - 1;

	while(optTours[i].duties[j]== -1)
		j--;

	if((n = dutyList[acTypeInd][optTours[i].duties[j]].repoDemandInd) >= 0)
			availStart[n] = optAircraftID[i];
	else
			availEnd[dutyList[acTypeInd][optTours[i].duties[j]].lastDemInd] = optAircraftID[i];

	return 0;


	//for (j=0; j<MAX_WINDOW_DURATION; j++)
	//{
	//	if (optTours[i].duties[j] < 0)
	//		continue;
	//	for (k=0; k<maxTripsPerDuty; k++)
	//	{
	//		if ((m = dutyList[acTypeInd][optTours[i].duties[j]].demandInd[k]) < 0)
	//			break;
	//		lastDemandInd = m;
	//	}
	//	if ((n = dutyList[acTypeInd][optTours[i].duties[j]].repoDemandInd) >= 0)
	//	{
	//		lastDemandInd = -1*(n+1);
	//	}
	//}
	//if (lastDemandInd >= 0)  // drop off after
	//{
	//	availEnd[lastDemandInd] = optAircraftID[i];
	//}
	//else // drop off before
	//{
	//	lastDemandInd = lastDemandInd*(-1) - 1;
	//	availStart[lastDemandInd] = optAircraftID[i];
	//}
	//return 0;
}

/************************************************************************************************
*	Function	getOptTourPlanes				Date last modified:  8/01/06 BGC, 2/2/07 SWO	*
*	Purpose:	Assigns planes to tours in the optimal solution.								*
************************************************************************************************/

static int
getOptTourPlanes (void)
{
	int i, j, pickupInd, iteration, flag, acTypeIndex, k, d, demInd;
	int counter; // 03/27/08 ANG

	numOptExgTours=0;

	for (i=0; i<numExgTourCols; i++)
	{
		if (optSolution[numCharterCols+i])
		{
			optExgTours[numOptExgTours] = exgTourList[i];
			numOptExgTours ++;

			//START - Split one column into 2 tours - used when dealing with existing tour involving 1 crewpair and 2 aircraft - 03/27/08 ANG
			if (exgTourList[i].acInd2 > -1){
				optExgTours[numOptExgTours].acInd = exgTourList[i].acInd2;
				optExgTours[numOptExgTours].cost = exgTourList[i].cost2;
				optExgTours[numOptExgTours].crewPairInd = exgTourList[i].crewPairInd;
				counter = 0;
				for (j=0; j<MAX_LEGS; j++)
				{
					if (exgTourList[i].demandInd2[j] > -1){
						optExgTours[numOptExgTours].demandInd[counter] = exgTourList[i].demandInd2[j];
						counter++;
					}
				}
				optExgTours[numOptExgTours].dropoffInd = exgTourList[i].dropoffInd2;
				optExgTours[numOptExgTours].dropoffType = exgTourList[i].dropoffType2;
				optExgTours[numOptExgTours].pickupInd = exgTourList[i].pickupInd2;
				optExgTours[numOptExgTours].pickupType = exgTourList[i].pickupType2;
				numOptExgTours ++;
			}
			//END - Split one column into 2 tours - used when dealing with existing tour involving 1 crewpair and 2 aircraft - 03/27/08 ANG
		}
	}

	numOptTours=0;

	for (i=0; i<numCols-numInitialCols; i++) // new tours are contained in numInitialCols .. numCols-1
	{
		if (optSolution[numInitialCols+i])
		{
			optTours[numOptTours] = tourList[i];
			numOptTours ++;
		}
	}

	if ((optAircraftID = (int *) calloc (numOptTours+numOptExgTours, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((availStart = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	//Move to addApptCols RLZ 04/25/08
	//if ((availEnd = (int *) calloc (numDemand, sizeof (int))) == NULL)
	//{
	//	logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
	//	writeWarningData(myconn); exit(1);
	//}

	if ((isManaged = (int *) calloc (numDemand, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((availRepoConnxn = (int *) calloc (numLegs, sizeof (int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}


	if ((outTimes = (time_t *) calloc (numDemand, sizeof (time_t))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((inTimes = (time_t *) calloc (numDemand, sizeof (time_t))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in getOptTourPlanes().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	for (i=0; i<numDemand; i++)
	{
		availStart[i] = 0; // ID of plane that is available at at the start of the demand. 
		//availEnd[i] = 0; // ID of plane that is available at the end.
		isManaged[i] = 0; // Indicator whether managed.
		outTimes[i] = 0; // Actual out (may be different from reqOut). 
		inTimes[i] = 0; // Actual in (may be different from reqOut). 
	}

	// -- Get the adjusted out times -- //
	for (i=0; i<numOptTours; i++)
	{
		acTypeIndex = crewPairList[optTours[i].crewPairInd].acTypeIndex;
		for (j=0; j<MAX_WINDOW_DURATION; j++)
		{
			if ((k = optTours[i].duties[j]) < 0)
				continue;

			d = 0;
			while ((demInd = dutyList[acTypeIndex][k].demandInd[d]) >= 0 && (d < maxTripsPerDuty)) //Fei's correction
			{
				outTimes[demInd] = (time_t) 60 * dutyList[acTypeIndex][k].startTm[d];
				inTimes[demInd] = (time_t) outTimes[demInd] + 60 * demandList[demInd].elapsedTm[acTypeIndex];
				d ++;
			}
		}
	}

	for (i=0; i<numOptExgTours; i++)
	{
		d=0;
		//while ((demInd = optExgTours[i].demandInd[d]) >= 0)
		while ((demInd = optExgTours[i].demandInd[d]) >= 0 && d < MAX_LEGS) // 04/23/08 ANG
		{
			for (j=0; j<numLegs; j++)
			{
				if (legList[j].demandID == demandList[demInd].demandID)
				{
					outTimes[demInd] = legList[j].schedOut;
					inTimes[demInd] = legList[j].schedIn; 
					// Don't want to use adjSchedIn because we are "retaining" the current schedule.
					break;
				}
			}
			d++;
		}
	}


	for (i=0; i<numDemand; i++)
	{
		if (outTimes[i] == 0)
		{
			outTimes[i] = demandList[i].reqOut;
			inTimes[i] = demandList[i].reqIn;
		}
	}

	for (i=0; i<numLegs; i++)
	{
		availRepoConnxn[i] = 0; // ID of plane that is available
	}
	
	for (i=0; i<numOptExgTours; i++) // Updates where planes are available based on existing solution
	{
		if (optExgTours[i].acInd > -1)
	//	if (((acList[optExgTours[i].acInd].specConnxnConstr[MAX_WINDOW_DURATION]) ||
	//		optExgTours[i].pickupType == 1))
		{
			optAircraftID[numOptTours+i] = acList[optExgTours[i].acInd].aircraftID;
			if (optExgTours[i].dropoffType == 2)
			{
				availStart[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
			}
			else if (optExgTours[i].dropoffType == 3)
			{
				availEnd[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
			}
			else if (optExgTours[i].dropoffType == 4)
			{
				availRepoConnxn[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
			}
		}
		else
		{
			optAircraftID[numOptTours+i] = 0;
		}
	}

	for (i=0; i<numOptTours; i++)
	{
		if ((optAircraftID[i] = getAircraftID (i)) > 0)
		{	
			if (optTours[i].dropPlane)
			{
				updatePlaneAvailability (i);
			}
		}
		else
		{
			optAircraftID[i] = 0;
		}
	}

//For susan's way to deal with the appt at the beginning of the pws. It can be removed completely together with function addApptCols()
 //comment this in, if addApptCols is comment in.
	//for (i=0; i<numDemand; i++)	{
	//	if (demandList[i].isAppoint && availEnd[i] == 0 && !optSolution[i]) 
	//	{
	//		availEnd[i] = demandList[i].aircraftID;
	//	}
	//}

	//for (i = numExgTourCols + numCharterCols - 1; i< numInitialCols ; i++){
	for (i = numExgTourCols + numCharterCols - 1; i < numExgTourCols + numCharterCols  + numApptCols ; i++){
		if (optSolution[i]){
			logMsg(logFile, "Pure Appt Col: %d \n", i);
		}
	}

	for (i = numExgTourCols + numCharterCols  + numApptCols ; i < numInitialCols ; i++){
		if (optSolution[i]){
			logMsg(logFile, "Pure Crew AC Col: %d \n", i);
		}
	}




	for (iteration=0; iteration<numOptTours+numOptExgTours; iteration ++) 
	{
		flag = 1;
		for(i=0; i<numOptTours; i++)
		{
			if (optAircraftID[i] == 0) // Tour hasn't been assigned an aircraft yet
			{
				flag = 0;
				if (optTours[i].crewArcType == 2)
				{
					pickupInd = crewPairList[optTours[i].crewPairInd].crewPUSList[optTours[i].crewArcInd]->demandInd;
					if (availStart[pickupInd] > 0) // If aircraft is available at start of the pickup demand index
					{
						optAircraftID[i] = availStart[pickupInd];
						availStart[pickupInd] = 0;
						if (optTours[i].dropPlane) // If plane is dropped off at the end of the tour
						{
							updatePlaneAvailability (i);
						}
					}
				}
				else if (optTours[i].crewArcType == 3)
				{
					pickupInd = crewPairList[optTours[i].crewPairInd].crewPUEList[optTours[i].crewArcInd]->demandInd;
					if (availEnd[pickupInd] > 0) 
					{// If plane is available at the pickup location after a demand leg
						optAircraftID[i] = availEnd[pickupInd];
						availEnd[pickupInd] = 0;
						if (optTours[i].dropPlane) // If dropping off plane, update availability
						{
							updatePlaneAvailability (i);
						}
					}
				}
			}
		}

		/*
		*	Do the same thing for existing tours.
		*	Loop through the existing tours looking for existing plane, and update plane location if found.
		*	Both this loop and the above loop have finite termination because one new optAircraftID is found
		*	in each iteration. The complexity of the above loop is O(numOptTours^2) while that of the loop
		*	below is O(numExgTour^2). It could possibly be done more efficiently, but this takes negligible time
		*	anyway.
		*/
		for (i=0; i<numOptExgTours; i++)
		{
			if (optAircraftID[numOptTours+i] == 0)
			{
				flag = 0;
				if (optExgTours[i].pickupType == 2)
				{
					pickupInd = optExgTours[i].pickupInd;
					if (availStart[pickupInd] > 0)
					{
						optAircraftID[numOptTours+i] = availStart[pickupInd];
						availStart[pickupInd] = 0;
						if (optExgTours[i].dropoffType == 2)
						{
							availStart[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 3)
						{
							availEnd[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 4)
						{
							availRepoConnxn[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}			
					}
				}
				else if (optExgTours[i].pickupType == 3)
				{
					pickupInd = optExgTours[i].pickupInd;
					if (availEnd[pickupInd] > 0)
					{
						optAircraftID[numOptTours+i] = availEnd[pickupInd];
						availEnd[pickupInd] = 0;
						if (optExgTours[i].dropoffType == 2)
						{
							availStart[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 3)
						{
							availEnd[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 4)
						{
							availRepoConnxn[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}		
					}
				}
				else if (optExgTours[i].pickupType == 4)
				{
					pickupInd = optExgTours[i].pickupInd;
					if (availRepoConnxn[pickupInd] > 0)
					{
						optAircraftID[numOptTours+i] = availRepoConnxn[pickupInd];
						availRepoConnxn[pickupInd] = 0;
						if (optExgTours[i].dropoffType == 2)
						{
							availStart[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 3)
						{
							availEnd[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}
						else if (optExgTours[i].dropoffType == 4)
						{
							availRepoConnxn[optExgTours[i].dropoffInd] = optAircraftID[numOptTours+i];
						}		
					}
				}
			}
		}
		if (flag) // flag indicates that all optAircraftIDs have been populated.
			break;
	}

	
	

	return 0;
}

/************************************************************************************************
*	Function	getDemandIndex								Date last modified:  8/01/06 BGC	*
*	Purpose:	Returns demand index corresponding to a particular ID.							*
************************************************************************************************/

static int
getDemandIndex (int demandID)
{
	int i;
	for (i=0; i<numDemand; i++)
	{
		if (demandID == demandList[i].demandID)
			return i;
	}
	return -1;
}

/************************************************************************************************
*	Function	constructManagedLegs			Date last modified:  8/01/06 BGC, 2/08/06 SWO	*
*	Purpose:	Constructs the managed leg output list.											*
************************************************************************************************/

static int
constructManagedLegs (void)
{
	int i, cpInd, acID, availAptID, j, k, m, n, acTypeInd, lastDemandInd, availFBOID, x, hasFlown;
	time_t availTime;
	int fltTm, blkTm, elapsedTm, stops;
    int y;//11/16/07 ANG
	CrewEndTourRecord *tPtr;//11/16/07 ANG
//	double tempCost;//11/16/07 ANG
//	int fltTmX, blkTmX, elapsedTmX, stopsX;//11/16/07 ANG

	tPtr = (CrewEndTourRecord *) calloc((size_t) 1, (size_t) sizeof(CrewEndTourRecord)); // 11/16/07 ANG
	if(! tPtr) {
		logMsg(logFile,"%s Line %d, Out of Memory in constructManagedLegs().\n", __FILE__,__LINE__);
		exit(1);
	}

	for (i=0; i<numOptTours; i++)
	{ // Look through optTourList and optAircraftID and spit out data.
		cpInd = optTours[i].crewPairInd;
		acTypeInd = crewPairList[cpInd].acTypeIndex;
		acID = optAircraftID[i];
//TEMP FOR DEBUGGING
//		logMsg(logFile, "Tour for Aircraft: %d, Cost: %f.\n", acID, optTours[i].cost);
//END TEMP
		//hasFlown = 0;



		if (optTours[i].crewArcType == 1) // Crew-plane arc
		{
			availAptID = acList[crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].acInd].availAirportID;
			availFBOID = acList[crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].acInd].availFboID;
		}
		else if (optTours[i].crewArcType == 2) //crew-pickupStart arc
		{
			availAptID = demandList[crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->demandInd].outAirportID;
			availFBOID = demandList[crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->demandInd].outFboID;
		}
		else //crew-pickupEnd arc
		{
			availAptID = demandList[crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->demandInd].inAirportID;
			availFBOID = demandList[crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->demandInd].inFboID;
		}

		for (j=0; j<MAX_WINDOW_DURATION; j++)
		{   hasFlown = 0;
			if (optTours[i].duties[j] < 0)
				continue;
			for (k=0; k<maxTripsPerDuty; k++)
			{
				if ((m = dutyList[acTypeInd][optTours[i].duties[j]].demandInd[k]) < 0)
					break;
				lastDemandInd = m;
				if (demandList[lastDemandInd].outAirportID != availAptID) 
				{// If location where plane is available is not the outAirport, create managed repositioning leg. 
					propMgdLegs[numPropMgdLegs].aircraftID = acID;
					propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[cpInd].crewListInd[0]].crewID;
					propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[cpInd].crewListInd[1]].crewID;
					propMgdLegs[numPropMgdLegs].crewPairInd = cpInd;
					propMgdLegs[numPropMgdLegs].schedOutAptID = availAptID;
					propMgdLegs[numPropMgdLegs].schedOutFBOID = availFBOID;
					propMgdLegs[numPropMgdLegs].schedInAptID = demandList[lastDemandInd].outAirportID;
					propMgdLegs[numPropMgdLegs].schedInFBOID = demandList[lastDemandInd].outFboID;
					propMgdLegs[numPropMgdLegs].demandID = 0; // 0 indicates a repo.

					if(!hasFlown)// if this is first flight leg of duty (including positioning legs), leave as late as possible for repo.
					{
						getFlightTime(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, 
							acTypeList[acTypeInd].aircraftTypeID, month, 0, &fltTm, &blkTm, &elapsedTm, &stops);
						propMgdLegs[numPropMgdLegs].schedOut = 
							60*getRepoDepartTm(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, (int)(outTimes[lastDemandInd]/60) - optParam.turnTime, elapsedTm);
						propMgdLegs[numPropMgdLegs].schedIn = propMgdLegs[numPropMgdLegs].schedOut + 60*elapsedTm;

					}
					else // leave as early as possible.
					{
						getFlightTime(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, 
							acTypeList[acTypeInd].aircraftTypeID, month, 0, &fltTm, &blkTm, &elapsedTm, &stops);
						propMgdLegs[numPropMgdLegs].schedIn = 
							60*getRepoArriveTm(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, (int)(availTime/60), elapsedTm);
						propMgdLegs[numPropMgdLegs].schedOut = propMgdLegs[numPropMgdLegs].schedIn - 60*elapsedTm;
					}





					hasFlown = 1;
					numPropMgdLegs ++;
				}
				if (demandList[lastDemandInd].isAppoint == 0)// Create managed demand leg if not a maintenance/appointment leg
				{
					isManaged[lastDemandInd] = 1;
					propMgdLegs[numPropMgdLegs].aircraftID = acID;
					propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[cpInd].crewListInd[0]].crewID;
					propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[cpInd].crewListInd[1]].crewID;
					propMgdLegs[numPropMgdLegs].crewPairInd = cpInd;
					propMgdLegs[numPropMgdLegs].demandID = demandList[lastDemandInd].demandID;
					propMgdLegs[numPropMgdLegs].schedIn = inTimes[lastDemandInd];
					propMgdLegs[numPropMgdLegs].schedInAptID = demandList[lastDemandInd].inAirportID;
					propMgdLegs[numPropMgdLegs].schedInFBOID = demandList[lastDemandInd].inFboID;
					propMgdLegs[numPropMgdLegs].schedOut = outTimes[lastDemandInd];
					propMgdLegs[numPropMgdLegs].schedOutAptID = demandList[lastDemandInd].outAirportID;
					propMgdLegs[numPropMgdLegs].schedOutFBOID = demandList[lastDemandInd].outFboID;
					hasFlown = 1;
					numPropMgdLegs ++;
				}

				//START - 11/16/07 ANG
				if(optParam.autoFlyHome == 1){
					if(demandList[lastDemandInd].isAppoint == 4){
						for(y = 0, tPtr = crewEndTourList; y < crewEndTourCount; ++y, ++tPtr) {
							//if( tPtr->aircraftID == acID &&
							//	tPtr->airportID == demandList[lastDemandInd].outAirportID &&
							//	(tPtr->crewID1 == crewList[crewPairList[cpInd].crewListInd[0]].crewID || tPtr->crewID1 == crewList[crewPairList[cpInd].crewListInd[1]].crewID) &&
							//	(tPtr->recordType == 1 || tPtr->recordType == 2) &&
							//	tPtr->covered == 1){
							//		getFlightTime(availAptID, demandList[lastDemandInd].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &fltTmX, &blkTmX, &elapsedTmX, &stopsX);
							//		tPtr->cost = (fltTmX*acTypeList[acTypeInd].operatingCost)/60 + (stopsX+1)*acTypeList[acTypeInd].taxiCost;
							//		//fprintf(logFile, "Just for debug (need to be deleted): Flight time for acID %d is %d. \n", acID, fltTmX);
							//		break;
							//}//end if
							//else 
							if (tPtr->aircraftID == acID &&
								tPtr->airportID == demandList[lastDemandInd].outAirportID &&
								tPtr->crewID1 != crewList[crewPairList[cpInd].crewListInd[0]].crewID && 
								tPtr->crewID1 != crewList[crewPairList[cpInd].crewListInd[1]].crewID &&
								(tPtr->recordType == 1 || tPtr->recordType == 2) &&
								tPtr->covered == 1){
								tPtr->wrongCrew = 1;
								break;
							}//end else if
						}//end for
					}//end if
				}//end if
				//END - 11/16/07 ANG

				availAptID = demandList[lastDemandInd].inAirportID;
				availFBOID = demandList[lastDemandInd].inFboID;
				availTime = inTimes[lastDemandInd] + 60*(demandList[lastDemandInd].turnTime);

			}  //end k=0 loop
			if ((n = dutyList[acTypeInd][optTours[i].duties[j]].repoDemandInd) >= 0)
			{ // If tour involves a duty with repo at the end, create a repo managed leg.
				lastDemandInd = n;
				propMgdLegs[numPropMgdLegs].aircraftID = acID;
				propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[cpInd].crewListInd[0]].crewID;
				propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[cpInd].crewListInd[1]].crewID;
				propMgdLegs[numPropMgdLegs].crewPairInd = cpInd;
				propMgdLegs[numPropMgdLegs].schedOutAptID = availAptID;
				propMgdLegs[numPropMgdLegs].schedOutFBOID = availFBOID;
				propMgdLegs[numPropMgdLegs].schedInAptID = demandList[lastDemandInd].outAirportID;
				propMgdLegs[numPropMgdLegs].schedInFBOID = demandList[lastDemandInd].outFboID;
				propMgdLegs[numPropMgdLegs].demandID = 0; // 0 indicates a repo.
				getFlightTime(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, 
						acTypeList[acTypeInd].aircraftTypeID, month, 0, &fltTm, &blkTm, &elapsedTm, &stops);			
                
                /*//START - 11/16/07 ANG
				if(optParam.autoFlyHome == 1){
					if(demandList[lastDemandInd].isAppoint == 4){
						for(y = 0, tPtr = crewEndTourList; y < crewEndTourCount; ++y, ++tPtr) {
							if( tPtr->aircraftID == acID &&
								tPtr->airportID == demandList[lastDemandInd].outAirportID &&
								(tPtr->crewID1 == crewList[crewPairList[cpInd].crewListInd[0]].crewID || tPtr->crewID1 == crewList[crewPairList[cpInd].crewListInd[1]].crewID) &&
								(tPtr->recordType == 1 || tPtr->recordType == 2) &&
								tPtr->covered == 1){
									getFlightTime(availAptID, demandList[lastDemandInd].outAirportID, acTypeList[acTypeInd].aircraftTypeID, month, 0, &fltTmX, &blkTmX, &elapsedTmX, &stopsX);
									tPtr->cost = (fltTmX*acTypeList[acTypeInd].operatingCost)/60 + (stopsX+1)*acTypeList[acTypeInd].taxiCost;
									//fprintf(logFile, "Just for debug (need to be deleted): Flight time for acID %d is %d. \n", acID, fltTmX);
									break;
							}//end if
						}//end for
					}//end if
				}//end if
				//END - 11/16/07 ANG*/

				if (dutyList[acTypeInd][optTours[i].duties[j]].demandInd[0] < 0) //if a repo-only duty
				{
					if(optTours[i].dropPlane > 1){//dropPlane > 1, dropPlane indicates the time that the plane is dropped to get pilot home on time
						propMgdLegs[numPropMgdLegs].schedIn = optTours[i].dropPlane*60;
						propMgdLegs[numPropMgdLegs].schedOut = propMgdLegs[numPropMgdLegs].schedIn - 60*elapsedTm;
					}
					else if(j == 0){  //else if this repo-only duty is the first duty for the crew, it is possible that it started earlier
						// (per repoASAP in calculateArcsToFirstDuties) and we should scan arcs to get crewArc start time
						if (optTours[i].crewArcType == 1){ // Crew-plane arc
							for(x = 0; x<crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].numArcs; x++){
								if(optTours[i].duties[j]== crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].arcList[x].destDutyInd){
									propMgdLegs[numPropMgdLegs].schedOut = crewPairList[cpInd].crewPlaneList[optTours[i].crewArcInd].arcList[x].startTm*60;
									propMgdLegs[numPropMgdLegs].schedIn = propMgdLegs[numPropMgdLegs].schedOut + 60*elapsedTm;
									break;
								}
							}
						}
						else if (optTours[i].crewArcType == 2){ //crew-pickupStart arc
							for(x = 0; x<crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->numArcs; x++){
								if(optTours[i].duties[j]== crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->arcList[x].destDutyInd){
									propMgdLegs[numPropMgdLegs].schedOut = crewPairList[cpInd].crewPUSList[optTours[i].crewArcInd]->arcList[x].startTm*60;
									propMgdLegs[numPropMgdLegs].schedIn = propMgdLegs[numPropMgdLegs].schedOut + 60*elapsedTm;
									break;
								}
							}
						}
						else { //crew-pickupEnd arc
							for(x = 0; x<crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->numArcs; x++){
								if(optTours[i].duties[j]== crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->arcList[x].destDutyInd){
									propMgdLegs[numPropMgdLegs].schedOut = crewPairList[cpInd].crewPUEList[optTours[i].crewArcInd]->arcList[x].startTm*60;
									propMgdLegs[numPropMgdLegs].schedIn = propMgdLegs[numPropMgdLegs].schedOut + 60*elapsedTm;
									break;
								}
							}
						}
					}
					else{
						propMgdLegs[numPropMgdLegs].schedOut = 
							60*getRepoDepartTm(propMgdLegs[numPropMgdLegs].schedOutAptID, propMgdLegs[numPropMgdLegs].schedInAptID, dutyList[acTypeInd][optTours[i].duties[j]].endTm, elapsedTm);
						propMgdLegs[numPropMgdLegs].schedIn = propMgdLegs[numPropMgdLegs].schedOut + 60*elapsedTm;
					}
				}
				else  //this is NOT a repo-only duty, but just has a repo at the end
				{
					propMgdLegs[numPropMgdLegs].schedIn = dutyList[acTypeInd][optTours[i].duties[j]].endTm*60;
					propMgdLegs[numPropMgdLegs].schedOut = propMgdLegs[numPropMgdLegs].schedIn - 60 * elapsedTm;
				}
				availAptID = demandList[lastDemandInd].outAirportID;
				availFBOID = demandList[lastDemandInd].outFboID;
				availTime = outTimes[lastDemandInd];
				numPropMgdLegs ++;
			}
		}
	}

	/*
	*	Do the same as above for existing tours. Slightly easier because repo legs are (should be?) contained in 
	*	the legList.
	*/
	for (i=0; i<numOptExgTours; i++)
	{
//TEMP FOR DEBUGGING
//		logMsg(logFile, "Tour for Aircraft: %d, Cost: %f.\n", optAircraftID[numOptTours+i], optExgTours[i].cost);
//END TEMP
		for (j=0; j<numLegs; j++)
		{
			if (legList[j].dropped && (legList[j].crewPairID == crewPairList[optExgTours[i].crewPairInd].crewPairID) 
				&& (legList[j].aircraftID == optAircraftID[numOptTours+i])		//	&& (legList[j].acInd == optExgTours[i].acInd)
				&& (!legList[j].inLockedTour))  // RLZ 09/01/2009 the tour is complete till a leg is dropped, 
				break;
			if ((legList[j].crewPairID == crewPairList[optExgTours[i].crewPairInd].crewPairID) 
				&& (legList[j].aircraftID == optAircraftID[numOptTours+i])		//	&& (legList[j].acInd == optExgTours[i].acInd)
				&& (!legList[j].inLockedTour))
			{
				propMgdLegs[numPropMgdLegs].aircraftID = optAircraftID[numOptTours+i];
				propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[optExgTours[i].crewPairInd].crewListInd[0]].crewID;
				propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[optExgTours[i].crewPairInd].crewListInd[1]].crewID;
				propMgdLegs[numPropMgdLegs].crewPairInd = optExgTours[i].crewPairInd;
				propMgdLegs[numPropMgdLegs].demandID = legList[j].demandID;
				propMgdLegs[numPropMgdLegs].exgTour = 1;
				
				if ((legList[j].demandID > 0) && ((k = getDemandIndex (legList[j].demandID)) >= 0))
					isManaged[k] = 1;
				
				propMgdLegs[numPropMgdLegs].schedInFBOID = legList[j].inFboID;
				propMgdLegs[numPropMgdLegs].schedOutFBOID = legList[j].outFboID;
				propMgdLegs[numPropMgdLegs].schedIn = legList[j].schedIn;
				propMgdLegs[numPropMgdLegs].schedInAptID = legList[j].inAirportID;
				propMgdLegs[numPropMgdLegs].schedOut = legList[j].schedOut;
				propMgdLegs[numPropMgdLegs].schedOutAptID = legList[j].outAirportID;
				numPropMgdLegs ++;
			}
			//START - 03/20/08 ANG
			else if (optExgTours[i].crewPairInd2 > -1 && (legList[j].crewPairID == crewPairList[optExgTours[i].crewPairInd2].crewPairID) 
				&& (legList[j].aircraftID == optAircraftID[numOptTours+i])		//	&& (legList[j].acInd == optExgTours[i].acInd)
				&& (!legList[j].inLockedTour))
			{
				propMgdLegs[numPropMgdLegs].aircraftID = optAircraftID[numOptTours+i];
				propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[optExgTours[i].crewPairInd2].crewListInd[0]].crewID;
				propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[optExgTours[i].crewPairInd2].crewListInd[1]].crewID;
				propMgdLegs[numPropMgdLegs].crewPairInd = optExgTours[i].crewPairInd2;
				propMgdLegs[numPropMgdLegs].demandID = legList[j].demandID;
				propMgdLegs[numPropMgdLegs].exgTour = 1;
				
				if ((legList[j].demandID > 0) && ((k = getDemandIndex (legList[j].demandID)) >= 0))
					isManaged[k] = 1;
				
				propMgdLegs[numPropMgdLegs].schedInFBOID = legList[j].inFboID;
				propMgdLegs[numPropMgdLegs].schedOutFBOID = legList[j].outFboID;
				propMgdLegs[numPropMgdLegs].schedIn = legList[j].schedIn;
				propMgdLegs[numPropMgdLegs].schedInAptID = legList[j].inAirportID;
				propMgdLegs[numPropMgdLegs].schedOut = legList[j].schedOut;
				propMgdLegs[numPropMgdLegs].schedOutAptID = legList[j].outAirportID;
				numPropMgdLegs ++;
			}
			//END- 03/20/08 ANG
		}
	}
	for (j=0; j<numLegs; j++){ //writing locked tour legs
		if (legList[j].inLockedTour){
			propMgdLegs[numPropMgdLegs].aircraftID = legList[j].aircraftID;
			propMgdLegs[numPropMgdLegs].captainID = crewList[crewPairList[legList[j].crewPairInd].crewListInd[0]].crewID;
			propMgdLegs[numPropMgdLegs].FOID = crewList[crewPairList[legList[j].crewPairInd].crewListInd[1]].crewID;
			propMgdLegs[numPropMgdLegs].crewPairInd = legList[j].crewPairInd;
			propMgdLegs[numPropMgdLegs].demandID = legList[j].demandID;
			
			if ((legList[j].demandID > 0) && ((k = getDemandIndex (legList[j].demandID)) >= 0))
				isManaged[k] = 1;
			
			propMgdLegs[numPropMgdLegs].schedInFBOID = legList[j].inFboID;
			propMgdLegs[numPropMgdLegs].schedOutFBOID = legList[j].outFboID;
			propMgdLegs[numPropMgdLegs].schedIn = legList[j].schedIn;
			propMgdLegs[numPropMgdLegs].schedInAptID = legList[j].inAirportID;
			propMgdLegs[numPropMgdLegs].schedOut = legList[j].schedOut;
			propMgdLegs[numPropMgdLegs].schedOutAptID = legList[j].outAirportID;
			numPropMgdLegs ++;
		}
	}
	// Going to sort list by sched out (will be used to generate crew assignments to planes).
	qsort((void *) propMgdLegs, numPropMgdLegs, sizeof(ProposedMgdLeg), compareMgdLegsSchedOut);

	return 0;
}


/************************************************************************************************
*	Function	constructUnmanagedLegs						Date last modified:  8/01/06 BGC	*
*	Purpose:	Constructs the unmanaged leg output list.										*
************************************************************************************************/

static int
constructUnmanagedLegs (void)
{
	int i;
	for (i=0; i<numOptDemand; i++)
	{ /* 
	  *	For all charters, create unmanaged leg. Not looking at CPLEX output here because we already know from
	  *	optTours and optExgTour which demands are not being covered. 
	  */
		
		if ((!demandList[i].isAppoint) && (!isManaged[i]) && (demandList[i].demandID <= actualMaxDemandID))
		{
			propUnmgdLegs[numPropUnmgdLegs].arrivalAptID = demandList[i].inAirportID;
			propUnmgdLegs[numPropUnmgdLegs].arrivalFBOID = demandList[i].inFboID;
			propUnmgdLegs[numPropUnmgdLegs].demandID = demandList[i].demandID;
			propUnmgdLegs[numPropUnmgdLegs].departureAptID = demandList[i].outAirportID;
			propUnmgdLegs[numPropUnmgdLegs].departureFBOID = demandList[i].outFboID;
			propUnmgdLegs[numPropUnmgdLegs].schedIn = demandList[i].reqIn;
			propUnmgdLegs[numPropUnmgdLegs].schedOut = demandList[i].reqOut;
			numPropUnmgdLegs ++;
		}
	}
	qsort((void *) propUnmgdLegs, numPropUnmgdLegs, sizeof(ProposedUnmgdLeg), compareUnmgdLegsID);
	// Sorting output by increasing ID. Check with Harry if he needs it sorted in any particular way.
	return 0;
}

/************************************************************************************************
*	Function	constructCrewAssignment			Date last modified:  8/01/06 BGC, 2/08/07 SWO	*
*	Purpose:	Builds the proposed crew assignment list from the proposed managed leg			*
*				solution.																		*
************************************************************************************************/

static int
constructCrewAssignment (void)
{
	int i, crewID, acID, j, c;
	time_t startTm, endTm;

	if ((scanned = (int **) calloc (numPropMgdLegs, sizeof (int *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructCrewAssignment().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<numPropMgdLegs; i++)
	{
		if ((scanned[i] = (int *) calloc (2, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in constructCrewAssignment().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		scanned[i][0] = 0; // [][0] is for captain.
		scanned[i][1] = 0;// [][1] is for FO
	}
	if((scannedFirst = (int *) calloc (numCrew, sizeof(int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructCrewAssignment().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	/*
	*	For each proposed managed leg, go through the list of subsequent legs to check for that pilot-aircraft 
	*	combination. Stop when pilot is flying another plane or in another position, or when aircraft is flown by another pilot, or when 
	*   pilot has time to rest between duties.
	*/
	for (i=0; i<numPropMgdLegs; i++)
	{
		if (!scanned[i][0])
		{
			crewID = propMgdLegs[i].captainID;
			//find index of captain
			for (c=0; c<numCrew; c++){
				if (crewList[c].crewID == crewID)
					break;
			}
			if(c == numCrew){//writeWarningData(myconn); exit(1) with error message if crewList index is not found
				logMsg(logFile,"%s Line %d, crewList index for crew ID %d not found.\n", __FILE__,__LINE__,crewID);
				writeWarningData(myconn); exit(1);
			}
			acID = propMgdLegs[i].aircraftID;
			if(scannedFirst[c] == 0){ //if this is the first leg we have scanned for the pilot
				scannedFirst[c] = 1;
				//if (pilot is already on duty at start of planning window OR if he has been notified of next duty start time - 
				//origActCode == 0 in either case) AND crew pair has flown plane AND pilot doesn't have time to rest before this leg....
				//Note:  give 5 minute tolerance on min overnight time
				if(crewList[c].origActCode == 0 && crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 1 &&
					difftime(propMgdLegs[i].schedOut, crewList[c].origAvailDT) < 60*((crewList[c].origBlockTm? optParam.postFlightTm : 0)+ optParam.minRestTm + optParam.preFlightTm - 5))
					
					startTm = crewList[c].origAvailDT - 60*crewList[c].origDutyTm;
				else if(crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 0)
					startTm = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
				else
					startTm = propMgdLegs[i].schedOut - 60*optParam.preFlightTm;
			}
			else
				startTm = propMgdLegs[i].schedOut - 60*optParam.preFlightTm;
			endTm = propMgdLegs[i].schedIn + 60*optParam.postFlightTm;
			scanned[i][0] = 1;

			for (j=i+1; j<numPropMgdLegs; j++)
			{
				if ((propMgdLegs[j].captainID == crewID) && (propMgdLegs[j].aircraftID == acID) && 
					difftime(propMgdLegs[j].schedOut, endTm) < 60*(optParam.minRestTm + optParam.preFlightTm - 5)) //give 5 minute tolerance on min overnight time
				{
					endTm = propMgdLegs[j].schedIn + 60*optParam.postFlightTm;
					scanned[j][0] = 1;
				}
				else if (((propMgdLegs[j].captainID == crewID) && (propMgdLegs[j].aircraftID != acID)) || 
						((propMgdLegs[j].aircraftID == acID) && (propMgdLegs[j].captainID != crewID)) ||
						(propMgdLegs[j].FOID == crewID) || 
						((propMgdLegs[j].captainID == crewID) && (propMgdLegs[j].aircraftID == acID) && 
							difftime(propMgdLegs[j].schedOut, endTm) >= 60*(optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm - 5)))
				{
					break;
				}
			}
			propCrewAssignment[numPropCrewDays].aircraftID = acID;
			propCrewAssignment[numPropCrewDays].crewID = crewID;
			propCrewAssignment[numPropCrewDays].position = 1;
			propCrewAssignment[numPropCrewDays].startTm = startTm;
			//modify post-flight time if this is the last day of the crewPairs tour
			if(endTm > (crewPairList[propMgdLegs[i].crewPairInd].pairEndTm - 24*3600))
				endTm += 60*(optParam.finalPostFltTm - optParam.postFlightTm);
			propCrewAssignment[numPropCrewDays].endTm = endTm;
			numPropCrewDays ++;
		}
		if (!scanned[i][1])
		{
			crewID = propMgdLegs[i].FOID;
			//find index of flight officer
			for (c=0; c<numCrew; c++){
				if (crewList[c].crewID == crewID)
					break;
			}
			if(c == numCrew){//writeWarningData(myconn); exit(1) with error message if crewList index is not found
				logMsg(logFile,"%s Line %d, crewList index for crew ID %d not found.\n", __FILE__,__LINE__,crewID);
				writeWarningData(myconn); exit(1);
			}
			acID = propMgdLegs[i].aircraftID;
			if(scannedFirst[c] == 0){ //if this is the first leg we have scanned for the pilot
				scannedFirst[c] = 1;
				//if (pilot is already on duty at start of planning window OR if he has been notified of next duty start time -
				//origActCode == 0 in either case) AND crew pair has flown plane AND pilot doesn't have time to rest before this leg....
				//Note:  give 5 minute tolerance on min overnight time
				if(crewList[c].origActCode == 0 && crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 1 &&
					difftime(propMgdLegs[i].schedOut, crewList[c].origAvailDT) < 60*((crewList[c].origBlockTm? optParam.postFlightTm : 0)+ optParam.minRestTm + optParam.preFlightTm - 5))
			
					startTm = crewList[c].origAvailDT - 60*crewList[c].origDutyTm;
				else if(crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 0)
					startTm = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
				else
					startTm = propMgdLegs[i].schedOut - 60*optParam.preFlightTm;
			}
			else
				startTm = propMgdLegs[i].schedOut - 60*optParam.preFlightTm;
			endTm = propMgdLegs[i].schedIn + 60*optParam.postFlightTm;
			scanned[i][1] = 1;

			for (j=i+1; j<numPropMgdLegs; j++)
			{
				if ((propMgdLegs[j].FOID == crewID) && (propMgdLegs[j].aircraftID == acID) &&
					difftime(propMgdLegs[j].schedOut, endTm) < 60*(optParam.minRestTm + optParam.preFlightTm - 5)) //give 5 minute tolerance on min overnight time
				{
					endTm = propMgdLegs[j].schedIn + 60*optParam.postFlightTm;
					scanned[j][1] = 1;
				}
				else if (((propMgdLegs[j].FOID == crewID) && (propMgdLegs[j].aircraftID != acID)) ||
						((propMgdLegs[j].aircraftID == acID) && (propMgdLegs[j].FOID != crewID)) ||
						(propMgdLegs[j].captainID == crewID) ||
						((propMgdLegs[j].FOID == crewID) && (propMgdLegs[j].aircraftID == acID) && 
							difftime(propMgdLegs[j].schedOut, endTm) >= 60*(optParam.postFlightTm + optParam.minRestTm + optParam.preFlightTm - 5)))

				{
					break;
				}
			}
			
			propCrewAssignment[numPropCrewDays].aircraftID = acID;
			propCrewAssignment[numPropCrewDays].crewID = crewID;
			propCrewAssignment[numPropCrewDays].position = 2;
			propCrewAssignment[numPropCrewDays].startTm = startTm;
			//modify post-flight time if this is the last day of the crewPairs tour
			if(endTm > (crewPairList[propMgdLegs[i].crewPairInd].pairEndTm - 24*3600))
				endTm += 60*(optParam.finalPostFltTm - optParam.postFlightTm);
			propCrewAssignment[numPropCrewDays].endTm = endTm;	
			numPropCrewDays ++;
		}
	}
	qsort((void *) propCrewAssignment, numPropCrewDays, sizeof(ProposedCrewAssg), comparePropCrewDays);
	qsort((void *) propMgdLegs, numPropMgdLegs, sizeof(ProposedMgdLeg), compareMgdLegs);
	return 0;
}

/************************************************************************************************
*	Function	constructCrewAssignment2RLZ			Date last modified:  04/17/09 RLZ	*
*	Purpose:	Builds the proposed crew assignment list from the proposed managed leg			*
*				solution.																		*
************************************************************************************************/

static int
constructCrewAssignment2RLZ (void)
{   
	int i, j, crewInd1, crewInd2;

    time_t time_1, time_2;

	for (i = numExgTourCols + numCharterCols  + numApptCols ; i < numInitialCols ; i++){
		if (optSolution[i]){
			j = i - (numExgTourCols + numCharterCols  + numApptCols);

			// This is a simplified version. More complicated one could use crewList[c].origActCode to enumerate all the cases;
			crewInd1 = crewPairList[crewAcTourList[j].crewPairInd].crewListInd[0];
			crewInd2 = crewPairList[crewAcTourList[j].crewPairInd].crewListInd[1];
			time_1 = crewList[crewInd1].origAvailDT - 60*crewList[crewInd1].origDutyTm;
			time_2 = crewList[crewInd2].origAvailDT - 60*crewList[crewInd2].origDutyTm;

			if ( max(time_1,time_2) - min(time_1,time_2) >= 10*60*60)   //diff by more than 10 hours
				time_1 = time_2 = max(time_1,time_2);


			propCrewAssignment[numPropCrewDays].aircraftID = acList[crewAcTourList[j].acInd].aircraftID;
			propCrewAssignment[numPropCrewDays].crewID = crewList[crewInd1].crewID;
			propCrewAssignment[numPropCrewDays].position = 1;
			propCrewAssignment[numPropCrewDays].startTm = time_1;
            //run_time_t; // crewPairList[crewAcTourList[j].crewPairInd].availDT;
			propCrewAssignment[numPropCrewDays].endTm = propCrewAssignment[numPropCrewDays].startTm + optParam.maxDutyTm * 60;
			numPropCrewDays ++;


			
			propCrewAssignment[numPropCrewDays].aircraftID = acList[crewAcTourList[j].acInd].aircraftID;
			propCrewAssignment[numPropCrewDays].crewID = crewList[crewPairList[crewAcTourList[j].crewPairInd].crewListInd[1]].crewID;
			propCrewAssignment[numPropCrewDays].position = 2;
			propCrewAssignment[numPropCrewDays].startTm = time_2;
			propCrewAssignment[numPropCrewDays].endTm = propCrewAssignment[numPropCrewDays].startTm + optParam.maxDutyTm * 60;
			numPropCrewDays ++;
		}
	}

	qsort((void *) propCrewAssignment, numPropCrewDays, sizeof(ProposedCrewAssg), comparePropCrewDays);
	return 0;

}



/************************************************************************************************
*	Function	constructTravelRequest			Date last modified:  11/25/2008, 05/04/2009, 05/28/09  Jintao     *
*	Purpose:	Builds Travel request for crew from the proposed managed leg solution.	        *
*																						        *
************************************************************************************************/
static int
constructTravelRequest (void)
{
	int i, crewID, acID, j, c;
	static int **trl_scanned = NULL;
	static int *trl_scannedFirst = NULL;
	ProposedMgdLeg *curr_propmdgleg, *next_propmdgleg;
	int break_indicator =0;// used to tell swap drop or get home drop, =1 :change plane, =2 :get home
	int s;
	int gethome_latetest_arr_flag = 0;
	DateTime tmp_dt1, tmp_dt2;
	time_t tmp_dt3;
	DateTime dt_runTime;
	int noneed_cstrl;
	int hours;
    TravelRequest *trlrqstPtr;


	dt_runTime = dt_run_time_GMT;

	tmp_dt1 = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_run_time_GMT);
	
	curr_propmdgleg = (ProposedMgdLeg *) calloc (1, sizeof (ProposedMgdLeg));
	if(!curr_propmdgleg){
        logMsg(logFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

    next_propmdgleg = (ProposedMgdLeg *) calloc (1, sizeof (ProposedMgdLeg));
	if(!next_propmdgleg){
        logMsg(logFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((trl_scanned = (int **) calloc (numPropMgdLegs, sizeof (int *))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}
	for (i=0; i<numPropMgdLegs; i++)
	{
		if ((trl_scanned[i] = (int *) calloc (2, sizeof (int))) == NULL)
		{
			logMsg(logFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
		}
		trl_scanned[i][0] = 0; // [][0] is for captain.
		trl_scanned[i][1] = 0;// [][1] is for FO
	}//initialize trl_scanned
	if((trl_scannedFirst = (int *) calloc (numCrew, sizeof(int))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	
	//	For each proposed managed leg, go through the list of subsequent legs to check for that pilot-aircraft 
	//	combination. Stop when pilot is flying another plane or in another position, or when aircraft is flown by another pilot.
	//
	for (i=0; i<numPropMgdLegs; i++)
	{
		if (!trl_scanned[i][0])
		{
			crewID = propMgdLegs[i].captainID;
			//find index of captain
			for (c=0; c<numCrew; c++){
				if (crewList[c].crewID == crewID)
					break;
			}
			if(c == numCrew){//writeWarningData(myconn); exit(1) with error message if crewList index is not found
				logMsg(logFile,"%s Line %d, crewList index for crew ID %d not found.\n", __FILE__,__LINE__,crewID);
				writeWarningData(myconn); exit(1);
			}
			acID = propMgdLegs[i].aircraftID;
			if(trl_scannedFirst[c] == 0){ //if this is the first leg we have scanned for the pilot
				trl_scannedFirst[c] = 1;
				if(crewList[c].availAirportID!=propMgdLegs[i].schedOutAptID){
                    //tmp_dt1 = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_run_time_GMT);
					if(crewList[c].lastCsTravelLeg)
					   tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, crewList[c].lastCsTravelLeg ->travel_dptTm);
					else 
					   tmp_dt2 = 0;
					if(crewList[c].lastCsTravelLeg)
					   tmp_dt3 = DateTimeToTime_t(crewList[c].lastCsTravelLeg->travel_dptTm);
					else
					   tmp_dt3 = 0;
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
						logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
		                writeWarningData(myconn); exit(1);
	                 }

					if(crewList[c].lastCsTravelLeg && tmp_dt2 >=tmp_dt1 && tmp_dt3 > crewList[c].lastActivityLeg_recout){
					   trlrqstPtr->crewID = crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].lastCsTravelLeg->dpt_aptID;
					   trlrqstPtr->arr_aptID_travel = propMgdLegs[i].schedOutAptID;
					   trlrqstPtr->earliest_dept = DateTimeToTime_t(tmp_dt2);//need to refine
					   trlrqstPtr->latest_arr = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
					   trlrqstPtr->cancelexstticket = 1;
                       trlrqstPtr->tixquestid_cancelled = crewList[c].lastCsTravelLeg->rqtID;
					}
					else{
					   trlrqstPtr->crewID = crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].availAirportID;
					   trlrqstPtr->arr_aptID_travel = propMgdLegs[i].schedOutAptID;
					   trlrqstPtr->earliest_dept = crewList[c].availDT;
					   trlrqstPtr->latest_arr = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
					}
					if(crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 1 || crewList[c].lastActivityLeg_flag){
                        if(crewList[c].lastActivityLeg_flag)
						    trlrqstPtr->off_aircraftID = crewList[c].lastActivityLeg_aircraftID;
						else 
							trlrqstPtr->off_aircraftID = 0;
						trlrqstPtr->flight_purpose = 3; //changing plane
					}
					else{
						trlrqstPtr->off_aircraftID = 0;
						trlrqstPtr->flight_purpose = 1; //on tour
					}
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel, trlrqstPtr->arr_aptID_travel))
                        trlrqstPtr->groundtravel = 0;
					else
						trlrqstPtr->groundtravel = 1;
					trlrqstPtr->on_aircraftID = propMgdLegs[i].aircraftID;
					crewList[c].travel_request_created = 1;
					travelRqsts++;
					trlrqstPtr->rqtindex = travelRqsts;
					trlrqstPtr->buyticket = 1;
                    if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
		              logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
		              exit(1);
		            }
				}
			}
			trl_scanned[i][0] = 1;
			curr_propmdgleg = &propMgdLegs[i];

			for (j=i+1; j<numPropMgdLegs; j++)
			{
				if ((propMgdLegs[j].captainID == crewID) && (propMgdLegs[j].aircraftID == acID)) //give 5 minute tolerance on min overnight time
				{
					trl_scanned[j][0] = 1;
					curr_propmdgleg = &propMgdLegs[j];
					continue;

				}//case for still being with the airplane
				else if ((propMgdLegs[j].captainID == crewID && propMgdLegs[j].aircraftID != acID) || 
					(propMgdLegs[j].FOID == crewID && propMgdLegs[j].aircraftID != acID)){
                    next_propmdgleg = &propMgdLegs[j];
					if(curr_propmdgleg->schedInAptID != next_propmdgleg->schedOutAptID)
                       break_indicator = 1;
					break;
				}// case for changing airplane
				else if((propMgdLegs[j].aircraftID == acID) && (propMgdLegs[j].captainID != crewID && propMgdLegs[j].FOID != crewID) && crewList[c].needgettinghome)
				{   break_indicator = 2;
				    break;
				}//case for dropping plane to get home
			}
			if(((break_indicator == 2) || j==numPropMgdLegs)&& crewList[c].needgettinghome){
				if(curr_propmdgleg->schedInAptID != crewList[c].endLoc){
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
						logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
		                writeWarningData(myconn); exit(1);
					}
					trlrqstPtr->crewID = crewID;
					trlrqstPtr->dept_aptID_travel = curr_propmdgleg->schedInAptID;
					trlrqstPtr->arr_aptID_travel = crewList[c].endLoc;
					trlrqstPtr->earliest_dept = curr_propmdgleg->schedIn + 60*optParam.finalPostFltTm;
					for(s=0; s<=crewList[c].stayLate;s++){
						if((crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension)> trlrqstPtr->earliest_dept){
							 trlrqstPtr->latest_arr = crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension;
							 gethome_latetest_arr_flag = 1;
							 break;
						}       
					}
					trlrqstPtr->off_aircraftID = curr_propmdgleg->aircraftID;
					trlrqstPtr->on_aircraftID = 0;
					trlrqstPtr->flight_purpose = 2;
					travelRqsts++;
					crewList[c].travel_request_created = 1;
					if(!gethome_latetest_arr_flag){
						fprintf(logFile,"Can't find latest arrival time before tour end (Overtime included)");
						trlrqstPtr->latest_arr = trlrqstPtr->earliest_dept;
					}
					gethome_latetest_arr_flag = 0;
					trlrqstPtr->rqtindex = travelRqsts;
					hours = difftime(trlrqstPtr->latest_arr, trlrqstPtr->earliest_dept) / 3600;
					if(hours <= optParam.sendhomecutoff + optParam.maxCrewExtension/60){
						trlrqstPtr->buyticket = 1; 
					}
					else
						trlrqstPtr->buyticket = 0; 
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
					else{
                        trlrqstPtr->groundtravel = 1;
                        crewList[c].noneed_trl_gettinghome = 1;
					}

					if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
						  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
						  exit(1);
					} 
				}
				else
					 crewList[c].noneed_trl_gettinghome = 1;
			}
			if(break_indicator == 1){
				if(curr_propmdgleg->schedInAptID != next_propmdgleg->schedOutAptID){
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
							logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
					}
					trlrqstPtr->crewID = crewID; 
					trlrqstPtr->dept_aptID_travel = curr_propmdgleg->schedInAptID;
					trlrqstPtr->arr_aptID_travel = next_propmdgleg->schedOutAptID;
					trlrqstPtr->earliest_dept = curr_propmdgleg->schedIn + 60*optParam.postFlightTm;//need to refine later 
					trlrqstPtr->latest_arr = next_propmdgleg->schedOut - 60*optParam.preFlightTm;//need to refine
					trlrqstPtr->off_aircraftID = curr_propmdgleg->aircraftID;
					trlrqstPtr->on_aircraftID = next_propmdgleg->aircraftID;
					trlrqstPtr->flight_purpose = 3;
					travelRqsts++;
					crewList[c].travel_request_created = 1;
					trlrqstPtr->rqtindex = travelRqsts;
					trlrqstPtr->buyticket = 1;
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
					else{
                        trlrqstPtr->groundtravel = 1;
					}
					if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
						  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
						  exit(1);
					}
				}
			}
            break_indicator = 0;
		}
		if (!trl_scanned[i][1])
		{
			crewID = propMgdLegs[i].FOID;
			//find index of flight officer
			for (c=0; c<numCrew; c++){
				if (crewList[c].crewID == crewID)
					break;
			}
			if(c == numCrew){//writeWarningData(myconn); exit(1) with error message if crewList index is not found
				logMsg(logFile,"%s Line %d, crewList index for crew ID %d not found.\n", __FILE__,__LINE__,crewID);
				writeWarningData(myconn); exit(1);
			}
			acID = propMgdLegs[i].aircraftID;
			if(trl_scannedFirst[c] == 0){ //if this is the first leg we have scanned for the pilot
				trl_scannedFirst[c] = 1;
				if(crewList[c].availAirportID!=propMgdLegs[i].schedOutAptID){
					//tmp_dt1 = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_run_time_GMT);
					if(crewList[c].lastCsTravelLeg)
					   tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, crewList[c].lastCsTravelLeg->travel_dptTm);
					else
					   tmp_dt2 = 0;
					if(crewList[c].lastCsTravelLeg)
					   tmp_dt3 = DateTimeToTime_t(crewList[c].lastCsTravelLeg->travel_dptTm);
					else
					   tmp_dt3 = 0;
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
						logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
		                writeWarningData(myconn); exit(1);
	                }
					if(crewList[c].lastCsTravelLeg && tmp_dt2 >=tmp_dt1 && tmp_dt3 > crewList[c].lastActivityLeg_recout){
					   trlrqstPtr->crewID = crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].lastCsTravelLeg->dpt_aptID;
					   trlrqstPtr->arr_aptID_travel = propMgdLegs[i].schedOutAptID;
					   trlrqstPtr->earliest_dept = DateTimeToTime_t(tmp_dt2);//need to refine
					   trlrqstPtr->latest_arr = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
                       trlrqstPtr->cancelexstticket = 1;
					   trlrqstPtr->tixquestid_cancelled = crewList[c].lastCsTravelLeg->rqtID;
					}
					else{
					   trlrqstPtr->crewID = crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].availAirportID;
					   trlrqstPtr->arr_aptID_travel = propMgdLegs[i].schedOutAptID;
					   trlrqstPtr->earliest_dept = crewList[c].availDT;
					   trlrqstPtr->latest_arr = propMgdLegs[i].schedOut - 60*optParam.firstPreFltTm;
					}
					//need to add latest arrival
					if(crewPairList[propMgdLegs[i].crewPairInd].hasFlownFirst == 1 || crewList[c].lastActivityLeg_flag){
                        if(crewList[c].lastActivityLeg_flag)
						    trlrqstPtr->off_aircraftID = crewList[c].lastActivityLeg_aircraftID;
						else 
							trlrqstPtr->off_aircraftID = 0;
						trlrqstPtr->flight_purpose = 3; //changing plane
					}
					else{
						trlrqstPtr->off_aircraftID = 0;
						trlrqstPtr->flight_purpose = 1; //on tour
					}
					trlrqstPtr->on_aircraftID = propMgdLegs[i].aircraftID;
					travelRqsts++;
					crewList[c].travel_request_created = 1;
					trlrqstPtr->rqtindex = travelRqsts;
					trlrqstPtr->buyticket = 1;
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
					else{
                        trlrqstPtr->groundtravel = 1;
					}
                    if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
		              logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
		              exit(1);
		            }
				}
			}
			trl_scanned[i][1] = 1;
			curr_propmdgleg = &propMgdLegs[i];
            for (j=i+1; j<numPropMgdLegs; j++)
			{
				if ((propMgdLegs[j].FOID == crewID) && (propMgdLegs[j].aircraftID == acID)) 
				{
					trl_scanned[j][1] = 1;
					curr_propmdgleg = &propMgdLegs[j];

				}//case for still staying with the airplane
				else if ((propMgdLegs[j].captainID == crewID && propMgdLegs[j].aircraftID != acID) || 
					(propMgdLegs[j].FOID == crewID && propMgdLegs[j].aircraftID != acID)){
                    next_propmdgleg = &propMgdLegs[j];
					if(curr_propmdgleg->schedInAptID != next_propmdgleg->schedOutAptID)
						break_indicator = 1;
				    break; 
				}// case for changing airplane
				else if((propMgdLegs[j].aircraftID == acID) && (propMgdLegs[j].captainID != crewID && propMgdLegs[j].FOID != crewID) && crewList[c].needgettinghome)
				{   break_indicator = 2;
				    break;
				}//case for dropping plane to get home
			}
			if(((break_indicator == 2) || j==numPropMgdLegs)&& crewList[c].needgettinghome){
				if(curr_propmdgleg->schedInAptID != crewList[c].endLoc){
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
						logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
		                writeWarningData(myconn); exit(1);
	                }
					trlrqstPtr->crewID = crewID;
					trlrqstPtr->dept_aptID_travel = curr_propmdgleg->schedInAptID;
					trlrqstPtr->arr_aptID_travel = crewList[c].endLoc;
					trlrqstPtr->earliest_dept = curr_propmdgleg->schedIn + 60*optParam.finalPostFltTm;//need to refine later
					for(s=0; s<=crewList[c].stayLate;s++){
						if((crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension)> trlrqstPtr->earliest_dept){
							 trlrqstPtr->latest_arr = crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension;
							 gethome_latetest_arr_flag = 1;
							 break;
						}       
					}
					trlrqstPtr->off_aircraftID = curr_propmdgleg->aircraftID;
					trlrqstPtr->on_aircraftID = 0;
					trlrqstPtr->flight_purpose = 2;
					travelRqsts++;
					crewList[c].travel_request_created = 1;
					if(!gethome_latetest_arr_flag){
						fprintf(logFile,"Can't find latest arrival time before tour end (Overtime included)");
						trlrqstPtr->latest_arr = trlrqstPtr->earliest_dept;
					}
					gethome_latetest_arr_flag = 0;
					trlrqstPtr->rqtindex = travelRqsts;
					hours = difftime(trlrqstPtr->latest_arr, trlrqstPtr->earliest_dept) / 3600;
					if(hours <= optParam.sendhomecutoff + optParam.maxCrewExtension/60){
                       trlrqstPtr->buyticket = 1;
					}
					else 
					   trlrqstPtr->buyticket = 0;
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
					else{
                        trlrqstPtr->groundtravel = 1;
                        crewList[c].noneed_trl_gettinghome = 1;
					}
					if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
						  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
						  exit(1);
						}
				}
			   else
				   crewList[c].noneed_trl_gettinghome =1;  
			}//send crew home
			if(break_indicator == 1){
				if(curr_propmdgleg->schedInAptID != next_propmdgleg->schedOutAptID){
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
							logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
							writeWarningData(myconn); exit(1);
					}
					trlrqstPtr->crewID = crewID;
					trlrqstPtr->dept_aptID_travel = curr_propmdgleg->schedInAptID;
					trlrqstPtr->arr_aptID_travel = next_propmdgleg->schedOutAptID;
					trlrqstPtr->earliest_dept = curr_propmdgleg->schedIn + 60*optParam.postFlightTm;//need to refine later 
					trlrqstPtr->latest_arr = next_propmdgleg->schedOut - 60*optParam.preFlightTm;//need to refine
					trlrqstPtr->off_aircraftID = curr_propmdgleg->aircraftID;
					trlrqstPtr->on_aircraftID = next_propmdgleg->aircraftID;
					trlrqstPtr->flight_purpose = 3;
					travelRqsts++;
					crewList[c].travel_request_created = 1;
					trlrqstPtr->rqtindex = travelRqsts;
					trlrqstPtr->buyticket = 1;
					if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
					else{
                        trlrqstPtr->groundtravel = 1;
					}
					if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
						  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
						  exit(1);
					}
				}
			}//change plane	
			break_indicator = 0;
		}
	}

	//find the crew who needs go to home but no assignments for them during planning window
	for (c=0; c<numCrew; c++){
				if(crewList[c].travel_request_created == 1)
					continue;
                if(crewList[c].endRegDay == PAST_WINDOW)
				    continue;
				if(crewList[c].noneed_trl_gettinghome)
					continue;
				if(crewList[c].availAirportID!=crewList[c].endLoc){
                    if(crewList[c].lastCsTravelLeg)
					   tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, crewList[c].lastCsTravelLeg ->travel_dptTm);
					else
					   tmp_dt2 = 0;
					if(crewList[c].lastCsTravelLeg)
					   tmp_dt3 = DateTimeToTime_t(crewList[c].lastCsTravelLeg->travel_dptTm);
					else
					   tmp_dt3 = 0;
					if((trlrqstPtr = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) {   
						logMsg(logFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
		                writeWarningData(myconn); exit(1);
	                 }
					if(crewList[c].lastCsTravelLeg && tmp_dt2 >=tmp_dt1 && tmp_dt3 > crewList[c].lastActivityLeg_recout){
					   trlrqstPtr->crewID = crewList[c].crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].lastCsTravelLeg->dpt_aptID;
					   trlrqstPtr->arr_aptID_travel = crewList[c].endLoc;
					   trlrqstPtr->earliest_dept = DateTimeToTime_t(tmp_dt2);//need to refine
					   
					}
					else{
					   trlrqstPtr->crewID = crewList[c].crewID;
					   trlrqstPtr->dept_aptID_travel = crewList[c].availAirportID;
					   trlrqstPtr->arr_aptID_travel = crewList[c].endLoc;
					   trlrqstPtr->earliest_dept = crewList[c].availDT;
					}
				    for(s=0; s<=crewList[c].stayLate;s++){
					   if((crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension)> trlrqstPtr->earliest_dept){
                         trlrqstPtr->latest_arr = crewList[c].tourEndTm + 60*(s*24*60)+ 60*optParam.maxCrewExtension;
						 gethome_latetest_arr_flag = 1;
						 break;
					   }       
					}
			       if(crewList[c].lastActivityLeg_flag)
			         trlrqstPtr->off_aircraftID = crewList[c].lastActivityLeg_aircraftID;
				   else 
				     trlrqstPtr->off_aircraftID = 0;
                   trlrqstPtr->on_aircraftID = 0;
			       trlrqstPtr->flight_purpose = 2;
			       travelRqsts++;
			       crewList[c].travel_request_created = 1;
			       if(!gethome_latetest_arr_flag){
				    fprintf(logFile,"Can't find latest arrival time before tour end (Overtime included)");
                    trlrqstPtr->latest_arr = trlrqstPtr->earliest_dept;
			       }
                   gethome_latetest_arr_flag = 0;
				   trlrqstPtr->rqtindex = travelRqsts;
				   hours = difftime(trlrqstPtr->latest_arr, trlrqstPtr->earliest_dept) / 3600;
				   if(hours <= optParam.sendhomecutoff + optParam.maxCrewExtension/60){
					    trlrqstPtr->buyticket = 1;
				   }
				   else
					    trlrqstPtr->buyticket = 0;
				   if(!checkAptNearby(trlrqstPtr->dept_aptID_travel,trlrqstPtr->arr_aptID_travel))
						trlrqstPtr->groundtravel = 0;
				   else{
                        trlrqstPtr->groundtravel = 1;
					}
				   if(!(travelRqstRoot = RBTreeInsert(travelRqstRoot, trlrqstPtr, travelRequestCmp))) {
						  logMsg(logFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
						  exit(1);
				   }
				}	
			}
	return 0;
}




/************************************************************************************************
*	Function	writeProposedSolution						Date last modified:  8/01/06 BGC	*
*	Purpose:	Outputs the proposed solution to the log file.									*
************************************************************************************************/

static int
writeProposedSolution (void)
{
	char opbuf1[1024], opbuf2[1024];
	DateTime dt1, dt2;
	int i, numPropPosLegs;
//	int j, found, index; //03/10/08 ANG
//	int days, hours, minutes, seconds, msecs, timediff;//03/10/08 ANG

	fprintf (logFile, "\n\n*****************************\n");
	fprintf (logFile, "*                           *\n");
	fprintf (logFile, "*     PROPOSED SOLUTION     *\n");
	fprintf (logFile, "*                           *\n");
	fprintf (logFile, "*****************************\n\n");


	fprintf (logFile, "Solution summary:\n");
	fprintf (logFile, "Number of managed legs      = %3d.\n", numPropMgdLegs);
	numPropPosLegs = 0;
	for (i=0; i<numPropMgdLegs; i++)
	{
		if (!propMgdLegs[i].demandID)
			numPropPosLegs ++;
	}
	fprintf (logFile, "Number of positioning legs  = %3d (%2.1f %% of all managed legs).\n", 
		numPropPosLegs, (100 * (double) numPropPosLegs/ (double) numPropMgdLegs));
	fprintf (logFile, "Number of unmanaged legs    = %3d (%2.1f %% of all demand legs).\n\n", 
		numPropUnmgdLegs, 
		(100 * (double) numPropUnmgdLegs/ (double) (numPropMgdLegs + numPropUnmgdLegs - numPropPosLegs)));

	fprintf (logFile, "Num existing tours retained = %d\n", numOptExgTours);
	fprintf (logFile, "Num new tours generated     = %d\n\n", numOptTours);

	fprintf (logFile, "Managed legs:\n");
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	fprintf (logFile, "| Demand ID | Aircraft ID |  PIC ID  |  SIC ID  | Out Apt ID | Out FBO ID |  In Apt ID |  In FBO ID |     Out Time     |     In Time      | ex?|\n");
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	for (i=0; i<numPropMgdLegs; i++)
	{
		dt1 = dt_time_tToDateTime (propMgdLegs[i].schedOut);
		dt2 = dt_time_tToDateTime (propMgdLegs[i].schedIn);
		fprintf (logFile, "|  %6d   |   %6d    |  %6d  |  %6d  |   %6d   |   %6d   |   %6d   |   %6d   | %15s | %15s | %2d |\n",
					propMgdLegs[i].demandID, 
					propMgdLegs[i].aircraftID,
					propMgdLegs[i].captainID,
					propMgdLegs[i].FOID,
					propMgdLegs[i].schedOutAptID,
					propMgdLegs[i].schedOutFBOID,
					propMgdLegs[i].schedInAptID,
					propMgdLegs[i].schedInFBOID,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
					propMgdLegs[i].exgTour
					);
		if ((i < (numPropMgdLegs-1)) && (propMgdLegs[i].captainID != propMgdLegs[i+1].captainID))
		{
			fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
		}

	}
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+\n\n");
	fprintf (logFile, "Crew assignments:\n");

	fprintf (logFile, "+-----------+---------------+----------+--------------------+-------------------+\n");
	fprintf (logFile, "|  Crew ID  |  Aircraft ID  | Position |     Start Time     |     End Time      |\n");
	fprintf (logFile, "+-----------+---------------+----------+--------------------+-------------------+\n");
	for (i=0; i<numPropCrewDays; i++)
	{

		dt1 = dt_time_tToDateTime (propCrewAssignment[i].startTm);
		dt2 = dt_time_tToDateTime (propCrewAssignment[i].endTm);
		fprintf (logFile, "|  %6d   |    %6d     |    %1d     |  %15s  |  %15s |\n",
					propCrewAssignment[i].crewID,
					propCrewAssignment[i].aircraftID,
					propCrewAssignment[i].position,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"));
	}
	fprintf (logFile, "+-----------+---------------+----------+--------------------+-------------------+\n\n");

	fprintf (logFile, "Unmanaged legs:\n");
	fprintf (logFile, "+-----------+------------+------------+------------+------------+------------------+------------------+\n");
	fprintf (logFile, "| Demand ID | Out Apt ID | Out FBO ID |  In Apt ID |  In FBO ID |     Out Time     |     In Time      |\n");
	fprintf (logFile, "+-----------+------------+------------+------------+------------+------------------+------------------+\n");

	for (i=0; i<numPropUnmgdLegs; i++)
	{	
		dt1 = dt_time_tToDateTime (propUnmgdLegs[i].schedOut);
		dt2 = dt_time_tToDateTime (propUnmgdLegs[i].schedIn);
		fprintf (logFile, "|  %6d   |   %6d   |   %6d   |   %6d   |   %6d   | %15s | %15s |\n",
					propUnmgdLegs[i].demandID, 
					propUnmgdLegs[i].departureAptID,
					propUnmgdLegs[i].departureFBOID,
					propUnmgdLegs[i].arrivalAptID,
					propUnmgdLegs[i].arrivalFBOID,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"));
	}
	fprintf (logFile, "+-----------+------------+------------+------------+------------+------------------+------------------+\n\n");

	//Vector print out - 03/10/08 ANG
	/*fprintf (logFile, "Vector legs:\n");
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+-------+\n");
	fprintf (logFile, "| Demand ID | Aircraft ID |  PIC ID  |  SIC ID  | Out Apt ID | Out FBO ID |  In Apt ID |  In FBO ID | Initial Out Time | Proposed Out Time| Flex? |\n");
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+-------+\n");
	for (i=0; i<numPropMgdLegs; i++)
	{
		found = 0;
		index = 0;
		for(j=0; j<numDemand; j++){
			if(demandList[j].demandID == propMgdLegs[i].demandID && demandList[j].contractFlag == 1){
				found = 1;
				index = j;
			}
		}
		if(found == 1 && propMgdLegs[i].demandID > 0){
			dt1 = dt_time_tToDateTime (demandList[index].reqOut);
			dt2 = dt_time_tToDateTime (propMgdLegs[i].schedOut);
			dt_dateTimeDiff(dt1, dt2, &days, &hours, &minutes, &seconds, &msecs);
			timediff = (24 * 60 * days) + (60 * hours) + minutes;
			fprintf (logFile, "|  %6d   |   %6d    |  %6d  |  %6d  |   %6d   |   %6d   |   %6d   |   %6d   | %15s | %15s | %5d |\n",
					propMgdLegs[i].demandID, 
					propMgdLegs[i].aircraftID,
					propMgdLegs[i].captainID,
					propMgdLegs[i].FOID,
					propMgdLegs[i].schedOutAptID,
					propMgdLegs[i].schedOutFBOID,
					propMgdLegs[i].schedInAptID,
					propMgdLegs[i].schedInFBOID,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
					(timediff ? ((dt2 >= dt1) ? timediff : -timediff) : 0));
		}
	}
	fprintf (logFile, "+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+-------+\n\n");
	*/

	return 0;
}

/****************************************************************************************************
*	Function	constructOptimalSchedule	Date last modified:  8/01/06 BGC, updated 02/23/07 SWO	*
*	Purpose:	Constructs the optimal schedule from the optimization output.						*
****************************************************************************************************/

static int
constructOptimalSchedule (void)
{
	numPropCrewDays=0;
	numPropMgdLegs=0;
	numPropUnmgdLegs=0;

	//Calloc upper bounds on all array size
	if ((propCrewAssignment = (ProposedCrewAssg *) calloc (numCrew*7, sizeof (ProposedCrewAssg))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructOptimalSchedule().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	// Upper bound on number of proposed mgd legs is num demand + one repo per demand.
	if ((propMgdLegs = (ProposedMgdLeg *) calloc (2*numDemand, sizeof (ProposedMgdLeg))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructOptimalSchedule().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((propUnmgdLegs = (ProposedUnmgdLeg *) calloc (numDemand, sizeof (ProposedUnmgdLeg))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructOptimalSchedule().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((optTours = (Tour *) calloc (2*numDemand, sizeof (Tour))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructOptimalSchedule().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	if ((optExgTours = (ExgTour *) calloc (numExgTourCols, sizeof (ExgTour))) == NULL)
	{
		logMsg(logFile,"%s Line %d: Out of Memory in constructOptimalSchedule().\n", __FILE__,__LINE__);
		writeWarningData(myconn); exit(1);
	}

	getOptTourPlanes ();

	constructManagedLegs ();

	constructUnmanagedLegs ();

	constructCrewAssignment ();

	if (PURE_CREW_AC_FLAG) constructCrewAssignment2RLZ ();

	if (TRAVEL_REQ_FLAG)  constructTravelRequest ();

	writeProposedSolution ();

	return 0;
}

/************************************************************************************************
*	Function	freeAndNullMemory							Date last modified:  8/01/06 BGC	*
*	Purpose:	Clear all memory.																*
************************************************************************************************/

static int
freeAndNullMemory ()
{
	int i;
	free (demandRowIndex);
	demandRowIndex = NULL;
	free (crewPairRowIndex);
	crewPairRowIndex = NULL;
	for (i=0; i<numBeforeRows; i++)
	{
		free (beforeRowIndex[i]);
		beforeRowIndex[i] = NULL;
	}
	free (beforeRowIndex);
	beforeRowIndex = NULL;
	for (i=0; i<numAfterRows; i++)
	{
		free (afterRowIndex[i]);
		afterRowIndex[i] = NULL;
	}
	free (afterRowIndex);
	afterRowIndex = NULL;
	free (planeRowIndex);
	planeRowIndex = NULL;
	free(charBuf);
	charBuf = NULL;
	free (optSolution);
	optSolution = NULL;
	free (rowInd);
	rowInd = NULL;
	free (rowCoeff);
	rowCoeff = NULL;

	free(demandIndexToRow);
	demandIndexToRow = NULL;
	free(planeIndexToRow);
	planeIndexToRow = NULL;
	free(crewPairIndexToRow);
	crewPairIndexToRow = NULL;

	free (optAircraftID);
	optAircraftID = NULL;

	free (availStart);
	availStart = NULL;

	free (availEnd);
	availEnd = NULL;

	free (isManaged);
	isManaged = NULL;

	for (i=0; i<numPropMgdLegs; i++)
	{
		free (scanned[i]);
		scanned[i] = NULL;
	}
	free (scanned);
	scanned = NULL;

	free (optTours);
	optTours = NULL;

	return 0;
}

/************************************************************************************************
*	Function	showTour									Date last modified:  8/14/06 BGC	*
*	Purpose:	Displays an tour. Used while debugging.  Not currently called.					*
************************************************************************************************/
static int 
showTour (int i)
{
	int j, k, m, repo;
	fprintf (logFile, "\n----------------------------------------------------------------------------\n");
	fprintf (logFile, "Index: %d, Cost: %f, Red Cost: %f, crewArcIndex: %d, crewArcType: %d, crewPairInd: %d, dropPlane: %d, finalApptInd: %d\n", 
		i,
		tourList[i].cost,
		tourList[i].redCost,
		tourList[i].crewArcInd,
		tourList[i].crewArcType,
		tourList[i].crewPairInd, 
		tourList[i].dropPlane, 
		tourList[i].finalApptDemInd);

	if (tourList[i].crewArcType == 1)
	{
		fprintf (logFile, "Aircraft ID: %d, Actype ID: %d\n",
			acList[crewPairList[tourList[i].crewPairInd].crewPlaneList[tourList[i].crewArcInd].acInd].aircraftID,
			acTypeList[crewPairList[tourList[i].crewPairInd].acTypeIndex].aircraftTypeID);
	}

	fprintf (logFile, "Demands: ");
	for (j=0; j<MAX_WINDOW_DURATION; j++)
	{
		if (tourList[i].duties[j] < 0)
			continue;
		repo = dutyList[crewPairList[tourList[i].crewPairInd].acTypeIndex][tourList[i].duties[j]].repoDemandInd;
		for (k=0; k<maxTripsPerDuty; k++)
		{
			if ((m = dutyList[crewPairList[tourList[i].crewPairInd].acTypeIndex][tourList[i].duties[j]].demandInd[k]) < 0)
				break;
			fprintf (logFile, "%d ", demandList[m].demandID);
		}
	}
	fprintf (logFile, "\nLast repo demand ID: %d\n", (repo == -1? 0 : demandList[repo].demandID));
	fprintf (logFile, "----------------------------------------------------------------------------\n\n");
	return 0;
}


/************************************************************************************************
*	Function	showPilot									Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showPilot (int crewInd)
{
	DateTime start, end, dt1;
	char st[1024], en[1024], opbuf1[1024];

	fprintf (logFile, "-----------------------------------------------------------------------\n");
	fprintf (logFile, "CREW DATA\n");
	fprintf (logFile, "Crew index: %d\n", crewInd);	
	fprintf (logFile, "Crew ID: %d\n", crewList[crewInd].crewID);	
	start = dt_time_tToDateTime (crewList[crewInd].tourStartTm);
	end = dt_time_tToDateTime (crewList[crewInd].tourEndTm);
	fprintf (logFile, "Tour start tm: %s\n", dt_DateTimeToDateTimeString(start, st, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Tour end tm: %s\n", dt_DateTimeToDateTimeString(end, en, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Start early: %f\n", crewList[crewInd].startEarly);
	fprintf (logFile, "Stay late: %f\n", crewList[crewInd].stayLate);
	dt1 = dt_time_tToDateTime (crewList[crewInd].availDT);
	fprintf (logFile, "Avail DT: %s\n", dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Avail airport ID: %d\n", crewList[crewInd].availAirportID);
	fprintf (logFile, "Activity code: %d\n", crewList[crewInd].activityCode);
	fprintf (logFile, "Home airport: %d\n", crewList[crewInd].endLoc);
	fprintf (logFile, "-----------------------------------------------------------------------\n");
	return 0;
}

/************************************************************************************************
*	Function	showCrewPair								Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showCrewPair (int cpInd)
{
	int i=0;
	DateTime dtStart, dtEnd, dt1;
	char start[1024], end[1024], opbuf1[1024];

	fprintf (logFile, "-----------------------------------------------------------------------\n");
	fprintf (logFile, "CREW PAIR DATA\n");
	fprintf (logFile, "Crew pair index: %d\n", cpInd);
	fprintf (logFile, "Crew pair ID: %d\n", crewPairList[cpInd].crewPairID);
	fprintf (logFile, "Aircraft IDs: ");
	while (crewPairList[cpInd].aircraftID[i])
	{
		if (crewPairList[cpInd].lockTour[i])
		{
			fprintf (logFile, "%d* ", crewPairList[cpInd].aircraftID[i]);
		}
		else
		{
			fprintf (logFile, "%d ", crewPairList[cpInd].aircraftID[i]);
		}
		i ++;
	}
	fprintf (logFile, "\n");
	fprintf (logFile, "Has flown first: %d\n", crewPairList[cpInd].hasFlownFirst);
	fprintf (logFile, "Opt aircraft ID: %d\n", crewPairList[cpInd].optAircraftID);
	dtStart = dt_time_tToDateTime (crewPairList[cpInd].pairStartTm);
	dtEnd = dt_time_tToDateTime (crewPairList[cpInd].pairEndTm);
	fprintf (logFile, "Pair start tm: %s\n", dt_DateTimeToDateTimeString(dtStart, start, "%Y/%m/%d %H:%M"));	
	fprintf (logFile, "Pair end tm: %s\n", dt_DateTimeToDateTimeString(dtEnd, end, "%Y/%m/%d %H:%M"));	
	fprintf (logFile, "Start day: %d\n", crewPairList[cpInd].startDay);
	fprintf (logFile, "End day: %d\n", crewPairList[cpInd].endDay);
	fprintf (logFile, "End reg day: %d\n", crewPairList[cpInd].endRegDay);
	fprintf (logFile, "Avail Apt ID: %d\n", crewPairList[cpInd].availAptID);
	dt1 = dt_time_tToDateTime (crewPairList[cpInd].availDT);		
	fprintf (logFile, "Avail DT: %s\n", dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"));	
	fprintf (logFile, "Activity code: %d\n", crewPairList[cpInd].activityCode);
	fprintf (logFile, "Duty time: %d\n", crewPairList[cpInd].dutyTime);
	fprintf (logFile, "Block time: %d\n", crewPairList[cpInd].blockTm);
	showPilot (crewPairList[cpInd].crewListInd[0]);
	showPilot (crewPairList[cpInd].crewListInd[1]);
	fprintf (logFile, "-----------------------------------------------------------------------\n");
	return 0;
}



/************************************************************************************************
*	Function	showAircraft								Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showAircraft (int acInd)
{

	char opbuf1[1024];
	DateTime dt1;

	fprintf (logFile, "-----------------------------------------------------------------------\n");
	fprintf (logFile, "AIRCRAFT DATA\n");
	fprintf (logFile, "Aircraft index: %d\n", acInd);
	fprintf (logFile, "Aircraft ID   : %d\n", acList[acInd].aircraftID);
	fprintf (logFile, "Ac Type Index : %d\n", acList[acInd].acTypeIndex);
	fprintf (logFile, "Ac Type ID    : %d\n", acList[acInd].aircraftTypeID);
	dt1 = dt_time_tToDateTime (acList[acInd].availDT);
	fprintf (logFile, "AvailDT       : %s\n", dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Avail Apt ID  : %d\n", acList[acInd].availAirportID);
	fprintf (logFile, "-----------------------------------------------------------------------\n");
	return 0;
}


/************************************************************************************************
*	Function	showLeg								Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showLeg (int legInd)
{
	DateTime dt1, dt2, dt3, dt4, dt5;
	char opbuf1[1024], opbuf2[1024], opbuf3[1024], opbuf4[1024], opbuf5[1024];
	int demInd;

	fprintf (logFile, "-----------------------------------------------------------------------\n");
	fprintf (logFile, "LEG DATA\n");
	fprintf (logFile, "Leg index: %d\n", legInd);
	fprintf (logFile, "Demand ID: %d\n", legList[legInd].demandID);
	fprintf (logFile, "Aircraft ID: %d\n", legList[legInd].aircraftID);
	fprintf (logFile, "Crew pair ID: %d\n", legList[legInd].crewPairID);
	fprintf (logFile, "Out airport ID: %d\n", legList[legInd].outAirportID);
	fprintf (logFile, "In airport ID: %d\n", legList[legInd].inAirportID);
	dt1 = dt_time_tToDateTime (legList[legInd].schedOut);
	dt2 = dt_time_tToDateTime (legList[legInd].schedIn);
	dt3 = dt_time_tToDateTime (legList[legInd].adjSchedIn);
	fprintf (logFile, "Sched out : %s\n", dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Sched in : %s\n", dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"));
	fprintf (logFile, "Adj sched in : %s\n", dt_DateTimeToDateTimeString(dt3, opbuf3, "%Y/%m/%d %H:%M"));

	if (legList[legInd].demandID > 0)
	{
		demInd = getDemandIndex (legList[legInd].demandID);
		dt4 = dt_time_tToDateTime (demandList[demInd].reqOut);
		dt5 = dt_time_tToDateTime (demandList[demInd].reqIn);
		fprintf (logFile, "Req out : %s\n", dt_DateTimeToDateTimeString(dt4, opbuf4, "%Y/%m/%d %H:%M"));
		fprintf (logFile, "Req in : %s\n", dt_DateTimeToDateTimeString(dt5, opbuf5, "%Y/%m/%d %H:%M"));
	}

	fprintf (logFile, "-----------------------------------------------------------------------\n");
	return 0;
}
/************************************************************************************************
*	Function	showExgTourDetail							Date last modified:  8/01/06 BGC	*
*	Purpose:	Displays an existing tour. Used while debugging.  Not currently called.			*
************************************************************************************************/

static int 
showExgTourDetail (int exgTourInd)
{
	int i;
	fprintf (logFile, "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	fprintf (logFile, "EXISTING TOUR INDEX %d\n", exgTourInd);
	showAircraft (exgTourList[exgTourInd].acInd);
	showCrewPair (exgTourList[exgTourInd].crewPairInd);
	for (i=0; i<numLegs; i++)
	{
		if ((legList[i].acInd == exgTourList[exgTourInd].acInd) &&
			(legList[i].crewPairInd == exgTourList[exgTourInd].crewPairInd))
		{
			showLeg (i);
		}
	}
	fprintf (logFile, "***********************************************************************\n\n");
	return 0;
}


/************************************************************************************************
*	Function	showExgTour									Date last modified:  8/01/06 BGC	*
*	Purpose:	Displays an existing tour. Used while debugging									*
************************************************************************************************/

static int 
showExgTour (int i)
{
	int j=0;
	fprintf (logFile, "Aircraft ID: %d \n Cost: %f\n crew pair ID: %d\n droppoff demand ID: %d\n dropoff type: %d\n pickup demand ind: %d \n pickup type: %d \n",
		acList[exgTourList[i].acInd].aircraftID,
		exgTourList[i].cost,
		crewPairList[exgTourList[i].crewPairInd].crewPairID,
		(exgTourList[i].dropoffInd >= 0? demandList[exgTourList[i].dropoffInd].demandID: 0),
		exgTourList[i].dropoffType,
		exgTourList[i].pickupInd,
		exgTourList[i].pickupType);
	fprintf (logFile, "Demands: ");
	while (exgTourList[i].demandInd[j] >= 0)
	{
		fprintf (logFile, "%d ", demandList[exgTourList[i].demandInd[j]].demandID);
		j ++;
	}
	fprintf (logFile, "\n");
	return 0;
}


/************************************************************************************************
*	Function	checkExgTourFeasibility						Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
checkExgTourFeasibility (int exgTourInd)
{

	int n=0, i, legs[MAX_LEGS], j;
	for (i=0; i<MAX_LEGS; i++)
	{
		if (exgTourList[exgTourInd].demandInd[i] < 0)
		{
			break;
		}
		for (j=0; j<numLegs; j++)
		{
			if (legList[j].demandInd == exgTourList[exgTourInd].demandInd[i])
			{
				legs[n] = j;
				break;
			}
		}
		n ++;
	}
	// n is the number of demands, legs[] contains the corresponding scheduled leg.
	
	return 0;
}


/************************************************************************************************
*	Function	checkExgToursFeasibility					Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
checkExgToursFeasibility (void)
{

	int i;
	for (i=0; i<numExgTours; i++)
	{
		checkExgTourFeasibility (i);
	}
	return 0;
}



/************************************************************************************************
*	Function	showExistingTours							Date last modified:  8/XX/06 BGC	*
*	Purpose:	For debugging. Not currently called.											*
************************************************************************************************/
static int
showExistingTours (void)
{
	int i;
	for (i=0; i<numExgTours; i++)
	{
		showExgTourDetail (i);
	}
	return 0;
}

/************************************************************************************************
*	Function	getOptimalSchedule							Date last modified:  8/01/06 BGC	*
*	Purpose:	Entry point for scheduleSolver.													*
************************************************************************************************/

int
getOptimalSchedule ()
{
	//int i, j;
	//for (i=0; i<numDemand; i++)
	//{
	//	fprintf (logFile, "\nDemand ID: %d\n", demandList[i].demandID);
	//	fprintf (logFile, "Ac type ID: %d\n", demandList[i].aircraftTypeID);
	//	for (j=0; j<numAcTypes; j++)
	//	{
	//		fprintf (logFile, "Elapsed time[%d]: %d\n", j, demandList[i].elapsedTm[j]);
	//		fprintf (logFile, "Block time[%d]: %d\n", j, demandList[i].blockTm[j]);
	//	}
	//}


	qsort((void *) ownerList, numOwners, sizeof(Owner), compareOwners);
	// Sorted owner list by ownerID for searching later.
	//logMsg (logFile, "** Going to build existing tours.\n");
	//buildExistingTours();

	//showExistingTours ();

	logMsg (logFile, "** Starting optimization.\n");
	optimizeIt ();
	logMsg (logFile, "** Optimization complete... preparing output.\n");
	constructOptimalSchedule ();	
	logMsg (logFile, "** Proposed schedule generated.\n");

	if(optParam.autoFlyHome == 1){// 11/21/07 ANG
		printCrewEndTourSummary(); 
	}

	//printMaintList();//for debug - 04/22/08 ANG

	freeAndNullMemory ();
	return 0;
}

/************************************************************************************************
*	Function	checkAptNearby							Date last modified:  12/15/08 Jintao	*
*	Purpose:	Check if two airports are clost to each other.													*
************************************************************************************************/
int checkAptNearby(int aptid1, int aptid2)
{ 
  int x;
  x = 0;
  while(x < aptList[aptid1].numMaps){
	if(aptList[aptid1].aptMapping[x].airportID == aptid2 && aptList[aptid1].aptMapping[x].duration <=60){
	   return 1;
	}
	x++;
  }
  return 0;
}



static int travelRequestCmp(void *a1, void *b1)
{
	TravelRequest *a = (TravelRequest *) a1;
	TravelRequest *b = (TravelRequest *) b1;
	int ret;

	if(ret = a->rqtindex - b->rqtindex)
		return(ret);
	return(0);
}