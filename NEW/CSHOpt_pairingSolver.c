#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "datetime.h"
#include "logMsg.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_pairCrews.h"
#include "CSHOpt_pairingSolver.h"
#include "CSHOpt_output.h"

#include <ilcplex/cplex.h>

extern FILE *logFile;
extern MY_CONNECTION *myconn;
extern struct optParameters optParam;

static int *uniquePilots;
static int *uniqueAC;
static int numUniquePilots;
static int numUniqueAC;

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
*	Function	compareInts									Date last modified:  6/21/06 BGC	*
*	Purpose:	Compares two integers for qsort.												*
************************************************************************************************/

int compareInts (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}

/************************************************************************************************
*	Function	getUniquePilots								Date last modified:  6/21/06 BGC	*
*	Purpose:	Given the set of pilot IDS, creates an indexed list	for model building.			*
************************************************************************************************/

static int *
getUniquePilots (MatchingArc *matchingArcs, int numMatchingArcs, int *numUniquePilots)
{
	MatchingArc *temp;
	int * uniquePilots, last=0, *allPilots, i;

	if ((allPilots = (int *) calloc (2*numMatchingArcs, sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in getUniquePilots().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		
	temp = matchingArcs;
	while (temp)
	{
		allPilots[last] = temp->p1;
		last ++;
		allPilots[last] = temp->p2;
		last ++;
		temp = temp->next;
	}

	qsort((void *) allPilots, 2*numMatchingArcs, sizeof(int), compareInts);

	// List is not sorted by crew ID.

	(*numUniquePilots) = 0;
	for (i=0; i<2*numMatchingArcs-1; i++)
	{
		if (allPilots[i] != allPilots[i+1])
			(*numUniquePilots) ++;
	}
	(*numUniquePilots) ++;

	if ((uniquePilots = (int *) calloc ((*numUniquePilots), sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in getUniquePilots().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}	

	last = 0;
	for (i=0; i<2*numMatchingArcs-1; i++)
	{
		if (allPilots[i] != allPilots[i+1])
		{
			uniquePilots[last] = allPilots[i];
			last ++;
		}
	}
	uniquePilots[last] = allPilots[numMatchingArcs*2-1];

	free (allPilots);
	allPilots = NULL;

	return uniquePilots;
}

/************************************************************************************************
*	Function	getUniqueAC									Date last modified:  6/21/06 BGC	*
*	Purpose:	Given the set of ircraft IDs, creates an indexed list				 			*
*				for model building.																*
************************************************************************************************/

static int *
getUniqueAC (MatchingArc *matchingArcs, int numMatchingArcs, int *numUniqueAC)
{
	MatchingArc *temp;
	int * uniqueAC, last=0, *allAC, i;

	if ((allAC = (int *) calloc (numMatchingArcs, sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in getUniqueAC().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		
	temp = matchingArcs;
	while (temp)
	{
		allAC[last] = temp->ac;
		last ++;
		temp = temp->next;
	}

	qsort((void *) allAC, numMatchingArcs, sizeof(int), compareInts);

	// List is not sorted by aircraft ID.

	(*numUniqueAC) = 0;
	for (i=0; i<numMatchingArcs-1; i++)
	{
		if (allAC[i] != allAC[i+1])
			(*numUniqueAC) ++;
	}
	(*numUniqueAC) ++;

	if ((uniqueAC = (int *) calloc ((*numUniqueAC), sizeof (int))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in getUniqueAC().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}	

	last = 0;
	for (i=0; i<numMatchingArcs-1; i++)
	{
		if (allAC[i] != allAC[i+1])
		{
			uniqueAC[last] = allAC[i];
			last ++;
		}
	}
	uniqueAC[last] = allAC[numMatchingArcs-1];

	free (allAC);
	allAC = NULL;

	return uniqueAC;
}

/************************************************************************************************
*	Function	getIndex									Date last modified:  6/21/06 BGC	*
*	Purpose:	Given a sorted array of integers, returns the index of a key using binary		*
*				search. Borrowed code from Harry's airportLatLon.c.								*
************************************************************************************************/

static int
getIndex (int key, int *vector, int n)
{
	int low, high, mid, cond;

	low = 0;
	high = n - 1;

	while (low <= high) {
		mid = low + (high - low) / 2;
		if ((cond = (key - vector[mid])) < 0)
			high = mid -1;
		else if (cond > 0)
			low = mid + 1;
		else {
			return mid;
		}
	}
	return -1;
}

/************************************************************************************************
*	Function	optimizeIt									Date last modified:  6/21/06 BGC	*
*	Purpose:	Computes the optimal matching.													*
************************************************************************************************/

static int
optimizeIt (MatchingArc *matchingArcs, int numMatchingArcs, int *optMatching)
{

	CPXENVptr env = NULL;
	CPXLPptr lp = NULL;
	int status, numRows, i, zeroInt=0;
	double *rhs, benefit, zeroDbl=0, oneDbl=1, *solution;
	MatchingArc *temp;
	char *sense  = NULL, *ctype;
	char errmsg[1024];
	int index[3];
	//int index[2]; //HWG
	double coeff[3]; 
	//double coeff[2]; //HWG

	CPXFILEptr cplexLogFile;

	coeff[0] = 1; coeff[1] = 1; coeff[2] = 1;
	//coeff[0] = 1; coeff[1] = 1; //coeff[2] = 1; //HWG

	env = CPXopenCPLEX (&status); // Cplex started. Checks for license, etc.
	if ( env == NULL ) {
		logMsg(logFile, "Could not open CPLEX environment.\n");
		CPXgeterrorstring (env, status, errmsg);
		logMsg(logFile,"%s", errmsg);
		writeWarningData(myconn); exit(1);
	}
	cplexLogFile = CPXfopen ("./Logfiles/PairingCPLEXLog.txt", "w");
	status = CPXsetlogfile (env, cplexLogFile);
	if ( status ) {
		fprintf (stderr, "Failure to set log file, error %d.\n", status);
		writeWarningData(myconn); exit(1);
	}

	lp = CPXcreateprob (env, &status, "PilotPairing"); // P\Empty problem instance created.
	if ( lp == NULL ) {
		logMsg(logFile, "Failed to create LP.\n");
		writeWarningData(myconn); exit(1);
	}

	numRows = (numUniquePilots + numUniqueAC);
	//numRows = numUniquePilots; // 12/11/2009 HWG

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
	for (i=0; i<numRows; i++) 
	{
		sense[i] = 'L';
		rhs[i] = optParam.pairingModelRHS; //12/10/2009 ANG
		//rhs[i] = 1;
	}
	status = CPXnewrows (env, lp, numRows, rhs, sense, NULL, NULL);
	// Added numRows. All are <= 1 constraints.

	if (status)
	{
		logMsg(logFile, "CPLEX failed to create rows.\n");
		CPXgeterrorstring (env, status, errmsg);
		logMsg(logFile,"%s", errmsg);
		writeWarningData(myconn); exit(1);
	}

	temp = matchingArcs;

	while (temp)
	{
		index[0] = getIndex (temp->p1, uniquePilots, numUniquePilots);
		index[1] = getIndex (temp->p2, uniquePilots, numUniquePilots);
		index[2] = numUniquePilots + getIndex (temp->ac, uniqueAC, numUniqueAC); //HWG
		benefit = round(temp->benefit);

		#ifdef DEBUGGING // Check will be executed only if run in debug mode.
		status = CPXcheckaddcols (env, lp, 1, 3, &benefit, &zeroInt, index, coeff,  &zeroDbl, &oneDbl, NULL);
		//status = CPXcheckaddcols (env, lp, 1, 2, &benefit, &zeroInt, index, coeff,  &zeroDbl, &oneDbl, NULL); // More Crew Pair - 12/10/2009 ANG
		if (status)
		{
			logMsg(logFile, "Column error.\n");
			CPXgeterrorstring (env, status, errmsg);
			logMsg(logFile,"%s", errmsg);
			writeWarningData(myconn); exit(1);
		}
		#endif

		status = CPXaddcols (env, lp, 1, 3, &benefit, &zeroInt, index, coeff,  &zeroDbl, &oneDbl, NULL);
		//status = CPXaddcols (env, lp, 1, 2, &benefit, &zeroInt, index, coeff,  &zeroDbl, &oneDbl, NULL); // 12/11/2009 HWG
		/* 
		*	Adds a column for each matching arc. Each column has 3 non-zeros --- two pilots and one aircraft.
		*/
		if (status)
		{
			logMsg(logFile, "CPLEX failed to create column.\n");
			CPXgeterrorstring (env, status, errmsg);
			logMsg(logFile,"%s", errmsg);
			writeWarningData(myconn); exit(1);
		}
		temp = temp->next;
	}

	CPXchgobjsen (env, lp, CPX_MAX); // Defines the problem as a maximization problem.

	if ((ctype = (char *) calloc (CPXgetnumcols(env,lp), sizeof (char))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}		

	for (i=0; i<CPXgetnumcols(env,lp); i++)
	{
		ctype[i] = 'B';
	}

	status = CPXcopyctype (env, lp, ctype);
	if ( status ) {
		fprintf (stderr, "Failed to copy ctype\n");
		CPXgeterrorstring (env, status, errmsg);
		logMsg(logFile,"%s", errmsg);
		writeWarningData(myconn); exit(1);
	}

	status = CPXsetintparam (env, CPX_PARAM_CLIQUES, 2);
	status = CPXsetintparam (env, CPX_PARAM_MIPEMPHASIS, CPX_MIPEMPHASIS_OPTIMALITY);
	status = CPXsetintparam (env, CPX_PARAM_FRACCUTS, 2);
	status = CPXsetdblparam (env, CPX_PARAM_TILIM, 60);
	status = CPXsetdblparam (env, CPX_PARAM_EPGAP, 0.001);

	status = CPXmipopt (env, lp); // Solves MIP.
	if (status) {
		logMsg(logFile, "Failed to optimize MIP.\n");
		CPXgeterrorstring (env, status, errmsg);
		logMsg(logFile,"%s", errmsg);
		writeWarningData(myconn); exit(1);
	}
/*
	CPXgetchannels(env, NULL, NULL, NULL, &logChannel);
	CPXmsg(logChannel, "\n\n");
	CPXmsg(logChannel, "+--------------------------+\n");
	CPXmsg(logChannel, "| CPLEX parameters:        |\n");		
	CPXmsg(logChannel, "+--------------------------+\n");
	CPXmsg(logChannel, "| Cliques    = 3           |\n");
	CPXmsg(logChannel, "| Frac Cuts  = 2           |\n");
	CPXmsg(logChannel, "| Emphasis   = Optimality  |\n");
	CPXmsg(logChannel, "| Time limit = 60 seconds  |\n");
	CPXmsg(logChannel, "| MIP gap    = 0.1%%        |\n");
	CPXmsg(logChannel, "+--------------------------+\n");
*/
	CPXfclose (cplexLogFile);

	if ((solution = (double *) calloc (numMatchingArcs, sizeof (double))) == NULL)
	{
			logMsg(logFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
			writeWarningData(myconn); exit(1);
	}


	status = CPXgetmipx (env, lp, solution, 0, numMatchingArcs-1);
	// Optimal solution is now stored in the vector "solution".
	if (status) {
		logMsg(logFile, "Failed to recover optimal solution.\n");
		CPXgeterrorstring (env, status, errmsg);
		logMsg(logFile,"%s", errmsg);
		writeWarningData(myconn); exit(1);
	}


	for (i=0; i<numMatchingArcs; i++)
	{
		if (solution[i] > 0.5)
		{
			optMatching[i] = 1;
		}
		else
		{
			optMatching[i] = 0;
		}
		// Rounds the optimal solution (CPLEX solves to within a little bit of optimality and integrality. 
	}


	if ( lp != NULL ) {
		status = CPXfreeprob (env, &lp); // Free memory.
		if ( status ) {
			logMsg (logFile, "CPXfreeprob failed, error code %d.\n", status);
		}
	}
	
	if ( env != NULL ) {
		status = CPXcloseCPLEX (&env); // Discard license.
		
		if ( status ) {
			logMsg (logFile, "Could not close CPLEX environment.\n");
			CPXgeterrorstring (env, status, errmsg);
			logMsg (logFile, "%s", errmsg);
		}
	}

	free (rhs);
	free (sense);
	free (solution);
	free(ctype);
	ctype = NULL;
	solution = NULL;
	sense = NULL;
	rhs = NULL;
	return 0;
}

/************************************************************************************************
*	Function	clearMemory									Date last modified:  6/21/06 BGC	*
*	Purpose:	Clears memory allocated by calloc.												*
************************************************************************************************/
static int
clearMemory ()
{
	free (uniquePilots);
	free (uniqueAC);
	uniquePilots = NULL;
	uniqueAC = NULL;
	return 0;
}

/************************************************************************************************
*	Function	getOptimalMatching							Date last modified:  6/21/06 BGC	*
*	Purpose:	Computes the optimal matching.													*
************************************************************************************************/

int
getOptimalMatching (MatchingArc *matchingArcs, int numMatchingArcs, int *optMatching)
{
	uniquePilots = getUniquePilots (matchingArcs, numMatchingArcs, &numUniquePilots);
	uniqueAC = getUniqueAC (matchingArcs, numMatchingArcs, &numUniqueAC);
	logMsg (logFile, "** Start CPLEX.\n");
	optimizeIt (matchingArcs, numMatchingArcs, optMatching);
	logMsg (logFile, "** End CPLEX.\n");
	clearMemory ();

	return 0;
}