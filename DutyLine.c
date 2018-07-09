#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <crtdbg.h>
#include "logMsg.h"
#include <string.h>
#include "datetime.h"
#include "bintree.h"
#include "localTime.h"
#include "CSHOpt_struct.h"
#include "CSHOpt_processInput.h"//getFlightTime
#include "CSHOpt_dutyNodes.h"//getRepoArriveTm
#include "CSHOpt_scheduleSolver.h"//checkAptNearby
#include "CSHOpt_pairCrews.h"//checkIfXlsPlus, checkIfCj4
#include "CSHOpt_callOag.h"
#include "DutyLine.h"
#include <ilcplex/cplex.h>

//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------control parameters
#define DL_MAX_NUM_AC_PER_DUTY 2 /*maximum number of ac's assigned to a duty in the opt solution */
#define DL_MAX_NUM_TEMP_DATA 2
#define DL_MAX_NUM_TRAVEL_PER_PILOT 2

#define DL_F_CHECK_DATA_DEBUG /*check errors in the data structure */
#define DL_F_PRINT_DATA_DEBUG /*print all used datas structure*/
#define DL_F_SOLUTION_DEBUG /*check and print the solution*/

//#define DL_F_LP_WRITE_DEBUG /*write the LP to a file*/
//#define DL_F_LP_COL_CHECK_DEBUG /*checl a column when adding it*/

//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------control constants
static const time_t RegCrewStartTm = 13*3600 ; //seconds, 1pm gmt //for crewing interval
static const time_t RegCrewEndTm = 22*3600 ; //seconds, 10pm gmt //for crewing interval
static const double CrewCostPerHour = -10.0; //crew interval intersecting cost //for alligning the crew interval to [RegCrewStartTm, RegCrewEndTm]
static const double RHSDem = 5.0; //temp for dem coverage //righ hand side of the covering constraints
static const double PenaltyDem = 1000.0; //temp penalty for an uncovered demand, 
static const double RHSRegPerDay = 10.0; //temp for region coverage //righ hand side of the covering constraints
static const double PenaltyRegPerDay = 1000.0; //temp penalty for an uncovered region
static const double PenaltyAcPerDay = 1600.0; //penalty for an uncovered ac on a day, 
static const double PenaltyMacPerDay = 1600.0; //penalty for an uncovered mac on a day, 
static const time_t DiscrtCrewInterval = 3600;//discretize the crewing time interval, seconds
static const time_t DiscrtLPInterval = 600; //discretize connection time in the LP, seconds
static const time_t MinCrewInterval = 4*3600;//minimal crewing interval to crew an ac, seconds
static const time_t RecoverDeptDelay = 3*3600;//daparture time delay for contingancy
static const time_t MaxTimeT = 2147483646; //max of the time_t

//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------external
extern OptParameters optParam;
extern FILE *dlFile;
extern time_t firstEndOfDay;
extern int month;
extern int withOag;
extern char logFileName[512];//main.c
extern DateTime dt_run_time_GMT; 

extern Crew *crewList;
extern int numCrew;
extern Demand *demandList;
extern int numDemand;
extern ProposedMgdLeg *propMgdLegs ;
extern int numPropMgdLegs ;
extern CrewPair *crewPairList;
extern int numCrewPairs;
extern Aircraft *acList; //may update available location in getLegsCP()
extern int numAircraft;
extern AircraftType *acTypeList;
extern int numAcTypes;
extern OrigDemInfo *origDemInfos; //original demand list
extern int numOrigDem;
extern time_t *outTimes ;//will be updated in this file
extern time_t *inTimes ;//will be updated in this file
extern ExgTour *optExgTours ;
extern int numOptExgTours ;
extern int *optSolution ;

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------data
typedef enum 
{
	DL_FromExist=0, //existing opt tours
	DL_FromNew, //opt tours that are not existing
	DL_NewAvail //new tour to crew ac
} DL_TourStartType ;

typedef enum 
{
	DL_NotAvail=0,
	DL_AfterLeg,
	DL_AfterLegMidDuty,
	DL_AfterAppCol,
	DL_AfterAvail
} DL_AvailType ; //after which an ac/pilot is available

typedef enum 
{
	DL_Pilot_Status_Active=0,
	DL_Pilot_Status_Rest_B4Leg,
	DL_Pilot_Status_Rest_B4FirstLeg
} DL_PilotStatus ; 

typedef enum 
{
	DL_DemNotUsed=0,
	DL_DemInNewTour,
	DL_DemInExgTour,
	DL_DemInAppCol
} DL_DemUsedType ; //for each demand in the demand list

typedef enum 
{
	DL_KO_NotKeep=0,
	DL_KO_TmFixed, //start time AND end time are fixed
	DL_KO_Combined, //duty is combined to another duty (on the same day)
	DL_KO_DutyTime, //max duty time violates
	DL_KO_NoTravel //travel to duty not feasible
} DL_KeepOriginalType ; //not adjusting a duty //type on an adjusted duty ( a colum in the LP )

typedef enum 
{
	DL_NotFixed=0,
	DL_StartTmFixed,
	DL_EndTmFixed,
	DL_BothTmFixed,
	DL_DutyCombined 
} DL_TmFixType ; //fixing start or end time of a duty //type on a duty before adjustment ( original duty )

typedef enum 
{
	DL_Row_StartInfo = 0,
	DL_Row_StartInfo_Con, //connecting each day of a tour
	DL_Row_StartInfo_ConTm, //connecting time each day of a tour
	DL_Row_Pilot, //available pilots
	DL_Row_PilotTm, //available time
	DL_Row_Ac,
	DL_Row_AcTm,
	DL_Row_OptDuty, //cover each original duty
	DL_Row_CrewedAc, //crew each available ac
	DL_Row_CvdDem, //cover demands
	DL_Row_CvdReg, //cover regions
	DL_Row_Total 
} DL_RowType ; //type of rows in the LP //must update NumRowTypes if add new row types

typedef enum 
{
	DL_Col_LP = 0, 
	DL_Col_Dem, //uncovered dem penalty
	DL_Col_Ac,  //uncrewed ac penalty
	DL_Col_Total
} DL_ColType ;//type of cols in the LP  //must update NumColTypes if add new row types

typedef struct dl_LegInfo
{
	int legIndCP; //index in proposedLegs (sorted by crew pair index )
	int acInd ; 
	int day ; //day of the duty that this leg is contained
	int schOutDay ; //day of schedule out time 
	int schInDay ;
	const ProposedMgdLeg *leg ; //original leg
	struct dl_LegInfo *prevPilotLegs[2] ; //previous leg of the same pilot, for each pilot
	struct dl_LegInfo *nextPilotLegs[2] ; 
	struct dl_LegInfo *prevAcLeg ; //previous leg of the same ac
	struct dl_LegInfo *nextAcLeg ;
} DL_LegInfo ;

typedef struct dl_AvailInfo
{
	int ind ; //ac or pilot index
	DL_PilotStatus status;
	DL_AvailType availAfter; //available after 
	time_t tm; //available time
	int day; //available day
	int apt ;//available airport
	time_t dutyStartTm; //for pilot
	//time_t latestLegEndB4HTravel; //latest leg end time before any feasible home travels, initialized to be (tour end time - post flight time)
	int inLegs; //ac or pilot used in original solution 
} DL_AvailInfo ;//available info of an ac or pilot

//orignal duty (current cp-ac-duty, from the opt solution)
typedef struct dl_OrigDuty
{
	int dutyNum;//temp
	int acInd[DL_MAX_NUM_AC_PER_DUTY];
	int acEndLeg[DL_MAX_NUM_AC_PER_DUTY];
	int numAc;
	DL_LegInfo *firstLegP ;
	DL_LegInfo *lastLegP ;
	DL_TmFixType tmFixed ;
} DL_OrigDuty ;//for each crew pair on each day

typedef struct dl_TourStartInfo
{
	int index; //index in the list of DL_TourStartInfo
	DL_TourStartType type ; //tour type
	int cpInd ;
	time_t firstLegStart ; //start time of the first leg, 0 if no legs

	DL_AvailInfo ac, pilots[2];

	int acCpDay ; //ac and cp available day
	int legStartDay, legEndDay, lastPossibleDay ; 

	time_t latestLegEndB4HTravel ;//latest leg end time (duty end time - post flight time) before any feasible home travel start time
	// == MaxTimeT if both pilots are not going home after current tour and within the window

	//struct dl_TourStartInfo* nextAcOrigTour;
	//struct dl_TourStartInfo* nextPilotOrigTour[2];
} DL_TourStartInfo ; //for each tour (orig opt tour, or new crew-ac tour)

typedef struct dl_NewDuty //adjusted duty
{
	const DL_TourStartInfo *startInfoP; //tour start info of the tour containing this duty
	const DL_OrigDuty *origDutyP; //original duty (before adjustment, in the original opt solution )

	time_t startTm; //leg start, no pre flight time
	time_t endTm; //leg end, no post flight time
	time_t pilotStartTm[2] ; 
	time_t pDutyStartTm[2] ; //pilot duty start time before "startTm"

	//temp
	int acInd; //first ac assigned to this duty
	int acAirport; //available airport of "acInd"
	time_t acTm; //available time of "acInd"
	time_t pTms[2]; //pilot available time
	int pAirports[2]; //pilot available airports
	
	DL_KeepOriginalType keepOriginal; //keep it the same as the original 
	double cost; //cost of feasible travels to this duty
	int firstDay; //day of the first duty of the tour containing this duty
	int day; //day of this duty

	int *coverDems; //indices of demands covered by this duty ( index in demand list )
	int numCoverDems; 
	double *coverDemCoefs; //cost to cover each demand (cost of the repo leg to the demand )
	int *coverRegions;
	int numCoverRegions;
	double *coverRegionCoefs;
} DL_NewDuty;

typedef struct dl_LpColumn
{
	DL_NewDuty *newDutyP; 
	int fDuty; //whether this is the first duty of the tour
	int dropOff; //whether drop off to another tour
	int nextDay; //whether drop off to the next duty of the same tour
} DL_LpColumn ;

//temp holding data for generating a feasible new duty
typedef struct dl_DataOneDay
{
	const DL_TourStartInfo *startInfoP;
	const DL_OrigDuty *origDutyP;

	//interval for start and end time
	time_t eStartTm, lStartTm, eEndTm, lEndTm;
	
	int acInd, acAirport, pAirports[2];
	time_t acTm, pTms[2], pilotStartTm[2];
	
	DL_KeepOriginalType keepOriginal;
	int firstDay; 
	int day; //current day
} DL_DataOneDay;

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------log file
FILE *dlFile = NULL ;

//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------constant
static const int Minute = 60 ;
static const int Hour = 3600 ;
static const int DayInSecs = 24*3600 ;
static const int DefaultAllocSize = 128;
static const int NewDutyDefaultAllocSize = 16;//allocate new duties
static const int LPColDefaultAllocSize = 2048;//allocate LP columns
static const int CvdIndAllocSize = 5;//allocate covered dem/regions
static const int NumRowTypes = 12;
static const int NumColTypes = 4;
	
//----------------------------------------------------------------------------------------------------------------------------------------------------------compare functions used to sort
	static int compareTourStartInfo (const DL_TourStartInfo *a, const DL_TourStartInfo *b);//first leg start time, acInd, cpInd
	static int compareLpColumn (const DL_LpColumn *a, const DL_LpColumn *b);
	static int compareLpColumnTm (const DL_LpColumn *a, const DL_LpColumn *b);
	static int compareMgdLegsAC (const ProposedMgdLeg *a, const ProposedMgdLeg *b);//acID, start time
	static int compareLegInfoCP (const DL_LegInfo *a, const DL_LegInfo *b);
	static int compareDlColumn (const DL_NewDuty *a, const DL_NewDuty *b);
	static int compareCrewAssign (const ProposedCrewAssg *a, const ProposedCrewAssg *b);
	static int travelRequestCmp(void *t, void *r) ;

//---------------------------------------------------------------------------------------------------------------------------------------------allocate functions used to allocate memory
	static DL_NewDuty *allocANewDuty(DL_NewDuty **newDutiesP, int *countP);
	static DL_LpColumn *allocAnLpColumn(DL_LpColumn **lpColsP, int *countP);
	static DL_TourStartInfo *allocAStartInfo(DL_TourStartInfo **tourStartInfosP, int *countP);
	static int *allocCvdDemForCL( DL_NewDuty *inP);

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------utility functions
	static int timeToDay(const time_t timeT) ; //covert a time to day in the planning horizon
	static int acIDToInd(const int acID) ;//convert an ac ID to ac index in the ac list
	//given a duty consisting of legs [sInd, eInd] in the list legInfosCP, return the day of this duty
	static int getDayFromMgdLegs(const DL_DemUsedType *dlDemUsed, const DL_LegInfo *legInfosCP, const int sInd, const int eInd, int *dayP);
	//given a duty consisting of legs [sInd, eInd] in the list legInfosCP, divide these legs according to aircrafts
	static int getAcIndFromMgdLegs(const DL_LegInfo *legInfosCP,  const int sInd, const int eInd, int *acInd, int *acEndLeg, int *numAcP ) ;
	static time_t getCurMaxDutyTm(const int firstDuty, const time_t curTime, const int curApt );//get maximum duty time (accroding to crew fatigue rules)
	static double getCrewIntervalCost(const time_t tZero, const time_t tOne) ;//get cost of the interval [tZero, tOne] intersecting with "normal" crew duty interval
	static int copyNewDuty ( DL_NewDuty *destP, const DL_NewDuty *origP );
	static int freeNewDuty( DL_NewDuty *origP );
	static int isFeasibleDQPilotsAndAc(const int cpInd, const int acInd); 

//--------------------------------------------------------------------------------------------------------------------------------------------------procedure declaration and dependency
	//LEVEL 0, COLUMN 0
	static int getDemUsed(DL_DemUsedType **dlDemUsedP); //get demands that are in the original opt solution
			//LEVEL 1, COLUMN 0
			static int getLongMaintlInfo(const DL_DemUsedType *dlDemUsed, int **longMaintP, int *numLongMaintP);//get list of long maintenance: long to re-assign
			static int printLongMaintlInfo(const int *longMaint, const int numLongMaint) ;
		
	//LEVEL 0, COLUMN 1
	static int getLegsCP( DL_LegInfo **legInfosCPP );//re-generate legs
			//LEVEL 1, COLUMN 1
			static int checkLegsCP(const DL_LegInfo *legInfosCP) ;
			static int printManagedLegs(const DL_LegInfo *legInfosCP ) ;

	//LEVEL 1, COLUMN 2 //depend on (0,0), (0,1)
	static int getOriginalDuties(const DL_DemUsedType *dlDemUsed, DL_LegInfo *legInfosCP, DL_OrigDuty ***origDutiesP ) ;
		//LEVEL 2, COLUMN 2
		static int checkOrigDuties(const DL_OrigDuty **origDuties, const DL_LegInfo *legInfosCP) ;
		static int printOrigDuties(const DL_OrigDuty **origDuties) ;

	//LEVEL 2, COLUMN 3 //depend on (0,0), (0,1), (1,0), (1,2)
	static int getTourStartInfo(const DL_DemUsedType *dlDemUsed, const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const int *longMaint
	, const int numLongMaint, DL_TourStartInfo **tourStartInfosP, int *numStartInfoP, DL_AvailInfo **availAcsP, DL_AvailInfo **availPilotsP);

		//LEVEL 3, COLUMN 3
		static int checkTourStartInfos(const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, 
		const int numStartInfo, const DL_AvailInfo *availAcs, const DL_AvailInfo *availPilots );

		static int printTourStartInfos( const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_AvailInfo *availAcs
		, const DL_AvailInfo *availPilots);

	//LEVEL 2, COLUMN 4
	static int getNewDutiesWithTempData(DL_DataOneDay* tmpDataP, const int numTmpData, DL_NewDuty **newDutiesP, int *numNewDutiesP) ;

		//LEVEL 3, COLUMN 4
		static int getNewDutiesOnADay(const int* longMaint, const int numLongMaint, const DL_OrigDuty **origDuties, const DL_TourStartInfo *startP, const int dayZero
		, const int dayOne, DL_NewDuty **newDutiesP, int *numNewDutiesP) ;

			//LEVEL 4, COLUMN 4 //depend on (1,2), (2,3)
			static int getNewDuties(const int* longMaint, const int numLongMaint, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos
			, const int numStartInfo, DL_NewDuty *****ndP, int ****numNDP);

				//LEVEL 5, COLUMN 4 //depend on (0,1), (1,2), (2,3)
				static int checkNewDuties(const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, const int numStartInfo
				, const DL_NewDuty ****ndP, const int ***numNDP);

				static int printNewDuties( const DL_NewDuty ****newDuties, const int ***numNewDuties, const int numStartInfo);


	//LEVEL 5, COLUMN 5 //depend on (0,1), (1,2), (2,3), (4,4)
	static int buildAndSolve (const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_AvailInfo *availAcs
	, const DL_AvailInfo *availPilots, DL_NewDuty ****ndP, const int ***numNDP, const DL_LegInfo *legInfosCP, ProposedCrewAssg **crewAssignP, int *numCrewAssignP
	, BINTREENODE **travelsRootP, int *numTravelsP);

		//LEVEL 6
		static int returnCrewAssigns ( const DL_LegInfo *legInfosCP, const DL_LpColumn *pickedLpCols, const int numPickedLpCols, ProposedCrewAssg **crewAssignP
		, int *numCrewAssignP);

		static int returnTravelRequests ( const DL_OrigDuty **origDuties, DL_LpColumn *pickedLpCols, const int numPickedLpCols, BINTREENODE **travelsRootP, int *numTravelsP );

		static int printSolution ( const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_OrigDuty **origDuties 
		, const DL_LegInfo *legInfosCP, const DL_LpColumn *pickedLpCols, const int numPickedLpCols, const DL_AvailInfo *availAcs
		, const DL_AvailInfo *availPilots, const int *colStartInd, const int *dColToDInd, const int *aColToAInd, const double *x, const double obj);




//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------PROCEDURES
int dutyLine(ProposedCrewAssg **crewAssignP, int *numCrewAssignP, BINTREENODE **travelsRootP, int *numTravelsP)
{
	int i, j, k, fDay ;
	char dlFileName[512] ;
	time_t startTm, endTm;
	
	int *longMaint = NULL;
	int numLongMaint = 0 ;
	DL_LegInfo *legInfosCP = NULL;
	DL_OrigDuty **origDuties = NULL ; //[crew pair][day]
	DL_TourStartInfo *tourStartInfos = NULL;
	int numStartInfo = 0 ;
	DL_NewDuty ****newDuties = NULL ; //[tour start][day][first duty or not][index]
	int ***numNewDuties = NULL ;//[tour start][day][first duty or not]
	DL_AvailInfo *availAcs=NULL, *availPilots=NULL ; //[ac index], [pilot index]
	DL_DemUsedType *dlDemUsed=NULL; //[demand index in demand list]

	int *regions = NULL; //temp

	startTm = time(NULL) ;
	printf("\n Duty Line ... \n") ;
/////////////////////////////////////////////////////////////////////////////////////////////set the output file
	memset(dlFileName, 0, sizeof(dlFileName));
	for(i=0; i < sizeof(logFileName) - 1; i++)
		if( logFileName[i] == '.' && logFileName[i+1] == 't') //remove .txt from the file name
			break;
		else
			dlFileName[i] = logFileName[i];
	_ASSERTE( sizeof(logFileName) == sizeof(dlFileName) && i +12 < sizeof(logFileName)); //will add _output.xml
	dlFileName[i] = '\0';//add end
	strcat(dlFileName, "_DutyLine.txt");
	if( (dlFile = fopen (dlFileName, "w")) == NULL)
	{
		logMsg(dlFile,"%s Line %d: Cannot write Duty Line file.\n", __FILE__,__LINE__);
		exit(1);
	}
/////////////////////////////////////////////////////////////////////////////////////////////end set the output file

#ifdef DL_F_CHECK_DATA_DEBUG
	//check dual qualified crew
	for(i=0; i < numCrew; i++)
	{
		if( j = crewList[i].dqOtherCrewPos )
		{
			_ASSERTE( (j == -1 || j == 1 ) && i + j >= 0 && i + j < numCrew && crewList[i].crewID == crewList[i + j].crewID && (crewList[i].isDup || crewList[i + j].isDup ) 
			&& crewList[i + j].dqOtherCrewPos ) ;
		} else
			_ASSERTE( !crewList[i].isDup );

		if( crewList[i].isDup )
		{
			j = crewList[i].dqOtherCrewPos ;
			_ASSERTE( (j == -1 || j == 1 ) && i + j >= 0 && i + j < numCrew && !crewList[i + j].isDup && crewList[i].crewID == crewList[i + j].crewID ) ;
		} 
	}
	//for(i=0; i < numCrewPairs; i++)
	//	if( crewPairList[i].aircraftID[0] || crewPairList[i].optAircraftID )
			//_ASSERTE( crewPairList[i].aircraftID[0] == crewPairList[i].optAircraftID );
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getDemUsed(&dlDemUsed) ;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getLegsCP( &legInfosCP );
#ifdef DL_F_CHECK_DATA_DEBUG
	checkLegsCP(legInfosCP) ;
#endif
#ifdef DL_F_PRINT_DATA_DEBUG
	printManagedLegs( legInfosCP ) ;
#endif
	
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getLongMaintlInfo(dlDemUsed, &longMaint, &numLongMaint) ;//get long maint that are in the solution
#ifdef DL_F_PRINT_DATA_DEBUG
	printLongMaintlInfo(longMaint, numLongMaint) ;
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getOriginalDuties( dlDemUsed, legInfosCP, &origDuties ) ;	//generate duty assignment of ac-pilot-duty on each day, according to the original optimal solution
#ifdef DL_F_CHECK_DATA_DEBUG
	checkOrigDuties(origDuties, legInfosCP) ;
#endif
#ifdef DL_F_PRINT_DATA_DEBUG
	printOrigDuties(origDuties) ;
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getTourStartInfo(dlDemUsed, legInfosCP, origDuties, longMaint, numLongMaint, &tourStartInfos, &numStartInfo, &availAcs, &availPilots);
#ifdef DL_F_PRINT_DATA_DEBUG
	printTourStartInfos(tourStartInfos, numStartInfo, availAcs, availPilots) ;
#endif
#ifdef DL_F_CHECK_DATA_DEBUG
	checkTourStartInfos(legInfosCP, origDuties, tourStartInfos, numStartInfo, availAcs, availPilots) ;
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	getNewDuties(longMaint, numLongMaint, origDuties, tourStartInfos, numStartInfo, &newDuties, &numNewDuties ) ;
#ifdef DL_F_PRINT_DATA_DEBUG
	printNewDuties( newDuties, numNewDuties, numStartInfo);
#endif
#ifdef DL_F_CHECK_DATA_DEBUG
	checkNewDuties(legInfosCP, origDuties, tourStartInfos, numStartInfo, newDuties, numNewDuties );
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	buildAndSolve (origDuties, tourStartInfos, numStartInfo, availAcs, availPilots, newDuties, numNewDuties, legInfosCP, crewAssignP, numCrewAssignP
	,travelsRootP, numTravelsP );	
	_ASSERTE( *numCrewAssignP > 0 && *crewAssignP != NULL );

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////free memory
	_ASSERTE( newDuties && numNewDuties );
	for(i=0; i < numStartInfo ; i ++)
	{
		_ASSERTE(  newDuties[i] && numNewDuties[i] );
		for(fDay=0; fDay < optParam.planningWindowDuration; fDay ++)
		{
			_ASSERTE( numNewDuties[i][fDay] && newDuties[i][fDay] );
			
			for(j=0; j < 2; j ++)
				if( newDuties[i][fDay][j] != NULL )
				{
					_ASSERTE( numNewDuties[i][fDay][j] > 0 );
					for(k=0; k < numNewDuties[i][fDay][j]; k ++)
						freeNewDuty( &(newDuties[i][fDay][j][k]) );
					free( newDuties[i][fDay][j] );
					newDuties[i][fDay][j] = NULL;
				}
			free( newDuties[i][fDay] );
			newDuties[i][fDay] = NULL;
			free( numNewDuties[i][fDay] );
			numNewDuties[i][fDay] = NULL ;
		}
		free( newDuties[i] );
		newDuties[i] = NULL;
		free( numNewDuties[i] );
		numNewDuties[i] = NULL ;
	}
	free( newDuties );
	newDuties = NULL;
	free( numNewDuties );
	numNewDuties = NULL ;
	//end

	if( longMaint != NULL )
	{
		free( longMaint );
		longMaint = NULL ;
	}
	if( regions != NULL )
	{
		free( regions );
		regions = NULL ;
	}
	if( origDuties != NULL )
	{
		for(i=0; i < numCrewPairs; i ++ )
			if( origDuties[i] != NULL ) //current cp not assigned
			{
				free( origDuties[i] ) ;
				origDuties[i] = NULL ;
			}
		free(origDuties);
		origDuties = NULL;
	}
	if( tourStartInfos != NULL )
	{
		free( tourStartInfos ) ;
		tourStartInfos = NULL ;
	}
	if( legInfosCP != NULL )
	{
		free( legInfosCP );
		legInfosCP = NULL ;
	}
	if( availAcs != NULL )
	{
		free(availAcs);
		availAcs= NULL;
	}
	if( availPilots != NULL )
	{
		free(availPilots);
		availPilots = NULL;
	}
	if( dlDemUsed != NULL )
	{
		free( dlDemUsed );
		dlDemUsed = NULL ;
	}
	
	endTm = time(NULL) ;
	fprintf(dlFile, "\n\n -->total running time: %d \n", endTm-startTm);
	if( fclose(dlFile) )
	{	
		logMsg(dlFile, "can't close Duty Line File \n");
		exit(1);
	}

	return 0;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int buildAndSolve (const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_AvailInfo *availAcs
, const DL_AvailInfo *availPilots, DL_NewDuty ****ndP, const int ***numNDP, const DL_LegInfo *legInfosCP, ProposedCrewAssg **crewAssignP, int *numCrewAssignP
, BINTREENODE **travelsRootP, int *numTravelsP)
{
	int isConnect, numLpCols, numPickedLpCols;
	int *rowStartInd=NULL, *colStartInd=NULL, *indices=NULL, *acUsed=NULL, *pilotUsed=NULL, *dColToDInd=NULL, *aColToAInd=NULL ;
	int status, i, j, k, m, n, day, sDay, tDay, curInd, demInd, cpInd, fixStartInd, numTotalCols ;
	CPXENVptr env = NULL;
	CPXLPptr lp = NULL;
	double *right=NULL, *coefficients=NULL, *x=NULL;
	char *sign=NULL, **rowNames=NULL, *charBuf=NULL;
	double cost, penalty;
	DL_NewDuty *curDutyP=NULL;
	DL_LpColumn *lpCols=NULL, *curP=NULL, *pickedLpCols=NULL;
	time_t tempTm;
	
	const time_t PostPlusRestTm =  Minute*(optParam.postFlightTm + optParam.minRestTm );
	const int OneK = 1024;
	const int cmatbeg = 0; //for cplex
	const int ccnt = 1;
	const double lb= 0.0;
	const double ub= 1.0;
	const char ctype = 'B' ;
	const char lu = 'L' ;
	const char uu = 'U' ;
	const double roundToOne = 0.5 ;

	//free the original crew assignments
	_ASSERTE( *numCrewAssignP > 0 && *crewAssignP != NULL );
	for(i=0; i < *numCrewAssignP; i ++)
		_ASSERTE( (*crewAssignP)[i].numCoverDems == 0 && (*crewAssignP)[i].coverDems == NULL );
	free( *crewAssignP );
	*crewAssignP = NULL ;
	*numCrewAssignP = 0 ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////get row numbers
	if (!(rowStartInd = (int *) calloc (NumRowTypes, sizeof (int)))) //row start index for each row type
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(colStartInd = (int *) calloc (NumColTypes, sizeof (int)))) //newDutyP start index for each newDutyP type
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	
	rowStartInd[DL_Row_StartInfo] = 0 ; //start infos
	rowStartInd[DL_Row_StartInfo_Con] = rowStartInd[DL_Row_StartInfo] + numStartInfo;//pickup-dropoff on each day
	rowStartInd[DL_Row_StartInfo_ConTm] = rowStartInd[DL_Row_StartInfo_Con] + numStartInfo*optParam.planningWindowDuration;//pickup-dropoff time on each day
	rowStartInd[DL_Row_Pilot] = rowStartInd[DL_Row_StartInfo_ConTm] + numStartInfo*optParam.planningWindowDuration;//available pilot
	rowStartInd[DL_Row_PilotTm] = rowStartInd[DL_Row_Pilot] + numCrew ;//available pilot tm
	rowStartInd[DL_Row_Ac] = rowStartInd[DL_Row_PilotTm] + numCrew ;//available ac
	rowStartInd[DL_Row_AcTm] = rowStartInd[DL_Row_Ac] + numAircraft ;//available ac tm
	rowStartInd[DL_Row_OptDuty] = rowStartInd[DL_Row_AcTm] + numAircraft ;//opt duties
	rowStartInd[DL_Row_CrewedAc] = rowStartInd[DL_Row_OptDuty] + numCrewPairs*optParam.planningWindowDuration ;//crewed ac
	rowStartInd[DL_Row_CvdDem] = rowStartInd[DL_Row_CrewedAc] + numAircraft*optParam.planningWindowDuration ;//covered dem
	rowStartInd[DL_Row_CvdReg] = rowStartInd[DL_Row_CvdDem] + numDemand ;//covered reg
	rowStartInd[DL_Row_Total] = rowStartInd[DL_Row_CvdReg] ;
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////allocate
	if (!(right = (double *) calloc (rowStartInd[DL_Row_Total], sizeof (double))))//right hand side
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(sign = (char *) calloc (rowStartInd[DL_Row_Total], sizeof (char))))//signes
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(coefficients = (double *) calloc (rowStartInd[DL_Row_Total], sizeof (double))))//coefficients in a column
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(indices = (int *) calloc (rowStartInd[DL_Row_Total], sizeof (int))))//row indices in a column
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if ((charBuf = (char *) calloc (OneK, sizeof (char))) == NULL)//for error message
	{
		logMsg(dlFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		exit(1);
	}
	if ((rowNames = (char **) calloc (rowStartInd[DL_Row_Total], sizeof (char *))) == NULL)
	{
		logMsg(dlFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
		exit(1);
	}
	for (i=0; i<rowStartInd[DL_Row_Total]; i++)
		if ((rowNames[i] = (char *) calloc (OneK, sizeof (char))) == NULL)
		{
			logMsg(dlFile,"%s Line %d: Out of Memory in optimizeIt().\n", __FILE__,__LINE__);
			exit(1);
		}
	if ( ! (env = CPXopenCPLEX (&status)) ) 
	{
		logMsg(dlFile, "Could not open CPLEX environment.\n");
		exit(1);
	}
	if ( !(lp = CPXcreateprob (env, &status, "DL")) ) 
	{
		logMsg(dlFile, "Failed to create LP.\n");
		exit(1);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////generate empty rows: get signs and right hand side
	for (i=rowStartInd[DL_Row_StartInfo]; i<rowStartInd[DL_Row_StartInfo+1] ; i++)
	{
		k = i-rowStartInd[DL_Row_StartInfo] ;
		
		sprintf(rowNames[i], "Row_%d_StartInfo_Cover_ind_%d_%d_%d_%d", i, k, acList[tourStartInfos[k].ac.ind].aircraftID
		, crewList[tourStartInfos[k].pilots[0].ind].crewID, crewList[tourStartInfos[k].pilots[1].ind].crewID );

		right[i] = 1;

		if( tourStartInfos[k].type == DL_NewAvail ) 
			sign[i] = 'L';
		else 
			sign[i] = 'E'; //must cover original opt tours
	}
	//tour start connection UNIT on each day
	_ASSERTE( rowStartInd[DL_Row_StartInfo_Con] + numStartInfo*optParam.planningWindowDuration == rowStartInd[DL_Row_StartInfo_Con + 1] ) ;
	k = rowStartInd[DL_Row_StartInfo_Con];//row index; initialize
	for(i=0; i < numStartInfo; i++)
		for(j=0; j < optParam.planningWindowDuration; j++)
		{
			sprintf(rowNames[k], "Row_%d_StartInfo_Con_ind_%d_day_%d_%d_%d_%d", k, i, j, acList[tourStartInfos[i].ac.ind].aircraftID
			, crewList[tourStartInfos[i].pilots[0].ind].crewID, crewList[tourStartInfos[i].pilots[1].ind].crewID );
			
			right[k] = 0;
			sign[k] = 'L';
			k ++;
		}
	_ASSERTE( rowStartInd[DL_Row_StartInfo_Con + 1] == k );
	//tour start connection TIME on each day
	_ASSERTE( rowStartInd[DL_Row_StartInfo_ConTm] + numStartInfo*optParam.planningWindowDuration == rowStartInd[DL_Row_StartInfo_ConTm + 1] ) ;
	k = rowStartInd[DL_Row_StartInfo_ConTm];//row index; initialize
	for(i=0; i < numStartInfo; i++)
		for(j=0; j < optParam.planningWindowDuration; j++)
		{
			sprintf(rowNames[k], "Row_%d_StartInfo_Con_Tm_ind_%d_day_%d_%d_%d_%d", k, i, j, acList[tourStartInfos[i].ac.ind].aircraftID
			, crewList[tourStartInfos[i].pilots[0].ind].crewID, crewList[tourStartInfos[i].pilots[1].ind].crewID );
			
			right[k] = 0;
			sign[k] = 'L';
			k ++;
		}
	_ASSERTE( rowStartInd[DL_Row_StartInfo_ConTm + 1] == k );
	//available pilots UNIT
	for (i=rowStartInd[DL_Row_Pilot]; i<rowStartInd[DL_Row_Pilot+1] ; i++)
	{
		sprintf( rowNames[i], "Row_%d_Available_Pilots_ID_%d", i, crewList[i-rowStartInd[DL_Row_Pilot]].crewID );
		right[i] = 1;
		sign[i] = 'L';
	}
	//available pilots TIME
	for (i=rowStartInd[DL_Row_PilotTm]; i<rowStartInd[DL_Row_PilotTm+1] ; i++)
	{
		sprintf( rowNames[i], "Row_%d_Available_Pilots_Time_ID_%d", i, crewList[i-rowStartInd[DL_Row_PilotTm]].crewID );
		right[i] = 0;
		sign[i] = 'L';
	}
	//available acs UNIT
	for (i=rowStartInd[DL_Row_Ac]; i<rowStartInd[DL_Row_Ac+1] ; i++)
	{
		sprintf( rowNames[i], "Row_%d_Available_Aircrafts_ID_%d", i, acList[i-rowStartInd[DL_Row_Ac]].aircraftID);
		right[i] = 1;
		sign[i] = 'L';
	}
	//available acs TIME
	for (i=rowStartInd[DL_Row_AcTm]; i<rowStartInd[DL_Row_AcTm+1] ; i++)
	{
		sprintf( rowNames[i], "Row_%d_Available_Aircrafts_Time_ID_%d", i, acList[i-rowStartInd[DL_Row_AcTm]].aircraftID);
		right[i] = 0;
		sign[i] = 'L';
	}
	//cover opt duties
	k = rowStartInd[DL_Row_OptDuty];//row index; initialize
	for(i=0; i < numCrewPairs; i++)
		for(j=0; j < optParam.planningWindowDuration; j++)
		{
			sprintf( rowNames[k], "Row_%d_Opt_Duty_CP_%d_Day_%d", k, i, j);
			sign[k] = 'E';
			if( origDuties[i] && origDuties[i][j].numAc )
				right[k] = 1;
			else
				right[k] = 0;
			k ++ ;
		}
	//crew ac on each day
	k = rowStartInd[DL_Row_CrewedAc];//row index; initialize
	for(i=0; i < numAircraft; i++)
		for(j=0; j < optParam.planningWindowDuration; j++)
		{
			sprintf( rowNames[k], "Row_%d_Crew_Ac_%d_Day_%d", k, acList[i].aircraftID, j);
			right[k] = 1;
			sign[k] = 'G';
			k ++ ;
		}
	//cover dem
	for (i=rowStartInd[DL_Row_CvdDem]; i<rowStartInd[DL_Row_CvdDem+1] ; i++)
	{
		j = i-rowStartInd[DL_Row_CvdDem] ;
		sprintf( rowNames[i], "Row_%d_Demand_Covering_ID_%d", i, demandList[j].demandID);
		sign[i] = 'G';
		if( demandList[j].isAppoint || demandList[j].outAirportID == demandList[j].inAirportID )//not regular demand, not to cover
			right[i] = 0 ;
		else
			right[i] = RHSDem ;
	}

	//build empty rows
	if (status = CPXnewrows (env, lp, rowStartInd[DL_Row_Total], right, sign, NULL, rowNames))
	{
		logMsg(dlFile, "CPLEX failed to create rows.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(dlFile,"%s", charBuf);
		exit(1);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////add all columns
	numTotalCols = 0 ;
	numLpCols = 0 ; //columns corresponding to new dutyies
	colStartInd[DL_Col_LP] = 0 ;//start index for each type of columns
	for(i=0; i < numStartInfo ; i ++)
	{
		_ASSERTE( ndP[i] && numNDP[i] );//allocated

		for( day=0; day < optParam.planningWindowDuration; day ++ )//check each possible day to start
		{
			_ASSERTE( ndP[i][day] && numNDP[i][day] );//allocated

			for( k=0; k<2; k++ ) //two types: k=0 --> first duty
			{
				if( !numNDP[i][day][k] )
					continue;

				_ASSERTE( ndP[i][day][k] && numNDP[i][day][k] > 0 );

				for(j=0; j <numNDP[i][day][k]; j ++)//for each duty
				{
					curInd = 0; //for adding newDutyP to LP; initialize //number of non-zeros

					curDutyP = &(ndP[i][day][k][j]) ;
					cpInd = curDutyP->startInfoP->cpInd ;
					cost = curDutyP->cost ;
					_ASSERTE(curDutyP->startInfoP == &(tourStartInfos[i]));

					sprintf (charBuf, "Duty_Column_%d_StartInfo_%d_Day_%d_FirstDay_%d_%d_%d_%d", numTotalCols, i, day, curDutyP->firstDay
					, acList[curDutyP->acInd].aircraftID, crewPairList[cpInd].captainID, crewPairList[cpInd].flightOffID);	
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add dems covered by current duty
					for(m=0; m<curDutyP->numCoverDems; m++)
					{
						_ASSERTE( curDutyP->coverDems[m] >= 0 && curDutyP->coverDems[m] < numDemand );
						indices[curInd] = rowStartInd[DL_Row_CvdDem] + curDutyP->coverDems[m];//row index
						coefficients[curInd] = 1;
						curInd ++;
					}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////add opt duty covered by current duty
					if( curDutyP->origDutyP )
					{
						_ASSERTE( curDutyP->startInfoP->type != DL_NewAvail && origDuties[cpInd] && origDuties[cpInd][day].numAc
						&& curDutyP->origDutyP == &(origDuties[cpInd][day]) );

						indices[curInd] = rowStartInd[DL_Row_OptDuty] + cpInd*optParam.planningWindowDuration + day;//row index
						coefficients[curInd] = 1;
						curInd ++;
					}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add ac crewed by current duty
					if( !curDutyP->origDutyP ) //no legs on current day //one ac is assigned
					{
						indices[curInd] = rowStartInd[DL_Row_CrewedAc] + curDutyP->acInd*optParam.planningWindowDuration + day;//row index
						coefficients[curInd] = 1;
						curInd ++;
					} else //maybe more than one ac
					{
						_ASSERTE( curDutyP->origDutyP->numAc && curDutyP->origDutyP->acInd[0] == curDutyP->acInd );
						for(m=0; m<curDutyP->origDutyP->numAc; m++)
						{
							_ASSERTE( curDutyP->origDutyP->acInd[m] >= 0 );
							indices[curInd] = rowStartInd[DL_Row_CrewedAc] + curDutyP->origDutyP->acInd[m]*optParam.planningWindowDuration + day;//row index
							coefficients[curInd] = 1;
							curInd ++;
						}//end for(m=0; m<DL_MAX_NUM_AC_PER_DUTY; m++)
					}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add pick up
					if( !k )//first duty of a startInfo, add startInfo index and pick up from available acs and pilots
					{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add startInfo
						indices[curInd] = rowStartInd[DL_Row_StartInfo] + curDutyP->startInfoP->index;
						coefficients[curInd] = 1;
						curInd ++;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////not an opt tour, pick up from avail ac/pilot
						//no pickup for opt tours
						//pick up for the firs ac of the duty
						if( curDutyP->startInfoP->type == DL_NewAvail )
						{
							_ASSERTE( !origDuties[cpInd] || (curDutyP->startInfoP->pilots[0].availAfter == DL_AfterLeg 
							&& curDutyP->startInfoP->pilots[1].availAfter == DL_AfterLeg));

							for(m=0; m<2; m++)//each pilot
							{
								n = curDutyP->startInfoP->pilots[m].ind ;

								_ASSERTE( availPilots[n].availAfter );//pilot available

								indices[curInd] = rowStartInd[DL_Row_Pilot] + n;//pick up from available pilots
								coefficients[curInd] = 1;
								curInd ++; 

								//check if this pilot is dual qualified; if it is, then add 1 to the duplicate
								if( crewList[n].dqOtherCrewPos )
								{
									indices[curInd] = rowStartInd[DL_Row_Pilot] + n + crewList[n].dqOtherCrewPos ;
									coefficients[curInd] = 1;
									curInd ++; 
								}

								if( availPilots[n].availAfter == DL_AfterLeg )//consider time, only if pilot available afrer opt tours
								{
									_ASSERTE( optParam.windowEnd - curDutyP->pilotStartTm[m] + PostPlusRestTm  >= 0 );
									indices[curInd] = rowStartInd[DL_Row_PilotTm] + n;
									coefficients[curInd] = ceil((double)(optParam.windowEnd - curDutyP->pilotStartTm[m] + PostPlusRestTm )/(double)DiscrtLPInterval);
									curInd ++;
								}
							}//end for(m=0; m<2; m++)

							indices[curInd] = rowStartInd[DL_Row_Ac] + curDutyP->acInd;//pick up ac; first ac
							coefficients[curInd] = 1;
							curInd ++;

							//consider time, only if ac available afrer opt tours
							if( availAcs[curDutyP->acInd].availAfter == DL_AfterLeg )
							{
								_ASSERTE( optParam.windowEnd - curDutyP->startTm + Minute*optParam.turnTime >= 0 );
								indices[curInd] = rowStartInd[DL_Row_AcTm] + curDutyP->acInd;
								coefficients[curInd] = ceil((double)(optParam.windowEnd - curDutyP->startTm + Minute*optParam.turnTime)/(double)DiscrtLPInterval );
								curInd ++;
							}
						}//end if( curDutyP->startInfoP->type == DL_NewAvail )
					} else //not first duty, pick up from prev duty
					{
						//pick up from previous duty
						indices[curInd] = rowStartInd[DL_Row_StartInfo_Con] + (curDutyP->startInfoP->index*optParam.planningWindowDuration) + day;
						coefficients[curInd] = 1;
						curInd ++;

						//need to consider pick up time in the following cases
						if( curDutyP->startInfoP->type == DL_NewAvail //new tour
						|| !curDutyP->origDutyP //opt tour, but no legs on current day
						|| (!curDutyP->keepOriginal && curDutyP->origDutyP->tmFixed != DL_StartTmFixed ))//duty fixed or start time is fixed
						{
							tempTm = min(curDutyP->pilotStartTm[0], curDutyP->pilotStartTm[1]) ;

							_ASSERTE( ( curDutyP->day <= curDutyP->startInfoP->legStartDay || curDutyP->pilotStartTm[0] == curDutyP->pilotStartTm[1])  
							&&  curDutyP->startInfoP->acCpDay < day && optParam.windowEnd - tempTm + PostPlusRestTm >= 0 );

							indices[curInd] = rowStartInd[DL_Row_StartInfo_ConTm] + (curDutyP->startInfoP->index*optParam.planningWindowDuration) + day;
							coefficients[curInd] = ceil((double)(optParam.windowEnd - tempTm + PostPlusRestTm)/(double)DiscrtLPInterval );
							curInd ++;
						}
					}//end if( !k )

/////////////////////////////////////////////////////////////////////////////may have several dropoff options, and need to generate more than one columns
					fixStartInd = curInd ; //entries before curInd are the same in these columns; initialize
					isConnect = 0; //whether there is dropoff to the next duty/tour, i.e. whether any columns are added
					_ASSERTE( day <= curDutyP->startInfoP->lastPossibleDay && curDutyP->startInfoP->lastPossibleDay < optParam.planningWindowDuration );
					//get sDay: next leg start day; can only dropoff to days [day+1, sDay]
					if( origDuties[cpInd] )
					{
						for( sDay = day+1; sDay <= curDutyP->startInfoP->lastPossibleDay; sDay ++ )
							if( origDuties[cpInd][sDay].numAc )
								break;
					} else
						sDay = curDutyP->startInfoP->lastPossibleDay + 1;
					
					_ASSERTE( sDay <= optParam.planningWindowDuration );

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////drop off to the next tour start
					//original tour, and no more legs of the same crew pair later, check if can drop off
					if( curDutyP->startInfoP->type != DL_NewAvail && sDay > curDutyP->startInfoP->lastPossibleDay ) 
					{
						curInd = fixStartInd ;//initialize

						for(m=0; m<2; m++)//drop off pilots
							//if no more legs of this pilot, AND pilot is set to be available
							if( !origDuties[curDutyP->startInfoP->cpInd][curDutyP->startInfoP->legEndDay].lastLegP->nextPilotLegs[m]
							&& availPilots[curDutyP->startInfoP->pilots[m].ind].availAfter )
							{
								_ASSERTE ( optParam.windowEnd >= curDutyP->endTm ) ;
								indices[curInd] = rowStartInd[DL_Row_PilotTm] + curDutyP->startInfoP->pilots[m].ind;
								coefficients[curInd] = -floor((double)(optParam.windowEnd - curDutyP->endTm)/(double)DiscrtLPInterval );
								curInd ++;
							}

						//ac to drop off //only need to consider the last ac
						m = ( curDutyP->origDutyP ? curDutyP->origDutyP->acInd[curDutyP->origDutyP->numAc-1] : curDutyP->acInd );
						if( !origDuties[curDutyP->startInfoP->cpInd][curDutyP->startInfoP->legEndDay].lastLegP->nextAcLeg //no more legs of this ac
						&& availAcs[m].availAfter )//ac is set to be available(consider only time)
						{
							_ASSERTE ( optParam.windowEnd >= curDutyP->endTm ) ;
							indices[curInd] = rowStartInd[DL_Row_AcTm] + m;
							coefficients[curInd] = -floor((double)(optParam.windowEnd - curDutyP->endTm)/(double)DiscrtLPInterval ) ;
							curInd ++;
						}

						//if dropoff, add columns
						if ( curInd > fixStartInd )
						{
							isConnect = 1;
#ifdef DL_F_LP_COL_CHECK_DEBUG
							if (status = CPXcheckaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf))
#else
							if (status = CPXaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf ))
#endif
							{
								logMsg(dlFile, "CPLEX failed to create column.\n");
								CPXgeterrorstring (env, status, charBuf);
								logMsg(dlFile,"%s", charBuf);
								exit(1);
							}
							//generate the corresponding lp column
							curP = allocAnLpColumn( &lpCols, &numLpCols);
							curP->newDutyP = curDutyP ;
							curP->dropOff = 1;
							curP->nextDay = -1;
							curP->fDuty = ( k ? 0 : 1 ) ;

							numTotalCols ++;
						}//end if ( curInd > fixStartInd )
					}//if( sDay > curDutyP->startInfoP->lastPossibleDay )

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////drop off to the next duty on tDay
					//dropoff to days in [day+1, sDay] of the same tour
					for( tDay = day + 1; tDay <= min(sDay, optParam.planningWindowDuration-1) ; tDay ++)
					{
						isConnect = 1;
						curInd = fixStartInd ;

						//connect on the next day
						indices[curInd] = rowStartInd[DL_Row_StartInfo_Con] + (curDutyP->startInfoP->index*optParam.planningWindowDuration) + tDay;
						coefficients[curInd] = -1;
						curInd ++;

						//time on the next day
						_ASSERTE ( optParam.windowEnd >= curDutyP->endTm ) ;
						indices[curInd] = rowStartInd[DL_Row_StartInfo_ConTm] + (curDutyP->startInfoP->index*optParam.planningWindowDuration) + tDay;
						coefficients[curInd] = -floor((double)(optParam.windowEnd - curDutyP->endTm)/(double)DiscrtLPInterval );
						curInd ++;

#ifdef DL_F_LP_COL_CHECK_DEBUG
						if (status = CPXcheckaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf))
#else
						if (status = CPXaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf))
#endif
						{
							logMsg(dlFile, "CPLEX failed to create column.\n");
							CPXgeterrorstring (env, status, charBuf);
							logMsg(dlFile,"%s", charBuf);
							exit(1);
						}
						//generate the corresponding lp column
						curP = allocAnLpColumn( &lpCols, &numLpCols);
						curP->newDutyP = curDutyP ;
						curP->dropOff = 0;
						curP->nextDay = tDay;
						curP->fDuty = ( k ? 0 : 1 ) ;

						numTotalCols ++;
					}//end for( tDay = day + 1; tDay < sDay; tDay ++)

					if( !isConnect )//no columns generated so far for this duty; no drop off to next duty/tour start; add the original column
					{
						_ASSERTE( curInd == fixStartInd );
#ifdef DL_F_LP_COL_CHECK_DEBUG
						if (status = CPXcheckaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf))
#else
						if (status = CPXaddcols(env, lp, ccnt, curInd, &cost, &cmatbeg, indices, coefficients, &lb, NULL, &charBuf))
#endif
						{
							logMsg(dlFile, "CPLEX failed to create column.\n");
							CPXgeterrorstring (env, status, charBuf);
							logMsg(dlFile,"%s", charBuf);
							exit(1);
						}
						curP = allocAnLpColumn( &lpCols, &numLpCols);
						curP->newDutyP = curDutyP ;
						curP->dropOff = 0;
						curP->nextDay = -1;
						curP->fDuty = ( k ? 0 : 1 ) ;

						numTotalCols ++;
					}//if( !isConnect )

				}//end for(j=0; j <numNDP[i][day][k]; j ++)
			}//end for( k=0; k<2; k++ )
		}//for( day=0; day < optParam.planningWindowDuration; day ++ )
	}//end for(i=0; i < numStartInfo ; i ++)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add dem cover columns
	colStartInd[DL_Col_Dem]  = numTotalCols ;
	if (!(dColToDInd = (int *) calloc (numDemand, sizeof (int)))) //demand index of each demand column; only to-be-covered demands are added to the columns
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	for( demInd=0; demInd<numDemand; demInd++)//generate dem columns
	{
		if( demandList[demInd].isAppoint || demandList[demInd].outAirportID == demandList[demInd].inAirportID )
			continue ;

		sprintf (charBuf, "Demand_Covering_Column_%d_ID_%d", numTotalCols, demandList[demInd].demandID);	

		indices[0] = rowStartInd[DL_Row_CvdDem] + demInd;//row index
		coefficients[0] = RHSDem; //temp
		curInd = 1;
		
#ifdef DL_F_LP_COL_CHECK_DEBUG
		if (status = CPXcheckaddcols(env, lp, ccnt, curInd, &PenaltyDem, &cmatbeg, indices, coefficients, &lb, &ub, &charBuf))
#else
		if (status = CPXaddcols(env, lp, ccnt, curInd, &PenaltyDem, &cmatbeg, indices, coefficients, &lb, &ub, &charBuf))
#endif
		{
			logMsg(dlFile, "CPLEX failed to create dem column.\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(dlFile,"%s", charBuf);
			exit(1);
		}
		dColToDInd[numTotalCols-colStartInd[DL_Col_Dem]] = demInd ;//keep demand index
		_ASSERTE( numTotalCols-colStartInd[DL_Col_Dem] < numDemand );
		numTotalCols ++;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////add ac cover columns
	colStartInd[DL_Col_Ac]  = numTotalCols ;
	if (!(aColToAInd = (int *) calloc (numAircraft*optParam.planningWindowDuration, sizeof (int)))) //temp
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	for( i=0; i<numAircraft; i++)//generate ac columns
	{
		penalty = ( acList[i].isMac ? PenaltyMacPerDay : PenaltyAcPerDay ) ;//use smaller penalty for mac
		for(j=0; j < optParam.planningWindowDuration; j ++ )
		{
			//if( !availAcs[i].availAfter )
			//	continue;

			sprintf (charBuf, "Aircraft_Crewing_Column_%d_ID_%d_Day_%d", numTotalCols, acList[i].aircraftID, j);	

			indices[0] = rowStartInd[DL_Row_CrewedAc] + i*optParam.planningWindowDuration + j ;//row index
			coefficients[0] = 1;
			curInd = 1;

#ifdef DL_F_LP_COL_CHECK_DEBUG
			if (status = CPXcheckaddcols(env, lp, ccnt, curInd, &penalty, &cmatbeg, indices, coefficients, &lb, &ub, &charBuf))
#else
			if (status = CPXaddcols(env, lp, ccnt, curInd, &penalty, &cmatbeg, indices, coefficients, &lb, &ub, &charBuf))
#endif
			{
				logMsg(dlFile, "CPLEX failed to create ac column.\n");
				CPXgeterrorstring (env, status, charBuf);
				logMsg(dlFile,"%s", charBuf);
				exit(1);
			}
			aColToAInd[numTotalCols-colStartInd[DL_Col_Ac]] = i ;//keep ac ind
			numTotalCols ++;
		}
	}
	colStartInd[DL_Col_Ac+1] = numTotalCols ;
	_ASSERTE( numTotalCols > 0 && numTotalCols == CPXgetnumcols (env, lp) && numLpCols == colStartInd[DL_Col_Dem] - colStartInd[DL_Col_LP]);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////solve
	for(i=0; i < numLpCols; i++)//set binary varaible
	{
		if ( status = CPXchgctype (env, lp, ccnt, &i, &ctype) ) 
		{
			fprintf (stderr, "Failed to copy ctype\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(dlFile,"%s", charBuf);
			exit(1);
		}
		if ( status = CPXchgbds (env, lp, ccnt, &i, &lu, &lb) ) 
		{
			fprintf (stderr, "Failed to copy lb\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(dlFile,"%s", charBuf);
			exit(1);
		}
		if ( status = CPXchgbds (env, lp, ccnt, &i, &uu, &ub) ) 
		{
			fprintf (stderr, "Failed to copy lb\n");
			CPXgeterrorstring (env, status, charBuf);
			logMsg(dlFile,"%s", charBuf);
			exit(1);
		}
	}

	CPXsetintparam (env, CPX_PARAM_SCRIND, 1);

#ifdef DL_F_LP_WRITE_DEBUG
	if(status = CPXwriteprob(env, lp, "./Logfiles/DL_LP.lp", NULL))
	{
		fprintf (stderr, "Failed to write the problem \n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(dlFile,"%s", charBuf);
		exit(1);
	}
#endif

	if (status = CPXmipopt (env, lp)) 
	{
		logMsg(dlFile, "Failed to optimize MIP.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(dlFile,"%s", charBuf);
		exit(1);
	}	

	if ( status = CPXgetobjval (env, lp, &cost) ) 
	{
		logMsg(dlFile, "Failed to get objective function value.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(dlFile,"%s", charBuf);
		exit(1);
	}
	printf("\n obj = %f \n", cost );

	//solution
	if ( !(x = (double *) calloc (numTotalCols, sizeof (double))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}	
	//get solution
	if ( status = CPXgetmipx (env, lp, x, 0, numTotalCols-1) ) 
	{
		logMsg(dlFile, "Failed to recover optimal solution.\n");
		CPXgeterrorstring (env, status, charBuf);
		logMsg(dlFile,"%s", charBuf);
		exit(1);
	}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////get columns picked in opt solution
	//get number of opt lp columns
	numPickedLpCols = 0;
	_ASSERTE( colStartInd[DL_Col_LP] == 0 );
	for(i=0; i < numLpCols; i++)
		if( x[i] >= roundToOne )
			numPickedLpCols ++;
	_ASSERTE( numPickedLpCols );
	//will ALLOCATE space for opt lp columns
	if ( !(pickedLpCols = (DL_LpColumn *) calloc (numPickedLpCols, sizeof (DL_LpColumn))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	m=0;
	for(i=0; i < numLpCols; i++)
		if( x[i] >= roundToOne )
		{
			memmove( &(pickedLpCols[m]), &(lpCols[i]), sizeof(DL_LpColumn));
			m ++;
		}
	_ASSERTE(m == numPickedLpCols);
	qsort((void *) pickedLpCols, numPickedLpCols, sizeof(DL_LpColumn ), compareLpColumn); //ac index, cp index, days
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////END

	returnCrewAssigns ( legInfosCP, pickedLpCols, numPickedLpCols, crewAssignP, numCrewAssignP);
	returnTravelRequests ( origDuties, pickedLpCols, numPickedLpCols, travelsRootP, numTravelsP );
#ifdef DL_F_SOLUTION_DEBUG
	printSolution ( tourStartInfos, numStartInfo, origDuties, legInfosCP, pickedLpCols, numPickedLpCols, availAcs, availPilots, colStartInd, dColToDInd, aColToAInd, x, cost);
#endif

	if( rowNames != NULL )
	{
		for (i=0; i<rowStartInd[DL_Row_Total]; i++)
		{	
			free( rowNames[i] );
			rowNames[i] = NULL;
		}
		free( rowNames );
		rowNames = NULL ;
	}
	if( rowStartInd != NULL )
	{
		free( rowStartInd );
		rowStartInd = NULL;
	}
	if( indices != NULL )
	{
		free( indices );
		indices = NULL;
	}
	if( right != NULL )
	{
		free( right );
		right = NULL;
	}
	if( coefficients != NULL )
	{
		free( coefficients );
		coefficients = NULL;
	}
	if( x != NULL )
	{
		free( x );
		x = NULL;
	}
	if( sign != NULL )
	{
		free( sign );
		sign = NULL;
	}
	if( charBuf != NULL )
	{
		free( charBuf );
		charBuf = NULL ;
	}
	if( lpCols != NULL )
	{
		free(lpCols);
		lpCols = NULL;
	}
	if ( lp != NULL && (status = CPXfreeprob (env, &lp))) 
	{
		logMsg ( dlFile, "CPXfreeprob failed, error code %d.\n", status);
		exit(1);
	}
	if ( env != NULL && (status = CPXcloseCPLEX (&env)) )
	{
		logMsg ( dlFile, "CPXcloseCPLEX failed, error code %d.\n", status);
		exit(1);
	}
	if( dColToDInd != NULL )
	{
		free(dColToDInd);
		dColToDInd = NULL;
	}
	if( aColToAInd != NULL )
	{
		free(aColToAInd);
		aColToAInd = NULL;
	}
	if( pickedLpCols != NULL )
	{
		free(pickedLpCols);
		pickedLpCols = NULL;
	}
	
	return 0;
}


static int returnCrewAssigns ( const DL_LegInfo *legInfosCP, const DL_LpColumn *pickedLpCols, const int numPickedLpCols, ProposedCrewAssg **crewAssignP
, int *numCrewAssignP )
{
	int *caToOptCol=NULL ;
	int i, j, k, m ;
	char opbuf1[1024], opbuf2[1024] ;
	DL_NewDuty *curDutyP=NULL;
	time_t tempTm;
	ProposedCrewAssg *dlCrewAssigns=NULL;
	int numDlCrewAssigns = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////get the crew newDuties
	numDlCrewAssigns = 2*DL_MAX_NUM_AC_PER_DUTY*numPickedLpCols; //temp //max number of crew newDuties
	if (!(dlCrewAssigns = (ProposedCrewAssg *) calloc (numDlCrewAssigns, sizeof (ProposedCrewAssg))))//pilot used in a column in the solution
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(caToOptCol  = (int *) calloc (numDlCrewAssigns, sizeof (int))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	for(i=0; i < numDlCrewAssigns; i ++)
		caToOptCol[i] = -1 ; //initialize

	k = 0; //ca index
	for(i=0; i < numPickedLpCols; i ++)
		for(j=0; j < 2; j++)//for each pilot
		{
			_ASSERTE( k < numDlCrewAssigns );

			if( !pickedLpCols[i].newDutyP->origDutyP ) //one ac assigned
			{
				dlCrewAssigns[k].aircraftID = acList[pickedLpCols[i].newDutyP->acInd].aircraftID ; //first ac
				dlCrewAssigns[k].crewID = crewList[pickedLpCols[i].newDutyP->startInfoP->pilots[j].ind].crewID ;
				dlCrewAssigns[k].position = j + 1 ;
				dlCrewAssigns[k].startTm = pickedLpCols[i].newDutyP->pDutyStartTm[j] ; //duty start time
				dlCrewAssigns[k].endTm = pickedLpCols[i].newDutyP->endTm + Minute*optParam.postFlightTm;

				if( (dlCrewAssigns[k].numCoverDems = pickedLpCols[i].newDutyP->numCoverDems) > 0 )
				{
					if (!(dlCrewAssigns[k].coverDems = (int *) calloc (dlCrewAssigns[k].numCoverDems, sizeof (int))))
					{
						logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
						exit(1);
					}
					memmove(dlCrewAssigns[k].coverDems, pickedLpCols[i].newDutyP->coverDems, dlCrewAssigns[k].numCoverDems*sizeof(int)) ;
				}

				caToOptCol[k] = i ;
				k ++;
			} else//generate crew assignment for each ac
			{
				tempTm = pickedLpCols[i].newDutyP->pDutyStartTm[j] ;//start time of current ac
				for(m=0; m<pickedLpCols[i].newDutyP->origDutyP->numAc; m ++)//go through each ac
				{
					//dlCrewAssigns[k].aircraftID = acList[pickedLpCols[i].newDutyP->acInd].aircraftID ; //first ac
					dlCrewAssigns[k].aircraftID = acList[pickedLpCols[i].newDutyP->origDutyP->acInd[m]].aircraftID ; //first ac
					dlCrewAssigns[k].crewID = crewList[pickedLpCols[i].newDutyP->startInfoP->pilots[j].ind].crewID ;
					dlCrewAssigns[k].position = j + 1 ;
					dlCrewAssigns[k].startTm = tempTm ;//note
					if( m == pickedLpCols[i].newDutyP->origDutyP->numAc-1 )//last ac
						dlCrewAssigns[k].endTm = pickedLpCols[i].newDutyP->endTm + Minute*optParam.postFlightTm;//include post flight time
					else
					{
						dlCrewAssigns[k].endTm = legInfosCP[pickedLpCols[i].newDutyP->origDutyP->acEndLeg[m]].leg->schedIn ;//leg end time
						tempTm = dlCrewAssigns[k].endTm ;
					}

					if( (dlCrewAssigns[k].numCoverDems = pickedLpCols[i].newDutyP->numCoverDems) > 0 )
					{
						if (!(dlCrewAssigns[k].coverDems = (int *) calloc (dlCrewAssigns[k].numCoverDems, sizeof (int))))
						{
							logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
							exit(1);
						}
						memmove(dlCrewAssigns[k].coverDems, pickedLpCols[i].newDutyP->coverDems, dlCrewAssigns[k].numCoverDems*sizeof(int)) ;
					}

					caToOptCol[k] = i ;
					k ++;
				}//end for(m=0; m<pickedLpCols[i].newDutyP->origDutyP->numAc; m ++)//go through each ac
			}//end more than one acs
		}//end for(j=0; j < 2; j++)//for each pilot

	_ASSERTE( k <= numDlCrewAssigns );
	numDlCrewAssigns = k ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////check ca
	for(i=0; i < numDlCrewAssigns; i ++)
	{
		_ASSERTE( caToOptCol[i] >= 0 && caToOptCol[i] < numPickedLpCols );
		curDutyP = pickedLpCols[caToOptCol[i]].newDutyP ;//new duty that current crew assignment is derived from

		_ASSERTE( dlCrewAssigns[i].crewID == crewPairList[curDutyP->startInfoP->cpInd].captainID
		|| dlCrewAssigns[i].crewID == crewPairList[curDutyP->startInfoP->cpInd].flightOffID );

		_ASSERTE( !dlCrewAssigns[i].numCoverDems || dlCrewAssigns[i].coverDems );

		k = ( dlCrewAssigns[i].crewID == crewPairList[curDutyP->startInfoP->cpInd].captainID ? 0 : 1 ); //whether current pilot is captain or officer
		m = ( pickedLpCols[caToOptCol[i]].fDuty && curDutyP->startInfoP->pilots[k].status == DL_Pilot_Status_Rest_B4FirstLeg ) ;//whether first duty of curernt pilot's tour

		_ASSERTE( dlCrewAssigns[i].position == k + 1
		&& (dlCrewAssigns[i].endTm - dlCrewAssigns[i].startTm <= getCurMaxDutyTm(m, dlCrewAssigns[i].startTm, pickedLpCols[caToOptCol[i]].newDutyP->pAirports[k] )
		|| curDutyP->keepOriginal )//max duty time
		&& ( !curDutyP->origDutyP //contain the original interval
		|| ((dlCrewAssigns[i].startTm <= curDutyP->origDutyP->firstLegP->leg->schedOut || curDutyP->origDutyP->tmFixed == DL_StartTmFixed 
			|| curDutyP->keepOriginal ) && dlCrewAssigns[i].endTm >= curDutyP->origDutyP->lastLegP->leg->schedIn ))); 

		//between duties of the same pilot
		for(j=0; j < numDlCrewAssigns; j ++)
		{
			if( j == i || dlCrewAssigns[j].crewID != dlCrewAssigns[i].crewID )
				continue ;

			//_ASSERTE( dlCrewAssigns[j].startTm >= dlCrewAssigns[i].endTm || dlCrewAssigns[i].startTm >= dlCrewAssigns[j].endTm );
			if( dlCrewAssigns[j].startTm < dlCrewAssigns[i].endTm + Minute*optParam.minRestTm 
			&& dlCrewAssigns[i].startTm < dlCrewAssigns[j].endTm + Minute*optParam.minRestTm )
				fprintf(dlFile, "\n Crew Assignment: Duty %d and Duty %d intersect \n", caToOptCol[i], caToOptCol[j]);
				
			if( caToOptCol[j] == caToOptCol[i] )//same duty, different acs, no rest
				_ASSERTE( dlCrewAssigns[j].aircraftID != dlCrewAssigns[i].aircraftID 
				&& (dlCrewAssigns[j].startTm == dlCrewAssigns[i].endTm || dlCrewAssigns[i].startTm == dlCrewAssigns[j].endTm ));
			
			//	else
			//		_ASSERTE( dlCrewAssigns[j].startTm - dlCrewAssigns[i].endTm >= Minute*optParam.minRestTm  //must be able to rest
			//		|| dlCrewAssigns[i].startTm - dlCrewAssigns[j].endTm >= Minute*optParam.minRestTm );
			//}
		}

		//between duties of the same ac
		for(j=0; j < numDlCrewAssigns; j ++)
		{
			if( j == i || dlCrewAssigns[j].aircraftID != dlCrewAssigns[i].aircraftID )
				continue ;
			
			if( caToOptCol[j] == caToOptCol[i] )//same duty, one capatin, one officer
			{
				_ASSERTE(dlCrewAssigns[j].endTm == dlCrewAssigns[i].endTm && dlCrewAssigns[j].crewID == crewList[curDutyP->startInfoP->pilots[1-k].ind].crewID);
				continue;
			}

			_ASSERTE( optParam.postFlightTm >= optParam.turnTime && (curDutyP->startTm >= pickedLpCols[caToOptCol[j]].newDutyP->endTm + Minute*optParam.turnTime 
			|| curDutyP->endTm + Minute*optParam.turnTime <= pickedLpCols[caToOptCol[j]].newDutyP->startTm ));
		}

		if( !curDutyP->origDutyP || curDutyP->origDutyP->numAc == 1 )//one ac assigned
		{
			_ASSERTE( dlCrewAssigns[i].startTm == curDutyP->pDutyStartTm[k] 
			&& dlCrewAssigns[i].endTm == curDutyP->endTm + Minute*optParam.postFlightTm && dlCrewAssigns[i].aircraftID ==  acList[curDutyP->acInd].aircraftID );
		}  else
		{
			for(m=0; m<curDutyP->origDutyP->numAc; m ++)//go through each ac
				if( acList[curDutyP->origDutyP->acInd[m]].aircraftID == dlCrewAssigns[i].aircraftID )//found
				{
					if( m == 0 )//first ac
						_ASSERTE( dlCrewAssigns[i].startTm == curDutyP->pDutyStartTm[k]);
					else
						_ASSERTE( dlCrewAssigns[i].startTm == legInfosCP[curDutyP->origDutyP->acEndLeg[m-1]].leg->schedIn );//previous ac end time

					if( m == curDutyP->origDutyP->numAc - 1 )//last ac
						_ASSERTE( dlCrewAssigns[i].endTm == curDutyP->endTm + Minute*optParam.postFlightTm );
					else
						_ASSERTE( dlCrewAssigns[i].endTm == legInfosCP[curDutyP->origDutyP->acEndLeg[m]].leg->schedIn );

					break;
				}
			_ASSERTE( m<curDutyP->origDutyP->numAc );//must contain
		}
	}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////end check solution

	//after checking solution //crew assigments sorted by crew ID, then start time
	qsort((void *) dlCrewAssigns, numDlCrewAssigns, sizeof(ProposedCrewAssg), compareCrewAssign);

	fprintf (dlFile, "\n --> Crew assignments ( %d total ) :\n", numDlCrewAssigns);

	fprintf (dlFile, "+-----------+---------------+----------+--------------------+-------------------+\n");
	fprintf (dlFile, "|  Crew ID  |  Aircraft ID  | Position |     Start Time     |     End Time      |\n");
	fprintf (dlFile, "+-----------+---------------+----------+--------------------+-------------------+\n");
	for (i=0; i<numDlCrewAssigns; i++)
		fprintf (dlFile, "|  %6d   |    %6d     |    %1d     |  %15s  |  %15s |\n",
					dlCrewAssigns[i].crewID,
					dlCrewAssigns[i].aircraftID,
					dlCrewAssigns[i].position,
					dt_DateTimeToDateTimeString(dt_time_tToDateTime (dlCrewAssigns[i].startTm), opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt_time_tToDateTime (dlCrewAssigns[i].endTm), opbuf2, "%Y/%m/%d %H:%M"));
	fprintf (dlFile, "+-----------+---------------+----------+--------------------+-------------------+\n\n");

	*crewAssignP = dlCrewAssigns ;
	*numCrewAssignP = numDlCrewAssigns ;

	if( caToOptCol != NULL )
	{
		free(caToOptCol );
		caToOptCol = NULL ;
	}
	
	return 0;
}

static int printSolution ( const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_OrigDuty **origDuties 
, const DL_LegInfo *legInfosCP, const DL_LpColumn *pickedLpCols, const int numPickedLpCols, const DL_AvailInfo *availAcs
, const DL_AvailInfo *availPilots, const int *colStartInd, const int *dColToDInd, const int *aColToAInd, const double *x, const double obj)
{
	int **acUsed=NULL, *pilotUsed=NULL ;
	int i, j, k, m, n, demInd, cpInd ;
	char opbuf1[1024], opbuf2[1024], opbuf3[1024], opbuf4[1024], opbuf5[1024], opbuf6[1024], opbuf7[1024];
	DL_NewDuty *curDutyP=NULL;
	double oTravelCost, nTravelCost, demPen, acPen;
	const double roundToOne = 0.5 ;
	
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////check each opt duty is covered
	fprintf (dlFile, "\n\n --> Uncovered opt duties :\n");
	for(i=0; i < numCrewPairs; i++)
	{
		if( !origDuties[i] )
			continue;
		for(j=0; j < optParam.planningWindowDuration; j++)
		{
			if( !origDuties[i][j].numAc )
				continue;
			m = 0;
			for(k=0; k < numPickedLpCols; k++)
				if( pickedLpCols[k].newDutyP->origDutyP && pickedLpCols[k].newDutyP->origDutyP == &(origDuties[i][j]) )
					m ++;
			if( !m )
				fprintf(dlFile, " Opt duty with cp %d day %d not covered \n", i, j );
		}
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////print duties with more than one acs
	fprintf (dlFile, "\n\n --> Duty with more than one airplanes :\n");
	for(i=0; i < numPickedLpCols; i++)
	{
		curDutyP = pickedLpCols[i].newDutyP ;
		if( curDutyP->origDutyP && curDutyP->origDutyP->numAc > 1 )
		{
			fprintf(dlFile, " Opt column %d contains %d aircrafts: ", i, curDutyP->origDutyP->numAc );
			for(j=0; j < curDutyP->origDutyP->numAc; j ++)
				fprintf(dlFile, " %d ; ", acList[curDutyP->origDutyP->acInd[j]].aircraftID );
			fprintf(dlFile, " \n" );
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////print soln
	fprintf (dlFile, "\n\n --> Solution :\n");
	j = -1 ; //current tour index
	for(i=0; i < numPickedLpCols; i++)
	{
		curDutyP = pickedLpCols[i].newDutyP ;

		if( j == -1 || curDutyP->startInfoP->index != j )
		{
			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------------+------------+------------------+------------+------------------+------------------+------------------+------------------+------------------+------------+------------+------------+------------+------------+------------+ \n");
			fprintf (dlFile, "| Day        | First Day  | Keep Orig  | Crew Pair  | Aircraft   | Ac Airport | Ac Avail Tm      | P0 Airport | P0 Avail Tm      | P1 Airport | P1 Avail Tm      | P0 Start Time    | P1 Start Time    | Start Time       | End Time         | Cost       | # Cvd Dems | Is First   | Drop off   | Next Day   | Has Legs   | \n");
			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------------+------------+------------------+------------+------------------+------------------+------------------+------------------+------------------+------------+------------+------------+------------+------------+------------+ \n");
		}

		fprintf (dlFile, "| %10d | %10d | %10d | %10d | %10d | %10d | %16s | %10d | %16s | %10d | %16s | %16s | %16s | %16s | %16s | %10.2f | %10d | %10d | %10d | %10d | %10d | \n"
		, curDutyP->day
		, curDutyP->firstDay
		, curDutyP->keepOriginal
		, curDutyP->startInfoP->cpInd
		, acList[curDutyP->startInfoP->ac.ind].aircraftID
		, curDutyP->acAirport
		, ( !curDutyP->acTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->acTm), opbuf1, "%Y/%m/%d %H:%M"))
		, curDutyP->pAirports[0]
		, ( !curDutyP->pTms[0] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->pTms[0]), opbuf2, "%Y/%m/%d %H:%M"))
		, curDutyP->pAirports[1]
		, ( !curDutyP->pTms[1] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->pTms[1]), opbuf3, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->pilotStartTm[0] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->pilotStartTm[0]), opbuf4, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->pilotStartTm[1] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->pilotStartTm[1]), opbuf5, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->startTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->startTm), opbuf6, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->endTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->endTm), opbuf7, "%Y/%m/%d %H:%M"))
		, curDutyP->cost, curDutyP->numCoverDems, pickedLpCols[i].fDuty, pickedLpCols[i].dropOff, pickedLpCols[i].nextDay
		, ( curDutyP->origDutyP ? 1 : 0 ) ) ;

		fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------------+------------+------------------+------------+------------------+------------------+------------------+------------------+------------------+------------+------------+------------+------------+------------+------------+ \n");

		j = curDutyP->startInfoP->index ;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////print soln in less fields
	fprintf (dlFile, "\n\n --> Solution (simple) :\n");
	fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+------------+ \n");
	fprintf (dlFile, "| Index      | Aircraft   | P0         | P1         | FirstDuty? | Day        | Next Day   | Drop Off?  | Start Time       | End Time         | Orig Start Time  | Orig End Time    | Tour Index | \n");
	fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+------------+ \n");

	for(i=0; i < numPickedLpCols; i++)
	{
		curDutyP = pickedLpCols[i].newDutyP ;
		cpInd = curDutyP->startInfoP->cpInd ;

		fprintf (dlFile, "| %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %16s | %16s | %16s | %16s | %10d | \n"
		, i 
		, acList[curDutyP->acInd].aircraftID
		, crewPairList[cpInd].captainID
		, crewPairList[cpInd].flightOffID
		, pickedLpCols[i].fDuty, curDutyP->day
		, pickedLpCols[i].nextDay
		, pickedLpCols[i].dropOff
		, ( !curDutyP->startTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->startTm), opbuf1, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->endTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->endTm), opbuf2, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->origDutyP ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->origDutyP->firstLegP->leg->schedOut), opbuf3, "%Y/%m/%d %H:%M"))
		, ( !curDutyP->origDutyP ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (curDutyP->origDutyP->lastLegP->leg->schedIn), opbuf4, "%Y/%m/%d %H:%M")) 
		, curDutyP->startInfoP->index );

		if( i == numPickedLpCols - 1 || pickedLpCols[i].newDutyP->acInd != pickedLpCols[i+1].newDutyP->acInd || pickedLpCols[i].newDutyP->startInfoP->cpInd != pickedLpCols[i+1].newDutyP->startInfoP->cpInd )
			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+------------+ \n");
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////for each demand, print columns covering it
	fprintf (dlFile, "\n\n --> To-be-covered-demands and covering columns, divided by fleet type : \n\n");
	for(j=colStartInd[DL_Col_Dem]; j < colStartInd[DL_Col_Dem+1]; j++)
	{
		demInd = dColToDInd[j-colStartInd[DL_Col_Dem]] ;

		fprintf (dlFile, "Demand %d covered in column :", demandList[demInd].demandID );
		n = -1 ; //which newDutyP contains this demand
		for(i=0; i < numPickedLpCols; i++)
		{
			if( pickedLpCols[i].newDutyP->origDutyP )//check whether the demand is contained in this duty
			{
				for(m=pickedLpCols[i].newDutyP->origDutyP->firstLegP->legIndCP; m <= pickedLpCols[i].newDutyP->origDutyP->lastLegP->legIndCP; m ++ )
					if( legInfosCP[m].leg->demandID == demandList[demInd].demandID )
					{
						n = i ;
						break;
					}
			}
			for(k=0; k<pickedLpCols[i].newDutyP->numCoverDems; k++)//check whether the demand is covered by this duty
				if( pickedLpCols[i].newDutyP->coverDems[k] == demInd )
					if( pickedLpCols[i].newDutyP->origDutyP )
						fprintf (dlFile, "n-trivial %d ; ", i );
					else
						fprintf (dlFile, "trivial %d ; ", i );
		}
		fprintf (dlFile, " \n --> with deficit : %.2f \n", x[j] );
		//_ASSERTE( n >= 0 || !dlDemUsed[demInd] );
		if( n >= 0)
			fprintf (dlFile, " --> contained in column : %d \n ", n );
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////for each ac, print columns containing it
	if (!(acUsed = (int **) calloc (numAircraft, sizeof (int*))))//ac used in a column in the solution
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	for(i=0; i<numAircraft; i ++)
		if (!(acUsed[i] = (int *) calloc (optParam.planningWindowDuration, sizeof (int))))//ac used in a column in the solution
		{
			logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
			exit(1);
		}

	fprintf (dlFile, "\n\n --> Available aircrafts and covering columns, divided by fleet type : \n\n");
	for(m=0; m < numAcTypes; m++)
	{
		fprintf (dlFile, "\n Fleet Type %d : \n", m );

		for(j=0; j < numAircraft; j++)
		{
			if( acList[j].acTypeIndex != m )
				continue;

			fprintf (dlFile, "Aircraft %d ; Orig available day %d;  \n", acList[j].aircraftID, timeToDay(acList[j].availDT) );

			for( k=0; k < optParam.planningWindowDuration; k ++)
			{
				fprintf (dlFile, "On day %d : ", k );

				_ASSERTE( acUsed[j][k] == 0 );
				for(i=0; i < numPickedLpCols; i++)//find an opt newDutyP covering this ac on this day
				{
					if( pickedLpCols[i].newDutyP->day != k )
						continue;

					if( !pickedLpCols[i].newDutyP->origDutyP )//no legs
					{
						if( pickedLpCols[i].newDutyP->acInd != j )//use pickedLpCols[i].newDutyP->acInd //one ac
							continue;
						fprintf (dlFile, " trivial %d ; ", i );
					} else
					{
						for(n=0; n < pickedLpCols[i].newDutyP->origDutyP->numAc; n ++)//go through all acs of this duty
							if( pickedLpCols[i].newDutyP->origDutyP->acInd[n] == j )
								break;
						if( n >= pickedLpCols[i].newDutyP->origDutyP->numAc )//not found
							continue;
						fprintf (dlFile, " n-trivial %d ; ", i );
					}
					acUsed[j][k] = 1 ;
				}
				if( !acUsed[j][k] )//not crewed
				{
					_ASSERTE( colStartInd[DL_Col_Ac] + j*optParam.planningWindowDuration + k < colStartInd[DL_Col_Ac+1]
					&& aColToAInd[j*optParam.planningWindowDuration + k] == j );

					if( availAcs[j].availAfter )
						fprintf (dlFile, " Not used; AvailableAfter; " );
					else
						fprintf (dlFile, " Not used; Not AvailableAfter; ");
					fprintf (dlFile, " Uncovered %.2f \n", x[colStartInd[DL_Col_Ac] + j*optParam.planningWindowDuration + k]);
				} else
					fprintf (dlFile, " Used \n");
			}//end for( k=0; k < optParam.planningWindowDuration; k ++)
		}//end for(j=0; j < numAircraft; j++)
	}//end for(m=0; m < numAcTypes; m++)

	for(j=0; j < numAircraft; j++)
		for( k=0; k < optParam.planningWindowDuration; k ++)
			if( x[colStartInd[DL_Col_Ac] + j*optParam.planningWindowDuration + k] < roundToOne )
				_ASSERTE( acUsed[j][k] ) ;

////////////////////////////////////////////////////////////////////////////////////////////////////////////for each pilot, print columns containing it
	if (!(pilotUsed = (int *) calloc (numCrew, sizeof (int))))//pilot used in a column in the solution
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	fprintf (dlFile, "\n\n --> Available pilots and covering columns, divided by fleet type : \n\n");
	for(m=0; m < numAcTypes; m++)
	{
		fprintf (dlFile, "\n Fleet Type %d : \n", m );

		for(j=0; j < numCrew; j++)
		{
			if( crewList[j].acTypeIndex != m )
				continue;

			fprintf (dlFile, "Pilot %d :", crewList[j].crewID );

			for(i=0; i < numPickedLpCols; i++)
				if( pickedLpCols[i].newDutyP->startInfoP->pilots[0].ind == j || pickedLpCols[i].newDutyP->startInfoP->pilots[1].ind == j )
				{
					if( pickedLpCols[i].newDutyP->origDutyP )
						fprintf (dlFile, "n-trivial %d ; ", i );
					else
						fprintf (dlFile, "trivial %d ; ", i );
					pilotUsed[j] = 1 ;
				}

			if( pilotUsed[j] )
				fprintf (dlFile, " Used \n");
			else if( !availPilots[j].availAfter )
				fprintf (dlFile, " Not used; Not AvailableAfter \n");
			else
				fprintf (dlFile, " Not used; AvailableAfter  \n");
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////for each crew pair, print columns containing it
	fprintf (dlFile, "\n\n --> Available crew pairs and covering columns, divided by fleet type : \n\n");
	for(m=0; m < numAcTypes; m++)
	{
		fprintf (dlFile, "\n Fleet Type %d : \n", m );

		for(j=0; j < numCrewPairs; j++)
		{
			if( crewPairList[j].acTypeIndex != m )
				continue;

			fprintf (dlFile, "Crew pair %d ( %d, %d) :", j, crewPairList[j].captainID, crewPairList[j].flightOffID);

			k = 0 ;
			for(i=0; i < numPickedLpCols; i++)
				if( pickedLpCols[i].newDutyP->startInfoP->cpInd == j )
				{
					if( pickedLpCols[i].newDutyP->origDutyP )
						fprintf (dlFile, "n-trivial %d ; ", i );
					else
						fprintf (dlFile, "trivial %d ; ", i );
					k = 1 ;
				}
			if( !k ) //not used 
			{
				if( pilotUsed[crewPairList[j].crewListInd[0]] || pilotUsed[crewPairList[j].crewListInd[1]] )
					fprintf (dlFile, " Not used; Pilot(s) used by other tours \n");
				else if( !availPilots[crewPairList[j].crewListInd[0]].availAfter ||  !availPilots[crewPairList[j].crewListInd[1]].availAfter )
					fprintf (dlFile, " Not used; Pilot(s) not AvailableAfter \n");
				else
					fprintf (dlFile, " Not used; Crew pair AvailableAfter \n");
			} else
				fprintf (dlFile, " Used \n");
		}
	}//end for(m=0; m < numAcTypes; m++)

/////////////////////////////////////////////////////////////////////////////////////////////////////////for each start info, print columns containing it
	fprintf (dlFile, "\n\n --> All possible (crew pair, ac) paris, divided by fleet type : \n\n");
	for(m=0; m < numAcTypes; m++)
	{
		fprintf (dlFile, "\n Fleet Type %d : \n", m );

		for(j=0; j < numStartInfo; j++)
		{
			if( crewPairList[tourStartInfos[j].cpInd].acTypeIndex != m )
				continue;

			fprintf (dlFile, "Start info %d, ac (%d), pilots(%d, %d) :", j, acList[tourStartInfos[j].ac.ind].aircraftID, crewPairList[tourStartInfos[j].cpInd].captainID
			, crewPairList[tourStartInfos[j].cpInd].flightOffID);

			k = 0 ;
			for(i=0; i < numPickedLpCols; i++)
				if( pickedLpCols[i].newDutyP->startInfoP->index == tourStartInfos[j].index )
				{
					fprintf (dlFile, " %d ; ", i );
					k = 1 ;
				}
			if( !k ) //not used 
			{
				_ASSERTE( tourStartInfos[j].type == DL_NewAvail && availAcs[tourStartInfos[j].ac.ind].availAfter 
				&& availPilots[crewPairList[tourStartInfos[j].cpInd].crewListInd[0]].availAfter
				&& availPilots[crewPairList[tourStartInfos[j].cpInd].crewListInd[1]].availAfter) ;

				for(i=0; i < optParam.planningWindowDuration; i++)
					if( acUsed[tourStartInfos[j].ac.ind][i] )
						break;

				if( i < optParam.planningWindowDuration
				|| pilotUsed[crewPairList[tourStartInfos[j].cpInd].crewListInd[0]] || pilotUsed[crewPairList[tourStartInfos[j].cpInd].crewListInd[1]] )
					fprintf (dlFile, " Not used: Ac/Pilot(s) used by other tours \n");
				else
					fprintf (dlFile, " Not Used; Available \n");
			} else
				fprintf (dlFile, " Used \n");
		}
	}//end for(m=0; m < numAcTypes; m++)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////total cost
	oTravelCost = nTravelCost = 0 ;
	for(i=0; i < numPickedLpCols; i++)
	{
		if( pickedLpCols[i].newDutyP->startInfoP->type == DL_NewAvail )
			nTravelCost += pickedLpCols[i].newDutyP->cost ;
		else
			oTravelCost += pickedLpCols[i].newDutyP->cost ;
	}

	demPen = acPen = 0 ;
	for(i=colStartInd[DL_Col_Dem]; i < colStartInd[DL_Col_Dem+1]; i++)
		if( x[i] > 0 )//uncovered demand
			demPen += PenaltyDem*x[i] ;

	for(i=colStartInd[DL_Col_Ac]; i < colStartInd[DL_Col_Ac+1]; i++)
		if( x[i] > 0 )//uncrewed ac
			acPen += (acList[aColToAInd[i-colStartInd[DL_Col_Ac]]].isMac ? PenaltyMacPerDay : PenaltyAcPerDay)*x[i] ;

	//_ASSERTE( tCost == oTravelCost + nTravelCost + demPen + acPen);

	fprintf(dlFile, "\n Previous travel cost: %.2f \n New Additionl travel cost: %.2f \n Demand covering penalty: %.2f \n Aircraft crewing penalty: %.2f \n Total : %.2f \n Objective: %.2f \n "
	, oTravelCost , nTravelCost , demPen , acPen, oTravelCost + nTravelCost + demPen + acPen, obj); 

	fprintf(dlFile, "\n Duties that are kept original: \n");
	for(i=0; i < numPickedLpCols; i++)
	{
		if( !pickedLpCols[i].newDutyP->keepOriginal )
			continue;

		switch ( pickedLpCols[i].newDutyP->keepOriginal )
		{
			case DL_KO_DutyTime:
				fprintf(dlFile, " Duty %d : Maximum duty time violates \n", i);
				break;

			case DL_KO_TmFixed:
				fprintf(dlFile, " Duty %d : Start and end time fixed \n", i);
				break;

			case DL_KO_NoTravel:
				fprintf(dlFile, " Duty %d : No feasible travels \n", i);
				break;

			case DL_KO_Combined:
				fprintf(dlFile, " Duty %d : Combined with other duties \n", i);
				break;

			default:
				_ASSERTE( 0 == 1);
		}
	}

	fflush(dlFile);
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////end print out solution

	if( acUsed != NULL )
	{
		for( k=0; k < optParam.planningWindowDuration; k ++)
		{
			free( acUsed[k] );
			acUsed[k] = NULL ;
		}
		free( acUsed );
		acUsed = NULL;
	}
	if( pilotUsed != NULL )
	{
		free( pilotUsed );
		pilotUsed = NULL;
	}

	return 0;
}


static int getNewDuties(const int* longMaint, const int numLongMaint, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, const int numStartInfo
, DL_NewDuty *****ndP, int ****numNDP)
{
	int i, fDay, dayZero, dayOne ;
	DL_NewDuty ****newDuties = NULL;
	int ***numNewDuties = NULL;
	DL_OrigDuty *prevCpAc=NULL, *curCpAc=NULL ;

	_ASSERTE( numStartInfo > 0 );
	if (!(newDuties = (DL_NewDuty ****) calloc (numStartInfo, sizeof (DL_NewDuty***))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(numNewDuties = (int ***) calloc (numStartInfo, sizeof (int**))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}

	for(i=0; i < numStartInfo ; i ++)
	{
		if (!(newDuties[i] = (DL_NewDuty ***) calloc (optParam.planningWindowDuration, sizeof (DL_NewDuty**))))
		{
			logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
			exit(1);
		}
		if (!(numNewDuties[i] = (int **) calloc (optParam.planningWindowDuration, sizeof (int*))))
		{
			logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
			exit(1);
		}

		for(fDay=0; fDay < optParam.planningWindowDuration; fDay ++)
		{
			if (!(newDuties[i][fDay] = (DL_NewDuty **) calloc (2, sizeof (DL_NewDuty*))))
			{
				logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
				exit(1);
			}
			if (!(numNewDuties[i][fDay] = (int *) calloc (2, sizeof (int))))
			{
				logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
				exit(1);
			}
		}

		fDay = -1 ; //first feasible crewing duty: including travels
		//process days before  tourStartInfos[i].legStartDay: 
		//dayZero: first crewing duty
		for( dayZero = tourStartInfos[i].acCpDay; dayZero <= tourStartInfos[i].legStartDay; dayZero ++ )
			if( !getNewDutiesOnADay(longMaint, numLongMaint, origDuties, &(tourStartInfos[i]), dayZero, dayZero, &(newDuties[i][dayZero][0]), &(numNewDuties[i][dayZero][0])) 
			&& fDay == -1 )
				fDay = dayZero ;

		if( fDay >= 0 )//if there exist feasible crew days
		{
			for( dayOne=fDay+1; dayOne <= tourStartInfos[i].lastPossibleDay; dayOne ++ )//process days that do not include travels
				getNewDutiesOnADay(longMaint, numLongMaint, origDuties, &(tourStartInfos[i]), -1, dayOne, &( newDuties[i][dayOne][1]), &(numNewDuties[i][dayOne][1]) );
		} else
			_ASSERTE( tourStartInfos[i].type == DL_NewAvail );
	}//end for(i=0; i < numStartInfo ; i ++)


	*ndP = newDuties ;
	*numNDP = numNewDuties ;
	return 0;
}


static int checkNewDuties(const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, const int numStartInfo
, const DL_NewDuty ****ndP, const int ***numNDP)
{
	int i, j, k, m, day;
	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ; //07/17/2017 ANG - only for debugging purpose
	
	_ASSERTE( ndP && numNDP ); // && naColumns && numNACols ) ;

	for(i=0; i < numStartInfo ; i ++)
	{
		_ASSERTE( ndP[i] && numNDP[i] );//allocated

		for( day=0; day < optParam.planningWindowDuration; day ++ )//check each possible day to start
		{
			_ASSERTE( ndP[i][day] && numNDP[i][day] );//allocated

			if( tourStartInfos[i].type != DL_NewAvail && origDuties[tourStartInfos[i].cpInd] && origDuties[tourStartInfos[i].cpInd][day].numAc )
				_ASSERTE( numNDP[i][day][0] + numNDP[i][day][1] );

			for( k=0; k<2; k++ ) //two types: k=0 --> first duty
			{
				if( numNDP[i][day][k] )//duties generated
				{
					_ASSERTE( ndP[i][day][k] && numNDP[i][day][k] > 0 );

					for(j=0; j <numNDP[i][day][k]; j ++)//for each duty
					{
						const DL_NewDuty *curP = &(ndP[i][day][k][j]) ;

						_ASSERTE( !curP->keepOriginal || (numNDP[i][day][k] == 1 && curP->origDutyP && curP->cost == 0 ));

						_ASSERTE( curP->startInfoP == &(tourStartInfos[i]) //same start info
						&& curP->day == day  //same day
						&& ( curP->firstDay == -1 || curP->startInfoP->acCpDay <= curP->firstDay ) //start after available
						&& (curP->endTm - curP->startTm >= MinCrewInterval || curP->origDutyP ) //long enough interval
						//&& curP->cost >= 0 //valid cost
						&& curP->acInd >= 0
						&& curP->startInfoP->acCpDay <= day && day <= curP->startInfoP->lastPossibleDay//day in range
						&& ((curP->pilotStartTm[0] < curP->startTm && curP->pilotStartTm[1] < curP->startTm ) || ( curP->origDutyP 
						&& (curP->origDutyP->tmFixed == DL_StartTmFixed || curP->origDutyP->tmFixed == DL_BothTmFixed || curP->origDutyP->tmFixed == DL_DutyCombined )))
						&& curP->pilotStartTm[0] > 0 && curP->pilotStartTm[1] > 0 
						&& curP->pilotStartTm[0] <= curP->pDutyStartTm[0] && curP->pilotStartTm[1] <= curP->pDutyStartTm[1]
						&& ( curP->keepOriginal 
							|| (curP->acAirport > 0 && curP->acTm > 0 && curP->pAirports[0] > 0 && curP->pTms[0] > 0 && curP->pAirports[1] > 0 && curP->pTms[1] > 0 ))); //valid start time

						if( k ) //not the first duty
							_ASSERTE( curP->day > curP->firstDay //not the first duty
							//&& curP->cost == 0 //no travels
							&& (curP->day <= curP->startInfoP->legStartDay || curP->pilotStartTm[0] == curP->pilotStartTm[1]
								|| curP->pDutyStartTm[0] == curP->pDutyStartTm[1] )); //no travels
						else//first duty
							_ASSERTE( curP->day == curP->firstDay );

						if( curP->origDutyP ) //legs on current day
						{
							_ASSERTE( curP->startInfoP->type != DL_NewAvail //must be an opt tour
							&& (k || curP->startInfoP->ac.ind == curP->origDutyP->acInd[0]) //same ac for the first duty
							&& curP->startInfoP->cpInd == curP->origDutyP->firstLegP->leg->crewPairInd //same cp
							&& origDuties[curP->startInfoP->cpInd] && origDuties[curP->startInfoP->cpInd][day].numAc
							&& curP->origDutyP == &( origDuties[curP->startInfoP->cpInd][day] ) //same duty
							&& curP->startTm <= curP->origDutyP->firstLegP->leg->schedOut && curP->endTm >= curP->origDutyP->lastLegP->leg->schedIn ); //vaild crew interval
							//connect following legs
							if( curP->origDutyP->tmFixed == DL_EndTmFixed || curP->origDutyP->tmFixed == DL_BothTmFixed || curP->origDutyP->tmFixed == DL_DutyCombined )
								_ASSERTE( curP->endTm == curP->origDutyP->lastLegP->leg->schedIn );
							else
							{
								if( curP->origDutyP->lastLegP->nextAcLeg )
									_ASSERTE( curP->endTm + Minute*optParam.turnTime <= curP->origDutyP->lastLegP->nextAcLeg->leg->schedOut );

								for(m=0; m <2; m++)
									if( curP->origDutyP->lastLegP->nextPilotLegs[m] )
										//_ASSERTE( curP->endTm + longRestTm <= curP->origDutyP->lastLegP->nextPilotLegs[m]->leg->schedOut );
										_ASSERTE( curP->endTm + (longRestTm + Minute*acTypeList[crewPairList[curP->startInfoP->cpInd].acTypeIndex].preFlightTm) <= curP->origDutyP->lastLegP->nextPilotLegs[m]->leg->schedOut ); // 07/17/2017 ANG
							}
							//connect previous legs
							if( curP->origDutyP->tmFixed == DL_StartTmFixed || curP->origDutyP->tmFixed == DL_BothTmFixed || curP->origDutyP->tmFixed == DL_DutyCombined )
								_ASSERTE( curP->startTm == curP->origDutyP->firstLegP->leg->schedOut );
							else
							{
								if( curP->origDutyP->firstLegP->prevAcLeg )
									_ASSERTE( curP->origDutyP->firstLegP->prevAcLeg->leg->schedIn + Minute*optParam.turnTime <= curP->startTm );

								for(m=0; m <2; m++)
									if( curP->origDutyP->firstLegP->prevPilotLegs[m] )
										//_ASSERTE( curP->origDutyP->firstLegP->prevPilotLegs[m]->leg->schedIn + longRestTm <= curP->startTm );
										_ASSERTE( curP->origDutyP->firstLegP->prevPilotLegs[m]->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[curP->startInfoP->cpInd].acTypeIndex].preFlightTm) <= curP->startTm ); // 07/17/2017 ANG
							}
						} else
						{
							_ASSERTE( !origDuties[curP->startInfoP->cpInd] || !origDuties[curP->startInfoP->cpInd][day].numAc ) ;
						}
					}//end j
				} else //no feasible duties 
					_ASSERTE( !origDuties[tourStartInfos[i].cpInd] || !origDuties[tourStartInfos[i].cpInd][day].numAc //no legs on current day
					//can't start after a leg, and first leg is not in the first duty
					|| (tourStartInfos[i].type != DL_NewAvail && ( (k==0 && day > tourStartInfos[i].legStartDay) || (k==1 && day == tourStartInfos[i].legStartDay )))
					|| (tourStartInfos[i].type == DL_NewAvail && day < tourStartInfos[i].acCpDay ));
			}//end for( k=0; k<2; k++ ) 
		}//end for( day=0; day < optParam.planningWindowDuration; day ++ )
	}//end for(i=0; i < numStartInfo ; i ++)

	return 0;
}

static int timeToDay(const time_t timeT)
{
	int day;

	for(day=0; day < MAX_WINDOW_DURATION; day ++)
		if( timeT <= firstEndOfDay + day*DayInSecs)
			break;
	//_ASSERTE( day < optParam.planningWindowDuration );
	day = min( day, optParam.planningWindowDuration - 1); //if day is later

	return day;
}

static int acIDToInd(const int acID)
{
	int acInd;

	_ASSERTE( acID > 0 );
	for(acInd = 0; acInd < numAircraft; acInd ++)
		if( acList[acInd].aircraftID == acID )
			break;
	_ASSERTE( acInd < numAircraft );

	return acInd;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int getOriginalDuties(const DL_DemUsedType *dlDemUsed, DL_LegInfo *legInfosCP, DL_OrigDuty ***origDutiesP )
{
	int day,firstTourLeg, lastTourLeg, lastDutyLeg, firstDutyLeg, acInd[DL_MAX_NUM_AC_PER_DUTY], acEndLeg[DL_MAX_NUM_AC_PER_DUTY] ;
	int j, m, numAcInd, cpInd, dutyNum ;
	DL_OrigDuty **origDuties=NULL, *curDutyP=NULL, *nextDutyP=NULL ;
	DL_LegInfo *legP=NULL ;

	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ;// 07/17/2017 ANG

	//allocate
	_ASSERTE( numCrewPairs > 0 ) ;
	if (!(origDuties = (DL_OrigDuty **) calloc (numCrewPairs, sizeof (DL_OrigDuty*))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////go through legs to find duties of each crew pair
	firstTourLeg = 0 ;//index in legInfosCP[]
	while( firstTourLeg < numPropMgdLegs )
	{
		cpInd = legInfosCP[firstTourLeg].leg->crewPairInd ; //current cp
 
		//starting from firstTourLeg, get last leg of the same cp //assume legs are sorted
		for(lastTourLeg = firstTourLeg ; lastTourLeg < numPropMgdLegs-1 ; lastTourLeg ++)//numPropMgdLegs-1
			if( legInfosCP[lastTourLeg+1].leg->crewPairInd !=  cpInd )
				break;
		
		//current tour: [firstTourLeg, lastTourLeg]: all legs of this crew pair ind 
		//allocate
		_ASSERTE( origDuties[cpInd] == NULL );
		if( !( origDuties[cpInd] = (DL_OrigDuty *) calloc (optParam.planningWindowDuration, sizeof (DL_OrigDuty))))
		{
			logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
			exit(1);
		}

		//go through all legs in [firstTourLeg, lastTourLeg] to find all duties
		firstDutyLeg = firstTourLeg ; //initialize first leg of first duty
		dutyNum = 0; //dutyNum-th duty in this tour
		while( firstDutyLeg <= lastTourLeg )
		{
			//find end of this duty
			for( lastDutyLeg = firstDutyLeg ; lastDutyLeg < lastTourLeg; lastDutyLeg ++)
				//if ( legInfosCP[lastDutyLeg+1].leg->schedOut - legInfosCP[lastDutyLeg].leg->schedIn >= longRestTm )
				if ( legInfosCP[lastDutyLeg+1].leg->schedOut - legInfosCP[lastDutyLeg].leg->schedIn >= (longRestTm + Minute*acTypeList[crewPairList[legInfosCP[lastTourLeg+1].leg->crewPairInd].acTypeIndex].preFlightTm) ) // 07/17/2017 ANG
					break;

			//current duty: [firstDutyLeg, lastDutyLeg]
			//find the day of this duty //return the first day if un-decidable (e.g. single leg across two days)
			getDayFromMgdLegs(dlDemUsed, legInfosCP, firstDutyLeg, lastDutyLeg, &day) ;
			
			//get list of acs assigned to this duty
			getAcIndFromMgdLegs(legInfosCP, firstDutyLeg, lastDutyLeg, acInd, acEndLeg, &numAcInd ) ;

			curDutyP = &(origDuties[cpInd][day]) ;
			if( curDutyP->numAc > 0 ) //this cp has been assigned: possible when more than one duties on a single day
			{
				_ASSERTE( curDutyP->firstLegP != NULL && curDutyP->lastLegP != NULL && curDutyP->acInd[0] >= 0 && curDutyP->numAc >= 1 && numAcInd > 0 
				&& curDutyP->lastLegP->legIndCP == firstDutyLeg - 1 ) ; //curDutyP must be right before current duty, ASSUME order of legs

				//add new acs //check the first ac: if the first ac is the same as the last ac, skip the first ac in acInd
				if( acInd[0] == curDutyP->acInd[curDutyP->numAc-1] )
				{
					m = 1 ;
					curDutyP->acEndLeg[curDutyP->numAc-1] = acEndLeg[0] ;
				} else
					m = 0 ;

				for( j = m; j < numAcInd; j++ )//add new acs 
				{
					curDutyP->acInd[curDutyP->numAc] = acInd[j] ;
					curDutyP->acEndLeg[curDutyP->numAc] = acEndLeg[j] ;
					curDutyP->numAc ++ ;
					_ASSERTE( curDutyP->numAc <= DL_MAX_NUM_AC_PER_DUTY ) ;
				}

				//update the last leg of the duty
				curDutyP->lastLegP = &(legInfosCP[lastDutyLeg]) ;
				curDutyP->tmFixed = DL_DutyCombined ;
			} else //generate a new duty
			{
				curDutyP->firstLegP = &(legInfosCP[firstDutyLeg]) ;
				curDutyP->lastLegP = &(legInfosCP[lastDutyLeg]) ;
				curDutyP->dutyNum = dutyNum ;
				curDutyP->numAc = numAcInd ;
				memmove( curDutyP->acInd, acInd, sizeof(curDutyP->acInd)) ;
				memmove( curDutyP->acEndLeg, acEndLeg, sizeof(curDutyP->acEndLeg)) ;

				//no enough rest time, infeasible; will not adjust this duty, i.e. keep the original
				//if( (curDutyP->firstLegP->prevPilotLegs[0] && curDutyP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm > curDutyP->firstLegP->leg->schedOut) 
				if( (curDutyP->firstLegP->prevPilotLegs[0] && curDutyP->firstLegP->prevPilotLegs[0]->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) > curDutyP->firstLegP->leg->schedOut) // 07/17/2017 ANG
				//|| (curDutyP->firstLegP->prevPilotLegs[1] && curDutyP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm > curDutyP->firstLegP->leg->schedOut) 
				|| (curDutyP->firstLegP->prevPilotLegs[1] && curDutyP->firstLegP->prevPilotLegs[1]->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) > curDutyP->firstLegP->leg->schedOut) // 07/17/2017 ANG
				//|| (curDutyP->lastLegP->nextPilotLegs[0] && curDutyP->lastLegP->leg->schedIn + longRestTm > curDutyP->lastLegP->nextPilotLegs[0]->leg->schedOut ) 
				|| (curDutyP->lastLegP->nextPilotLegs[0] && curDutyP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) > curDutyP->lastLegP->nextPilotLegs[0]->leg->schedOut ) // 07/17/2017 ANG
				//|| (curDutyP->lastLegP->nextPilotLegs[1] && curDutyP->lastLegP->leg->schedIn + longRestTm > curDutyP->lastLegP->nextPilotLegs[1]->leg->schedOut ))
				|| (curDutyP->lastLegP->nextPilotLegs[1] && curDutyP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) > curDutyP->lastLegP->nextPilotLegs[1]->leg->schedOut )) // 07/17/2017 ANG
					curDutyP->tmFixed = DL_BothTmFixed ; 
				else
				{
					//can't adjust the start time earlier
					if( (curDutyP->firstLegP->prevAcLeg && curDutyP->firstLegP->prevAcLeg->leg->schedIn + Minute*optParam.turnTime >= curDutyP->firstLegP->leg->schedOut) 
					|| curDutyP->firstLegP->leg->schedOut <= optParam.windowStart
					|| acList[acInd[0]].availDT >= curDutyP->firstLegP->leg->schedOut 
					|| crewList[crewPairList[cpInd].crewListInd[0]].availDT + Minute*( crewList[crewPairList[cpInd].crewListInd[0]].activityCode == 2 ? optParam.firstPreFltTm 
					//: optParam.preFlightTm ) >= curDutyP->firstLegP->leg->schedOut
					: acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm ) >= curDutyP->firstLegP->leg->schedOut //07/17/2017 ANG
					|| crewList[crewPairList[cpInd].crewListInd[1]].availDT + Minute*( crewList[crewPairList[cpInd].crewListInd[1]].activityCode == 2 ? optParam.firstPreFltTm 
					//: optParam.preFlightTm ) >= curDutyP->firstLegP->leg->schedOut 
					: acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm ) >= curDutyP->firstLegP->leg->schedOut //07/17/2017 ANG
					//|| (curDutyP->firstLegP->prevPilotLegs[0] && curDutyP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm == curDutyP->firstLegP->leg->schedOut) 
					|| (curDutyP->firstLegP->prevPilotLegs[0] && curDutyP->firstLegP->prevPilotLegs[0]->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) == curDutyP->firstLegP->leg->schedOut) // 07/17/2017 ANG
					//|| (curDutyP->firstLegP->prevPilotLegs[1] && curDutyP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm == curDutyP->firstLegP->leg->schedOut) )
					|| (curDutyP->firstLegP->prevPilotLegs[1] && curDutyP->firstLegP->prevPilotLegs[1]->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) == curDutyP->firstLegP->leg->schedOut) ) // 07/17/2017 ANG
						curDutyP->tmFixed = DL_StartTmFixed ;

					if((curDutyP->lastLegP->nextAcLeg && curDutyP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= curDutyP->lastLegP->nextAcLeg->leg->schedOut ) 
					|| curDutyP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= optParam.windowEnd 
					//|| (curDutyP->lastLegP->nextPilotLegs[0] && curDutyP->lastLegP->leg->schedIn + longRestTm == curDutyP->lastLegP->nextPilotLegs[0]->leg->schedOut ) 
					|| (curDutyP->lastLegP->nextPilotLegs[0] && curDutyP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) == curDutyP->lastLegP->nextPilotLegs[0]->leg->schedOut ) //07/17/2017 ANG
					//|| (curDutyP->lastLegP->nextPilotLegs[1] && curDutyP->lastLegP->leg->schedIn + longRestTm == curDutyP->lastLegP->nextPilotLegs[1]->leg->schedOut ))
					|| (curDutyP->lastLegP->nextPilotLegs[1] && curDutyP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) == curDutyP->lastLegP->nextPilotLegs[1]->leg->schedOut )) //07/17/2017 ANG
						curDutyP->tmFixed = ( curDutyP->tmFixed == DL_StartTmFixed ? DL_BothTmFixed : DL_EndTmFixed ) ;
				}
				dutyNum ++ ; 
			}//end if( !flag )

			//get day for each leg
			for(j=firstDutyLeg; j <= lastDutyLeg; j ++)
			{
				_ASSERTE( legInfosCP[j].day == -1 );
				legInfosCP[j].day = day ;
			}
			
			firstDutyLeg = lastDutyLeg + 1; //update new duty start
		}//while( firstDutyLeg <= lastTourLeg )
		firstTourLeg = lastTourLeg + 1; //update tour
	}//end ( firstTourLeg < numPropMgdLegs )

	//go through each cp assignment, and fix start time and end time for special cases
	for( cpInd=0; cpInd < numCrewPairs; cpInd ++)
	{
		if( origDuties[cpInd] == NULL )
			continue;

		for( day=0; day < optParam.planningWindowDuration; day ++)
		{
			//if not assigned on current day, or both start and end times are already fixed
			if( !origDuties[cpInd][day].numAc )
				continue;

			curDutyP = &(origDuties[cpInd][day]) ;

////////////////////////////////////////////////////////////////////if last ac or pilot dropped off after current duty, then fix the end time of current duty
			if(curDutyP->tmFixed == DL_NotFixed || curDutyP->tmFixed == DL_StartTmFixed )
			{
				//find the next assigned day of the same crew pair
				nextDutyP = NULL ;
				for( j = day+1; j < optParam.planningWindowDuration; j ++)
					if( origDuties[cpInd][j].numAc )
					{
						nextDutyP = &(origDuties[cpInd][j]) ;
						break;
					}

				//if ac is dropped off after current duty, and used later, then fix the end time of current duty
				//only check the last ac
				//check next ac legs
				if( ( !nextDutyP || curDutyP->lastLegP->acInd != nextDutyP->firstLegP->acInd )//ac is not used by this crew pair later
				&& (legP = curDutyP->lastLegP->nextAcLeg) != NULL && legP->leg->crewPairInd != cpInd //ac used later by another cp
				&& legP->legIndCP == origDuties[legP->leg->crewPairInd][legP->day].firstLegP->legIndCP ) //ac used in the FIRST leg of a duty
				{
					_ASSERTE( origDuties[legP->leg->crewPairInd] != NULL && origDuties[legP->leg->crewPairInd][legP->day].numAc
					&& curDutyP->lastLegP->leg->schedIn + Minute*optParam.turnTime < legP->leg->schedOut ) ;

					curDutyP->tmFixed = ( curDutyP->tmFixed == DL_NotFixed ? DL_EndTmFixed : DL_BothTmFixed ) ;

					//dont fix the start time of the next duty, because feasibility against CURRENT DUTY END TIME (FIXED) will be checked, when generating next duty's crew intervals
					/*
					if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_NotFixed )
						origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_StartTmFixed ;
					else if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_EndTmFixed )
						origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_BothTmFixed ;
					*/
				}

				//check next pilot legs
				if( !nextDutyP )//if last duty of the current crew pair
					for(m=0; m<2; m++)
					{
						if( ( legP = curDutyP->lastLegP->nextPilotLegs[m]) != NULL )
						{
							_ASSERTE( legP->leg->crewPairInd != cpInd
							//&& curDutyP->lastLegP->leg->schedIn + longRestTm < legP->leg->schedOut
							&& curDutyP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[legP->leg->crewPairInd].acTypeIndex].preFlightTm) < legP->leg->schedOut // 07/17/2017 ANG
							&& origDuties[legP->leg->crewPairInd] != NULL && origDuties[legP->leg->crewPairInd][legP->day].numAc
							&& legP->legIndCP == origDuties[legP->leg->crewPairInd][legP->day].firstLegP->legIndCP );//legP is the first leg in its duty

							curDutyP->tmFixed = ( curDutyP->tmFixed == DL_NotFixed ? DL_EndTmFixed : DL_BothTmFixed ) ;

							//dont fix the start time of the next duty, because feasibility against CURRENT DUTY END TIME (FIXED) will be checked, when generating next duty's crew intervals
							/*
							if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_NotFixed )
								origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_StartTmFixed ;
							else if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_EndTmFixed )
								origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_BothTmFixed ;
							*/
						}
					}
			}//end if(curDutyP->tmFixed == DL_NotFixed || curDutyP->tmFixed == DL_StartTmFixed )

			//consider the case of rest time between duties [previous duty, current duty] is in [longRestTm, longRestTm + 2*DiscrtLPInterval]
			//in this case, fix the end time of previous duty and start time of current duty
			for(m=0; m<2; m++)
				if( legP = curDutyP->firstLegP->prevPilotLegs[m]) //previous 
				{
					//if( legP->leg->schedIn + longRestTm < curDutyP->firstLegP->leg->schedOut
					if( legP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) < curDutyP->firstLegP->leg->schedOut // 07/17/2017 ANG
					//&& curDutyP->firstLegP->leg->schedOut < legP->leg->schedIn + longRestTm + 2*DiscrtLPInterval )
					&& curDutyP->firstLegP->leg->schedOut < legP->leg->schedIn + (longRestTm + Minute*acTypeList[crewPairList[cpInd].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval ) // 07/17/2017 ANG
					{
						//ASSUME connection time before this duty is not in the LP constraints, if this duty has fixed start time
						if( curDutyP->tmFixed == DL_NotFixed )
							curDutyP->tmFixed = DL_StartTmFixed ;
						else if( curDutyP->tmFixed == DL_EndTmFixed )
							curDutyP->tmFixed = DL_BothTmFixed ;

						if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_NotFixed )
							origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_EndTmFixed ;
						else if( origDuties[legP->leg->crewPairInd][legP->day].tmFixed == DL_StartTmFixed )
							origDuties[legP->leg->crewPairInd][legP->day].tmFixed = DL_BothTmFixed ;

						break;
					}
				}//end if( legP = curDutyP->firstLegP->prevPilotLegs[m])
		}//end for( day=0; day < optParam.planningWindowDuration; day ++)
	}//end for( cpInd=0; cpInd < numCrewPairs; cpInd ++)

	*origDutiesP = origDuties ;
	return 0;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int getDayFromMgdLegs(const DL_DemUsedType *dlDemUsed, const DL_LegInfo *legInfosCP, const int sInd, const int eInd, int *dayP)
{
	int i, j ;

	//duty: [firstDutyLeg, n], find the day of this duty
	*dayP = -1 ; //initial
	
	for( i = sInd; i <= eInd; i ++)
	{
		//if there is a non-trivial demand, return day of the demand
		if( legInfosCP[i].leg->demandID > 0 )
		{
			*dayP= legInfosCP[i].schOutDay ;
			break;
		}
		//if same day for two consecutive legs, return 
		if( i < eInd && legInfosCP[i].schInDay == legInfosCP[i+1].schOutDay )
		{
			*dayP = legInfosCP[i].schInDay ;
			break;
		}
	}

	if( *dayP == -1 ) //not setted yet, remaining case: all legs are repo legs, and if there are more than one leg, then they are on different days
	{
		//case 1: two or more repo legs, find the maint/app in between, and set day
		for( i = sInd; i < eInd; i ++)
			if( legInfosCP[i].schInDay != legInfosCP[i+1].schOutDay )
			{
				//find the maint/app in between
				for( j = 0; j < numDemand; j ++ )
					if( demandList[j].isAppoint && dlDemUsed[j] && demandList[j].aircraftID == legInfosCP[i].leg->aircraftID
					&& outTimes[j] >= legInfosCP[i].leg->schedIn && inTimes[j] <= legInfosCP[i+1].leg->schedOut )
						break;

				_ASSERTE( j <  numDemand );
				*dayP = timeToDay(outTimes[j]) ;
				break ;
			}

		//case 2: single repo leg, return the early start day
		if( *dayP == -1 )
		{
			_ASSERTE ( legInfosCP[sInd].leg->demandID <= 0 && sInd == eInd );
			*dayP = legInfosCP[sInd].schOutDay ;
		}
	}//end if( day == -1 )

	_ASSERTE( *dayP >= 0 && *dayP < optParam.planningWindowDuration );
	return 0 ;
}

static int getAcIndFromMgdLegs(const DL_LegInfo *legInfosCP,  const int sInd, const int eInd, int *acInd, int *acEndLeg, int *numAcP)
{
	int i ;

	//initialize//acInd not unique
	for(i = 0; i < DL_MAX_NUM_AC_PER_DUTY; i ++ )
	{
		acInd[i] = -1;
		acEndLeg[i] = -1;
	}

	*numAcP = 0;
	for( i = sInd; i <= eInd; i ++)
		if( i == eInd || legInfosCP[i].leg->aircraftID != legInfosCP[i+1].leg->aircraftID ) //i: last leg of this ac
		{
			/*
			//check whether this ac is in the list
			for(j = 0; j < *numAcP; j ++)
				if( propMgdLegsCP[i].aircraftID == acList[acInd[j]].aircraftID )
					break ;
			if( j < *numAcP )//ac is alreadu kept
				continue ;
			*/

			acInd[*numAcP] = legInfosCP[i].acInd ;
			acEndLeg[*numAcP] = i ;

			(*numAcP) ++ ;
			_ASSERTE( *numAcP < DL_MAX_NUM_AC_PER_DUTY );
		}

	_ASSERTE( *numAcP > 0 );
	return 0 ;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int getTourStartInfo(const DL_DemUsedType *dlDemUsed, const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const int *longMaint
, const int numLongMaint, DL_TourStartInfo **tourStartInfosP, int *numStartInfoP, DL_AvailInfo **availAcsP, DL_AvailInfo **availPilotsP)
{
	int i, j, day, k, m, n, acInd, pInds[2], fixedAcID; // *pilotInLegs, *acInLegs, *availACApt, *availPilotApt, *availPilotDay ;
	time_t tempTm, departTm, dutyStartTm, arrivalTm, latestLegEndB4HTravel[2] ;
	DL_TourStartInfo *infoP=NULL ; //, tempInfo ;
	DL_LegInfo *firstLeg=NULL, *lastLeg=NULL ;
	DL_AvailInfo *availAcs=NULL, *availPilots=NULL;
	double cost;
	ProposedMgdLeg *legP=NULL;
	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ;// 07/17/2017 ANG

	_ASSERTE( numAircraft > 0 && numCrew > 0 && *tourStartInfosP == NULL && *numStartInfoP == 0 );

//////////////////////////////////////////////////////////////////////////////////////////////////////////allocate and initialize available pilots and acs
	if (!(availAcs = (DL_AvailInfo *) calloc (numAircraft, sizeof (DL_AvailInfo))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	if (!(availPilots = (DL_AvailInfo *) calloc (numCrew, sizeof (DL_AvailInfo))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}
	for(i=0; i<numAircraft; i++)
	{
		availAcs[i].ind = i ;
		availAcs[i].day = -1 ;
	}
	for(i=0; i<numCrew; i++)
	{
		availPilots[i].ind = i ;
		availPilots[i].day = -1 ;
		//availPilots[i].latestLegEndB4HTravel = crewList[i].tourEndTm - Minute*optParam.postFlightTm ; //initialize
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////get available ac ( available to crew aircrafts )
	//assume order of legs: ac, then leg start time
	for(j = 0; j < numPropMgdLegs ; j ++)
		//j: last leg of this ac
		if( j == numPropMgdLegs - 1 || propMgdLegs[j+1].aircraftID != propMgdLegs[j].aircraftID)
		{
			day = timeToDay( propMgdLegs[j].schedOut );
			acInd = acIDToInd(propMgdLegs[j].aircraftID);

			availAcs[acInd].inLegs = 1;//ac used in legs(in solution)

			//find this leg in legInfosCP[]
			for(k=0; k < numPropMgdLegs ; k ++)
				if( legInfosCP[k].leg == &(propMgdLegs[j]))
					break;
			_ASSERTE( k < numPropMgdLegs && !legInfosCP[k].nextAcLeg && origDuties[propMgdLegs[j].crewPairInd] );

			if( propMgdLegs[j].schedIn + Minute*optParam.turnTime + MinCrewInterval <= optParam.windowEnd )//enough time left to crew an ac
			{
				const DL_OrigDuty *curDutyP = &(origDuties[propMgdLegs[j].crewPairInd][legInfosCP[k].day]) ;

				_ASSERTE( curDutyP->numAc && k >= curDutyP->firstLegP->legIndCP && k <= curDutyP->lastLegP->legIndCP );
				
				//setting ac available if crew tour is ending//not considering overtime to crew acs, OR ac is dropped off by current crew
				if( (crewList[crewPairList[propMgdLegs[j].crewPairInd].crewListInd[0]].endRegDay <= day
				|| crewList[crewPairList[propMgdLegs[j].crewPairInd].crewListInd[1]].endRegDay <= day 
				|| legInfosCP[k].nextPilotLegs[0] || legInfosCP[k].nextPilotLegs[1] ))
				{
					availAcs[acInd].apt = propMgdLegs[j].schedInAptID ;
					availAcs[acInd].tm = propMgdLegs[j].schedIn + Minute*optParam.turnTime; //set time
					//if current leg is not the last leg of a duty, i.e. more than one airplanes are assigned. Current airplane will be dropped as in middle of a duty
					availAcs[acInd].availAfter = ( k == curDutyP->lastLegP->legIndCP ? DL_AfterLeg : DL_AfterLegMidDuty );
					availAcs[acInd].day = timeToDay(availAcs[acInd].tm) ;
				}
			}//end enough time left to crew an ac
		}

	//ac available after app column
	for( j = 0; j < numDemand ; j ++ )
		//if maint/app is covered and ac not used in legs, then maint/app must be in either existing tours or app columns
		//there must be legs in ext tours --> this ac has been setted to be in-legs as above
		if( demandList[j].isAppoint && dlDemUsed[j] && !availAcs[demandList[j].acInd].inLegs
		&& availAcs[demandList[j].acInd].tm < inTimes[j] + Minute*demandList[j].turnTime ) //availACTm initialized to zero
		{
			_ASSERTE( dlDemUsed[j] == DL_DemInAppCol && availAcs[demandList[j].acInd].tm == 0);
			//note: an app column may contains more than one maint/app
			acInd = demandList[j].acInd;
			availAcs[acInd].inLegs = 1;//set to be used in legs(in solution)

			if( inTimes[j] + Minute*demandList[j].turnTime + MinCrewInterval <= optParam.windowEnd )
			{
				availAcs[acInd].tm = inTimes[j] + Minute*demandList[j].turnTime ; //note: use inTimes[j]
				availAcs[acInd].apt = demandList[j].inAirportID ;
				availAcs[acInd].availAfter = DL_AfterAppCol ;
				availAcs[acInd].day = timeToDay(availAcs[acInd].tm) ;
			}
		}

	//conside ac not used in solution
	for( acInd = 0; acInd < numAircraft ; acInd ++)
		if( !availAcs[acInd].inLegs && acList[acInd].availDT + MinCrewInterval <= optParam.windowEnd )
		{
			_ASSERTE( availAcs[acInd].tm == 0 );
			availAcs[acInd].tm = acList[acInd].availDT ;
			availAcs[acInd].apt = acList[acInd].availAirportID ;
			availAcs[acInd].availAfter = DL_AfterAvail ;
			availAcs[acInd].day = timeToDay(availAcs[acInd].tm) ;
		} //else if( availAcs[acInd].tm )
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////END

//////////////////////////////////////////////////////////////////////////////////set start info for each opt tour, also set pilots available after an opt tour
	for(i=0; i < numCrewPairs; i ++)//set start info for each opt tour, also set available pilots
	{
		if( origDuties[i] == NULL )
			continue ;
		
		pInds[0] = crewPairList[i].crewListInd[0] ;//initialize
		pInds[1] = crewPairList[i].crewListInd[1] ;

		//generate the start info of tour of this cp
		infoP = allocAStartInfo(tourStartInfosP, numStartInfoP);

		//legStartDay: day of the first duty: cp day
		for( infoP->legStartDay=0; infoP->legStartDay < optParam.planningWindowDuration; (infoP->legStartDay) ++ )
			if( origDuties[i][infoP->legStartDay].numAc )
				break;

		//legEndDay: day of the last duty: cp day
		for( infoP->legEndDay=optParam.planningWindowDuration -1 ; infoP->legEndDay >= 0; (infoP->legEndDay) -- )
			if( origDuties[i][infoP->legEndDay].numAc )
				break;

		_ASSERTE( infoP->legStartDay < optParam.planningWindowDuration && infoP->legStartDay >= 0 && infoP->legEndDay >= infoP->legStartDay ) ;

		//temp holder //first and lasr leg of this cp
		firstLeg = origDuties[i][infoP->legStartDay].firstLegP ;
		lastLeg = origDuties[i][infoP->legEndDay].lastLegP ;

		//generate infoP
		infoP->type = ( firstLeg->leg->exgTour ? DL_FromExist : DL_FromNew ) ;
		infoP->firstLegStart = firstLeg->leg->schedOut ;
		infoP->cpInd = i ;
		
/////////////////////////////////////////////////////////////////////////////////////////////////////////////available ac info before this tour: first ac
		infoP->ac.ind = firstLeg->acInd ;//first ac
		infoP->ac.apt = firstLeg->leg->schedOutAptID ; //note
		//set ac available time before current cp tour, without considering maint/app
		if( firstLeg->prevAcLeg != NULL ) //the last leg of this ac before leg j
			infoP->ac.tm = firstLeg->prevAcLeg->leg->schedIn + Minute*optParam.turnTime ;
		else //not found
			infoP->ac.tm = acList[infoP->ac.ind].availDT ;

		//compare ac available time with covered maint/app that are after ac available and before first leg
		for( k= 0; k < numDemand; k ++ )
			if( demandList[k].isAppoint && dlDemUsed[k] && demandList[k].acInd == infoP->ac.ind 
			&& inTimes[k] + Minute*demandList[k].turnTime <= infoP->firstLegStart && inTimes[k] + Minute*demandList[k].turnTime  > infoP->ac.tm )
				infoP->ac.tm = inTimes[k] + Minute*demandList[k].turnTime;
		//day available
		infoP->ac.day = timeToDay(infoP->ac.tm) ;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////get lastPossibleDay
		if( lastLeg->nextAcLeg != NULL )  //ac will be used later
			infoP->lastPossibleDay = min( optParam.planningWindowDuration - 1, lastLeg->nextAcLeg->day ) ; //note: same day as the next ac leg
		else
			infoP->lastPossibleDay = optParam.planningWindowDuration - 1 ; 
		
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////available pilot info before this tour
		for( m=0; m < 2; m ++ )
		{
			//update lastPossibleDay
			if( lastLeg->nextPilotLegs[m] != NULL )
				n = min(crewList[pInds[m]].endRegDay, min( infoP->lastPossibleDay, lastLeg->nextPilotLegs[m]->day)) ; //note: same day as the next pilot leg
			else 
				n = min(crewList[pInds[m]].endRegDay, infoP->lastPossibleDay) ; 
			infoP->lastPossibleDay = max( n, infoP->legEndDay ) ;//may overtime to do the duty

			infoP->pilots[m].ind = pInds[m] ;
			if( firstLeg->prevPilotLegs[m] != NULL ) //pilot has a leg before
			{
				// rest after previous leg
				infoP->pilots[m].apt = firstLeg->prevPilotLegs[m]->leg->schedInAptID ;
				infoP->pilots[m].tm = firstLeg->prevPilotLegs[m]->leg->schedIn + Minute*(optParam.postFlightTm + optParam.minRestTm); //ASSUME after rest
				infoP->pilots[m].status = DL_Pilot_Status_Rest_B4Leg ;
				infoP->pilots[m].dutyStartTm = 0 ;
				infoP->pilots[m].availAfter = DL_AfterLeg ;
			} else//available from start
			{
				infoP->pilots[m].apt = crewList[pInds[m]].availAirportID ;
				infoP->pilots[m].tm = crewList[pInds[m]].availDT ;
				infoP->pilots[m].availAfter = DL_AfterAvail ;
				if( crewList[pInds[m]].activityCode )//was resting
				{
					infoP->pilots[m].status = ( crewList[pInds[m]].activityCode == 1 ? DL_Pilot_Status_Rest_B4Leg : DL_Pilot_Status_Rest_B4FirstLeg ) ;
					infoP->pilots[m].dutyStartTm = 0 ;
				} else
				{
					_ASSERTE( crewList[pInds[m]].dutyTime >= 0 
					&& crewList[pInds[m]].dutyTime <= getCurMaxDutyTm(0, crewList[pInds[m]].availDT, crewList[pInds[m]].availAirportID )) ;//ASSUME duty start airport

					infoP->pilots[m].status = DL_Pilot_Status_Active ;
					infoP->pilots[m].dutyStartTm = crewList[pInds[m]].availDT - Minute*crewList[pInds[m]].dutyTime ;
				}
			}//end
			infoP->pilots[m].day = timeToDay(infoP->pilots[m].tm) ;
		}//end for( m=0; m < 2; m ++ )
		
		infoP->acCpDay = max(infoP->ac.day, max( infoP->pilots[0].day, infoP->pilots[1].day));
		infoP->acCpDay = ( infoP->acCpDay > infoP->legStartDay ? infoP->legStartDay : infoP->acCpDay ) ;//e.g. when start time fixed

////////////////////////////////////////////////////////////////////////////////////////////////////////////set available pilots after current tour
		availPilots[pInds[1]].inLegs = availPilots[pInds[0]].inLegs = 1 ;

		/*
		for( m=0; m < 2; m ++ )//for each pilot
		{
///////////////////////////////////////////////////////////////////////////////////this is the last leg of this pilot and need to consider travelling back home
			//set latestLegEndB4HTravel even if the pilots will not be available after current tour, becasue we will need it for both opt tours and new tours
			//was initialized to be the tour end time - post flight time
			if( lastLeg->nextPilotLegs[m] == NULL && crewList[pInds[m]].endRegDay < optParam.planningWindowDuration ) //last leg, and pilot must go home
			{
				_ASSERTE( availPilots[pInds[m]].latestLegEndB4HTravel == crewList[pInds[m]].tourEndTm - Minute*optParam.postFlightTm );
				if( lastLeg->leg->schedInAptID != crewList[pInds[m]].endLoc )//travel is non-trivial, after this tour
				{
					//check whether feasible to travel home (including over time)
					if( lastLeg->leg->schedIn + Minute*optParam.postFlightTm < crewList[pInds[m]].tourEndTm + DayInSecs*crewList[pInds[m]].stayLate
					&& !getCrewTravelDataLate(lastLeg->leg->schedIn + Minute*optParam.postFlightTm
					, (time_t)(crewList[pInds[m]].tourEndTm + DayInSecs*crewList[pInds[m]].stayLate)
					, lastLeg->leg->schedInAptID, crewList[pInds[m]].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag ))
						availPilots[pInds[m]].latestLegEndB4HTravel = max(lastLeg->leg->schedIn, min(departTm, crewList[pInds[m]].tourEndTm) - Minute*optParam.postFlightTm) ;
					else
						availPilots[pInds[m]].latestLegEndB4HTravel = lastLeg->leg->schedIn ; //infeasible, fix end time, pilot will not be set to available
				} else
					availPilots[pInds[m]].latestLegEndB4HTravel = max(lastLeg->leg->schedIn, crewList[pInds[m]].tourEndTm - Minute*optParam.postFlightTm ) ;
				_ASSERTE( availPilots[pInds[m]].latestLegEndB4HTravel >= lastLeg->leg->schedIn );
			}
		}
		*/

		infoP->latestLegEndB4HTravel = MaxTimeT ;
		for( m=0; m < 2; m ++ )//for each pilot, if it is the last leg, then consider feasibility of home travel AFTER THIS TOUR
		{
			if( lastLeg->nextPilotLegs[m] == NULL && crewList[pInds[m]].endRegDay < optParam.planningWindowDuration ) //last leg, and consider pilot going home
			{
				if( lastLeg->leg->schedInAptID != crewList[pInds[m]].endLoc )//travel is non-trivial, after this tour
				{
					//check whether feasible to travel home (including over time)
					if( lastLeg->leg->schedIn + Minute*optParam.postFlightTm < crewList[pInds[m]].tourEndTm + DayInSecs*crewList[pInds[m]].stayLate
					&& !getCrewTravelDataLate(lastLeg->leg->schedIn + Minute*optParam.postFlightTm
					, (time_t)(crewList[pInds[m]].tourEndTm + DayInSecs*crewList[pInds[m]].stayLate)
					, lastLeg->leg->schedInAptID, crewList[pInds[m]].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag ))
						latestLegEndB4HTravel[m] = max(lastLeg->leg->schedIn, min(departTm, crewList[pInds[m]].tourEndTm) - Minute*optParam.postFlightTm) ;
					else//infeasible to travel home, fix end time, pilot will not be set to available
						latestLegEndB4HTravel[m] = lastLeg->leg->schedIn ; 
				} else //trivial travel
					latestLegEndB4HTravel[m] = max(lastLeg->leg->schedIn, crewList[pInds[m]].tourEndTm - Minute*optParam.postFlightTm ) ;
				_ASSERTE( latestLegEndB4HTravel[m] >= lastLeg->leg->schedIn );

				infoP->latestLegEndB4HTravel = min(infoP->latestLegEndB4HTravel, latestLegEndB4HTravel[m]) ;
			} else
				latestLegEndB4HTravel[m] = MaxTimeT ;
		}//end for

		//check if there is a long maintenance later 
		for( n=0; n < numLongMaint; n ++)
		{
			_ASSERTE( outTimes[longMaint[n]] > 0 ) ;
			//there is long maint after leg j and (no more legs of this ac, or long maint ends before the next leg of this ac) 
			if( demandList[longMaint[n]].acInd == infoP->ac.ind && outTimes[longMaint[n]] >= lastLeg->leg->schedIn )
				break ;
		}

		//both pilots are not available in the following case:
		//1. pilots and ac are not used later, and no long maintenance after this tour
		//2. no long maintenance after this tour, and don't re-assign existing crew pairs, 
		//	and current ac is the last ac before window start ( if not the same, then this crew pair has been re-assigned, set them free )
		if( n >= numLongMaint )
			if( (lastLeg->nextPilotLegs[0] == NULL && lastLeg->nextPilotLegs[1] == NULL && lastLeg->nextAcLeg == NULL)
			|| (optParam.exgCPACLock && crewPairList[i].optAircraftID == acList[infoP->ac.ind].aircraftID))
				continue ; //pilots stay, not available

		//pilot is used later/tour end/after rest availabel out of window
		for( m=0; m < 2; m ++ )//for each pilot
		{
			if( infoP->legEndDay == optParam.planningWindowDuration - 1 //next day is out of planning horizon, no need to check further, pilot not available
			|| crewList[pInds[m]].endRegDay <= infoP->legEndDay //if tour ends, then not available (no overtime)
			//if this pilot is used on later days: not setting this pilot to pair with other pilots, i.e. only consider current pair and the next pair
			|| lastLeg->nextPilotLegs[m]
			//|| optParam.windowEnd < lastLeg->leg->schedIn + longRestTm + MinCrewInterval //no enough time to crew an ac
			|| optParam.windowEnd < lastLeg->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pInds[m]].acTypeIndex].preFlightTm) + MinCrewInterval // 07/17/2017 ANG
			//|| latestLegEndB4HTravel[m] < lastLeg->leg->schedIn + longRestTm + MinCrewInterval ) //NOTE //no time to travel
			|| latestLegEndB4HTravel[m] < lastLeg->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pInds[m]].acTypeIndex].preFlightTm) + MinCrewInterval ) // 07/17/2017 ANG
				continue ; //pilot m not available

			_ASSERTE(availPilots[pInds[m]].ind == pInds[m] && availPilots[pInds[m]].apt == 0 && availPilots[pInds[m]].tm == 0
			&& ( !lastLeg->nextAcLeg || !availAcs[lastLeg->acInd].tm )) ; //if ac has no later legs, then ac must not be available

			availPilots[pInds[m]].apt = lastLeg->leg->schedInAptID ;
			//set pilot to be available after rest //available to pair with other pilots; earliest available time
			availPilots[pInds[m]].tm = lastLeg->leg->schedIn + Minute*(optParam.minRestTm + optParam.postFlightTm) ; //ASSUME rest
			//available on the next day
			availPilots[pInds[m]].day = max( infoP->legEndDay + 1, timeToDay(availPilots[pInds[m]].tm) ) ;
			availPilots[pInds[m]].availAfter = DL_AfterLeg ;
			availPilots[pInds[m]].status = DL_Pilot_Status_Rest_B4Leg ;
			availPilots[pInds[m]].dutyStartTm = 0 ;
		}//end for( m=0; m < 2; m ++ )
	}//end for(i=0; i < numCrewPairs; i ++)

	
///////////////////////////////////////////////////////////////////////////////////////////////for each originally available pilot, get the available info
	for(i=0; i < numCrew; i++)
		if( !availPilots[i].inLegs && crewList[i].availDT + MinCrewInterval <= optParam.windowEnd 
		&& crewList[i].availDT + MinCrewInterval <= crewList[i].tourEndTm - Minute*optParam.postFlightTm ) //no overtime to crew ac //NOTE CURRENT LOCATION //A1
		{
			//set the tour end time for each pilot, if a pilot needs to travel home
			/*
			_ASSERTE( availPilots[i].latestLegEndB4HTravel == crewList[i].tourEndTm - Minute*optParam.postFlightTm );
			if( crewList[i].endRegDay < optParam.planningWindowDuration && crewList[i].availAirportID != crewList[i].endLoc ) //consider travelling back home
			{
				//whether feasible to travel home
				if ( !getCrewTravelDataLate(crewList[i].availDT, (time_t)(crewList[i].tourEndTm + DayInSecs*crewList[i].stayLate)
				, crewList[i].availAirportID, crewList[i].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag )
				&& crewList[i].availDT + MinCrewInterval <= dutyStartTm - Minute*optParam.postFlightTm )//enough time to crew ac
					availPilots[i].latestLegEndB4HTravel = min(crewList[i].tourEndTm, dutyStartTm) - Minute*optParam.postFlightTm ;
				else
					continue ;//not available
			}
			*/

			//check if can travel back home after earliest duty end time
			if( crewList[i].endRegDay < optParam.planningWindowDuration //consider to travel home
			&& crewList[i].availAirportID != crewList[i].endLoc //non trivial travel
			&& getCrewTravelDataLate(crewList[i].availDT + MinCrewInterval + Minute*optParam.postFlightTm
			, (time_t)(crewList[i].tourEndTm + DayInSecs*crewList[i].stayLate)
			, crewList[i].availAirportID, crewList[i].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag ))
				continue ;//not available

			availPilots[i].tm = crewList[i].availDT ;
			availPilots[i].apt = crewList[i].availAirportID ;
			availPilots[i].day = timeToDay(crewList[i].availDT);
			availPilots[i].availAfter = DL_AfterAvail ;
			if( crewList[i].activityCode )//was resting
			{
				availPilots[i].status = (crewList[i].activityCode == 1 ? DL_Pilot_Status_Rest_B4Leg : DL_Pilot_Status_Rest_B4FirstLeg ) ;
				availPilots[i].dutyStartTm = 0 ;
			} else
			{
				availPilots[i].status = DL_Pilot_Status_Active ;
				availPilots[i].dutyStartTm = crewList[i].availDT - Minute*crewList[i].dutyTime ;
			}
		}

	for(i=0; i < numCrewPairs; i ++)//////////////////////////////////////////////////////////////////////////////set start info for each available cp and ac
	{
		pInds[0] = crewPairList[i].crewListInd[0] ;//initialize
		pInds[1] = crewPairList[i].crewListInd[1] ;

		//if one pilot is in the solution, but not setted to be available per above, then this crew pair is not available
		//if( !availPilots[pInds[0]].tm || !availPilots[pInds[1]].tm )
		if( !availPilots[pInds[0]].availAfter || !availPilots[pInds[1]].availAfter )
			continue ;

		_ASSERTE( availPilots[pInds[0]].day >= 0 && availPilots[pInds[1]].day >= 0
		&& availPilots[pInds[0]].tm + MinCrewInterval + Minute*optParam.postFlightTm <= crewList[pInds[0]].tourEndTm
		&& availPilots[pInds[1]].tm + MinCrewInterval + Minute*optParam.postFlightTm <= crewList[pInds[1]].tourEndTm );

		//&& availPilots[pInds[0]].latestLegEndB4HTravel >= availPilots[pInds[0]].tm + MinCrewInterval
		//&& availPilots[pInds[1]].latestLegEndB4HTravel >= availPilots[pInds[1]].tm + MinCrewInterval
		//&& availPilots[pInds[0]].tm + MinCrewInterval <= optParam.windowEnd && availPilots[pInds[1]].tm + MinCrewInterval <= optParam.windowEnd );

		//if we are not allowed to re-assign existing crew pairs, then set fixedAcID to indicate must staying with the ac 
		//	current crew pair can only be used to crew this fixedAcID
		//only consider the case when both pilots being orginally available; 
		//if both pilots are available after legs, and must stay with the airplane, then each pilot has been setted to be unavailable
		//if one pilot is available after legs, and the other is available originally, then this crew pair has been re-assigned by the opt solution for some reason; not limiting them
		fixedAcID = 0 ;
		if( optParam.exgCPACLock && crewPairList[i].optAircraftID && availPilots[pInds[0]].availAfter == DL_AfterAvail && availPilots[pInds[1]].availAfter == DL_AfterAvail )
		{
			//check if there is a long maintenance 
			for( n=0; n < numLongMaint; n ++)
				if( acList[demandList[longMaint[n]].acInd].aircraftID == crewPairList[i].optAircraftID )
					break ;
			if( n >= numLongMaint )//no long maintenance, fix the ac ID
				fixedAcID = crewPairList[i].optAircraftID ;
		}

		for(acInd=0; acInd < numAircraft; acInd ++)
		{
			if( (optParam.exgCPACLock && fixedAcID && fixedAcID != acList[acInd].aircraftID)
			|| acList[acInd].acTypeIndex != crewPairList[i].acTypeIndex 
			|| !availAcs[acInd].tm
			|| !isFeasibleDQPilotsAndAc(i, acInd ))//not matching for XLS+ airplane
				continue;

			//earliest end time of a new duty (crewing)
			tempTm = Minute*optParam.postFlightTm + MinCrewInterval + max(availAcs[acInd].tm, max(availPilots[pInds[0]].tm, availPilots[pInds[1]].tm));
			_ASSERTE( tempTm <= optParam.windowEnd + Minute*optParam.postFlightTm ) ;

			//get cp/ac available day and check it against tour end day
			day = max(availAcs[acInd].day, max( availPilots[pInds[0]].day, availPilots[pInds[1]].day ));
			_ASSERTE( day >= 0 && day < optParam.planningWindowDuration ) ;
			if( crewList[pInds[0]].endRegDay < day || crewList[pInds[1]].endRegDay < day )
				continue;

			//check tour end time, check whether feasible to travel home (including over time) from AC AVAILABLE AIRPORT
			for(m=0; m < 2; m ++)
			{
				latestLegEndB4HTravel[m] = MaxTimeT ; //initialize

				if( crewList[pInds[m]].endRegDay >= optParam.planningWindowDuration ) //not considering travel home
					continue ;

				if( tempTm > crewList[pInds[m]].tourEndTm )//not crewing when overtime
					break;

				if( availAcs[acInd].apt == crewList[pInds[m]].endLoc ) //no travel
					latestLegEndB4HTravel[m] = crewList[pInds[m]].tourEndTm - Minute*optParam.postFlightTm ;
				else if( !getCrewTravelDataLate(tempTm, (time_t)(crewList[pInds[m]].tourEndTm + DayInSecs*crewList[pInds[m]].stayLate) //travel late
				, availAcs[acInd].apt, crewList[pInds[m]].endLoc, &departTm, &dutyStartTm, &arrivalTm, &cost, withOag ))
					latestLegEndB4HTravel[m] = min(departTm, crewList[pInds[m]].tourEndTm) - Minute*optParam.postFlightTm ;
				else
					break ;//infeasible
			}
			if( m < 2 )
				continue ;

			//get cp/ac available time and check it against tour end time
			//tempTm = MinCrewInterval + max(availAcs[acInd].tm, max(availPilots[pInds[0]].tm, availPilots[pInds[1]].tm));
			//if( availPilots[pInds[0]].latestLegEndB4HTravel < tempTm || availPilots[pInds[1]].latestLegEndB4HTravel < tempTm )
			//	continue;

///////////////////////////////////////////////////////////////////////allocate a tour start 
			infoP = allocAStartInfo(tourStartInfosP, numStartInfoP);

			memmove(&(infoP->ac), &(availAcs[acInd]), sizeof(DL_AvailInfo));
			memmove(&(infoP->pilots[0]), &(availPilots[pInds[0]]), sizeof(DL_AvailInfo));
			memmove(&(infoP->pilots[1]), &(availPilots[pInds[1]]), sizeof(DL_AvailInfo));

			infoP->cpInd = i ;
			infoP->firstLegStart = 0 ;
			infoP->type =  DL_NewAvail ;

			infoP->acCpDay = day ;
			infoP->lastPossibleDay = min(optParam.planningWindowDuration - 1, min( crewList[pInds[0]].endRegDay, crewList[pInds[1]].endRegDay));
			infoP->legStartDay = infoP->legEndDay = infoP->lastPossibleDay ; //note
			_ASSERTE( latestLegEndB4HTravel[0] > 0 && latestLegEndB4HTravel[1] > 0 );
			infoP->latestLegEndB4HTravel = min(latestLegEndB4HTravel[0], latestLegEndB4HTravel[1]) ;
		}//for(acInd=0; acInd < numAircraft; acInd ++)
	}//for(i=0; i < numCrewPairs; i ++)

	//firstLegStart, acInd, cpInd
	qsort( (*tourStartInfosP), (*numStartInfoP), sizeof(DL_TourStartInfo), compareTourStartInfo);

	for(i=0; i < *numStartInfoP; i ++)
		(*tourStartInfosP)[i].index = i ; //set index

	//return
	*availAcsP = availAcs;
	*availPilotsP = availPilots;

	return 0;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int getLongMaintlInfo(const DL_DemUsedType *dlDemUsed, int **longMaintP, int *numLongMaintP)
{
	int i, count, *longMaint, numLongMaint ;

	count = 0;
	for( i = 0; i < numDemand; i ++)
		if( demandList[i].isAppoint && dlDemUsed[i] && demandList[i].reqIn - demandList[i].reqOut >= Minute*optParam.maintTmForReassign)
			count ++ ;

	if( count )
	{
		numLongMaint = count;
		if(!( longMaint = (int *) calloc ( numLongMaint, sizeof (int) )))
		{
			logMsg(dlFile,"%s Line %d: Out of Memory.\n", __FILE__,__LINE__);
			exit(1);
		}

		count = 0;
		for( i = 0; i < numDemand; i ++)
			if( demandList[i].isAppoint && dlDemUsed[i] && demandList[i].reqIn - demandList[i].reqOut >= Minute*optParam.maintTmForReassign)
			{
				longMaint[count] = i ;
				count ++ ;
			}
		_ASSERTE( count == numLongMaint);

		*longMaintP = longMaint ;
		*numLongMaintP = numLongMaint ;
	} else
	{
		*longMaintP = NULL ;
		*numLongMaintP = 0 ;
	}

	return 0;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int getLegsCP( DL_LegInfo **legInfosCPP )
{
	int i, j, m, ind, prevInd ;
	DL_LegInfo *legInfosCP;//temp
	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ;// 07/17/2017 ANG - not used

	//sort in the order of ac ID
	qsort((void *) propMgdLegs, numPropMgdLegs, sizeof(ProposedMgdLeg), compareMgdLegsAC);

///////////////////////////////////////////////////////////////////////////////////////consider the case where a repo leg is skipped in the exg tour
	for( i = 0; i < numPropMgdLegs; i ++ )
		if(i == 0 || propMgdLegs[i].aircraftID != propMgdLegs[i-1].aircraftID )
		{
			ind = acIDToInd(propMgdLegs[i].aircraftID);
			if( acList[ind].availAirportID != propMgdLegs[i].schedOutAptID )
			{
				_ASSERTE( propMgdLegs[i].exgTour );
				acList[ind].availAirportID = propMgdLegs[i].schedOutAptID ;
				fprintf(dlFile, "\n Exception: aircraft %d available location updated \n", propMgdLegs[i].aircraftID );
			}
		}
///////////////////////////////////////////////////////////////////////////////////////END

	if (!(legInfosCP = (DL_LegInfo *) calloc (numPropMgdLegs, sizeof (DL_LegInfo))))
	{
		logMsg(dlFile,"%s Line %d: Out of Memory \n", __FILE__,__LINE__);
		exit(1);
	}

	for( i = 0; i < numPropMgdLegs; i ++ )//initialize
	{
		legInfosCP[i].leg = &(propMgdLegs[i]) ;
		legInfosCP[i].day = -1 ; //cp day
		legInfosCP[i].schInDay = timeToDay(propMgdLegs[i].schedIn);
		legInfosCP[i].schOutDay = timeToDay(propMgdLegs[i].schedOut);
		legInfosCP[i].acInd = acIDToInd(propMgdLegs[i].aircraftID) ;//ac ind
	}

	//increasing order of crew pair, then out time
	qsort((void *) legInfosCP, numPropMgdLegs, sizeof(DL_LegInfo), compareLegInfoCP);

	for( i = 0; i < numPropMgdLegs; i ++ )
	{
		legInfosCP[i].legIndCP = i ;
		_ASSERTE( legInfosCP[i].prevAcLeg == NULL &&  legInfosCP[i].prevPilotLegs[0] == NULL && legInfosCP[i].prevPilotLegs[1] == NULL );
			
		////////////////////////////////////////////get previous leg with the same ac
		prevInd = -1 ;//previous leg with the same ac
		for(j=numPropMgdLegs-1; j >= 0; j --) //go through each leg
			if(legInfosCP[j].acInd == legInfosCP[i].acInd && legInfosCP[j].leg->schedOut < legInfosCP[i].leg->schedOut //b4 current leg
			&& (prevInd == -1 || legInfosCP[j].leg->schedIn > legInfosCP[prevInd].leg->schedIn )) //closer
				prevInd = j ;

		if( prevInd >= 0 )//found
		{
			_ASSERTE( legInfosCP[prevInd].nextAcLeg == NULL ) ;

			legInfosCP[i].prevAcLeg = &(legInfosCP[prevInd]) ; //update
			legInfosCP[prevInd].nextAcLeg = &(legInfosCP[i]) ;  
		}
		////////////////////////////////////////////END

		////////////////////////////////////////////get previous leg with the same pilot
		for(m=0; m < 2; m ++)
		{
			ind = crewPairList[legInfosCP[i].leg->crewPairInd].crewListInd[m] ;//current pilot index

			prevInd = -1 ;//previous leg with the same pilot
			for(j=numPropMgdLegs-1; j >= 0; j --) //go through each leg
				if( ( legInfosCP[j].leg->captainID == crewList[ind].crewID || legInfosCP[j].leg->FOID == crewList[ind].crewID) 
				&& legInfosCP[j].leg->schedOut < legInfosCP[i].leg->schedOut 
				&& (prevInd == -1 || legInfosCP[j].leg->schedIn > legInfosCP[prevInd].leg->schedIn ) )
					prevInd = j ;
			
			if( prevInd >= 0 )//found
			{
				legInfosCP[i].prevPilotLegs[m] = &(legInfosCP[prevInd]) ;//update
				
				j =  (legInfosCP[prevInd].leg->captainID == crewList[ind].crewID ? 0 : 1 ); //pilot index in prevInd

				_ASSERTE( legInfosCP[prevInd].nextPilotLegs[j] == NULL ) ;
				
				legInfosCP[prevInd].nextPilotLegs[j] = &(legInfosCP[i]);

			}//end if( prevInd >= 0 )
		}//end for(m=0; m < 2; m ++)
	}//end for( i = 0; i < numPropMgdLegs; i ++ )

	*legInfosCPP = legInfosCP ;
	return 0 ;
}

static int printLongMaintlInfo(const int *longMaint, const int numLongMaint)
{
	char opbuf1[1024], opbuf2[1024], opbuf3[1024];
	DateTime dt1, dt2, dt3;
	int i, j;

	fprintf (dlFile, " \n\n--> long maint :\n");
	fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+-----------------+-----------------+------------------+------------------+------------------+\n");
	fprintf (dlFile, "| DemandID   | Early Adj  | Late Adj   | AC ID      | Hard Incl  | Orig ID    | Default Airport | Actual Airport  | OutTm (Original) | OutTm (Copy)     | OutTm (Solution) |\n");
	fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+-----------------+-----------------+------------------+------------------+------------------+\n");

	for (j=0; j<numLongMaint; j++)
	{	
		i = longMaint[j] ;

		if( optParam.withFlexOS)
			dt1 = dt_time_tToDateTime (origDemInfos[demandList[i].origDemInd].defaultOut);
		else
			dt1 = dt_time_tToDateTime (demandList[i].reqOut);

		dt2 = dt_time_tToDateTime (demandList[i].reqOut);
		dt3 = ( outTimes[i] == 0 ? 0 : dt_time_tToDateTime (outTimes[i])) ;

		fprintf (dlFile, "| %10d |   %6d   |   %6d   |   %6d   |   %6d   | %10d |   %10d    |   %10d    | %16s | %16s | %16s |\n",
					demandList[i].demandID, 
					demandList[i].earlyAdj, 
					demandList[i].lateAdj,
					demandList[i].aircraftID,
					1-demandList[i].skipIncl,
					demandList[i].origUniqueID,
					demandList[i].outAirportID,
					demandList[i].outAirportID,
					dt_DateTimeToDateTimeString(dt1, opbuf1, "%Y/%m/%d %H:%M"),
					dt_DateTimeToDateTimeString(dt2, opbuf2, "%Y/%m/%d %H:%M"),
					(dt3 == 0 ? "Not in new tours" : dt_DateTimeToDateTimeString(dt3, opbuf3, "%Y/%m/%d %H:%M")));
	}
	fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+-----------------+-----------------+------------------+------------------+------------------+\n");

	return 0 ;
}


static int printOrigDuties(const DL_OrigDuty **origDuties)
{
	int i, j, day;

	fprintf (dlFile, "\n\n--> current crewpair aircraft assignments :\n");

	for (j=0; j< numCrewPairs; j++)
	{	
		fprintf(dlFile, "\n--> crew pair %d: %d, %d : \n", j, crewPairList[j].captainID, crewPairList[j].flightOffID) ;

		if( origDuties[j] == NULL )
			continue;

		fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+");
		for(i=0; i < DL_MAX_NUM_AC_PER_DUTY; i ++)
			fprintf (dlFile, "------------+------------+");
		fprintf (dlFile, "\n" );

		fprintf (dlFile, "| Day        | DutyNum    | FirstLeg   | LastLeg    | PrevAcLeg  | NextAcLeg  | PrevPtLeg0 | PrevPtLeg1 | NextPtLeg0 | NextPtLeg1 | StartTmFix | EndTmFix   | DutyCombin |");
		for(i=0; i < DL_MAX_NUM_AC_PER_DUTY; i ++)
			fprintf (dlFile, " AssignAcID | AcEndLeg   |");
		fprintf (dlFile, "\n" );

		fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+");
		for(i=0; i < DL_MAX_NUM_AC_PER_DUTY; i ++)
			fprintf (dlFile, "------------+------------+");
		fprintf (dlFile, "\n" );

		for( day=0; day < optParam.planningWindowDuration; day ++ )
		{
			const DL_OrigDuty *inP = &(origDuties[j][day]) ;

			if( !inP->numAc )
				continue ;
			
			fprintf (dlFile, "| %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d |"
			, day, inP->dutyNum, inP->firstLegP->legIndCP, inP->lastLegP->legIndCP
			, ( inP->firstLegP->prevAcLeg == NULL ? -1 : inP->firstLegP->prevAcLeg->legIndCP ) 
			, ( inP->lastLegP->nextAcLeg == NULL ? -1 : inP->lastLegP->nextAcLeg->legIndCP )
			, ( inP->firstLegP->prevPilotLegs[0] == NULL ? -1 : inP->firstLegP->prevPilotLegs[0]->legIndCP )
			, ( inP->firstLegP->prevPilotLegs[1] == NULL ? -1 : inP->firstLegP->prevPilotLegs[1]->legIndCP )
			, ( inP->lastLegP->nextPilotLegs[0] == NULL ? -1 : inP->lastLegP->nextPilotLegs[0]->legIndCP )
			, ( inP->lastLegP->nextPilotLegs[1] == NULL ? -1 : inP->lastLegP->nextPilotLegs[1]->legIndCP )
			, ((inP->tmFixed == DL_StartTmFixed ||  inP->tmFixed == DL_BothTmFixed ) ? 1 : 0 ) 
			, ((inP->tmFixed == DL_EndTmFixed ||  inP->tmFixed == DL_BothTmFixed ) ? 1 : 0 ) 
			, ( inP->tmFixed == DL_DutyCombined ? 1 : 0 )) ;

			for(i=0; i < DL_MAX_NUM_AC_PER_DUTY; i ++)
				if( inP->acInd[i] >= 0 )
					fprintf (dlFile, " %10d | %10d |", acList[inP->acInd[i]].aircraftID, inP->acEndLeg[i] );
				else
					fprintf (dlFile, "            |            |") ;

			fprintf (dlFile, "\n" );

			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+");
			for(i=0; i < DL_MAX_NUM_AC_PER_DUTY; i ++)
				fprintf (dlFile, "------------+------------+");
			fprintf (dlFile, "\n" );
		}
	}
	return 0 ;
}

static int printTourStartInfos( const DL_TourStartInfo *tourStartInfos, const int numStartInfo, const DL_AvailInfo *availAcs, const DL_AvailInfo *availPilots)
{
	char opbuf1[1024], opbuf2[1024], opbuf3[1024], opbuf4[1024], opbuf5[1024];
	int j, count, temp = 0;

	fprintf (dlFile, "\n\n --> available acs:\n");
	fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------------+ \n");
	fprintf (dlFile, "| Ac Ind     | Ac ID      | Orig Apt   | Orig Availabl Tm | In Legs    | Avail Apt  | Avail Aftr | Avail Day  | Ac Available Tm  | \n");
	fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------------+ \n");

	for(j=0; j < numAircraft; j ++)
	{
		fprintf (dlFile, "| %10d | %10d | %10d | %16s | %10d | %10d | %10d | %10d | %16s | \n"
		, availAcs[j].ind
		, acList[availAcs[j].ind].aircraftID
		, acList[availAcs[j].ind].availAirportID
		, dt_DateTimeToDateTimeString(dt_time_tToDateTime (acList[availAcs[j].ind].availDT), opbuf1, "%Y/%m/%d %H:%M")
		, availAcs[j].inLegs
		, availAcs[j].apt
		, availAcs[j].availAfter
		, availAcs[j].day
		, ( !availAcs[j].tm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (availAcs[j].tm), opbuf2, "%Y/%m/%d %H:%M")) );
		fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------------+ \n");
	}

	fprintf (dlFile, "\n\n --> available pilots:\n");
	fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+ \n");
	fprintf (dlFile, "| Pilot Ind  | Pilot ID   | Orig Apt   | Orig Availabl Tm | In Legs    | Avail Stus | Avail Apt  | Avail Aftr | Avail Day  | Available Tm     | Duty Start Tm    | Latest Duty End  | Tour End Time    | \n");
	fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+ \n");

	for(j=0; j < numCrew; j ++)
	{
		fprintf (dlFile, "| %10d | %10d | %10d | %16s | %10d | %10d | %10d | %10d | %10d | %16s | %16s | %16s | %16s | \n"
		, availPilots[j].ind
		, crewList[availPilots[j].ind].crewID
		, crewList[availPilots[j].ind].availAirportID
		, dt_DateTimeToDateTimeString(dt_time_tToDateTime (crewList[availPilots[j].ind].availDT), opbuf5, "%Y/%m/%d %H:%M")
		, availPilots[j].inLegs
		, availPilots[j].status
		,  availPilots[j].apt
		, availPilots[j].availAfter
		, availPilots[j].day
		, ( !availPilots[j].tm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (availPilots[j].tm), opbuf1, "%Y/%m/%d %H:%M"))
		, ( !availPilots[j].dutyStartTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (availPilots[j].dutyStartTm), opbuf2, "%Y/%m/%d %H:%M"))
		, 0
		//, ( !availPilots[j].latestLegEndB4HTravel ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (availPilots[j].latestLegEndB4HTravel), opbuf3, "%Y/%m/%d %H:%M"))
		,  ( !crewList[j].tourEndTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (crewList[j].tourEndTm), opbuf4, "%Y/%m/%d %H:%M"))
		);
		fprintf (dlFile, "+------------+------------+------------+------------------+------------+------------+------------+------------+------------+------------------+------------------+------------------+------------------+ \n");
	}

	fprintf (dlFile, "\n\n --> crewpair aircraft start info:\n");
	fprintf (dlFile, "+------------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------------+ \n");
	fprintf (dlFile, "| First Leg Start  | Tour Ind   | Tour Type  | Fleet Type | Ac Cp Day  | First Day  | Last Day   | Last P Day | Ac ID      | Ac Airport | Ac Available Tm  | P0 ID      | P0 Status  | P0 Airport | P0 Available Tm  | P1 ID      | P1 Status  | P1 Airport | P1 Available Tm  | LegEndB4TourEnd  | \n");
	fprintf (dlFile, "+------------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------------+ \n");

	count = 0; 
	for (j=0; j< numStartInfo; j++)
	{	
		const DL_TourStartInfo *inP = &(tourStartInfos[j]) ;

		fprintf (dlFile, "| %16s | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %10d | %16s | %10d | %10d | %10d | %16s | %10d | %10d | %10d | %16s | %16s | \n"
		, ( !inP->firstLegStart ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->firstLegStart), opbuf1, "%Y/%m/%d %H:%M"))
		, j
		, inP->type
		, crewPairList[inP->cpInd].acTypeIndex
		, inP->acCpDay
		, inP->legStartDay
		, inP->legEndDay
		, inP->lastPossibleDay
		, acList[inP->ac.ind].aircraftID
		, inP->ac.apt
		, ( !inP->ac.tm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->ac.tm), opbuf2, "%Y/%m/%d %H:%M"))
		, crewPairList[inP->cpInd].captainID
		, inP->pilots[0].status
		, inP->pilots[0].apt
		, ( !inP->pilots[0].tm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pilots[0].tm), opbuf3, "%Y/%m/%d %H:%M"))
		, crewPairList[inP->cpInd].flightOffID
		, inP->pilots[1].status
		, inP->pilots[1].apt
		, ( !inP->pilots[1].tm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pilots[1].tm), opbuf4, "%Y/%m/%d %H:%M"))
		, ( !inP->latestLegEndB4HTravel ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->latestLegEndB4HTravel), opbuf5, "%Y/%m/%d %H:%M"))) ;

		if( count && !(count%25) )
		{
			fprintf (dlFile, "+------------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------------+ \n");
			fprintf (dlFile, "| First Leg Start  | Tour Ind   | Tour Type  | Fleet Type | Ac Cp Day  | First Day  | Last Day   | Last P Day | Ac ID      | Ac Airport | Ac Available Tm  | P0 ID      | P0 Status  | P0 Airport | P0 Available Tm  | P1 ID      | P1 Status  | P1 Airport | P1 Available Tm  | LegEndB4TourEnd  | \n");
			fprintf (dlFile, "+------------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------------+ \n");
		} else
			fprintf (dlFile, "+------------------+------------+------------+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------+------------+------------+------------------+------------------+ \n");
		count ++;
	}

	return 0;
}

static int printNewDuties( const DL_NewDuty ****newDuties, const int ***numNewDuties, const int numStartInfo)
{
	char opbuf1[1024], opbuf2[1024], opbuf3[1024], opbuf4[1024], opbuf5[1024], opbuf6[1024], opbuf7[1024];
	int i, j, k, m ;

	fprintf (dlFile, "\n\n --> All columns:\n");

	for(i=0; i < numStartInfo ; i ++)
	{
		for(j=0; j < optParam.planningWindowDuration; j ++)
		{
			if( !newDuties[i][j] )
				continue;

			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------------+------------+------------------+------------------+------------------+------------------+------------------+------------+------------+ \n");
			fprintf (dlFile, "| Day        | First Day  | TourStart  | Keep Orig  | Crew Pair  | Aircraft   | Ac Airport | Ac Avail Tm      | P0 Airport | P0 Avail Tm      | P1 Airport | P1 Avail Tm      | P0 Start Time    | P1 Start Time    | Start Time       | End Time         | Cost       | # Cvd Dems | \n");
			fprintf (dlFile, "+------------+------------+------------+------------+------------+------------+------------+------------------+------------+------------------+------------+------------------+------------------+------------------+------------------+------------------+------------+------------+ \n");

			for(k=0; k<2; k++)
			{
				if( !newDuties[i][j][k] )
					continue;

				for(m=0; m < numNewDuties[i][j][k]; m ++)
				{
					const DL_NewDuty *inP = &(newDuties[i][j][k][m]) ;

					fprintf (dlFile, "| %10d | %10d | %10d | %10d | %10d | %10d | %10d | %16s | %10d | %16s | %10d | %16s | %16s | %16s | %16s | %16s | %10.2f | %10d | \n"
					, inP->day
					, inP->firstDay
					, i
					, inP->keepOriginal
					, inP->startInfoP->cpInd
					, acList[inP->startInfoP->ac.ind].aircraftID
					, inP->acAirport
					, ( !inP->acTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->acTm), opbuf1, "%Y/%m/%d %H:%M"))
					, inP->pAirports[0]
					, ( !inP->pTms[0] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pTms[0]), opbuf2, "%Y/%m/%d %H:%M"))
					, inP->pAirports[1]
					, ( !inP->pTms[1] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pTms[1]), opbuf3, "%Y/%m/%d %H:%M"))
					, ( !inP->pilotStartTm[0] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pilotStartTm[0]), opbuf4, "%Y/%m/%d %H:%M"))
					, ( !inP->pilotStartTm[1] ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->pilotStartTm[1]), opbuf5, "%Y/%m/%d %H:%M"))
					, ( !inP->startTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->startTm), opbuf6, "%Y/%m/%d %H:%M"))
					, ( !inP->endTm ? " " : dt_DateTimeToDateTimeString(dt_time_tToDateTime (inP->endTm), opbuf7, "%Y/%m/%d %H:%M"))
					, inP->cost
					, inP->numCoverDems ) ;
				}//end 	for(m=0;
			}//end 	for(k=0;
		}//end 	for(j=0;
	}//end 	for(i=0;

	return 0;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int checkOrigDuties(const DL_OrigDuty **origDuties, const DL_LegInfo *legInfosCP)
{
	int i, j, m, day, index, cpInd, pilotInd[2], dutyNum, iPilotInd[2], jPilotInd[2] ;
	time_t tempTm;
	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ;//07/17/2017 ANG - only for debugging purpose

	_ASSERTE( origDuties != NULL && legInfosCP != NULL );
	for (cpInd=0; cpInd < numCrewPairs; cpInd++)
	{	
		if( origDuties[cpInd] == NULL )
			continue;

		pilotInd[0] = crewPairList[cpInd].crewListInd[0] ;
		pilotInd[1] = crewPairList[cpInd].crewListInd[1] ;
		dutyNum = 0 ;

		for( day=0; day < optParam.planningWindowDuration; day ++ )
		{
			const DL_OrigDuty *inP = &(origDuties[cpInd][day]) ; //initialize

			_ASSERTE( inP->numAc >= 0 );
			if( !inP->numAc ) //not assigned on this day
			{
				_ASSERTE( inP->dutyNum == 0 && inP->lastLegP == NULL && inP->firstLegP == NULL && inP->tmFixed == 0 && inP->numAc == 0);
				continue; 
			}

			_ASSERTE( inP->dutyNum == dutyNum 
			&& inP->lastLegP->legIndCP >= inP->firstLegP->legIndCP 
			&& inP->firstLegP->leg->crewPairInd == cpInd && inP->firstLegP->acInd == inP->acInd[0] 
			&& inP->lastLegP->schOutDay >= inP->firstLegP->schOutDay 
			&& (day == inP->firstLegP->schOutDay || day == inP->firstLegP->schOutDay + 1)) ; //ASSUME

			index = 0 ; //index for ac's in this duty
			for( i = inP->firstLegP->legIndCP; i < inP->lastLegP->legIndCP; i ++)
			{
				_ASSERTE( legInfosCP[i].leg->crewPairInd == legInfosCP[i+1].leg->crewPairInd 
				&& legInfosCP[i].leg->schedIn <= legInfosCP[i+1].leg->schedOut
				&& inP->acInd[index] >= 0 
				&& legInfosCP[i].acInd == inP->acInd[index] 
				&& day == legInfosCP[i].day );

				if( legInfosCP[i].acInd != legInfosCP[i+1].acInd )
				{
					_ASSERTE( inP->acEndLeg[index]  == i );
					index ++;
				}
			}
			_ASSERTE( inP->acInd[index] >= 0 && legInfosCP[i].acInd == inP->acInd[index] && inP->numAc == index+1 && inP->acEndLeg[index] == i ) ;

			//start time fixed 
			tempTm = inP->firstLegP->leg->schedOut ;
			if( inP->tmFixed == DL_StartTmFixed || inP->tmFixed == DL_BothTmFixed )
				_ASSERTE( (inP->firstLegP->prevAcLeg && inP->firstLegP->prevAcLeg->leg->schedIn + Minute*optParam.turnTime >= inP->firstLegP->leg->schedOut )
				|| acList[inP->acInd[0]].availDT >= tempTm 
				|| tempTm <= optParam.windowStart 
				//|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
				|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[0]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
				//|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
				|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[1]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
				//|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut)
				|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[0]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut) //07/17/2017 ANG
				//|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut)
				|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[1]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut) //07/17/2017 ANG
				//|| crewList[pilotInd[0]].availDT + Minute*( crewList[pilotInd[0]].activityCode == 2 ? optParam.firstPreFltTm : optParam.preFlightTm) >= tempTm 
				|| crewList[pilotInd[0]].availDT + Minute*( crewList[pilotInd[0]].activityCode == 2 ? optParam.firstPreFltTm : acTypeList[crewList[pilotInd[0]].acTypeIndex].preFlightTm) >= tempTm //07/17/2017 ANG
				//|| crewList[pilotInd[1]].availDT + Minute*( crewList[pilotInd[1]].activityCode == 2 ? optParam.firstPreFltTm : optParam.preFlightTm) >= tempTm ) ;
				|| crewList[pilotInd[1]].availDT + Minute*( crewList[pilotInd[1]].activityCode == 2 ? optParam.firstPreFltTm : acTypeList[crewList[pilotInd[1]].acTypeIndex].preFlightTm) >= tempTm ) ;//07/17/2017 ANG
				//|| (inP->firstLegP->prevAcLeg && inP->firstLegP->leg->crewPairInd != inP->firstLegP->prevAcLeg->leg->crewPairInd)
				//|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->leg->crewPairInd != inP->firstLegP->prevPilotLegs[0]->leg->crewPairInd)
				//|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->leg->crewPairInd != inP->firstLegP->prevPilotLegs[1]->leg->crewPairInd)
				//|| (inP->firstLegP->prevPilotLegs[0] != NULL && inP->firstLegP->leg->schedOut < inP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm + 2*DiscrtLPInterval )
				//|| (inP->firstLegP->prevPilotLegs[1] != NULL && inP->firstLegP->leg->schedOut < inP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm + 2*DiscrtLPInterval ));

			if( (inP->firstLegP->prevAcLeg && inP->firstLegP->prevAcLeg->leg->schedIn + Minute*optParam.turnTime >= inP->firstLegP->leg->schedOut )
			|| acList[inP->acInd[0]].availDT >= tempTm 
			|| tempTm <= optParam.windowStart
			//|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
			|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[0]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
			//|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
			|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + (longRestTm + Minute*acTypeList[crewList[pilotInd[1]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
			//|| crewList[pilotInd[0]].availDT + Minute*( crewList[pilotInd[0]].activityCode == 2 ? optParam.firstPreFltTm : optParam.preFlightTm) >= tempTm 
			|| crewList[pilotInd[0]].availDT + Minute*( crewList[pilotInd[0]].activityCode == 2 ? optParam.firstPreFltTm : acTypeList[crewList[pilotInd[0]].acTypeIndex].preFlightTm) >= tempTm //07/17/2017 ANG
			//|| crewList[pilotInd[1]].availDT + Minute*( crewList[pilotInd[1]].activityCode == 2 ? optParam.firstPreFltTm : optParam.preFlightTm) >= tempTm )
			|| crewList[pilotInd[1]].availDT + Minute*( crewList[pilotInd[1]].activityCode == 2 ? optParam.firstPreFltTm : acTypeList[crewList[pilotInd[1]].acTypeIndex].preFlightTm) >= tempTm ) //07/17/2017 ANG
				_ASSERTE( inP->tmFixed == DL_DutyCombined || inP->tmFixed == DL_StartTmFixed || inP->tmFixed == DL_BothTmFixed );
			
			//end time fixed
			tempTm = inP->lastLegP->leg->schedIn ;
			if( inP->tmFixed == DL_EndTmFixed || inP->tmFixed == DL_BothTmFixed )
				_ASSERTE( (inP->lastLegP->nextAcLeg && inP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= inP->lastLegP->nextAcLeg->leg->schedOut ) 
				//|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
				|| (inP->firstLegP->prevPilotLegs[0] && inP->firstLegP->prevPilotLegs[0]->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
				//|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut)
				|| (inP->firstLegP->prevPilotLegs[1] && inP->firstLegP->prevPilotLegs[1]->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->firstLegP->leg->schedOut) //07/17/2017 ANG
				//|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut)
				|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut) //07/17/2017 ANG
				//|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut)
				|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut) //07/17/2017 ANG
				|| (inP->lastLegP->nextAcLeg && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextAcLeg->leg->crewPairInd)
				|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextPilotLegs[0]->leg->crewPairInd)
				|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextPilotLegs[1]->leg->crewPairInd)
				|| inP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= optParam.windowEnd );

			if( (inP->lastLegP->nextAcLeg && inP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= inP->lastLegP->nextAcLeg->leg->schedOut ) 
			//|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut)
			|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[0]->leg->schedOut) //07/17/2017 ANG
			//|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + longRestTm + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut)
			|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->schedIn + (longRestTm + Minute*acTypeList[acList[inP->acInd[index]].acTypeIndex].preFlightTm) + 2*DiscrtLPInterval > inP->lastLegP->nextPilotLegs[1]->leg->schedOut) //07/17/2017 ANG
			|| (inP->lastLegP->nextAcLeg && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextAcLeg->leg->crewPairInd)
			|| (inP->lastLegP->nextPilotLegs[0] && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextPilotLegs[0]->leg->crewPairInd)
			|| (inP->lastLegP->nextPilotLegs[1] && inP->lastLegP->leg->crewPairInd != inP->lastLegP->nextPilotLegs[1]->leg->crewPairInd)
			|| inP->lastLegP->leg->schedIn + Minute*optParam.turnTime >= optParam.windowEnd )
				_ASSERTE( inP->tmFixed == DL_DutyCombined || inP->tmFixed == DL_EndTmFixed || inP->tmFixed == DL_BothTmFixed );

			dutyNum ++ ;
		}//end for( day=0; day < optParam.planningWindowDuration; day ++ )
	}//end for (cpInd=0; cpInd< numCrewPairs; cpInd++)

	//go through  legs to check days
	for( i = 0; i < numPropMgdLegs; i ++)
	{
		const DL_LegInfo *inP = &(legInfosCP[i]) ;
		_ASSERTE( inP->day >= 0 ) ;

		iPilotInd[0] = crewPairList[inP->leg->crewPairInd].crewListInd[0];
		iPilotInd[1] = crewPairList[inP->leg->crewPairInd].crewListInd[1];

		if( i < numPropMgdLegs - 1 && legInfosCP[i].leg->crewPairInd == legInfosCP[i+1].leg->crewPairInd )
			_ASSERTE( legInfosCP[i].day <= legInfosCP[i+1].day ) ;//assume sorted

		for( j = 0; j < numPropMgdLegs; j ++)//compare all other legs to legInfosCP[i]
		{
			if( j == i )
				continue;

			jPilotInd[0] = crewPairList[legInfosCP[j].leg->crewPairInd].crewListInd[0];
			jPilotInd[1] = crewPairList[legInfosCP[j].leg->crewPairInd].crewListInd[1];

			if( legInfosCP[j].acInd == inP->acInd )//same ac as i
			{
				if(  legInfosCP[j].leg->schedIn <= inP->leg->schedOut ) //j before i 
					_ASSERTE( legInfosCP[j].day <= inP->day );
				else
					_ASSERTE( legInfosCP[j].day >= inP->day );
			}

			for(m=0; m<2; m++) //check pilot m of leg i
				if( jPilotInd[0] == iPilotInd[m] || jPilotInd[1] == iPilotInd[m] )
				{
					if( legInfosCP[j].leg->schedIn <= inP->leg->schedOut )//j before i 
						_ASSERTE( legInfosCP[j].day <= inP->day );
					else
						_ASSERTE( legInfosCP[j].day >= inP->day) ;
				}
		}//end for( j = 0; j < numPropMgdLegs; j ++)

		//other direction
		_ASSERTE( (!inP->prevAcLeg || inP->prevAcLeg->day <= inP->day)
		&& ( !inP->nextAcLeg || inP->nextAcLeg->day >= inP->day ) 
		&& ( !inP->prevPilotLegs[0] || inP->prevPilotLegs[0]->day <= inP->day )
		&& ( !inP->prevPilotLegs[1] || inP->prevPilotLegs[1]->day <= inP->day )
		&& ( !inP->nextPilotLegs[0] || inP->nextPilotLegs[0]->day >= inP->day )
		&& ( !inP->nextPilotLegs[1] || inP->nextPilotLegs[1]->day >= inP->day )) ;
	}//end for( i = 0; i < numPropMgdLegs; i ++)

	return 0 ;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int checkLegsCP( const DL_LegInfo *legInfosCP )
{
	int i, j, m, k, iPilotInd[2], jPilotInd[2] ;
	//const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm =  Minute*(optParam.minRestTm + optParam.postFlightTm) ;//07/17/2017 ANG - only for debugging purpose

	_ASSERTE( legInfosCP != NULL ) ;
	
	for( i = 0; i < numPropMgdLegs; i ++)
	{
		const DL_LegInfo *inP = &(legInfosCP[i]) ; //leg

		iPilotInd[0] = crewPairList[inP->leg->crewPairInd].crewListInd[0];
		iPilotInd[1] = crewPairList[inP->leg->crewPairInd].crewListInd[1];
		
		_ASSERTE( inP->leg != NULL 
		&& inP->legIndCP == i
		&& inP->acInd == acIDToInd(inP->leg->aircraftID) 
		&& inP->schOutDay == timeToDay(inP->leg->schedOut)
		&& inP->schInDay == timeToDay(inP->leg->schedIn) );

		for( j = 0; j < numPropMgdLegs; j ++)//compare all other legs to legInfosCP[i]
		{
			if( j == i )
				continue;

			jPilotInd[0] = crewPairList[legInfosCP[j].leg->crewPairInd].crewListInd[0];
			jPilotInd[1] = crewPairList[legInfosCP[j].leg->crewPairInd].crewListInd[1];

			if( legInfosCP[j].acInd == inP->acInd )//same ac as i
			{
				if(  legInfosCP[j].leg->schedIn <= inP->leg->schedOut ) //j before i 
					_ASSERTE( inP->prevAcLeg != NULL 
					&& legInfosCP[j].leg->schedIn <= inP->prevAcLeg->leg->schedIn
					&& legInfosCP[j].nextAcLeg != NULL 
					&& legInfosCP[j].nextAcLeg->leg->schedOut <= inP->leg->schedOut );
				else
					_ASSERTE( inP->nextAcLeg != NULL 
					&& legInfosCP[j].leg->schedOut >= inP->leg->schedIn
					&& legInfosCP[j].leg->schedOut >= inP->nextAcLeg->leg->schedOut 
					&& legInfosCP[j].prevAcLeg != NULL 
					&& legInfosCP[j].prevAcLeg->leg->schedOut >= inP->leg->schedOut );
			}

			for(m=0; m<2; m++) //check pilot m
				if( jPilotInd[0] == iPilotInd[m] || jPilotInd[1] == iPilotInd[m] )
				{
					k = ( jPilotInd[0] == iPilotInd[m] ? 0 : 1 ) ; //pilot index in leg j

					if( legInfosCP[j].leg->schedIn <= inP->leg->schedOut )//j before i 
						_ASSERTE( inP->prevPilotLegs[m] != NULL 
						&& legInfosCP[j].leg->schedIn <= inP->prevPilotLegs[m]->leg->schedIn
						&& legInfosCP[j].nextPilotLegs[k] != NULL 
						&& legInfosCP[j].nextPilotLegs[k]->leg->schedOut <= inP->leg->schedOut );
					else
						_ASSERTE( legInfosCP[j].leg->schedOut >= inP->leg->schedIn 
						&& inP->nextPilotLegs[m] != NULL
						&& legInfosCP[j].leg->schedOut >= inP->nextPilotLegs[m]->leg->schedOut 
						&& legInfosCP[j].prevPilotLegs[k] != NULL 
						&& legInfosCP[j].prevPilotLegs[k]->leg->schedOut >= inP->leg->schedOut ) ;
				}
		}//end for( j = 0; j < numPropMgdLegs; j ++)

		//other direction
		_ASSERTE( inP->prevAcLeg == NULL
		|| (inP->prevAcLeg->acInd == inP->acInd && inP->prevAcLeg->leg->schedIn <= inP->leg->schedOut && inP->prevAcLeg->nextAcLeg == inP )) ;

		_ASSERTE( inP->nextAcLeg == NULL
		|| ( inP->nextAcLeg->acInd == inP->acInd && inP->nextAcLeg->leg->schedOut >= inP->leg->schedIn && inP->nextAcLeg->prevAcLeg == inP )) ;

		for(m=0; m<2; m++)//for each pilot
		{
			if( inP->prevPilotLegs[m] != NULL )
			{
				j = inP->prevPilotLegs[m]->leg->crewPairInd ; 
				if( crewPairList[j].crewListInd[0] == iPilotInd[m] )//index of current pilot in the prev leg
					j = 0 ;
				else
				{
					_ASSERTE( crewPairList[j].crewListInd[1] == iPilotInd[m] );
					j = 1;
				}

				_ASSERTE( inP->prevPilotLegs[m]->leg->schedIn <= inP->leg->schedOut && inP->prevPilotLegs[m]->nextPilotLegs[j] == inP) ;

			}
			//end if( inP->prevPilotLegs[m] != NULL )

			if( inP->nextPilotLegs[m] != NULL)
			{
				j = inP->nextPilotLegs[m]->leg->crewPairInd ;
				if( crewPairList[j].crewListInd[0] == iPilotInd[m] )//index of current pilot in the next leg
					j = 0 ;
				else
				{
					_ASSERTE( crewPairList[j].crewListInd[1] == iPilotInd[m] );
					j = 1;
				}

				_ASSERTE( inP->nextPilotLegs[m]->leg->schedOut >= inP->leg->schedIn && inP->nextPilotLegs[m]->prevPilotLegs[j] == inP ) ;
			}
			//end if( temP->nextPilotLegs[m] != NULL )

		}//end for(m=0; m<2; m++)
	}//end for( i = 0; i < numPropMgdLegs; i ++)

	return 0 ;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static int checkTourStartInfos( const DL_LegInfo *legInfosCP, const DL_OrigDuty **origDuties, const DL_TourStartInfo *tourStartInfos, 
const int numStartInfo, const DL_AvailInfo *availAcs, const DL_AvailInfo *availPilots )
{
	int i, j ;

	_ASSERTE( tourStartInfos != NULL && numStartInfo > 0 ) ;
	for (j=0; j< numStartInfo; j++)
	{	
		const DL_TourStartInfo *inP = &(tourStartInfos[j]) ;

		_ASSERTE( inP->ac.ind >= 0 && inP->cpInd >= 0 
		&& ( crewPairList[inP->cpInd].acTypeIndex == acList[inP->ac.ind].acTypeIndex || inP->type == DL_FromExist )
		&& inP->pilots[0].ind == crewPairList[inP->cpInd].crewListInd[0] 
		&& inP->pilots[1].ind == crewPairList[inP->cpInd].crewListInd[1]
		&& inP->acCpDay >= 0 
		&& inP->legStartDay >= inP->acCpDay 
		&& inP->legEndDay >= inP->legStartDay 
		&& inP->lastPossibleDay >= inP->legEndDay 
		&& optParam.planningWindowDuration >= inP->lastPossibleDay
		&& ( inP->firstLegStart == 0
		|| origDuties[inP->cpInd][inP->legStartDay].tmFixed == DL_StartTmFixed || origDuties[inP->cpInd][inP->legStartDay].tmFixed == DL_BothTmFixed 
		|| origDuties[inP->cpInd][inP->legStartDay].tmFixed == DL_DutyCombined
		|| (inP->ac.tm <= inP->firstLegStart 
		&& inP->pilots[0].tm - (inP->pilots[0].status != DL_Pilot_Status_Rest_B4FirstLeg ? 0 : DayInSecs*crewList[inP->pilots[0].ind].startEarly) <= inP->firstLegStart 
		&& inP->pilots[1].tm - (inP->pilots[1].status != DL_Pilot_Status_Rest_B4FirstLeg ? 0 : DayInSecs*crewList[inP->pilots[1].ind].startEarly) <= inP->firstLegStart ))) ;

		for(i=0; i<2; i++)
		{
			_ASSERTE( inP->pilots[i].apt > 0 && inP->pilots[i].tm > 0 && inP->latestLegEndB4HTravel > 0 );
			if( inP->pilots[i].status != DL_Pilot_Status_Active )
				_ASSERTE( inP->pilots[i].dutyStartTm == 0 );
			else
				_ASSERTE( inP->pilots[i].dutyStartTm > 0 && inP->pilots[i].dutyStartTm <= inP->pilots[i].tm );

			if( inP->type != DL_NewAvail ) //opt tour
			{
				_ASSERTE( origDuties[inP->cpInd] && origDuties[inP->cpInd][inP->legEndDay].numAc && inP->latestLegEndB4HTravel > 0 );
				if( inP->latestLegEndB4HTravel < MaxTimeT )
					_ASSERTE( inP->latestLegEndB4HTravel >= origDuties[inP->cpInd][inP->legEndDay].lastLegP->leg->schedIn//later than the last leg
					&& (inP->latestLegEndB4HTravel <= crewList[inP->pilots[i].ind].tourEndTm - Minute*optParam.postFlightTm //earlier than tour end time
					|| inP->latestLegEndB4HTravel == origDuties[inP->cpInd][inP->legEndDay].lastLegP->leg->schedIn ));
				else
					_ASSERTE( origDuties[inP->cpInd][inP->legEndDay].lastLegP->nextPilotLegs[i] == NULL
					|| crewList[inP->pilots[i].ind].endRegDay >= optParam.planningWindowDuration ) ;
			} else //new tour
				_ASSERTE( inP->latestLegEndB4HTravel >= max(inP->ac.tm, inP->pilots[i].tm) + MinCrewInterval 
				&& (crewList[inP->pilots[i].ind].endRegDay >= optParam.planningWindowDuration 
				|| inP->latestLegEndB4HTravel <= crewList[inP->pilots[i].ind].tourEndTm - Minute*optParam.postFlightTm)) ;

			/*
			if( inP->type != DL_NewAvail ) //opt tour
			{
				if( availPilots[inP->pilots[i].ind].tm )//pilot available after this opt tour
					_ASSERTE( availPilots[inP->pilots[i].ind].latestLegEndB4HTravel >= availPilots[inP->pilots[i].ind].tm + Minute*optParam.preFlightTm + MinCrewInterval );
				else
					_ASSERTE( origDuties[inP->cpInd] && origDuties[inP->cpInd][inP->legEndDay].numAc //last duty of this opt tour
					&& availPilots[inP->pilots[i].ind].latestLegEndB4HTravel >= origDuties[inP->cpInd][inP->legEndDay].lastLegP->leg->schedIn
					&& (availPilots[inP->pilots[i].ind].latestLegEndB4HTravel <= crewList[inP->pilots[i].ind].tourEndTm - Minute*optParam.postFlightTm
					|| availPilots[inP->pilots[i].ind].latestLegEndB4HTravel == origDuties[inP->cpInd][inP->legEndDay].lastLegP->leg->schedIn ));
			} else //new tour
				_ASSERTE( availPilots[inP->pilots[i].ind].latestLegEndB4HTravel >= inP->pilots[i].tm + MinCrewInterval 
				&& availPilots[inP->pilots[i].ind].latestLegEndB4HTravel <= crewList[inP->pilots[i].ind].tourEndTm - Minute*optParam.postFlightTm );
			*/
		}

		if( inP->type != DL_NewAvail )//from current optimal tours
		{
			_ASSERTE( origDuties[inP->cpInd] != NULL 
			&& origDuties[inP->cpInd][inP->legStartDay].numAc 
			&& origDuties[inP->cpInd][inP->legEndDay].numAc 
			&& inP->firstLegStart > 0 
			&& origDuties[inP->cpInd][inP->legStartDay].firstLegP->leg->schedOut == inP->firstLegStart ) ;
		} else//extra cp and ac
		{
			//for(i=inP->acCpDay; i <= inP->lastPossibleDay; i ++)
			//	_ASSERTE( origDuties[inP->cpInd] == NULL || !origDuties[inP->cpInd][i].numAc );
			_ASSERTE(inP->firstLegStart == 0 ) ; //&& !inP->nextAcOrigTour && !inP->nextPilotOrigTour[0] && !inP->nextPilotOrigTour[1] ) ;
		}//end
	}//end for (j=0; j< numStartInfo; j++)

///////////////////////////////////////////////available acs and pilots
	for(j=0; j<numAircraft; j++)
	{
		_ASSERTE( availAcs[j].dutyStartTm == 0 && !availAcs[j].status && availAcs[j].ind == j );
		if( availAcs[j].tm )
		{
			_ASSERTE( availAcs[j].apt && availAcs[j].availAfter 
			&& (availAcs[j].day == timeToDay(availAcs[j].tm) || availAcs[j].day == timeToDay(availAcs[j].tm) + 1));
		} else
			_ASSERTE( !availAcs[j].apt && !availAcs[j].availAfter && availAcs[j].day == -1);
	}

	for(j=0; j<numCrew; j++)
	{
		_ASSERTE( availPilots[j].ind == j );
		if( availPilots[j].tm )
		{
			_ASSERTE( availPilots[j].dutyStartTm <= availPilots[j].tm && availPilots[j].apt && availPilots[j].availAfter 
			&& (availPilots[j].day == timeToDay(availPilots[j].tm) || availPilots[j].day == timeToDay(availPilots[j].tm) + 1));
		} else
			_ASSERTE( !availPilots[j].dutyStartTm && !availPilots[j].apt && !availPilots[j].availAfter && availPilots[j].day == -1);
	}

	return 0 ;
}

static int printManagedLegs(const DL_LegInfo *legInfosCP)
{
	int i ;	
	char opbuf1[1024], opbuf2[1024] ;

	fprintf (dlFile, "--> Managed legs (order of crew pairs) :\n");
	fprintf (dlFile, "+------------+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	fprintf (dlFile, "| Index      | Demand ID | Aircraft ID |  PIC ID  |  SIC ID  | Out Apt ID | Out FBO ID |  In Apt ID |  In FBO ID |     Out Time     |     In Time      | ex?|\n");
	fprintf (dlFile, "+------------+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	for (i=0; i<numPropMgdLegs; i++)
	{
		fprintf (dlFile, "|   %6d   |  %6d   |   %6d    |  %6d  |  %6d  |   %6d   |   %6d   |   %6d   |   %6d   | %16s | %16s | %2d |\n",
			i, 
			legInfosCP[i].leg->demandID, 
			legInfosCP[i].leg->aircraftID,
			legInfosCP[i].leg->captainID,
			legInfosCP[i].leg->FOID,
			legInfosCP[i].leg->schedOutAptID,
			legInfosCP[i].leg->schedOutFBOID,
			legInfosCP[i].leg->schedInAptID,
			legInfosCP[i].leg->schedInFBOID,
			dt_DateTimeToDateTimeString(dt_time_tToDateTime (legInfosCP[i].leg->schedOut), opbuf1, "%Y/%m/%d %H:%M"),
			dt_DateTimeToDateTimeString(dt_time_tToDateTime (legInfosCP[i].leg->schedIn), opbuf2, "%Y/%m/%d %H:%M"),
			legInfosCP[i].leg->exgTour
		);
		
		if ( i < numPropMgdLegs-1 && legInfosCP[i].acInd != legInfosCP[i+1].acInd )
			fprintf (dlFile, "+------------+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	}
	fprintf (dlFile, "+------------+-----------+-------------+----------+----------+------------+------------+------------+------------+------------------+------------------+----+\n");
	return 0 ;
}


//get assignments of current cp and ac on dayOne
static int getNewDutiesOnADay(const int* longMaint, const int numLongMaint, const DL_OrigDuty **origDuties, const DL_TourStartInfo *startP, const int dayZero
, const int dayOne, DL_NewDuty **newDutiesP, int *numNewDutiesP)
{
	time_t departTm, dutyStartTm, arrivalTm, preFltTm, pAvailTm[2], availTmB4Duty, adjRestTm ; 
	double cost; 
	int i, j, lastInd, numTmpData ;
	DL_NewDuty *aP = NULL ;
	//const DL_TourStartInfo *startP = &(tourStartInfos[index]) ;
	const DL_OrigDuty *curDutyP = NULL, *prevDutyP = NULL, *nextDutyP = NULL ;
	DL_LegInfo *legP = NULL ;
	DL_DataOneDay tmpData[DL_MAX_NUM_TEMP_DATA], *curDataP = NULL;
	
	//const time_t longRestTm = Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.preFlightTm) ;
	const time_t longRestTm = Minute*(optParam.minRestTm + optParam.postFlightTm) ;// 07/17/2017 ANG
	const time_t longRestFirstTm = Minute*(optParam.minRestTm + optParam.postFlightTm + optParam.firstPreFltTm) ;
	//const time_t shortDutyTm =  Minute*(optParam.maxDutyTm - optParam.preFlightTm - optParam.postFlightTm) ;//used for elimination, feasible
	const time_t shortDutyTm =  Minute*(optParam.maxDutyTm - optParam.postFlightTm) ;// 07/17/2017 ANG

	memset(&tmpData, 0, sizeof(DL_DataOneDay) );//initialize, array of data entries
	memset(pAvailTm, 0, sizeof(pAvailTm) );//pilot available time including pre flight time
	availTmB4Duty = 0 ; //resource available time before current duty
	
	numTmpData = 0; //number of data entries //initialize
	curDataP = &(tmpData[numTmpData]);//temp hold current data

/////////////////////////////////////////////////////////////////////////get the time interval for current cp on dayOne
	if( origDuties[startP->cpInd] != NULL ) //cp is assigned in the opt solution
	{
		//consider this cp's assignment on dayOne
		curDutyP = &(origDuties[startP->cpInd][dayOne]) ;

		//duty before current day
		for( i = dayOne-1; i >= 0; i --)
			 if( origDuties[startP->cpInd][i].numAc )
			 {
				 prevDutyP = &(origDuties[startP->cpInd][i]) ;
				 break;
			 }

		//duty after current day
		for( i = dayOne+1; i < optParam.planningWindowDuration; i ++)
			if( origDuties[startP->cpInd][i].numAc )
			{
				nextDutyP = &(origDuties[startP->cpInd][i]) ;
				break ;
			}
		_ASSERTE( !nextDutyP || startP->type != DL_NewAvail ) ;

///////////////////////////////////////////////////////////////////////////////////////////////////set pilot available info before current duty
		if( prevDutyP )//there is prev duty for this cp
		{
			curDataP->pAirports[1] = curDataP->pAirports[0] =  prevDutyP->lastLegP->leg->schedInAptID ;
			curDataP->pTms[1] = curDataP->pTms[0] = prevDutyP->lastLegP->leg->schedIn + Minute*(optParam.postFlightTm + optParam.minRestTm) ;//ASSUME rest
			//pAvailTm[1] = pAvailTm[0] = curDataP->pTms[0] + Minute*optParam.preFlightTm ;//including pre flight time
			pAvailTm[1] = pAvailTm[0] = curDataP->pTms[0] + Minute*acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm ;//07/17/2017 ANG
		} else // first duty for this cp, use start info to set pilot's available time
		{
			for(j=0; j < 2 ; j++)
			{
				curDataP->pAirports[j] = startP->pilots[j].apt ;
				curDataP->pTms[j] = startP->pilots[j].tm ;
				//pAvailTm[j] = curDataP->pTms[j] + Minute*( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? optParam.firstPreFltTm : optParam.preFlightTm ) ;
				pAvailTm[j] = curDataP->pTms[j] + Minute*( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? optParam.firstPreFltTm : acTypeList[crewList[startP->pilots[j].ind].acTypeIndex].preFlightTm ) ; //07/17/2017 ANG
			}
		}

		if( curDutyP->numAc ) //cp is assigned on current day
		{
			_ASSERTE( startP->type != DL_NewAvail ) ;
///////////////////////////////////////////////////////////////////////////////////////////////////set ac available info before current duty
			//can not use the first ac of the tour
			curDataP->acInd = curDutyP->acInd[0] ; //take first ac of current duty
			if( (legP = curDutyP->firstLegP->prevAcLeg) != NULL )//there is prev ac leg
			{
				curDataP->acAirport = legP->leg->schedInAptID ;
				curDataP->acTm = legP->leg->schedIn + Minute*optParam.turnTime ;
			} else//no prev ac leg
			{
				_ASSERTE( ( !prevDutyP || prevDutyP->acInd[prevDutyP->numAc-1] != curDutyP->acInd[0] )
				&& ( acList[curDutyP->acInd[0]].availDT < curDutyP->firstLegP->leg->schedOut || curDutyP->tmFixed == DL_StartTmFixed 
					|| curDutyP->tmFixed == DL_BothTmFixed || curDutyP->tmFixed == DL_DutyCombined )
				&& acList[curDutyP->acInd[0]].availAirportID == curDutyP->firstLegP->leg->schedOutAptID );

				curDataP->acAirport = acList[curDutyP->acInd[0]].availAirportID ;
				curDataP->acTm = acList[curDutyP->acInd[0]].availDT;
			}

			//resource available time
			availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;

///////////////////////////////////////////////////////////////////////////////////////////////////set the interval
			if( curDutyP->tmFixed == DL_BothTmFixed || curDutyP->tmFixed == DL_DutyCombined 
			//|| curDutyP->lastLegP->leg->schedIn - curDutyP->firstLegP->leg->schedOut > shortDutyTm ) //time fixed (if there are two cp's of one pilot)
			|| curDutyP->lastLegP->leg->schedIn - curDutyP->firstLegP->leg->schedOut > (shortDutyTm - Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ) // 07/17/2017 ANG
			{
				curDataP->lStartTm = curDataP->eStartTm = curDutyP->firstLegP->leg->schedOut ;
				curDataP->lEndTm = curDataP->eEndTm = curDutyP->lastLegP->leg->schedIn ;

				if( curDutyP->tmFixed == DL_BothTmFixed )
					curDataP->keepOriginal = DL_KO_TmFixed ;
				else if( curDutyP->tmFixed == DL_DutyCombined )
					curDataP->keepOriginal = DL_KO_Combined ;
				else
					curDataP->keepOriginal = DL_KO_DutyTime ;
			} else
			{
///////////////////////////////////////////////////////////////////////////////////////////////////consider start time
				curDataP->lStartTm = curDutyP->firstLegP->leg->schedOut ;

				if( curDutyP->tmFixed != DL_StartTmFixed )//set early start time
				{
					_ASSERTE( availTmB4Duty <= curDataP->lStartTm ); 

					//start time: initialize to be day start time
					curDataP->eStartTm = (curDutyP->firstLegP->schOutDay == 0 ? optParam.windowStart : firstEndOfDay + (curDutyP->firstLegP->schOutDay  - 1)*DayInSecs ) ;
					
					//start time: compare to resource available time
					curDataP->eStartTm = max(curDataP->eStartTm, availTmB4Duty) ;

					//start time: compare to tour start time //not extending when overtime, unless pilot has alreadty started a leg
					for(j=0; j < 2; j ++)
						if( curDutyP->firstLegP->prevPilotLegs[j] == NULL 
						&& crewList[startP->pilots[j].ind].tourStartTm + Minute*optParam.firstPreFltTm > curDataP->eStartTm )
							curDataP->eStartTm = min(crewList[startP->pilots[j].ind].tourStartTm + Minute*optParam.firstPreFltTm, curDataP->lStartTm) ;
					
					_ASSERTE( curDataP->lStartTm >= curDataP->eStartTm ) ;
				} else
					curDataP->eStartTm = curDataP->lStartTm ;

///////////////////////////////////////////////////////////////////////////////////////////////////consider end time
				curDataP->eEndTm = curDutyP->lastLegP->leg->schedIn ;

				if( curDutyP->tmFixed != DL_EndTmFixed )
				{
					//end time: initialize to be day end time
					curDataP->lEndTm = ( curDutyP->lastLegP->schInDay == (optParam.planningWindowDuration - 1) ? optParam.windowEnd : firstEndOfDay + curDutyP->lastLegP->schInDay*DayInSecs );

					//end time: compare to the leg with the same ac after current duty, check against end time
					if( (legP = curDutyP->lastLegP->nextAcLeg ) != NULL && legP->leg->schedOut - Minute*optParam.turnTime < curDataP->lEndTm )
						curDataP->lEndTm = legP->leg->schedOut - Minute*optParam.turnTime ;

					//end time: compare to the leg with the same pilot after current duty, and tour end time
					for(j=0; j < 2; j ++)
					{
						if( (legP = curDutyP->lastLegP->nextPilotLegs[j] ) != NULL )
						{
							//if( legP->leg->schedOut - longRestTm <  curDataP->lEndTm )
							if( legP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) <  curDataP->lEndTm ) // 07/17/2017 ANG
								//curDataP->lEndTm = legP->leg->schedOut - longRestTm ;
								curDataP->lEndTm = legP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ; // 07/17/2017 ANG
						//} else if( availPilots[startP->pilots[j].ind].latestLegEndB4HTravel < curDataP->lEndTm )//not extending when overtime, unless pilot has next duty
						//	curDataP->lEndTm = max(availPilots[startP->pilots[j].ind].latestLegEndB4HTravel, curDataP->eEndTm) ;
						} else if( startP->latestLegEndB4HTravel < curDataP->lEndTm )//not extending when overtime, unless pilot has next duty
							curDataP->lEndTm = max(startP->latestLegEndB4HTravel, curDataP->eEndTm) ;
					}
					_ASSERTE( curDataP->eEndTm <= curDataP->lEndTm ) ;
				} else
					curDataP->lEndTm = curDataP->eEndTm ;

///////////////////////////////////////////////////////////////////////////////////////////////////consider end time
				//compare interval against max duty time
				//curDataP->lEndTm = min(curDataP->lEndTm, curDataP->lStartTm + shortDutyTm ) ; //if max interval is too long, consider max duty time
				curDataP->lEndTm = min(curDataP->lEndTm, curDataP->lStartTm + (shortDutyTm - Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ) ; // 07/17/2017 ANG
				//curDataP->eStartTm = max(curDataP->eStartTm, curDataP->eEndTm - shortDutyTm ) ;//if max interval is too long, consider max duty time
				curDataP->eStartTm = max(curDataP->eStartTm, curDataP->eEndTm - (shortDutyTm - Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ) ;// 07/17/2017 ANG

				_ASSERTE( curDataP->eStartTm <= curDataP->lStartTm && curDataP->eEndTm <= curDataP->lEndTm );

				if( curDataP->lStartTm - curDataP->eStartTm < DiscrtCrewInterval ) //interval too small, fix
					curDataP->eStartTm = curDataP->lStartTm ; //fix start time: use late start time

				if( curDataP->lEndTm - curDataP->eEndTm < DiscrtCrewInterval ) //interval too small, fix
					curDataP->lEndTm = curDataP->eEndTm ;//fix end time
			}//end else
			numTmpData ++ ;
		} else //current day not assigned, but is assigned on other days 
		{
			//start time: initialize early start time to be day start time
			curDataP->eStartTm = ( dayOne == 0 ? optParam.windowStart: (firstEndOfDay + (dayOne - 1)*DayInSecs) ) ;

			//end time: initialize late end time to be day end time
			curDataP->lEndTm = (dayOne == (optParam.planningWindowDuration - 1) ? optParam.windowEnd: (firstEndOfDay + dayOne*DayInSecs) ) ;

			_ASSERTE( prevDutyP || nextDutyP ); //exist either prev or next

			//special case: both prev and next duties exist AND have different airplanes --> generate two cases: one for each airplane; they must be in the same tour
			if( prevDutyP && nextDutyP )
			{
				_ASSERTE( startP->type != DL_NewAvail );
///////////////////////first sub-case: crew the ac of the prev duty --> check the next leg of this ac --> current duty must end before it
				curDataP->acInd = prevDutyP->acInd[prevDutyP->numAc -1] ;
				curDataP->acAirport = prevDutyP->lastLegP->leg->schedInAptID ;
				curDataP->acTm = prevDutyP->lastLegP->leg->schedIn + Minute*optParam.turnTime ;
				
				//start time: compare to resource available time
				availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;
				//no need to compare to tour start time
				curDataP->eStartTm = max( curDataP->eStartTm, availTmB4Duty);

				//end time: compare to next ac leg //needed
				if( (legP = prevDutyP->lastLegP->nextAcLeg) != NULL && legP->leg->schedOut - Minute*optParam.turnTime < curDataP->lEndTm )
					curDataP->lEndTm = legP->leg->schedOut - Minute*optParam.turnTime ;
				
				//end time: next pilot time; same for both pilots
				//if( nextDutyP->firstLegP->leg->schedOut - longRestTm < curDataP->lEndTm )
				if( nextDutyP->firstLegP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) < curDataP->lEndTm ) // 07/17/2017 ANG
					//curDataP->lEndTm = nextDutyP->firstLegP->leg->schedOut - longRestTm ;
					curDataP->lEndTm = nextDutyP->firstLegP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ; // 07/17/2017 ANG

				//no need to compare to tour end time

				if( curDataP->lEndTm - curDataP->eStartTm >= MinCrewInterval ) //feasible, will keep this crewing duty
					numTmpData ++ ;

///////////////////////second case: crew the ac of the next duty --> check the prev leg of this ac --> current duty must start after it
				if(prevDutyP->acInd[prevDutyP->numAc -1] != nextDutyP->acInd[0] )
				{
					//initialize current temp data
					//if previous temp data is infeasible, then it will be erased
					memset((curDataP = &(tmpData[numTmpData])), 0, sizeof(DL_DataOneDay)) ; 

					//pilots available
					curDataP->pAirports[1] = curDataP->pAirports[0] =  prevDutyP->lastLegP->leg->schedInAptID ;
					curDataP->pTms[1] = curDataP->pTms[0] = prevDutyP->lastLegP->leg->schedIn + Minute*(optParam.postFlightTm + optParam.minRestTm) ;//ASSUME rest
					//pAvailTm[1] = pAvailTm[0] = curDataP->pTms[0] + Minute*optParam.preFlightTm ;//including pre flight time
					pAvailTm[1] = pAvailTm[0] = curDataP->pTms[0] + Minute*acTypeList[acList[prevDutyP->acInd[prevDutyP->numAc -1]].acTypeIndex].preFlightTm ;//07/17/2017 ANG

					//ac available
					curDataP->acInd = nextDutyP->acInd[0] ;
					if( (legP = nextDutyP->firstLegP->prevAcLeg) != NULL )
					{
						curDataP->acAirport = legP->leg->schedInAptID ;
						curDataP->acTm = legP->leg->schedIn + Minute*optParam.turnTime ;
					} else//no prev ac leg
					{
						_ASSERTE( (acList[nextDutyP->acInd[0]].availDT <= nextDutyP->firstLegP->leg->schedOut || nextDutyP->tmFixed == DL_StartTmFixed  
							|| nextDutyP->tmFixed == DL_BothTmFixed || nextDutyP->tmFixed == DL_DutyCombined )
						&& acList[nextDutyP->acInd[0]].availAirportID == nextDutyP->firstLegP->leg->schedOutAptID );

						curDataP->acAirport = acList[curDutyP->acInd[0]].availAirportID ;
						curDataP->acTm = acList[curDutyP->acInd[0]].availDT ;
					}

					//start time: initialize early start time to be day start time
					curDataP->eStartTm = ( dayOne == 0 ? optParam.windowStart: (firstEndOfDay + (dayOne - 1)*DayInSecs) ) ;
					//start time: compare to resource available time
					availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;
					curDataP->eStartTm = max( curDataP->eStartTm, availTmB4Duty);

					//start time: no need to compare to tour start time

					//end time: initialize late end time to be day end time
					curDataP->lEndTm = (dayOne == (optParam.planningWindowDuration - 1) ? optParam.windowEnd: (firstEndOfDay + dayOne*DayInSecs) ) ;

					//end time: next pilot time; same for both pilots; also implying the next ac time
					//if( nextDutyP->firstLegP->leg->schedOut - longRestTm < curDataP->lEndTm )
					if( nextDutyP->firstLegP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) < curDataP->lEndTm ) // 07/17/2017 ANG
						//curDataP->lEndTm = nextDutyP->firstLegP->leg->schedOut - longRestTm ;
						curDataP->lEndTm = nextDutyP->firstLegP->leg->schedOut - (longRestTm + Minute*acTypeList[acList[curDataP->acInd].acTypeIndex].preFlightTm) ; // 07/17/2017 ANG
					
					//end time: no need to compare to tour end time

					if( curDataP->lEndTm - curDataP->eStartTm >= MinCrewInterval ) //feasible, will keep this crewing duty
						numTmpData ++ ;
					_ASSERTE( numTmpData <= DL_MAX_NUM_TEMP_DATA );
				}//end if

			} else if( prevDutyP && !nextDutyP )//prev, no next: crew the last ac of the prev duty
			{
				//two cases depending on startP : opt tour containing prevDutyP, or a new tour with same crew pair
				if( startP->type != DL_NewAvail )//include prevDutyP
				{
					curDataP->acInd = prevDutyP->acInd[prevDutyP->numAc -1] ;
					curDataP->acAirport = prevDutyP->lastLegP->leg->schedInAptID ;
					curDataP->acTm = prevDutyP->lastLegP->leg->schedIn + Minute*optParam.turnTime ;
				} else//ac different from prevDutyP
				{
					_ASSERTE( startP->ac.ind != prevDutyP->acInd[prevDutyP->numAc -1] && !prevDutyP->lastLegP->nextPilotLegs[0] && !prevDutyP->lastLegP->nextPilotLegs[1] ); 

					curDataP->acInd = startP->ac.ind ;
					curDataP->acAirport = startP->ac.apt ;
					curDataP->acTm = startP->ac.tm ;
				}

				//start time: compare to resource available time
				availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;
				//start time: no need to compare to tour start time
				curDataP->eStartTm = max( curDataP->eStartTm, availTmB4Duty);

				//end time: compare to the next leg of the same ac, for opt tours
				if(startP->type != DL_NewAvail && (legP = prevDutyP->lastLegP->nextAcLeg) != NULL && legP->leg->schedOut - Minute*optParam.turnTime < curDataP->lEndTm )
					curDataP->lEndTm = legP->leg->schedOut - Minute*optParam.turnTime ;

				//end time: compare to the next leg of the same pilot, or tour end time
				for(j=0; j < 2; j ++)
					if( (legP = prevDutyP->lastLegP->nextPilotLegs[j] ) != NULL )
					{
						//if( legP->leg->schedOut - longRestTm <  curDataP->lEndTm )
						if( legP->leg->schedOut - (longRestTm + Minute*acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm) <  curDataP->lEndTm ) // 07/17/2017 ANG
							//curDataP->lEndTm = legP->leg->schedOut - longRestTm ;
							curDataP->lEndTm = legP->leg->schedOut - (longRestTm + Minute*acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm) ; // 07/17/2017 ANG
					//} else if( availPilots[startP->pilots[j].ind].latestLegEndB4HTravel < curDataP->lEndTm )//not  extending when overtime, unless pilot has next duty
					//	curDataP->lEndTm = availPilots[startP->pilots[j].ind].latestLegEndB4HTravel ;
					} else if( startP->latestLegEndB4HTravel < curDataP->lEndTm )//not  extending when overtime, unless pilot has next duty
						curDataP->lEndTm = startP->latestLegEndB4HTravel ;


				if( curDataP->lEndTm - curDataP->eStartTm >= MinCrewInterval ) //feasible, will keep this crewing duty
					numTmpData ++ ;

			} else if( !prevDutyP && nextDutyP )//next, no prev: crew the first ac of the next duty: use start info
			{
				_ASSERTE( startP->type != DL_NewAvail && startP->ac.ind == nextDutyP->acInd[0] );

				curDataP->acInd = nextDutyP->acInd[0] ;
				curDataP->acAirport = startP->ac.apt ;
				curDataP->acTm = startP->ac.tm ;

				//start time: compare to resource available time
				availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;
				curDataP->eStartTm = max( curDataP->eStartTm, availTmB4Duty);

				//start time: compare tour start time
				for(j=0; j < 2; j ++)
					if( (legP = nextDutyP->firstLegP->prevPilotLegs[j] ) == NULL )
						curDataP->eStartTm = max( curDataP->eStartTm, crewList[startP->pilots[j].ind].tourStartTm + Minute*optParam.firstPreFltTm );

				//end time: next pilot time; same for both pilots; also implying next ac leg
				for(j=0; j < 2; j ++)
				{
					//adjRestTm = ( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? longRestFirstTm : longRestTm );
					adjRestTm = ( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? longRestFirstTm : (longRestTm + Minute*acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm) ); // 07/17/2017 ANG
					curDataP->lEndTm = min(curDataP->lEndTm, nextDutyP->firstLegP->leg->schedOut - adjRestTm) ;
				}
				//end time: no need to compare tour end time

				if( curDataP->lEndTm - curDataP->eStartTm >= MinCrewInterval ) //feasible, will keep this crewing duty
					numTmpData ++ ;
			}
		}//end else
	} else //this cp not assigned
	{
		_ASSERTE( startP->type == DL_NewAvail ) ; //&& !startP->nextAcOrigTour && !startP->nextPilotOrigTour[0] && !startP->nextPilotOrigTour[1] );

		for(j=0; j < 2 ; j++)
		{
			curDataP->pAirports[j] = startP->pilots[j].apt ;
			curDataP->pTms[j] = startP->pilots[j].tm ;
			//add pre flight time
			//pAvailTm[j] = curDataP->pTms[j] + Minute*( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? optParam.firstPreFltTm : optParam.preFlightTm ) ;
			pAvailTm[j] = curDataP->pTms[j] + Minute*( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg ? optParam.firstPreFltTm : acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm ) ; //07/17/2017 ANG
		}
		curDataP->acInd = startP->ac.ind ;
		curDataP->acAirport = startP->ac.apt ;
		curDataP->acTm = startP->ac.tm ;
		
		//compare to resource available time
		availTmB4Duty = max( curDataP->acTm, max(pAvailTm[0], pAvailTm[1])) ;

		//start time: initialize
		curDataP->eStartTm = ( dayOne == 0 ? optParam.windowStart: (firstEndOfDay + (dayOne - 1)*DayInSecs) ) ;
		//start time: compare to resource available time
		curDataP->eStartTm = max( curDataP->eStartTm, availTmB4Duty);
		//start time: compare tour start time
		for(j=0; j < 2; j ++)
			curDataP->eStartTm = max( curDataP->eStartTm, crewList[startP->pilots[j].ind].tourStartTm + Minute*optParam.firstPreFltTm );

		//end time: initialize
		curDataP->lEndTm = (dayOne == (optParam.planningWindowDuration - 1) ? optParam.windowEnd : (firstEndOfDay + dayOne*DayInSecs) ) ;
		//end time: no need to check later legs of the same ac or pilot, if startP->type == DL_NewAvail
		//end time: tour end time
		for(j=0; j < 2; j ++) //no legs after
			//curDataP->lEndTm = min( curDataP->lEndTm, availPilots[startP->pilots[j].ind].latestLegEndB4HTravel );
			curDataP->lEndTm = min( curDataP->lEndTm, startP->latestLegEndB4HTravel );

		if( curDataP->lEndTm - curDataP->eStartTm >= MinCrewInterval ) //feasible, will keep this crewing duty
			numTmpData ++ ;
	}//end else

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////adjust interval
	if( curDutyP && curDutyP->numAc )//cp is assigned on current day
		//_ASSERTE( numTmpData == 1 && ( tmpData[0].keepOriginal || tmpData[0].eEndTm - tmpData[0].lStartTm <= shortDutyTm ));
		_ASSERTE( numTmpData == 1 && ( tmpData[0].keepOriginal || tmpData[0].eEndTm - tmpData[0].lStartTm <= (shortDutyTm - Minute*acTypeList[crewPairList[startP->cpInd].acTypeIndex].preFlightTm) )); // 07/17/2017 ANG
	else//cp is not assigned: early start time and late end time are generated above
	{
		if( !numTmpData )
			return 1 ;

		lastInd = 0 ; //if tmpData[i] is feasible, copy tmpData[i] to tmpData[lastInd]
		for(i=0; i < numTmpData; i++)
		{
			_ASSERTE( tmpData[i].lEndTm - tmpData[i].eStartTm >= MinCrewInterval );

			//check long maintenance against [tmpData[i].eStartTm, tmpData[i].lEndTm]
			for(j=0; j<numLongMaint; j++)
				//same ac and intersect
				if( demandList[longMaint[j]].acInd == tmpData[i].acInd && tmpData[i].eStartTm < inTimes[longMaint[j]] && tmpData[i].lEndTm > outTimes[longMaint[j]] )
					break ;

			if( j<numLongMaint )
			{
				tmpData[i].eStartTm = (tmpData[i].eStartTm > outTimes[longMaint[j]] ? inTimes[longMaint[j]] : tmpData[i].eStartTm );
				tmpData[i].lEndTm = (tmpData[i].lEndTm < inTimes[longMaint[j]] ? outTimes[longMaint[j]] : tmpData[i].lEndTm );

				 if( tmpData[i].lEndTm - tmpData[i].eStartTm < MinCrewInterval ) //include the case tmpData[i].lEndTm < tmpData[i].eStartTm
					 continue ; //go to the next tmpData[i]
			}
			
			//get the complete interval
			tmpData[i].lStartTm = tmpData[i].lEndTm - MinCrewInterval ; //set late start time
			//set early end time
			if( tmpData[i].lStartTm - tmpData[i].eStartTm < DiscrtCrewInterval ) //interval may have been changed as above
			{
				tmpData[i].eStartTm = tmpData[i].lStartTm ; //fix start time to be late start time
				tmpData[i].eEndTm = tmpData[i].lEndTm ; //fix end time to be late end time
			} else
			{
				tmpData[i].eEndTm = tmpData[i].eStartTm + MinCrewInterval ; 
				_ASSERTE( tmpData[i].lEndTm - tmpData[i].eEndTm >= DiscrtCrewInterval ) ;
			}

			memmove(&(tmpData[lastInd]), &(tmpData[i]), sizeof(DL_DataOneDay) ) ;//copy
			lastInd ++ ;
		}//end for

		if( (numTmpData = lastInd ) == 0 )//no feasible
			return 1;
	}
/////////////////////////////////////////////////////////////////////////end get the time interval for current cp and ac on dayOne
	
/////////////////////////////////////////////////////////////////////////pre check travels
	_ASSERTE( dayZero != dayOne || numTmpData == 1 );
	if( dayZero == dayOne && !tmpData[0].keepOriginal )
		for( j=0; j < 2; j ++ )//for each pilot
		{
			_ASSERTE( tmpData[0].pAirports[j] > 0 && tmpData[0].pTms[j] > 0 && tmpData[0].acAirport > 0 );
			if( tmpData[0].pAirports[j] != tmpData[0].acAirport )//non trivial travel
			{
				if( startP->pilots[j].status == DL_Pilot_Status_Active )
					//preFltTm = Minute*optParam.preFlightTm ; //temp
					preFltTm = Minute* acTypeList[acList[startP->ac.ind].acTypeIndex].preFlightTm ; //07/17/2017 ANG
				else if ( startP->pilots[j].status == DL_Pilot_Status_Rest_B4Leg )
					//preFltTm = Minute*optParam.preFlightTm ;
					preFltTm = Minute* acTypeList[acList[startP->ac.ind].acTypeIndex].preFlightTm ;//07/17/2017 ANG
				else
				{
					_ASSERTE( startP->pilots[j].status == DL_Pilot_Status_Rest_B4FirstLeg );
					preFltTm = Minute*optParam.firstPreFltTm ;
				}

				//early travel to tmpData[0].lStartTm
				if ( tmpData[0].pTms[j] < tmpData[0].lStartTm - preFltTm
				&& !getCrewTravelDataEarly(tmpData[0].pTms[j], tmpData[0].lStartTm - preFltTm, tmpData[0].pAirports[j], tmpData[0].acAirport, &departTm
					, &dutyStartTm, &arrivalTm, &cost, withOag)) //will check again //not considering total duty time now
				{
					if( arrivalTm + preFltTm > tmpData[0].eStartTm )//need to update tmpData[0].eStartTm
					{
						if( tmpData[0].lStartTm - arrivalTm - preFltTm < DiscrtCrewInterval ) //new interval is too small
							tmpData[0].eStartTm = tmpData[0].lStartTm ; //fix start time: use late start time
						else
							tmpData[0].eStartTm = arrivalTm + preFltTm;
					}
				} else if( curDutyP && curDutyP->numAc ) //currently assigned on current day
				{
					tmpData[0].keepOriginal = DL_KO_NoTravel ; //travel not feasible, but keep it 
					tmpData[0].eStartTm = tmpData[0].lStartTm; //not adjusting 
					tmpData[0].lEndTm = tmpData[0].eEndTm;
					break; //keep original
				} else
					return 1; //infeasible for current day
			}
		}//end for
/////////////////////////////////////////////////////////////////////////end pre check travels

/////////////////////////////////////////////////////////////////////////set additional attributes
	for( i=0; i < numTmpData; i ++ )
	{
		tmpData[i].origDutyP = ( (curDutyP && curDutyP->numAc) ? curDutyP : NULL ) ; //not null --> legs assigned
		tmpData[i].firstDay = dayZero;
		tmpData[i].day = dayOne;
		tmpData[i].startInfoP = startP;
		_ASSERTE( tmpData[i].eStartTm <= tmpData[i].lStartTm && tmpData[i].eEndTm <= tmpData[i].lEndTm) ;
	}
/////////////////////////////////////////////////////////////////////////set additional attributes

	return (getNewDutiesWithTempData(tmpData, numTmpData, newDutiesP, numNewDutiesP));
}


//get newDuties of current cp and ac on dayOne
static int getNewDutiesWithTempData(DL_DataOneDay* tmpDataP, const int numTmpData, DL_NewDuty **newDutiesP, int *numNewDutiesP)
{
	time_t tZero, tOne, departTm, dutyStartTm, arrivalTm, pDutyStartTm[2][DL_MAX_NUM_TRAVEL_PER_PILOT];
	time_t pilotStartTm[2][DL_MAX_NUM_TRAVEL_PER_PILOT] ; 
	double cost, tCosts[2][DL_MAX_NUM_TRAVEL_PER_PILOT];
	int i, j, m, n, noNewCol, firstDuty[2] ;
	int flightTm, blockTm, elapsedTm, numStops, tempArr;
	DL_NewDuty *aP = NULL, temCol;
	//const DL_TourStartInfo *startP = &(tourStartInfos[index]) ;
	DL_DataOneDay* inP=NULL;
	time_t preFltTm[2] ;

	const time_t minDutyTm = Minute*(30 + optParam.postFlightTm) ;//for travel
	const int cont = 1 ; //trivial

	noNewCol = 1 ;//whether new columns found, return 
	for(i=0; i < numTmpData; i ++)
	{
		inP = &(tmpDataP[i]); 
		// inP->keepOriginal may be changed; fine because this procedure stops once inP->keepOriginal is changed

		//get pre flight time before current tour
		for(n=0; n < 2; n ++)//for each pilot
		{
			//whether first duty of a pilot's TOUR
			if( inP->day == inP->firstDay && inP->startInfoP->pilots[n].status == DL_Pilot_Status_Rest_B4FirstLeg )
				firstDuty[n] = 1; 
			else
				firstDuty[n] = 0 ;
			//pre flight time after travel or rest, and before the first duty of current tour
			//may need it even if current duty is not the first duty
			if( inP->startInfoP->pilots[n].status == DL_Pilot_Status_Active )
				//preFltTm[n] = Minute*optParam.preFlightTm ;//temp
				preFltTm[n] = Minute*acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ; // 07/17/2017 ANG
			else if ( inP->startInfoP->pilots[n].status == DL_Pilot_Status_Rest_B4FirstLeg )
				preFltTm[n] = Minute*optParam.firstPreFltTm ;
			else
			{
				_ASSERTE( inP->startInfoP->pilots[n].status == DL_Pilot_Status_Rest_B4Leg );
				//preFltTm[n] = Minute*optParam.preFlightTm ;
				preFltTm[n] = Minute*acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ; // 07/17/2017 ANG
			}
		}

////////////////////////////////////////////////////////////////////////////check interval [tZero, tOne]
		tZero = inP->eStartTm ; //left end starts with early start time
		while ( cont ) //temp
		{
			//keepOriginal == 1 if this duty contains legs and
			//both start time and end time are fixed, or current duty time violates, or no feasible travels to the first leg start time
			_ASSERTE( !inP->keepOriginal 
			|| (inP->startInfoP->type != DL_NewAvail && *numNewDutiesP == 0 && inP->eStartTm == inP->lStartTm && inP->eEndTm == inP->lEndTm )) ;
			
			memset( pDutyStartTm, 0, sizeof(pDutyStartTm) ) ; //pilot duty start time b4 tZero: to check total duty time //[pilot][#travel]
			memset( pilotStartTm, 0, sizeof(pilotStartTm) ) ; //pilot start time b4 tZero: to connect //[pilot][#travel]
			memset( tCosts, 0, sizeof(tCosts) ) ; //travel cost for each pilot //[pilot][#travel]

///////////////////////////////////////////////////////////////////////////check travels
			if( inP->day == inP->firstDay && !inP->keepOriginal )
			{
				for(n=0; n < 2; n ++)//for each pilot
				{
					if( inP->pAirports[n] == inP->acAirport )//if trivial travel
					{
						if(inP->startInfoP->pilots[n].status != DL_Pilot_Status_Active)//was resting
						{
							pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - preFltTm[n] ;//note: pre flight time
						//} else if (inP->pTms[n] + Minute*(optParam.minRestTm + optParam.preFlightTm) <= tZero ) //can rest: two options
						} else if (inP->pTms[n] + Minute*(optParam.minRestTm + acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ) <= tZero ) //07/17/2017 ANG
						{
							//pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - Minute*optParam.preFlightTm ;//option: rest
							pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - Minute* acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ;//07/17/2017 ANG
							pilotStartTm[n][1] = pDutyStartTm[n][1] = inP->startInfoP->pilots[n].dutyStartTm ;//option: no rest
						} else
							pilotStartTm[n][0] = pDutyStartTm[n][0] = inP->startInfoP->pilots[n].dutyStartTm; //start time when available
					} else //redo travel
					{
						m = 0 ; //index for travel <= DL_MAX_NUM_TRAVEL_PER_PILOT

						//travel then rest
						if ( inP->pTms[n] < tZero - preFltTm[n] - Minute*optParam.minRestTm )
						{
							if( inP->startInfoP->pilots[n].status != DL_Pilot_Status_Active )//resting now, travel late, then rest
							{
								if( !getCrewTravelDataLate(inP->pTms[n], tZero - preFltTm[n] - Minute*optParam.minRestTm, inP->pAirports[n], inP->acAirport, &departTm, 
								&dutyStartTm, &arrivalTm, &cost, withOag ))
								{
									_ASSERTE(cost ) ; //&& arrivalTm - dutyStartTm <= getCurMaxDutyTm(firstDuty[n], dutyStartTm, inP->pAirports[n] )) ;
									if( arrivalTm - dutyStartTm <= getCurMaxDutyTm(firstDuty[n], dutyStartTm, inP->pAirports[n] ) )
									{
										pDutyStartTm[n][m] = tZero - preFltTm[n] ;
										pilotStartTm[n][m] = dutyStartTm ;//travel start time
										tCosts[n][m] = cost ;
										m ++ ;
										_ASSERTE( m <= DL_MAX_NUM_TRAVEL_PER_PILOT );
									}
								} 
							//active now, travel early, then rest
							} else if( !getCrewTravelDataEarly(inP->pTms[n], tZero - preFltTm[n] - Minute*optParam.minRestTm, inP->pAirports[n], inP->acAirport, &departTm
							, &dutyStartTm, &arrivalTm, &cost, withOag ))
							{
								_ASSERTE(cost) ;
								//need to check travel duty time
								if (inP->pTms[n] + Minute*optParam.minRestTm <= dutyStartTm && arrivalTm - dutyStartTm 
									<= getCurMaxDutyTm(firstDuty[n], dutyStartTm, inP->pAirports[n] )) //max duty time after rest, before travel
								//active, duty time violates, but can rest before travel starts //avtive+rest+travel+rest
								{
									pDutyStartTm[n][m] = tZero - preFltTm[n] ;
									pilotStartTm[n][m] = dutyStartTm ;//travel start time
									tCosts[n][m] = cost ;
									m ++ ;
									_ASSERTE( m <= DL_MAX_NUM_TRAVEL_PER_PILOT );
								} else if ( arrivalTm  - inP->startInfoP->pilots[n].dutyStartTm 
									<= getCurMaxDutyTm(firstDuty[n], inP->startInfoP->pilots[n].dutyStartTm, inP->pAirports[n] ))//max duty time if no rest //active+travel+rest
								{
									pDutyStartTm[n][m] = tZero - preFltTm[n] ;
									pilotStartTm[n][m] = inP->startInfoP->pilots[n].dutyStartTm  ;//travel start time
									tCosts[n][m] = cost ;
									m ++ ;
									_ASSERTE( m <= DL_MAX_NUM_TRAVEL_PER_PILOT );
								}
							} 
						}//end if ( inP->pTms[n] < tZero - preFltTm[n] - Minute*optParam.minRestTm )
						
						//travel, then no rest
						if (  inP->pTms[n] < tZero - preFltTm[n]
						&& !getCrewTravelDataLate(inP->pTms[n], tZero - preFltTm[n], inP->pAirports[n], inP->acAirport, &departTm, &dutyStartTm, &arrivalTm
						, &cost, withOag ))
						{
							_ASSERTE(cost); // &&  arrivalTm - dutyStartTm <= getCurMaxDutyTm(firstDuty[n], dutyStartTm, inP->pAirports[n])) ;

							//check if can rest before travel
							//rest+travel
							if( inP->startInfoP->pilots[n].status != DL_Pilot_Status_Active || inP->pTms[n] + Minute*optParam.minRestTm <= dutyStartTm)
							{
								if( tZero + minDutyTm - dutyStartTm <= getCurMaxDutyTm(firstDuty[n], dutyStartTm, inP->pAirports[n] ))
								{
									pilotStartTm[n][m] = pDutyStartTm[n][m] = dutyStartTm ;//use travel start time
									tCosts[n][m] = cost ;
									m ++ ;
									_ASSERTE( m <= DL_MAX_NUM_TRAVEL_PER_PILOT );
								}
							} else if(tZero + minDutyTm  - inP->startInfoP->pilots[n].dutyStartTm //cannot rest before travel //active+travel
								<= getCurMaxDutyTm(firstDuty[n], inP->startInfoP->pilots[n].dutyStartTm, inP->pAirports[n] ))
							{
								pilotStartTm[n][m] = pDutyStartTm[n][m] = inP->startInfoP->pilots[n].dutyStartTm ;
								tCosts[n][m] = cost ;
								m ++ ;
								_ASSERTE( m <= DL_MAX_NUM_TRAVEL_PER_PILOT );
							}
						}//end if

						if( !m ) //can't find feasible travels, or duty time infeasible, check to see if keep the original
						{
							if( inP->origDutyP && tZero == inP->lStartTm )//current duty is an original duty, and current start time is the leg start time, i.e. original duty infeasible
							{
								if( *numNewDutiesP ) //may have generated feasible timings for this duty, due to different max duty times of crew fatigue rules
								{
									//erase them and only keep current one (original one)
									for(j=0; j < *numNewDutiesP; j ++ )
										freeNewDuty( &((*newDutiesP)[j])) ;
									free( *newDutiesP );
									*newDutiesP = NULL;
									*numNewDutiesP = 0 ;
								}
								inP->keepOriginal = DL_KO_NoTravel ; //note: changed a component in inP, OK since current tZero is the last tZero
							}
							break; ////////////////////////////////////////////////////////////tZero not feasible, will continue to the nexy tZero
						}//end if( !m )
						
					}//end redo travel
				}//end for(n=0; n < 2; n ++)
				if( n < 2 && !inP->keepOriginal ) //infeasible and not keeping the original duty, continue to the next tZero
					goto tZeroUpdate ;
			} 
///////////////////////////////////////////////////////////////////////////end check travels

			//if not first duty, or keep the original duty, set start time
			if( inP->day != inP->firstDay || inP->keepOriginal )
				for(n=0; n < 2; n ++)//for each pilot
				{
					if( inP->startInfoP->type == DL_NewAvail || inP->day <= inP->startInfoP->legStartDay )
						pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - preFltTm[n] ;//same pre flight time as the first duty
					else
						//pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - Minute*optParam.preFlightTm ;
						pilotStartTm[n][0] = pDutyStartTm[n][0] = tZero - Minute* acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ;//07/17/2017 ANG
				}

////////////////////////////////////////////////////////////////////////////////initialize the righ end
			tOne = (  inP->origDutyP ? inP->eEndTm : tZero + MinCrewInterval );
			_ASSERTE( tOne <= inP->lEndTm );

			while ( cont ) //for tOne
			{
				//////////////////////////////////////////////////////////////////////////////////////////////check total duty time
				if( !inP->keepOriginal )
				{
					for(m=0; m < DL_MAX_NUM_TRAVEL_PER_PILOT; m++)//first pilot's travel
					{
						if( pDutyStartTm[0][m] 
						&& tOne + Minute*optParam.postFlightTm -  pDutyStartTm[0][m] <= getCurMaxDutyTm(firstDuty[0], pDutyStartTm[0][m], inP->pAirports[0]))
						{
							for(n=0; n < DL_MAX_NUM_TRAVEL_PER_PILOT; n++)//second pilot's travel
								if( pDutyStartTm[1][n] 
								&& tOne + Minute*optParam.postFlightTm -  pDutyStartTm[1][n] <= getCurMaxDutyTm(firstDuty[1], pDutyStartTm[1][n], inP->pAirports[1]))
									break;
							if( n < DL_MAX_NUM_TRAVEL_PER_PILOT )
								break;
						}
					}
					if( m >= DL_MAX_NUM_TRAVEL_PER_PILOT ) //no fesible duty times found
					{
						if ( inP->origDutyP && tZero == inP->lStartTm && tOne == inP->eEndTm) //if original duty assigned, fix tOne
						{
							if( *numNewDutiesP ) //may have generated feasible timings for this duty, due to different max duty times of crew fatigue rules
							{
								//erase them and only keep current one (original one)
								for(j=0; j < *numNewDutiesP; j ++ )
									freeNewDuty( &((*newDutiesP)[j])) ;
								free( *newDutiesP );
								*newDutiesP = NULL;
								*numNewDutiesP = 0 ;
							}
							inP->keepOriginal = DL_KO_NoTravel ; //note
						} else
							break; //to the next TZERO: tOne increasing --> next tZero is infeasible
					}//end if
				}

				//generate a temp column, containing covering info
				memset(&temCol, 0, sizeof(DL_NewDuty) ); //initialize
				
				//check covered demands: they don't depend on the travels
				for(n=0; n < numDemand; n++)
				{
					//non-trivial, and demand end
					if( demandList[n].isAppoint || demandList[n].outAirportID == demandList[n].inAirportID )
						continue;

					m = crewPairList[inP->startInfoP->cpInd].acTypeIndex; //temp: ac type
					//upgrade and downgrade feasibility
					if( acTypeList[m].sequencePosn > (demandList[n].sequencePosn + max(demandList[n].maxUpgradeFromRequest, max(demandList[n].upgradeRecovery, acTypeList[m].maxUpgrades)))
					|| acTypeList[m].sequencePosn < demandList[n].sequencePosn - demandList[n].downgradeRecovery )
						continue;
					//get arrival time
					if( inP->acAirport != demandList[n].outAirportID )
					{
						getFlightTime( inP->acAirport, demandList[n].outAirportID, acTypeList[m].aircraftTypeID, month, 0, &flightTm, &blockTm, &elapsedTm, &numStops) ;
						if( ( tempArr = getRepoArriveTm( inP->acAirport, demandList[n].outAirportID,  (int)(tZero/Minute), elapsedTm )) == -1 )
							continue;
						arrivalTm = Minute*(tempArr + optParam.turnTime) ;
						cost = (flightTm*acTypeList[m].operatingCost)/Minute + (numStops+1)*acTypeList[m].taxiCost ;
					} else
					{
						arrivalTm = tZero ; //turn time
						cost = 0;
					}
					//time to cover this demand
					if( arrivalTm > demandList[n].reqOut + RecoverDeptDelay || demandList[n].reqIn +  max(0, arrivalTm - demandList[n].reqOut) > tOne )
						continue ;

					if( allocCvdDemForCL(&temCol) )
					{
						logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
						exit(1);
					}

					temCol.coverDemCoefs[temCol.numCoverDems-1] = cost;//legP: covering cost
					temCol.coverDems[temCol.numCoverDems-1] = n; //demand index
				}//end for(n=0; n < numDemand; n++)
				
				//if( temCol.numCoverDems > 0 || ( inP->origDutyP && tZero == inP->lStartTm && tOne == inP->eEndTm) ) //demands covered, or keep the original interval
				//{
					_ASSERTE( temCol.startTm == 0  ) ;
					noNewCol = 0 ; //new columns will be generated

					temCol.startInfoP = inP->startInfoP;
					temCol.origDutyP = inP->origDutyP ; //not null --> there are legs assigned
					//temCol.cpInd = startP->cpInd;
					temCol.firstDay = inP->firstDay;
					temCol.day = inP->day;
					temCol.startTm = tZero;
					temCol.endTm = tOne;
					
					temCol.acInd = inP->acInd;//first ac
					temCol.acAirport = inP->acAirport ;
					temCol.acTm = inP->acTm ;
					for(n=0; n<2; n++)
					{
						temCol.pAirports[n] = inP->pAirports[n] ;
						temCol.pTms[n] = inP->pTms[n] ;
					}
					temCol.keepOriginal = inP->keepOriginal ;
	
				//} else 
				//	goto tOneUpdate ; //continue to the next tOne

				//keepOriginal == 1 if this duty contains legs and
				//both start time and end time are fixed, or current duty time violates, or no feasible travels to the first leg start time
				//or no feasible travels for the original duty
				if( inP->keepOriginal ) //temCol.keepOriginal 
				{
					_ASSERTE(inP->startInfoP->type != DL_NewAvail && *numNewDutiesP == 0) ;

					aP = allocANewDuty( newDutiesP, numNewDutiesP);//allocate a new duty
					copyNewDuty( aP, &temCol ); //copy from temp duty
					//set the missing ones
					for(n=0; n < 2; n ++)//for each pilot
					{
						if( inP->startInfoP->type == DL_NewAvail || inP->day <= inP->startInfoP->legStartDay )
							aP->pilotStartTm[n] = aP->pDutyStartTm[n] = tZero - preFltTm[n] ;//same pre flight time as the first duty
						else
							//aP->pilotStartTm[n] = aP->pDutyStartTm[n] = tZero - Minute*optParam.preFlightTm ;
							aP->pilotStartTm[n] = aP->pDutyStartTm[n] = tZero - Minute* acTypeList[acList[inP->acInd].acTypeIndex].preFlightTm ;//07/17/2017 ANG
					}
				} else
				{
					_ASSERTE( !temCol.keepOriginal );
/////////////////////////////////////////////////////////////////////////////////////////////////////go through travels to generate actual newDuties
					for(m=0; m < DL_MAX_NUM_TRAVEL_PER_PILOT; m++)
					{
						if( !pDutyStartTm[0][m]
						|| tOne + Minute*optParam.postFlightTm -  pDutyStartTm[0][m] > getCurMaxDutyTm(firstDuty[0], pDutyStartTm[0][m], inP->pAirports[0] ))
							continue ;

						for(n=0; n < DL_MAX_NUM_TRAVEL_PER_PILOT; n++)
						{
							if( !pDutyStartTm[1][n]
							|| tOne + Minute*optParam.postFlightTm -  pDutyStartTm[1][n] > getCurMaxDutyTm(firstDuty[1], pDutyStartTm[1][n], inP->pAirports[1]))
								continue ;

							aP = allocANewDuty( newDutiesP, numNewDutiesP);
							_ASSERTE( aP != NULL );

							copyNewDuty( aP, &temCol ); //copy from temp newDutyP
	
							aP->cost = tCosts[0][m] + tCosts[1][n] ; //travel costs
							aP->pilotStartTm[0] = pilotStartTm[0][m] ; //pilot start time
							aP->pilotStartTm[1] = pilotStartTm[1][n] ;
							aP->pDutyStartTm[0] = pDutyStartTm[0][m] ; //pilot duty start time before tZero
							aP->pDutyStartTm[1] = pDutyStartTm[1][n] ;

							//interval intersecting cost
							aP->cost += getCrewIntervalCost(pDutyStartTm[0][m], tOne+Minute*optParam.postFlightTm) ;
							aP->cost += getCrewIntervalCost(pDutyStartTm[1][n], tOne+Minute*optParam.postFlightTm) ;
						}//end for(n=0; n < DL_MAX_NUM_TRAVEL_PER_PILOT; n++)
					}//end for(m=0; m < DL_MAX_NUM_TRAVEL_PER_PILOT; m++)
				}//end else
				
				freeNewDuty( &temCol );//reset the temp column

//tOneUpdate:
				if( tOne + DiscrtCrewInterval > inP->lEndTm || inP->keepOriginal )
					break;
				tOne += DiscrtCrewInterval ; //increase tOne
				_ASSERTE( tOne - tZero >= MinCrewInterval || inP->origDutyP );
			}//end for(tOne =

tZeroUpdate:
			if( inP->keepOriginal )
				break;
			if( tZero + DiscrtCrewInterval <= inP->lStartTm )
			{
				_ASSERTE( tZero + DiscrtCrewInterval + MinCrewInterval <= inP->lEndTm || inP->origDutyP );
				tZero += DiscrtCrewInterval ;
			} else if (tZero < inP->lStartTm )
				tZero = inP->lStartTm ; //keep inP->lStartTm
			else
				break;

		}//end for(tZero
	}//end for(i=0; i < numTmpData; i ++)

	return noNewCol ;
}

static time_t getCurMaxDutyTm(const int firstDuty, const time_t curTime, const int curApt )
{
	int minutesAML ;
	const time_t fdReducedMaxDutyTm = 12*3600 ;
	const time_t adReducedMaxDutyTm = 10*3600 ;
	const int fdIntervalEnd = 480 ; // 8am local
	const int adIntervalEnd = 300 ; //5am local

	minutesAML = minutesPastMidnight(curTime, curApt) ;

	if( minutesAML < adIntervalEnd )
		return adReducedMaxDutyTm ;
	else if( firstDuty && minutesAML < fdIntervalEnd )
		return fdReducedMaxDutyTm ;
	else
		return Minute*optParam.maxDutyTm ;
}

static double getCrewIntervalCost(const time_t tZero, const time_t tOne)
{
	struct tm *tempTmP=NULL, startTm, endTm ; 
	time_t start, end ;
	double cost = 0;

	_ASSERTE( tOne - tZero <= 24*Hour );

	tempTmP = gmtime(&tZero);
	memmove(&startTm, tempTmP, sizeof(startTm));

	tempTmP = gmtime(&tOne);
	memmove(&endTm, tempTmP, sizeof(endTm));

	start = Hour*startTm.tm_hour + Minute*startTm.tm_min + startTm.tm_sec; //seconds of day
	end = Hour*endTm.tm_hour + Minute*endTm.tm_min + endTm.tm_sec;//seconds of day

	cost = 0 ;
	if( startTm.tm_mday != endTm.tm_mday )//across days
	{
		//two intervals [start, 24hour], [0, end] intersect [RegCrewStartTm, RegCrewEndTm]
		//CrewCostPerHour
		_ASSERTE( 24*Hour - start + end == tOne - tZero );

		if( RegCrewEndTm > start ) //compare to [start, 24hour]
			cost = CrewCostPerHour*(double)(RegCrewEndTm - max(RegCrewStartTm, start))/(double)Hour ;

		if( RegCrewStartTm < end ) //compare to [0, end]
			cost += CrewCostPerHour*(double)(min(RegCrewEndTm, end) - RegCrewStartTm)/(double)Hour ;
	} else
	{
		_ASSERTE( end - start == tOne - tZero );
		// [start, end] intersects [RegCrewStartTm, RegCrewEndTm]
		if( end > RegCrewStartTm && start < RegCrewEndTm )
			cost = CrewCostPerHour*(double)(min(RegCrewEndTm, end) - max(RegCrewStartTm, start))/(double)Hour ;
	}

	return cost;
} 

static int copyNewDuty ( DL_NewDuty *destP, const DL_NewDuty *origP )
{

	_ASSERTE( origP != NULL );

	memmove( destP, origP, sizeof(DL_NewDuty) );

	if( origP->numCoverDems > 0 )
	{
		_ASSERTE( origP->coverDems != NULL && origP->coverDemCoefs != NULL );
		if( !(destP->coverDems = (int *) calloc(origP->numCoverDems, sizeof(int))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		memmove(destP->coverDems, origP->coverDems, origP->numCoverDems*sizeof(int) );

		if( !(destP->coverDemCoefs = (double *) calloc(origP->numCoverDems, sizeof(double))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		memmove(destP->coverDemCoefs, origP->coverDemCoefs, origP->numCoverDems*sizeof(double) );
	}

	if( origP->numCoverRegions > 0 )
	{
		_ASSERTE( origP->coverRegions != NULL && origP->coverRegionCoefs != NULL );
		if( !(destP->coverRegions = (int *) calloc(origP->numCoverRegions, sizeof(int))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		memmove(destP->coverRegions, origP->coverRegions, origP->numCoverRegions*sizeof(int) );

		if( !(destP->coverRegionCoefs = (double *) calloc(origP->numCoverRegions, sizeof(double))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		memmove(destP->coverRegionCoefs, origP->coverRegionCoefs, origP->numCoverRegions*sizeof(double) );
	} 

	return 0;
}

static int freeNewDuty ( DL_NewDuty *origP )
{
	if( origP->numCoverDems > 0 )
	{
		_ASSERTE( origP->coverDems != NULL && origP->coverDemCoefs != NULL );

		free( origP->coverDems );
		origP->coverDems = NULL ;

		free( origP->coverDemCoefs );
		origP->coverDemCoefs = NULL ;
	} else
		_ASSERTE( origP->coverDemCoefs == NULL && origP->coverDems == NULL ) ;

	if( origP->numCoverRegions > 0 )
	{
		_ASSERTE( origP->coverRegions != NULL && origP->coverRegionCoefs != NULL );

		free( origP->coverRegions );
		origP->coverRegions = NULL ;

		free( origP->coverRegionCoefs );
		origP->coverRegionCoefs = NULL ;
	} else
		_ASSERTE( origP->coverRegionCoefs == NULL && origP->coverRegions == NULL ) ;

	memset(origP, 0, sizeof(DL_NewDuty) );

	return 0;
}


static DL_NewDuty *allocANewDuty(DL_NewDuty **newDutiesP, int *countP)
{
	DL_NewDuty *temP;

	if(!(*newDutiesP)) 
	{
		if( !((*newDutiesP) = (DL_NewDuty *) calloc(NewDutyDefaultAllocSize, sizeof(DL_NewDuty))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		(*countP) ++ ;
		temP = (*newDutiesP);
		return temP;
	}

	if( ! ((*countP) % NewDutyDefaultAllocSize) )
	{
		if( !((*newDutiesP) = (DL_NewDuty *) realloc(*newDutiesP, (NewDutyDefaultAllocSize + (*countP))*sizeof(DL_NewDuty))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
	}

	temP = (*newDutiesP);
	temP += (*countP);
	(*countP) ++;
	
	memset(temP, 0 ,sizeof(DL_NewDuty));
	return temP;

	//memset(&((*newDutiesP)[*countP-1]),'\0',sizeof(DL_NewDuty));
	//return(&((*newDutiesP)[*countP-1]));
}

static DL_LpColumn *allocAnLpColumn(DL_LpColumn **lpColsP, int *countP)
{
	DL_LpColumn *temP;

	if(!(*lpColsP)) 
	{
		if( !((*lpColsP) = (DL_LpColumn *) calloc(LPColDefaultAllocSize, sizeof(DL_LpColumn))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		(*countP) ++ ;
		temP = (*lpColsP);
		return temP;
	}

	if( ! ((*countP) % LPColDefaultAllocSize) )
	{
		if( !((*lpColsP) = (DL_LpColumn *) realloc(*lpColsP, (LPColDefaultAllocSize + (*countP))*sizeof(DL_LpColumn))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
	}

	temP = (*lpColsP);
	temP += (*countP);
	(*countP) ++;
	
	memset(temP, 0 ,sizeof(DL_LpColumn));
	return temP;
}


static DL_TourStartInfo *allocAStartInfo(DL_TourStartInfo **tourStartInfosP, int *countP)
{
	DL_TourStartInfo *temP;

	if(!(*tourStartInfosP)) 
	{
		if( !((*tourStartInfosP) = (DL_TourStartInfo *) calloc(DefaultAllocSize, sizeof(DL_TourStartInfo))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		(*countP) ++ ;
		temP = (*tourStartInfosP);
		return temP;
	}

	if( ! ((*countP) % DefaultAllocSize) )
	{
		if( !((*tourStartInfosP) = (DL_TourStartInfo *) realloc(*tourStartInfosP, (DefaultAllocSize + (*countP))*sizeof(DL_TourStartInfo))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
	}

	temP = (*tourStartInfosP);
	temP += (*countP);
	(*countP) ++;
	
	memset(temP, 0 ,sizeof(DL_TourStartInfo));
	return temP;

}

static int *allocCvdDemForCL( DL_NewDuty *inP)
{
	if( inP->coverDems == NULL ) 
	{
		_ASSERTE( !inP->numCoverDems && inP->coverDemCoefs == NULL ) ;
		if( !(inP->coverDems = (int*) calloc(CvdIndAllocSize , sizeof(int))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		if( !(inP->coverDemCoefs = (double*) calloc(CvdIndAllocSize , sizeof(double))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		inP->numCoverDems ++ ;
		return 0 ;
	}
	if( ! (inP->numCoverDems % CvdIndAllocSize ) )
	{
		if( !( inP->coverDems = (int *) realloc(inP->coverDems, (CvdIndAllocSize+inP->numCoverDems )*sizeof(int))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
		if( !( inP->coverDemCoefs = (double *) realloc(inP->coverDemCoefs, (CvdIndAllocSize+inP->numCoverDems )*sizeof(double))))
		{
			logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
			exit(1);
		}
	}
	inP->numCoverDems ++;
	memset(&(inP->coverDems[inP->numCoverDems-1]), 0, sizeof(int));
	memset(&(inP->coverDemCoefs[inP->numCoverDems-1]), 0, sizeof(double));
	return 0 ;
}

static int compareTourStartInfo (const DL_TourStartInfo *a, const DL_TourStartInfo *b)
{
	if( a->firstLegStart != b->firstLegStart )
		return (int)(a->firstLegStart - b->firstLegStart ) ;
	else if( a->cpInd != b->cpInd )
		return (a->cpInd - b->cpInd);
	else
		return (a->ac.ind - b->ac.ind);
}

static int compareLpColumn (const DL_LpColumn *a, const DL_LpColumn *b)
{
	if( a->newDutyP->acInd != b->newDutyP->acInd )
		return ( a->newDutyP->acInd - b->newDutyP->acInd );
	if( a->newDutyP->startInfoP->cpInd != b->newDutyP->startInfoP->cpInd )
		return ( a->newDutyP->startInfoP->cpInd - b->newDutyP->startInfoP->cpInd );
	if( a->newDutyP->day != b->newDutyP->day )
		return ( a->newDutyP->day - b->newDutyP->day) ;
	return( (int)(a->newDutyP->startTm - b->newDutyP->startTm) );
}

static int compareLpColumnTm (const DL_LpColumn *a, const DL_LpColumn *b)
{
	if( a->newDutyP->startTm != b->newDutyP->startTm )
		return (int)(a->newDutyP->startTm - b->newDutyP->startTm);
	else
		return ( a->newDutyP->startInfoP->cpInd - b->newDutyP->startInfoP->cpInd );
}

static int compareMgdLegsAC (const ProposedMgdLeg *a, const ProposedMgdLeg *b)
{
	if ( a->aircraftID != b->aircraftID )
		return (a->aircraftID - b->aircraftID);
	else
		return (int)(a->schedOut - b->schedOut);
}

static int compareLegInfoCP (const DL_LegInfo *a, const DL_LegInfo *b)
{
	if (a->leg->crewPairInd != b->leg->crewPairInd)
		return (a->leg->crewPairInd - b->leg->crewPairInd);
	else
		return (int)(a->leg->schedOut - b->leg->schedOut);
}


static int compareCrewAssign (const ProposedCrewAssg *a, const ProposedCrewAssg *b)
{
	if( a->crewID != b->crewID )
		return ( a->crewID - b->crewID ) ;
	else
		return (int)(a->startTm - b->startTm);
}

static int getDemUsed(DL_DemUsedType **dlDemUsedP)
{
	int i, j, demInd, count;
	DL_DemUsedType *dlDemUsed=NULL; //when this demand is used in the opt solution

	if( !(dlDemUsed = (DL_DemUsedType *) calloc(numDemand , sizeof(DL_DemUsedType))))
	{
		logMsg(dlFile,"%s Line %d, Out of Memory \n", __FILE__, __LINE__);
		exit(1);
	}

	if( optParam.withFlexOS )
	{
		_ASSERTE( numOrigDem > 0 && numDemand >= numOrigDem );
		for( i=0; i < numOrigDem; i ++)
			_ASSERTE( origDemInfos[i].isAppoint || origDemInfos[i].numInd == 1 );//no copy for customer demand
		for( i=0; i < numDemand; i ++)
			_ASSERTE( demandList[i].isAppoint || origDemInfos[demandList[i].origDemInd].numInd == 1 );
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////new tour
	for(demInd=0; demInd < numDemand; demInd ++)
		if( outTimes[demInd] )
			dlDemUsed[demInd] = DL_DemInNewTour ; //will be assigned if it is in an existing tour

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////existing tour
	for(i = 0; i < numOptExgTours; i ++)//go through demand list of each opt existing tour
	{
		for (j=0; j<MAX_LEGS; j++)
		{
			if( ( demInd = optExgTours[i].demandInd[j] ) < 0 ) //end of the list
				break;

			if( demandList[demInd].isAppoint )//app/maint
			{
				_ASSERTE( outTimes[demInd] == 0 && !dlDemUsed[demInd] );
				outTimes[demInd] = demandList[demInd].reqOut ; /////////////////////////////////ASSUME req out time
				inTimes[demInd] = demandList[demInd].reqIn ;
				dlDemUsed[demInd] = DL_DemInExgTour;
			} else
			{
				//_ASSERTE( dlDemUsed[demInd] == DL_DemInNewTour); 
				//existing tour is divided into two tours, if two airplanes are assigned, but in the first tour, demands of the second ac are kept in demandInd2[]
				dlDemUsed[demInd] = DL_DemInExgTour; //re-write
			}
		}//end for (j=0; j<MAX_LEGS; j++)

		for (j=0; j<MAX_LEGS; j++)
		{
			if( ( demInd = optExgTours[i].demandInd2[j] ) < 0 )
				break;

			if( demandList[demInd].isAppoint )//app/maint
			{
				_ASSERTE( outTimes[demInd] == 0 && !dlDemUsed[demInd] );
				outTimes[demInd] = demandList[demInd].reqOut ; /////////////////////////////////ASSUME req out time
				inTimes[demInd] = demandList[demInd].reqIn ;
				dlDemUsed[demInd] = DL_DemInExgTour;
			} else
			{
				_ASSERTE( dlDemUsed[demInd] == DL_DemInNewTour);
				dlDemUsed[demInd] = DL_DemInExgTour; //re-write
			}
		}//for (j=0; j<MAX_LEGS; j++)
	}//for(i = 0; i < numOptExgTours; i ++)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////app column
	if( optParam.withFlexOS )
	{
		for (i=0; i<numOrigDem; i++)//note
		{	
			if( optSolution[i] == 1 )//uncovered
			{
				for(j=0; j < origDemInfos[i].numInd; j ++)
					_ASSERTE( !dlDemUsed[origDemInfos[i].indices[j]] );
				continue ;
			}

			count = 0 ; //go through copies
			for(j=0; j < origDemInfos[i].numInd; j ++)
				if( dlDemUsed[origDemInfos[i].indices[j] ] )//this copy is used
					count ++;
			_ASSERTE( count == 0 || count == 1); //at most one copy of an app/maint is picked

			if( count )//found used copy, skip, otherwise, a copy of thie orig dem must be contained in an app column
				continue;

			_ASSERTE( origDemInfos[i].isAppoint );

			for(j=0; j < origDemInfos[i].numInd; j ++)//go through copies to get
			{
				demInd = origDemInfos[i].indices[j] ;
				if( demandList[demInd].outAirportID == acList[origDemInfos[i].acInd].availAirportID ) //////////////ASSUME the first match ( the earliest start time )
				{
					outTimes[demInd] = demandList[demInd].reqOut ;
					inTimes[demInd] = demandList[demInd].reqIn ;
					dlDemUsed[demInd] = DL_DemInAppCol ;
					break;
				}
			}//end for(j=0; j < origDemInfos[i].numInd; j ++)
			_ASSERTE( j < origDemInfos[i].numInd );
		}//end for (i=0; i<numOrigDem; i++)
	} else
	{
		for (i=0; i<numDemand; i++)
		{	
			if( optSolution[i] == 1 )//uncovered
			{
				_ASSERTE(  !dlDemUsed[i] );
				continue ;
			}

			if( outTimes[i] == 0 )
			{
				_ASSERTE( demandList[i].isAppoint );
				outTimes[i] = demandList[i].reqOut ;
				inTimes[i] = demandList[i].reqIn ;
				dlDemUsed[i] = DL_DemInAppCol ;
			}
		}//end for (i=0; i<numDemand; i++)
	}//end else

	//print
	for(i=0; i<numDemand; i++)
	{
		_ASSERTE( (outTimes[i] == 0 && inTimes[i] == 0 && !dlDemUsed[i] ) || ( outTimes[i] > 0 && inTimes[i] > outTimes[i] && dlDemUsed[i] ));

		if( dlDemUsed[i] == DL_DemInNewTour )
			fprintf(dlFile, "\n Demand %d, appointment type %d, Used in new tours \n", demandList[i].demandID, demandList[i].isAppoint );
		else if( dlDemUsed[i] == DL_DemInExgTour )
			fprintf(dlFile, "\n Demand %d, appointment type %d, Used in exg tours \n", demandList[i].demandID, demandList[i].isAppoint );
		else if( dlDemUsed[i] == DL_DemInAppCol )
			fprintf(dlFile, "\n Demand %d, appointment type %d, Used in app columns \n", demandList[i].demandID, demandList[i].isAppoint);
		else
		{
			_ASSERTE( !dlDemUsed[i] );
			fprintf(dlFile, "\n Demand %d, appointment type %d, Not used \n", demandList[i].demandID, demandList[i].isAppoint );
		}
	}//end for(i=0; i<numDemand; i++)

	*dlDemUsedP = dlDemUsed ;
	return 0;
}


static int returnTravelRequests ( const DL_OrigDuty **origDuties, DL_LpColumn *pickedLpCols, const int numPickedLpCols, BINTREENODE **travelsRootP, int *numTravelsP )
{
	int i, j, k, n, s, c, cpInd, curAptID, numTravels;
	DateTime tmp_dt1, tmp_dt2;
	time_t tmp_dt3, curTm, preFltTm[2];
    TravelRequest *trP;
	DL_NewDuty  *curDutyP=NULL ;
	int *lastPilotCol = NULL;
	BINTREENODE *travelsRoot = NULL;
	char opbuf1[1024], opbuf2[1024] ;
	
	numTravels = 0 ;
	tmp_dt1 = dt_addToDateTime(Minutes, optParam.minTimeToNotify, dt_run_time_GMT);

	//last duty assigned to each pilot
	if((lastPilotCol = (int *) calloc (numCrew, sizeof(int))) == NULL)
	{
		logMsg(dlFile,"%s Line %d: Out of Memory in constructTravelRequest().\n", __FILE__,__LINE__);
		exit(1);
	}
	for(i=0; i < numCrew; i ++)
		lastPilotCol[i] = -1 ; //initialize 

	qsort((void *) pickedLpCols, numPickedLpCols, sizeof(DL_LpColumn ), compareLpColumnTm); //start time, cpInd

	fprintf(dlFile,"\n Travel requests: \n flight_purpose: 1 for on tour, 2 for off tour, 3 for changing plane.\n Travel to duties \n");
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	fprintf(dlFile,"| CrewID       | Depart_AptID | Early_Dpt_time   | Arrive_AptID | Late_Arr_time    |  Flight_purpose |  Off_ACID  |   On_ACID   |   buyticket | cancel_tix  | rqtid_cancelled  | groundtravel|   Scenario_ID  |\n");
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////travel to duties
	for (i=0; i<numPickedLpCols; i++)
	{
		if( !pickedLpCols[i].fDuty ) //not first duty of a crew pair
			continue ;

		//travel to curDutyP
		curDutyP = pickedLpCols[i].newDutyP ;
		cpInd = curDutyP->startInfoP->cpInd ; //crew pair index

		for(n=0; n < 2; n ++)//for each pilot
		{
			c = crewPairList[cpInd].crewListInd[n] ;//crew index
			
			if( curDutyP->pAirports[n] == curDutyP->acAirport ) //trivial travel
			{
				lastPilotCol[c] = i ; //keep track of last column of this pilot 
				continue;
			}

			//get pre flight time
			if( curDutyP->startInfoP->pilots[n].status == DL_Pilot_Status_Active )
				//preFltTm[n] = Minute*optParam.preFlightTm ;//temp
				preFltTm[n] = Minute*acTypeList[crewList[c].acTypeIndex].preFlightTm ;//07/17/2017 ANG
			else if ( curDutyP->startInfoP->pilots[n].status == DL_Pilot_Status_Rest_B4FirstLeg )
				preFltTm[n] = Minute*optParam.firstPreFltTm ;
			else
			{
				_ASSERTE( curDutyP->startInfoP->pilots[n].status == DL_Pilot_Status_Rest_B4Leg );
				//preFltTm[n] = Minute*optParam.preFlightTm ;
				preFltTm[n] = Minute*acTypeList[crewList[c].acTypeIndex].preFlightTm ;//07/17/2017 ANG
			}

			if((trP = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) 
			{   
				logMsg(dlFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
                exit(1);
             }

			trP->crewID = crewList[c].crewID;
			trP->arr_aptID_travel = curDutyP->acAirport;
			trP->dept_aptID_travel = curDutyP->pAirports[n];
			trP->on_aircraftID = acList[curDutyP->acInd].aircraftID;

			//get early departure and later arrival time // whether rest before and after travel
			if( !curDutyP->keepOriginal )
			{
				if( curDutyP->startInfoP->pilots[n].status != DL_Pilot_Status_Active)//was resting, 
				{
					trP->earliest_dept = curDutyP->pTms[n] ; //travel after available

					if( curDutyP->pilotStartTm[n] == curDutyP->pDutyStartTm[n] ) //no rest after travel arrival
					{
						trP->latest_arr =  curDutyP->startTm - preFltTm[n] ;
					} else//travel+rest
					{
						_ASSERTE( curDutyP->pDutyStartTm[n] == curDutyP->startTm - preFltTm[n] ); 
						trP->latest_arr =  curDutyP->startTm - preFltTm[n] - Minute*optParam.minRestTm ;
					}
				} else//active
				{
					//whether rest b4 travel
					if( curDutyP->pilotStartTm[n] == crewList[c].availDT - Minute*crewList[c].dutyTime )//no rest b4 travel
					{
						trP->earliest_dept = curDutyP->pTms[n] ;
					} else
					{
						_ASSERTE( curDutyP->pilotStartTm[n] >= curDutyP->pTms[n] + Minute*optParam.minRestTm );
						trP->earliest_dept = curDutyP->pTms[n] + Minute*optParam.minRestTm ; //start travel after rest
					}

					//whether rest after travel
					if( curDutyP->pilotStartTm[n] == curDutyP->pDutyStartTm[n] )//no rest after travel
					{
						trP->latest_arr =  curDutyP->startTm - preFltTm[n] ;
					} else//travel+rest
					{
						_ASSERTE( curDutyP->pDutyStartTm[n] == curDutyP->startTm - preFltTm[n] && curDutyP->pDutyStartTm[n] >= curDutyP->pTms[n] + Minute*optParam.minRestTm );
						trP->latest_arr =  curDutyP->startTm - preFltTm[n] - Minute*optParam.minRestTm ;
					}
				}
			} else //keep original
			{
				trP->earliest_dept = curDutyP->pTms[n] ;
				trP->latest_arr =  curDutyP->startTm - preFltTm[n] ;
			}

			//note special case: travel booked?
			if( lastPilotCol[c] < 0 && crewList[c].lastCsTravelLeg)//first duty for this pilot
			{
				//check last travel
				tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, crewList[c].lastCsTravelLeg ->travel_dptTm) ;
				tmp_dt3 = DateTimeToTime_t(crewList[c].lastCsTravelLeg->travel_dptTm) ;

				if( tmp_dt2 >=tmp_dt1 && tmp_dt3 > crewList[c].lastActivityLeg_recout )
				{
				   trP->dept_aptID_travel = crewList[c].lastCsTravelLeg->dpt_aptID;//overwrite
				   trP->earliest_dept = DateTimeToTime_t(tmp_dt2);//overwrite
				   trP->cancelexstticket = 1;
				   trP->tixquestid_cancelled = crewList[c].lastCsTravelLeg->rqtID;
				}
			}
			
			//get last ac assigned to this pilot
			if( lastPilotCol[c] < 0 )//first duty of this pilot
			{
				if(crewPairList[cpInd].hasFlownFirst == 1 || crewList[c].lastActivityLeg_flag)
				{
					trP->off_aircraftID = ( crewList[c].lastActivityLeg_flag ? crewList[c].lastActivityLeg_aircraftID : 0 );
					trP->flight_purpose = 3; //changing plane
				} else
				{
					trP->off_aircraftID = 0;
					trP->flight_purpose = 1; //on tour
				}
			} else//check last duty of this pilot
			{
				//last ac of this duty
				const DL_OrigDuty *tempDutyP = &( origDuties[pickedLpCols[lastPilotCol[c]].newDutyP->startInfoP->cpInd][pickedLpCols[lastPilotCol[c]].newDutyP->startInfoP->legEndDay] );
				_ASSERTE(  pickedLpCols[lastPilotCol[c]].newDutyP->startInfoP->type != DL_NewAvail ) ;
			
				trP->off_aircraftID = acList[tempDutyP->acInd[tempDutyP->numAc-1]].aircraftID;
				trP->flight_purpose = 3; //changing plane
			}

			if(!checkAptNearby(trP->dept_aptID_travel, trP->arr_aptID_travel))
                trP->groundtravel = 0;
			else
				trP->groundtravel = 1;

			crewList[c].travel_request_created = 1;
			trP->rqtindex = (++ numTravels);
			trP->buyticket = 1;

            if(!(travelsRoot = RBTreeInsert(travelsRoot, trP, travelRequestCmp))) 
			{
              logMsg(dlFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
              exit(1);
            }

			fprintf(dlFile,"| %-12d | %-12d | %10s | %-12d | %10s | %-15d | %10d | %11d | %-11d | %-11d | %-16d | %-11d |\n",
			trP->crewID,
			trP->dept_aptID_travel,
			dt_DateTimeToDateTimeString(dt_time_tToDateTime(trP->earliest_dept), opbuf1, "%Y/%m/%d %H:%M"),
			trP->arr_aptID_travel,
			dt_DateTimeToDateTimeString(dt_time_tToDateTime(trP->latest_arr), opbuf2, "%Y/%m/%d %H:%M"),
			trP->flight_purpose,
			trP->off_aircraftID,
			trP->on_aircraftID,
			trP->buyticket,
			trP->cancelexstticket,
			trP->tixquestid_cancelled,
			trP->groundtravel); 

			lastPilotCol[c] = i ; //update

		}//end for(n=0; n < 2; n ++)//for each pilot
	}//end for (i=0; i<numPickedLpCols; i++)
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////travels to home
	fprintf(dlFile,"\n Travel requests: \n flight_purpose: 1 for on tour, 2 for off tour, 3 for changing plane.\n Travel to home \n");
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	fprintf(dlFile,"| CrewID       | Depart_AptID | Early_Dpt_time   | Arrive_AptID | Late_Arr_time    |  Flight_purpose |  Off_ACID  |   On_ACID   |   buyticket | cancel_tix  | rqtid_cancelled  | groundtravel|   Scenario_ID  |\n");
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");
	
	for (c=0; c<numCrew; c++)
	{
        if(crewList[c].endRegDay == PAST_WINDOW) //not going home
		    continue;

		if( lastPilotCol[c] < 0 )//pilot is not used
		{
			curDutyP = NULL ;
			curAptID = crewList[c].availAirportID ;
			curTm = crewList[c].availDT ;
		} else
		{
			//get the last duty of current pilot: k, may or may not be an original duty
			k = lastPilotCol[c];
			for(j=lastPilotCol[c]+1; j < numPickedLpCols; j ++)//ASSUME picked cols are sorted increasing time
			{
				cpInd = pickedLpCols[j].newDutyP->startInfoP->cpInd ;
				if( crewPairList[cpInd].crewListInd[0] == c || crewPairList[cpInd].crewListInd[1] == c )
					k = j ;
			}
			curDutyP = pickedLpCols[k].newDutyP ;//last duty
			curAptID = (curDutyP->origDutyP ? curDutyP->origDutyP->lastLegP->leg->schedInAptID : curDutyP->acAirport );//last airport of this pilot
			curTm = curDutyP->endTm + Minute*optParam.postFlightTm ;
		}

		if( curAptID == crewList[c].endLoc )//trivial travel
			continue ;

		if((trP = (TravelRequest *) calloc((size_t) 1, sizeof(TravelRequest))) == NULL) 
		{   
			logMsg(dlFile,"%s Line %d, Out of Memory in constructTravelRequest().\n", __FILE__, __LINE__);
            exit(1);
         }

		trP->crewID = crewList[c].crewID;
		trP->earliest_dept = curTm ;
		trP->dept_aptID_travel = curAptID ;
		trP->arr_aptID_travel = crewList[c].endLoc;

		if( lastPilotCol[c] < 0 )//pilot is not used //special case
		{
			trP->off_aircraftID = (crewList[c].lastActivityLeg_flag ? crewList[c].lastActivityLeg_aircraftID : 0 );
			if(crewList[c].lastCsTravelLeg)
			{
			   tmp_dt2 = dt_addToDateTime(Minutes, -optParam.preBoardTime, crewList[c].lastCsTravelLeg ->travel_dptTm);
			   tmp_dt3 = DateTimeToTime_t(crewList[c].lastCsTravelLeg->travel_dptTm);
				
			   //special case
			   if(tmp_dt2 >=tmp_dt1 && tmp_dt3 > crewList[c].lastActivityLeg_recout )
			   {
				   trP->dept_aptID_travel = crewList[c].lastCsTravelLeg->dpt_aptID;
				   trP->earliest_dept = DateTimeToTime_t(tmp_dt2);//need to refine
			   }
			}
		} else
		{
			k = ( curDutyP->origDutyP ? curDutyP->origDutyP->acInd[curDutyP->origDutyP->numAc -1] : curDutyP->acInd ) ;
			trP->off_aircraftID = acList[k].aircraftID ;
		}

		//note: get latest arrival time
	    for(s=0; s<=crewList[c].stayLate;s++)
			if( crewList[c].tourEndTm + DayInSecs*s + Minute*optParam.maxCrewExtension > trP->earliest_dept)//note: may not be feasible?
			{
				trP->latest_arr = crewList[c].tourEndTm + DayInSecs*s + Minute*optParam.maxCrewExtension;
				break;
		   }       
		if( s > crewList[c].stayLate )
		{
			trP->latest_arr = trP->earliest_dept;
			fprintf(dlFile,"Can't find latest arrival time before tour end (Overtime included) \n");
		}
		
		trP->on_aircraftID = 0;
		trP->flight_purpose = 2;
		crewList[c].travel_request_created = 1;
		trP->rqtindex = (++ numTravels);

		if( (trP->latest_arr - trP->earliest_dept)/Hour <= optParam.sendhomecutoff + optParam.maxCrewExtension/Minute)
			trP->buyticket = 1;
		else
			trP->buyticket = 0;

		if(!checkAptNearby(trP->dept_aptID_travel,trP->arr_aptID_travel))
			trP->groundtravel = 0;
		else
			trP->groundtravel = 1;

		if(!(travelsRoot = RBTreeInsert(travelsRoot, trP, travelRequestCmp))) 
		{
			  logMsg(dlFile,"%s Line %d, RBTreeInsert() failed in constructTravelRequest().\n",__FILE__,__LINE__);
			  exit(1);
		}

		fprintf(dlFile,"| %-12d | %-12d | %10s | %-12d | %10s | %-15d | %10d | %11d | %-11d | %-11d | %-16d | %-11d |\n",
		trP->crewID,
		trP->dept_aptID_travel,
		dt_DateTimeToDateTimeString(dt_time_tToDateTime(trP->earliest_dept), opbuf1, "%Y/%m/%d %H:%M"),
		trP->arr_aptID_travel,
		dt_DateTimeToDateTimeString(dt_time_tToDateTime(trP->latest_arr), opbuf2, "%Y/%m/%d %H:%M"),
		trP->flight_purpose,
		trP->off_aircraftID,
		trP->on_aircraftID,
		trP->buyticket,
		trP->cancelexstticket,
		trP->tixquestid_cancelled,
		trP->groundtravel); 

	}//end for (c=0; c<numCrew; c++)
	fprintf(dlFile,"+--------------+--------------+------------------+--------------+------------------+-----------------+------------+-------------+-------------+-------------+------------------+-------------+----------------+\n");

	qsort((void *) pickedLpCols, numPickedLpCols, sizeof(DL_LpColumn ), compareLpColumn); //sort back

	if( lastPilotCol != NULL )
	{
		free( lastPilotCol );
		lastPilotCol = NULL;
	}

	*travelsRootP = travelsRoot;
	*numTravelsP = numTravels ;

	return 0;
}

static int travelRequestCmp(void *t, void *r)
{
	TravelRequest *a = (TravelRequest *) t;
	TravelRequest *b = (TravelRequest *) r;

	return( a->rqtindex - b->rqtindex );
}

static int isFeasibleDQPilotsAndAc(const int cpInd, const int acInd)
{
	int i, j ;
	_ASSERTE( acInd >= 0 && cpInd >= 0 );

	i = crewPairList[cpInd].crewListInd[0] ;
	j = crewPairList[cpInd].crewListInd[1] ;

	if(checkIfXlsPlus(acList[acInd].aircraftID) )
	{
		if( crewList[i].isDup != 1 || crewList[j].isDup != 1)
			return 0;
	} else if( crewList[i].isDup == 1 || crewList[j].isDup == 1)
		return 0;

	//if(checkIfCj4(acList[acInd].aircraftID) )
	//{
	//	if( crewList[i].isDup != 2 || crewList[j].isDup != 2)
	//		return 0;
	//} else if( crewList[i].isDup == 2 || crewList[j].isDup == 2)
	//	return 0;

	return 1 ;
}